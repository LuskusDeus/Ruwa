// SPDX-License-Identifier: MPL-2.0

#include "CanvasPanel.h"

#include "CanvasCursorManager.h"
#include "CanvasParameterOverlayWidget.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/canvas/engine/CanvasEngineSession.h"
#include "features/effects/LayerEffectRegistry.h"
#include "features/layers/model/LayerModel.h"
#include "features/theme/manager/ThemeManager.h"

#include <QCursor>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ruwa::ui::workspace {
namespace {

using namespace ruwa::core::effects;

const EffectParamDefinition* findParamDefinition(
    const LayerEffectDescriptor& descriptor, const QString& key)
{
    for (const EffectParamDefinition& param : descriptor.params) {
        if (param.key == key) {
            return &param;
        }
    }
    return nullptr;
}

qreal resolvedNumericValue(const LayerEffectState& state, const EffectParamDefinition& definition)
{
    return state.params.value(definition.key, definition.defaultValue).toDouble();
}

qreal constrainedValue(qreal value, qreal minimum, qreal maximum, qreal step)
{
    value = std::clamp(value, minimum, maximum);
    if (step > 0.0) {
        value = minimum + std::round((value - minimum) / step) * step;
    }
    return std::clamp(value, minimum, maximum);
}

QVariant storedNumericValue(qreal value, bool integral)
{
    return integral ? QVariant::fromValue(qRound(value)) : QVariant(value);
}

} // namespace

using namespace ruwa::core::effects;

void CanvasPanel::setEffectParameterOverlaySelection(
    const ruwa::core::layers::LayerId& layerId, const QUuid& effectId)
{
    if (m_effectParameterOverlayLayerId == layerId
        && m_effectParameterOverlayEffectId == effectId) {
        refreshEffectParameterOverlay();
        return;
    }

    finishEffectParameterOverlayDrag(true);
    m_effectParameterOverlayLayerId = layerId;
    m_effectParameterOverlayEffectId = effectId;
    refreshEffectParameterOverlay();
    updateCursorManagerOverlay();
    updateToolCursor();
}

void CanvasPanel::ensureEffectParameterOverlay()
{
    if (m_effectParameterOverlay || !m_contentWidget) {
        return;
    }

    m_effectParameterOverlay = new CanvasParameterOverlayWidget(m_contentWidget);
    m_effectParameterOverlay->setGeometry(m_contentWidget->rect());
    m_effectParameterOverlay->setDocumentToLocalFn([this](const QPointF& documentPosition) {
        if (!m_contentWidget || !m_viewportHostWidget) {
            return QPointF();
        }
        const QPoint hostTopLeft = m_viewportHostWidget->mapTo(m_contentWidget, QPoint(0, 0));
        return QPointF(hostTopLeft) + viewportFromDocument(documentPosition);
    });
    m_effectParameterOverlay->setPresentationChangedFn(
        [this]() { syncEffectParameterOverlayPresentation(); });
}

void CanvasPanel::syncEffectParameterOverlayPresentation()
{
    auto* presentation = inputPresentation();
    auto* view = inputView();
    if (!presentation) {
        return;
    }

    std::vector<ParameterControlOverlayState> states;
    if (view && m_effectParameterOverlay && m_effectParameterOverlay->isVisible() && m_contentWidget
        && m_viewportHostWidget) {
        const QSizeF extent = view->viewportExtent();
        const qreal scaleX = m_viewportHostWidget->width() > 0
            ? extent.width() / static_cast<qreal>(m_viewportHostWidget->width())
            : 1.0;
        const qreal scaleY = m_viewportHostWidget->height() > 0
            ? extent.height() / static_cast<qreal>(m_viewportHostWidget->height())
            : 1.0;
        const qreal radiusScale = (scaleX + scaleY) * 0.5;
        const QPoint hostTopLeft = m_viewportHostWidget->mapTo(m_contentWidget, QPoint(0, 0));
        const QColor primary = ruwa::ui::core::ThemeManager::instance().colors().primary;

        states.reserve(static_cast<size_t>(m_effectParameterOverlay->controls().size()));
        for (int i = 0; i < m_effectParameterOverlay->controls().size(); ++i) {
            const auto screen = m_effectParameterOverlay->screenControlAt(i);
            const QPointF viewportCenter = screen.center - QPointF(hostTopLeft);
            if (!std::isfinite(viewportCenter.x()) || !std::isfinite(viewportCenter.y())
                || !std::isfinite(screen.radius)) {
                continue;
            }

            ParameterControlOverlayState state;
            state.type = m_effectParameterOverlay->controlAt(i)->type;
            state.centerX = static_cast<float>(viewportCenter.x() * scaleX);
            state.centerY = static_cast<float>(viewportCenter.y() * scaleY);
            state.radius = static_cast<float>(std::max<qreal>(0.0, screen.radius * radiusScale));
            state.hoverProgress = static_cast<float>(m_effectParameterOverlay->hoverProgress(i));
            state.primaryColor = primary;
            states.push_back(std::move(state));
        }
    }
    presentation->setParameterControlOverlayState(std::move(states));
}

