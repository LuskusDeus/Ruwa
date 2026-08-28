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

    std::vector<aether::ParameterCircleOverlayState> states;
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

        states.reserve(static_cast<size_t>(m_effectParameterOverlay->circles().size()));
        for (int i = 0; i < m_effectParameterOverlay->circles().size(); ++i) {
            const auto screen = m_effectParameterOverlay->screenCircleAt(i);
            const QPointF viewportCenter = screen.center - QPointF(hostTopLeft);
            if (!std::isfinite(viewportCenter.x()) || !std::isfinite(viewportCenter.y())
                || !std::isfinite(screen.radius)) {
                continue;
            }

            aether::ParameterCircleOverlayState state;
            state.centerX = static_cast<float>(viewportCenter.x() * scaleX);
            state.centerY = static_cast<float>(viewportCenter.y() * scaleY);
            state.radius = static_cast<float>(std::max<qreal>(0.0, screen.radius * radiusScale));
            state.hoverProgress = static_cast<float>(m_effectParameterOverlay->hoverProgress(i));
            state.primaryColor = primary;
            states.push_back(std::move(state));
        }
    }
    presentation->setParameterCircleOverlayState(std::move(states));
}

void CanvasPanel::refreshEffectParameterOverlay()
{
    ensureEffectParameterOverlay();
    if (!m_effectParameterOverlay) {
        return;
    }

    m_effectParameterOverlay->setGeometry(m_contentWidget->rect());
    const bool controlsWereVisible = m_effectParameterOverlay->isVisible();
    QList<CanvasParameterCircleControl> circles;

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
                if (definition.type != EffectCanvasControlType::Circle) {
                    continue;
                }
                const auto* valueParam = findParamDefinition(*descriptor, definition.valueParamKey);
                const auto* centerXParam
                    = findParamDefinition(*descriptor, definition.centerXParamKey);
                const auto* centerYParam
                    = findParamDefinition(*descriptor, definition.centerYParamKey);
                if (!valueParam || !centerXParam || !centerYParam
                    || (valueParam->type != EffectParamType::Int
                        && valueParam->type != EffectParamType::Real)) {
                    continue;
                }

                CanvasParameterCircleControl circle;
                circle.id = definition.id;
                circle.valueParamKey = definition.valueParamKey;
                circle.documentCenter
                    = QPointF(resolvedNumericValue(*selectedEffect, *centerXParam),
                        resolvedNumericValue(*selectedEffect, *centerYParam));
                circle.documentRadius = resolvedNumericValue(*selectedEffect, *valueParam);
                circle.minimumValue = valueParam->minimumValue.toDouble();
                circle.maximumValue = valueParam->maximumValue.toDouble();
                circle.stepValue = valueParam->stepValue.toDouble();
                circle.integralValue = valueParam->type == EffectParamType::Int;
                circles.append(circle);
            }
        }
    }

    const QString hoveredId = m_effectParameterOverlayDragging
        ? m_effectParameterOverlayDragControlId
        : (m_effectParameterOverlay->circleAt(m_effectParameterOverlay->hoveredCircle())
                  ? m_effectParameterOverlay->circleAt(m_effectParameterOverlay->hoveredCircle())
                        ->id
                  : QString());
    m_effectParameterOverlay->setCircles(circles);
    m_effectParameterOverlay->setHoveredCircle(m_effectParameterOverlay->circleIndex(hoveredId));
    if (!circles.isEmpty()) {
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
    if (controlsWereVisible != !circles.isEmpty()) {
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
    const auto* circle
        = m_effectParameterOverlay ? m_effectParameterOverlay->circleAt(hit) : nullptr;
    if (!circle) {
        return false;
    }

    m_effectParameterOverlayDragging = true;
    m_effectParameterOverlayDragControlId = circle->id;
    m_effectParameterOverlay->setHoveredCircle(hit);
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
        const int index
            = m_effectParameterOverlay->circleIndex(m_effectParameterOverlayDragControlId);
        const auto* circlePointer = m_effectParameterOverlay->circleAt(index);
        if (!circlePointer) {
            finishEffectParameterOverlayDrag(true);
            return false;
        }
        const CanvasParameterCircleControl circle = *circlePointer;

        const QPointF viewportPosition
            = m_viewportHostWidget->mapFromGlobal(event->globalPosition());
        const QPointF documentPosition = documentFromViewport(viewportPosition);
        qreal value = std::hypot(documentPosition.x() - circle.documentCenter.x(),
            documentPosition.y() - circle.documentCenter.y());
        value = std::clamp(value, circle.minimumValue, circle.maximumValue);
        if (circle.stepValue > 0.0) {
            value = circle.minimumValue
                + std::round((value - circle.minimumValue) / circle.stepValue) * circle.stepValue;
            value = std::clamp(value, circle.minimumValue, circle.maximumValue);
        }

        m_effectParameterOverlay->setCircleRadius(circle.id, value);
        const QVariant storedValue
            = circle.integralValue ? QVariant::fromValue(qRound(value)) : QVariant(value);
        emit effectParameterOverlayChanged(m_effectParameterOverlayLayerId,
            m_effectParameterOverlayEffectId, circle.valueParamKey, storedValue);
        if (m_cursorManager) {
            m_cursorManager->updateCursorPosition(event->globalPosition().toPoint());
        }
        event->accept();
        return true;
    }

    const int hit = effectParameterOverlayHitTest(event->globalPosition());
    const int previousHit = m_effectParameterOverlay->hoveredCircle();
    m_effectParameterOverlay->setHoveredCircle(hit);
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
        m_effectParameterOverlay->setHoveredCircle(effectParameterOverlayHitTest(cursorPosition));
    }
    if (notifyEditor && !layerId.isNull() && !effectId.isNull()) {
        emit effectParameterOverlayEditFinished(layerId, effectId);
    }
    updateCursorManagerOverlay();
    updateToolCursor();
}

} // namespace ruwa::ui::workspace