void CanvasPanel::refreshEffectParameterOverlay()
{
    ensureEffectParameterOverlay();
    if (!m_effectParameterOverlay) {
        return;
    }

    m_effectParameterOverlay->setGeometry(m_contentWidget->rect());
    const bool controlsWereVisible = m_effectParameterOverlay->isVisible();
    QList<CanvasParameterControl> controls;

    if (m_layerModel && !m_effectParameterOverlayLayerId.isNull()
        && !m_effectParameterOverlayEffectId.isNull()) {
        const auto* layer = m_layerModel->layerById(m_effectParameterOverlayLayerId);
        const LayerEffectState* selectedEffect = nullptr;
        if (layer) {
            for (const LayerEffectState& effect : layer->effects) {
                if (effect.instanceId == m_effectParameterOverlayEffectId) {
                    selectedEffect = &effect;
                    break;
                }
            }
        }

        const LayerEffectDescriptor* descriptor = selectedEffect
            ? LayerEffectRegistry::instance().descriptor(selectedEffect->typeId)
            : nullptr;
        if (selectedEffect && descriptor) {
            for (const EffectCanvasControlDefinition& definition : descriptor->canvasControls) {
                if (definition.type != EffectCanvasControlType::Circle
                    && definition.type != EffectCanvasControlType::Position) {
                    continue;
                }
                const auto* valueParam = findParamDefinition(*descriptor, definition.valueParamKey);
                const auto* centerXParam
                    = findParamDefinition(*descriptor, definition.centerXParamKey);
                const auto* centerYParam
                    = findParamDefinition(*descriptor, definition.centerYParamKey);
                const auto isNumeric = [](const EffectParamDefinition* param) {
                    return param
                        && (param->type == EffectParamType::Int
                            || param->type == EffectParamType::Real);
                };
                if (!isNumeric(centerXParam) || !isNumeric(centerYParam)
                    || (definition.type == EffectCanvasControlType::Circle
                        && !isNumeric(valueParam))
                    || (definition.type == EffectCanvasControlType::Position
                        && definition.centerXParamKey == definition.centerYParamKey)) {
                    continue;
                }

                CanvasParameterControl control;
                control.id = definition.id;
                control.type = definition.type == EffectCanvasControlType::Position
                    ? CanvasParameterControlType::Position
                    : CanvasParameterControlType::Circle;
                control.valueParamKey = definition.valueParamKey;
                control.centerXParamKey = definition.centerXParamKey;
                control.centerYParamKey = definition.centerYParamKey;
                control.documentCenter
                    = QPointF(resolvedNumericValue(*selectedEffect, *centerXParam),
                        resolvedNumericValue(*selectedEffect, *centerYParam));
                if (control.type == CanvasParameterControlType::Circle) {
                    control.documentRadius = resolvedNumericValue(*selectedEffect, *valueParam);
                    control.minimumValue = valueParam->minimumValue.toDouble();
                    control.maximumValue = valueParam->maximumValue.toDouble();
                    control.stepValue = valueParam->stepValue.toDouble();
                    control.integralValue = valueParam->type == EffectParamType::Int;
                }
                control.minimumPosition = QPointF(
                    centerXParam->minimumValue.toDouble(), centerYParam->minimumValue.toDouble());
                control.maximumPosition = QPointF(
                    centerXParam->maximumValue.toDouble(), centerYParam->maximumValue.toDouble());
                control.positionStep = QPointF(
                    centerXParam->stepValue.toDouble(), centerYParam->stepValue.toDouble());
                control.integralX = centerXParam->type == EffectParamType::Int;
                control.integralY = centerYParam->type == EffectParamType::Int;
                controls.append(control);
            }
        }
    }

    const QString hoveredId = m_effectParameterOverlayDragging
        ? m_effectParameterOverlayDragControlId
        : (m_effectParameterOverlay->controlAt(m_effectParameterOverlay->hoveredControl())
                  ? m_effectParameterOverlay->controlAt(m_effectParameterOverlay->hoveredControl())
                        ->id
                  : QString());
    m_effectParameterOverlay->setControls(controls);
    m_effectParameterOverlay->setHoveredControl(m_effectParameterOverlay->controlIndex(hoveredId));
    if (m_effectParameterOverlayDragging
        && m_effectParameterOverlay->controlIndex(m_effectParameterOverlayDragControlId) < 0) {
        finishEffectParameterOverlayDrag(true);
    }
    if (!controls.isEmpty()) {
        m_effectParameterOverlay->raise();
        // Parameter geometry belongs directly above the GL surface, below the
        // canvas's floating UI and loading chrome.
        if (m_brushOverlay) {
            m_effectParameterOverlay->stackUnder(m_brushOverlay);
        } else if (m_loadingOverlay) {
            m_effectParameterOverlay->stackUnder(m_loadingOverlay);
        }
    }
    syncEffectParameterOverlayPresentation();
    if (controlsWereVisible != !controls.isEmpty()) {
        updateCursorManagerOverlay();
    }
}

int CanvasPanel::effectParameterOverlayHitTest(const QPointF& globalPosition) const
{
    if (!m_effectParameterOverlay || !m_effectParameterOverlay->isVisible() || !m_contentWidget
        || !m_viewportHostWidget) {
        return -1;
    }
    const QPointF viewportPosition = m_viewportHostWidget->mapFromGlobal(globalPosition);
    if (!m_viewportHostWidget->rect().contains(viewportPosition.toPoint())) {
        return -1;
    }
    const QPointF localPosition = m_contentWidget->mapFromGlobal(globalPosition);
    return m_effectParameterOverlay->hitTest(localPosition);
}

bool CanvasPanel::handleEffectParameterOverlayMousePress(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton || !isInteractionEnabled()) {
        return false;
    }
    const int hit = effectParameterOverlayHitTest(event->globalPosition());
    const auto* control
        = m_effectParameterOverlay ? m_effectParameterOverlay->controlAt(hit) : nullptr;
    if (!control) {
        return false;
    }

    m_effectParameterOverlayDragging = true;
    m_effectParameterOverlayDragControlId = control->id;
    const QPointF documentPosition
        = documentFromViewport(m_viewportHostWidget->mapFromGlobal(event->globalPosition()));
    m_effectParameterOverlayDragOffset = control->documentCenter - documentPosition;
    m_effectParameterOverlay->setHoveredControl(hit);
    if (m_cursorManager) {
        updateCursorManagerOverlay();
        m_cursorManager->updateCursorPosition(event->globalPosition().toPoint());
    }
    event->accept();
    return true;
}

bool CanvasPanel::handleEffectParameterOverlayMouseMove(QMouseEvent* event)
{
    if (!event || !m_effectParameterOverlay) {
        return false;
    }

    if (m_effectParameterOverlayDragging) {
        if (!(event->buttons() & Qt::LeftButton)) {
            finishEffectParameterOverlayDrag(true);
            return false;
        }
        const int index
            = m_effectParameterOverlay->controlIndex(m_effectParameterOverlayDragControlId);
        const auto* controlPointer = m_effectParameterOverlay->controlAt(index);
        if (!controlPointer) {
            finishEffectParameterOverlayDrag(true);
            return false;
        }
        const CanvasParameterControl control = *controlPointer;

        const QPointF viewportPosition
            = m_viewportHostWidget->mapFromGlobal(event->globalPosition());
        const QPointF documentPosition = documentFromViewport(viewportPosition);
        if (!std::isfinite(documentPosition.x()) || !std::isfinite(documentPosition.y())) {
            event->accept();
            return true;
        }
        if (control.type == CanvasParameterControlType::Position) {
            const QPointF target = documentPosition + m_effectParameterOverlayDragOffset;
            const QVariant x
                = storedNumericValue(constrainedValue(target.x(), control.minimumPosition.x(),
                                         control.maximumPosition.x(), control.positionStep.x()),
                    control.integralX);
            const QVariant y
                = storedNumericValue(constrainedValue(target.y(), control.minimumPosition.y(),
                                         control.maximumPosition.y(), control.positionStep.y()),
                    control.integralY);
            m_effectParameterOverlay->setControlPosition(
                control.id, QPointF(x.toDouble(), y.toDouble()));
            // Reuse the paired position editor's existing live-edit/undo path.
            const auto layerId = m_effectParameterOverlayLayerId;
            const auto effectId = m_effectParameterOverlayEffectId;
            emit effectParameterOverlayChanged(layerId, effectId, control.centerXParamKey, x);
            emit effectParameterOverlayChanged(layerId, effectId, control.centerYParamKey, y);
        } else {
            const qreal value
                = constrainedValue(std::hypot(documentPosition.x() - control.documentCenter.x(),
                                       documentPosition.y() - control.documentCenter.y()),
                    control.minimumValue, control.maximumValue, control.stepValue);
            m_effectParameterOverlay->setCircleRadius(control.id, value);
            emit effectParameterOverlayChanged(m_effectParameterOverlayLayerId,
                m_effectParameterOverlayEffectId, control.valueParamKey,
                storedNumericValue(value, control.integralValue));
        }
        if (m_cursorManager) {
            m_cursorManager->updateCursorPosition(event->globalPosition().toPoint());
        }
        event->accept();
        return true;
    }

    const int hit = effectParameterOverlayHitTest(event->globalPosition());
    const int previousHit = m_effectParameterOverlay->hoveredControl();
    m_effectParameterOverlay->setHoveredControl(hit);
    if (hit != previousHit) {
        updateCursorManagerOverlay();
    }
    if (hit >= 0) {
        if (m_cursorManager) {
            m_cursorManager->updateCursorPosition(event->globalPosition().toPoint());
        }
        event->accept();
        return true;
    }
    if (previousHit >= 0) {
        updateToolCursor();
    }
    return false;
}

bool CanvasPanel::handleEffectParameterOverlayMouseRelease(QMouseEvent* event)
{
    if (!event || !m_effectParameterOverlayDragging || event->button() != Qt::LeftButton) {
        return false;
    }
    finishEffectParameterOverlayDrag(true);
    event->accept();
    return true;
}

void CanvasPanel::finishEffectParameterOverlayDrag(bool notifyEditor)
{
    if (!m_effectParameterOverlayDragging) {
        return;
    }
    const auto layerId = m_effectParameterOverlayLayerId;
    const QUuid effectId = m_effectParameterOverlayEffectId;
    m_effectParameterOverlayDragging = false;
    m_effectParameterOverlayDragControlId.clear();
    if (m_effectParameterOverlay) {
        const QPoint cursorPosition
            = m_cursorManager ? m_cursorManager->activeCursorPosition() : QCursor::pos();
        m_effectParameterOverlay->setHoveredControl(effectParameterOverlayHitTest(cursorPosition));
    }
    if (notifyEditor && !layerId.isNull() && !effectId.isNull()) {
        emit effectParameterOverlayEditFinished(layerId, effectId);
    }
    updateCursorManagerOverlay();
    updateToolCursor();
}

} // namespace ruwa::ui::workspace
