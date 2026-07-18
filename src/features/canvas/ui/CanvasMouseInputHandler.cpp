// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   M O U S E   I N P U T   H A N D L E R
// ==========================================================================

#include "CanvasMouseInputHandler.h"
#include "CanvasInputHost.h"
#include "CanvasPanel.h"
#include "CanvasPanelHelpers.h"
#include "TextEditingController.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/transform/TransformState.h"
#include "features/layers/model/LayerModel.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushPackOverlay.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/canvas/ui/CanvasCursorManager.h"
#include "features/canvas/ui/CanvasPositionPickerOverlay.h"
#include "shared/tiles/TileBrush.h"
#include "shell/context-menu/ContextMenuSystem.h"
#include "services/input/StylusDebugService.h"
#include "services/input/StylusInputManager.h"

#include <QApplication>
#include <QInputDevice>
#include <QMouseEvent>
#include <QPointF>
#include <QPointingDevice>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>
#include <Qt>
#include <algorithm>
#include <cmath>
#include <memory>

namespace ruwa::ui::workspace {
namespace {

struct MousePointerSample {
    bool stylusLike = false;
    bool isEraser = false;
    float pressure = 1.0f;
};

QPoint panelLocalPos(const CanvasPanel* panel, const QMouseEvent* event)
{
    if (!panel || !event) {
        return QPoint();
    }

    return panel->mapFromGlobal(event->globalPosition().toPoint());
}

MousePointerSample sampleMousePointer(aether::OpenGLCanvasWidget* canvasWidget, QMouseEvent* event)
{
    MousePointerSample sample;
    if (!event) {
        return sample;
    }

    const auto deviceType = event->deviceType();
    const auto pointerType = event->pointerType();
    const auto* device = event->pointingDevice();

    const bool pointerIsFinger = pointerType == QPointingDevice::PointerType::Finger;
    const bool deviceIsTouchSurface = deviceType == QInputDevice::DeviceType::TouchScreen
        || deviceType == QInputDevice::DeviceType::TouchPad;
    const bool deviceHasPressure
        = device && device->hasCapability(QInputDevice::Capability::Pressure);
    const bool pointerIsStylus = pointerType == QPointingDevice::PointerType::Pen
        || pointerType == QPointingDevice::PointerType::Eraser
        || pointerType == QPointingDevice::PointerType::Cursor;
    const bool deviceIsStylus = deviceType == QInputDevice::DeviceType::Stylus
        || deviceType == QInputDevice::DeviceType::Airbrush
        || deviceType == QInputDevice::DeviceType::Puck;
    const bool directTouchContact
        = pointerIsFinger || (deviceIsTouchSurface && !pointerIsStylus && !deviceHasPressure);

    // Some pen displays arrive through Qt as mouse events from a touch surface.
    // Treat stylus pointers on those devices as pen input; only explicit finger
    // touches should stay on the non-pressure path.
    sample.stylusLike
        = !directTouchContact && (pointerIsStylus || deviceIsStylus || deviceHasPressure);
    sample.isEraser = pointerType == QPointingDevice::PointerType::Eraser;
    if (sample.stylusLike && !event->points().isEmpty()) {
        sample.pressure
            = std::clamp(static_cast<float>(event->points().first().pressure()), 0.0f, 1.0f);
    }

    if (!sample.stylusLike
        && ruwa::services::input::StylusInputManager::instance().usesNativeUiRouting()) {
        auto& stylusInput = ruwa::services::input::StylusInputManager::instance();
        if (stylusInput.isDispatchingNativeInput()) {
            // Use the per-packet pressure that StylusInputManager stored before
            // dispatching this specific synthetic mouse event.  Reading the
            // snapshot here would return the pressure of the LAST packet in the
            // batch, causing pressure-driven size/opacity artifacts on all intermediate points.
            sample.stylusLike = true;
            sample.pressure = std::clamp(stylusInput.dispatchPressure(), 0.0f, 1.0f);
        } else if (stylusInput.nativeCursorPosition()) {
            const auto snapshot = ruwa::services::input::StylusDebugService::instance()->snapshot();
            sample.stylusLike = true;
            sample.pressure = std::clamp(snapshot.winTabPressure, 0.0f, 1.0f);
        }
    }

    (void) canvasWidget;
    return sample;
}

aether::BrushStrokeHost::StrokeInputDevice strokeInputDeviceForSample(
    const MousePointerSample& sample)
{
    return sample.stylusLike ? aether::BrushStrokeHost::StrokeInputDevice::Stylus
                             : aether::BrushStrokeHost::StrokeInputDevice::Mouse;
}

} // namespace

CanvasMouseInputHandler::CanvasMouseInputHandler(CanvasPanel* panel)
    : m_host(panel)
    , m_panel(panel)
{
}

void CanvasMouseInputHandler::beginPendingRightClick(QMouseEvent* event)
{
    if (!event) {
        return;
    }

    m_pendingRightClick = true;
    m_pendingRightClickPressPos = event->globalPosition().toPoint();
    m_pendingRightClickLastGlobalPos = m_pendingRightClickPressPos;
}

void CanvasMouseInputHandler::updatePendingRightClick(QMouseEvent* event)
{
    if (!m_pendingRightClick || !event) {
        return;
    }

    m_pendingRightClickLastGlobalPos = event->globalPosition().toPoint();
}

bool CanvasMouseInputHandler::consumePendingRightClick(QMouseEvent* event)
{
    if (!m_pendingRightClick || !event || event->button() != Qt::RightButton) {
        return false;
    }

    updatePendingRightClick(event);
    const bool shouldShowPopup = m_pendingRightClick;
    const QPoint popupGlobalPos = m_pendingRightClickLastGlobalPos;
    cancelPendingRightClick();

    if (shouldShowPopup && m_panel) {
        m_panel->showBrushQuickPopup(popupGlobalPos);
        return true;
    }
    return false;
}

void CanvasMouseInputHandler::cancelPendingRightClick()
{
    m_pendingRightClick = false;
    m_pendingRightClickPressPos = {};
    m_pendingRightClickLastGlobalPos = {};
}

void CanvasMouseInputHandler::clearPendingMoveToolContentHit()
{
    m_pendingMoveToolContentHit = false;
    m_pendingMoveToolContentLayerId = QUuid();
    m_pendingMoveToolContentPressGlobalPos = {};
    m_pendingMoveToolContentPressWorldPos = {};
}

void CanvasMouseInputHandler::handleTextToolPress(const aether::Vector2& worldPos)
{
    auto* controller = m_panel ? m_panel->m_textEditingController : nullptr;
    if (!controller) {
        return;
    }

    m_textSelecting = false;

    if (auto* hit = controller->hitTextLayerAt(worldPos)) {
        controller->startExistingLayer(hit->id, worldPos);
        m_textSelecting = true;
        return;
    }

    if (controller->isEditing()) {
        controller->commit();
        return;
    }
    controller->startNewLayerAt(worldPos);
    m_textSelecting = true;
}

void CanvasMouseInputHandler::dispatchUncoalescedWorldMoves(
    QMouseEvent* event, const std::function<void(float, float)>& applyWorld)
{
    if (!m_panel || !event) {
        return;
    }
    auto& stylusInput = ruwa::services::input::StylusInputManager::instance();
    if (!stylusInput.usesNativeUiRouting() || !stylusInput.nativeCursorPosition()) {
        const QPointF currentGlobal = event->globalPosition();
        // Outside the GL viewport, WM_MOUSEMOVE history often mixes unrelated screen samples
        // (second monitor, chrome). Per-point clamping turns those into fake edge tours.
        if (m_panel->isGlobalOverGlViewport(currentGlobal)) {
            const QPoint currentScreenPos = currentGlobal.toPoint();
            const auto recovered
                = ruwa::services::input::StylusInputManager::recoverMouseMoveHistory(
                    currentScreenPos);
            for (const auto& rp : recovered) {
                if (!m_panel->isGlobalOverGlViewport(rp.pos)) {
                    continue;
                }
                const aether::Vector2 wp = m_panel->mapToViewportWorld(rp.pos);
                applyWorld(wp.x, wp.y);
            }
        }
    }
    const aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
    applyWorld(worldPos.x, worldPos.y);
}

bool CanvasMouseInputHandler::isPaintingLikeTool() const
{
    if (!m_host)
        return false;
    const auto tool = m_host->currentInputTool();
    return tool == CanvasPanel::ToolMode::Brush || tool == CanvasPanel::ToolMode::Eraser
        || tool == CanvasPanel::ToolMode::Blur || tool == CanvasPanel::ToolMode::Smudge
        || tool == CanvasPanel::ToolMode::Liquify;
}

bool CanvasMouseInputHandler::beginBrushSizeAdjust(QMouseEvent* event)
{
    auto* gl = m_host ? m_host->inputGlWidget() : nullptr;
    if (!event || !m_panel || !gl || !gl->isInitialized()) {
        return false;
    }

    const QPoint globalPos = event->globalPosition().toPoint();

    const qreal scaleX = gl->width() > 0
        ? static_cast<qreal>(gl->viewport().width()) / static_cast<qreal>(gl->width())
        : 1.0;
    const qreal scaleY = gl->height() > 0
        ? static_cast<qreal>(gl->viewport().height()) / static_cast<qreal>(gl->height())
        : 1.0;
    const QPoint localPos = gl->mapFromGlobal(globalPos);

    m_brushSizeAdjust = true;
    m_brushSizeAnchorGlobal = globalPos;
    m_brushSizeAnchorVx = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
    m_brushSizeAnchorVy = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
    m_brushSizeCursorScale = static_cast<float>((scaleX + scaleY) * 0.5);

    // Suppress the cursor manager so it stops moving the brush ring with the
    // mouse; then force the OS cursor to a horizontal resize arrow on top of
    // the fixed custom brush ring.
    if (auto* cursorManager = m_host->inputCursorManager()) {
        cursorManager->setSuppressed(true);
    }
    const QCursor resizeCursor(Qt::SizeHorCursor);
    gl->setCursor(resizeCursor);
    m_panel->setCursor(resizeCursor);

    if (QWidget::mouseGrabber() != m_panel) {
        m_panel->grabMouse();
    }

    applyBrushSizeAdjustOverlay();
    event->accept();
    return true;
}

void CanvasMouseInputHandler::updateBrushSizeAdjust(const QPoint& globalPos)
{
    auto* gl = m_host ? m_host->inputGlWidget() : nullptr;
    if (!m_brushSizeAdjust || !m_panel || !gl) {
        return;
    }

    const float dx = static_cast<float>(globalPos.x() - m_brushSizeAnchorGlobal.x());
    const float dy = static_cast<float>(globalPos.y() - m_brushSizeAnchorGlobal.y());
    const float distancePx = std::sqrt(dx * dx + dy * dy);

    const float zoom = gl->viewport().camera().zoom();
    const float worldRadius = (zoom > 0.0f) ? (distancePx / zoom) : distancePx;

    const QSize canvas = m_panel->canvasSize();
    const qreal normalized = ruwa::ui::widgets::normalizedSizeFromRadiusPxForCanvasMode(
        worldRadius, canvas.width(), canvas.height(), m_panel->hasFiniteDocumentBounds());
    m_panel->setBrushSizeNormalized(normalized);

    applyBrushSizeAdjustOverlay();
}

void CanvasMouseInputHandler::applyBrushSizeAdjustOverlay()
{
    auto* gl = m_host ? m_host->inputGlWidget() : nullptr;
    if (!m_panel || !gl) {
        return;
    }
    const float zoom = gl->viewport().camera().zoom();
    const float radiusViewport = gl->brush().radius() * zoom * m_brushSizeCursorScale;
    gl->setBrushCursorState(true, m_brushSizeAnchorVx, m_brushSizeAnchorVy, radiusViewport);
}

void CanvasMouseInputHandler::endBrushSizeAdjust()
{
    if (!m_brushSizeAdjust) {
        return;
    }
    m_brushSizeAdjust = false;

    if (m_panel) {
        if (QWidget::mouseGrabber() == m_panel) {
            m_panel->releaseMouse();
        }
        if (auto* gl = m_host->inputGlWidget()) {
            gl->unsetCursor();
            gl->setBrushCursorState(false, 0, 0, 0);
        }
        m_panel->unsetCursor();
        if (auto* cursorManager = m_host->inputCursorManager()) {
            cursorManager->setSuppressed(false);
            cursorManager->refreshCursorPosition();
        }
    }
}

bool CanvasMouseInputHandler::handleMousePress(QMouseEvent* event)
{
    auto* glWidget = m_host->inputGlWidget();
    if (!glWidget || !glWidget->isInitialized()) {
        return false;
    }

    if (m_panel->isPositionPickerActive()) {
        const bool handToolActive = m_host->currentInputTool() == CanvasPanel::ToolMode::Hand;
        if (event->button() == Qt::LeftButton && !handToolActive) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            m_panel->commitPositionPicking(QPointF(worldPos.x, worldPos.y));
            event->accept();
            return true;
        }
        if (event->button() != Qt::MiddleButton
            && !(event->button() == Qt::LeftButton && handToolActive)) {
            // Right-click (or any other button) backs out of picking, matching
            // the "right-click to back out" convention used elsewhere.
            m_panel->cancelPositionPicking();
            event->accept();
            return true;
        }
        // Middle-click (any tool) or left-click while the Hand tool is active
        // pans the canvas instead — a navigation gesture, not a pick, so (like
        // the Hand-tool exemption in setToolMode) it falls through to the
        // normal pan-start handling below instead of ending the session.
    }

    if (m_panel->temporaryMoveToolUndoCooldownActive()) {
        clearPendingMoveToolContentHit();
        cancelPendingRightClick();
        event->accept();
        return true;
    }

    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier)
        && !m_brushSizeAdjust && !m_host->isInputDrawingActive() && !m_panel->m_tabletActive
        && !glWidget->isTransformActive() && isPaintingLikeTool()) {
        return beginBrushSizeAdjust(event);
    }

    const bool keepTextEditorFocus = m_host->currentInputTool() == CanvasPanel::ToolMode::Text
        && m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing();
    if (!keepTextEditorFocus) {
        m_panel->setFocus();
    }
    const QPoint localPos = panelLocalPos(m_panel, event);

    // Transform mode mouse handling
    if (glWidget->isTransformActive()) {
        if (event->button() == Qt::LeftButton) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            float zoom = glWidget->viewport().camera().zoom();
            auto& ctrl = glWidget->transformController();
            const auto hit = ctrl.hitTestDetailed(worldPos, zoom);

            glWidget->beginTransformUndoStep();
            if (ctrl.mousePress(worldPos, zoom, event->modifiers())) {
                m_panel->m_transformDragCursorValid = true;
                m_panel->m_transformDragCursor = detail::cursorForTransformHandle(hit, ctrl.state(),
                    ctrl.cornersActAsRotationHandles(), glWidget->canvasContentFlipHorizontal(),
                    glWidget->canvasContentFlipVertical());
                if (auto* cursorManager = m_host->inputCursorManager()) {
                    cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
                    cursorManager->updateCursorPosition(event->globalPosition().toPoint());
                }
                event->accept();
                return true;
            }
            glWidget->discardTransformUndoStep();
            m_panel->confirmTransform();
            event->accept();
            return true;
        } else if (event->button() == Qt::RightButton) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            auto* gl = glWidget;
            auto& ctrl = gl->transformController();
            if (!gl->isMoveOnlyTransform() && ctrl.state().pointInTransformedRect(worldPos)) {
                QVariantList actions;
                const auto currentMode = gl->transformInteractionMode();

                QVariantMap classicAction;
                classicAction.insert(QStringLiteral("id"),
                    static_cast<int>(CanvasPanel::TransformActionModeClassic));
                classicAction.insert(QStringLiteral("text"), QObject::tr("Classic"));
                classicAction.insert(QStringLiteral("checked"),
                    currentMode == aether::TransformInteractionMode::Classic);
                classicAction.insert(QStringLiteral("enabled"),
                    currentMode != aether::TransformInteractionMode::Classic);
                actions.push_back(classicAction);

                QVariantMap deformAction;
                deformAction.insert(
                    QStringLiteral("id"), static_cast<int>(CanvasPanel::TransformActionModeDeform));
                deformAction.insert(QStringLiteral("text"), QObject::tr("Deform"));
                deformAction.insert(QStringLiteral("checked"),
                    currentMode == aether::TransformInteractionMode::Deform);
                deformAction.insert(QStringLiteral("enabled"),
                    currentMode != aether::TransformInteractionMode::Deform);
                actions.push_back(deformAction);

                QVariantMap context;
                context.insert(QStringLiteral("simpleActions"), actions);
                ruwa::ui::widgets::ContextMenuSystem::instance().showContextMenu(
                    ruwa::ui::widgets::ContextMenuType::SimpleActions,
                    event->globalPosition().toPoint(), context, m_panel);
                event->accept();
                return true;
            }

            cancelPendingRightClick();
            event->accept();
            return true;
        }
        if (event->button() == Qt::MiddleButton) {
            m_panel->m_isPanning = true;
            m_panel->m_panButton = event->button();
            m_panel->m_lastMousePos = event->globalPosition();
            glWidget->beginPanSampling();
            m_panel->updateToolCursor();
            event->accept();
            return true;
        }
    }

    if (event->button() == Qt::LeftButton) {
        if (m_panel->m_tempToolHold.active) {
            m_panel->m_tempToolHold.toolWasUsed = true;
        }

        if (m_host->currentInputTool() == CanvasPanel::ToolMode::CanvasResize) {
            auto& cam = glWidget->viewport().camera();
            if (m_panel->m_canvasResizeAwaitingRotationReset) {
                if (cam.isAnimating()) {
                    event->accept();
                    return true;
                }
                m_panel->m_canvasResizeAwaitingRotationReset = false;
            }
            if (!detail::isAngleEffectivelyZero(cam.rotation())) {
                m_panel->m_canvasResizeAwaitingRotationReset = true;
                cam.setRotationSmooth(0.0f);
                m_panel->requestRender();
                event->accept();
                return true;
            }

            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            const QPoint globalPos = event->globalPosition().toPoint();
            if (m_panel->m_canvasResizeController
                && m_panel->m_canvasResizeController->handleMousePress(
                    worldPos, globalPos, localPos, event->button())) {
                m_panel->m_canvasResizePreviewSize
                    = m_panel->m_canvasResizeController->targetCanvasSize();
                m_panel->syncToolStateOverlayContent();
                event->accept();
                return true;
            }
        }

        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Hand) {
            m_panel->m_isPanning = true;
            m_panel->m_panButton = Qt::LeftButton;
            m_panel->m_lastMousePos = event->globalPosition();
            glWidget->beginPanSampling();
            m_panel->updateToolCursor();
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Move) {
            // Move tool: move layer/selection content only (no pan)
            const bool pickLayerByContent = event->modifiers().testFlag(Qt::ControlModifier);
            if (pickLayerByContent && m_panel->m_layerModel) {
                const aether::Vector2 worldPos
                    = m_panel->mapToWorld(event->globalPosition().toPoint());
                const QUuid hitLayerId = glWidget->moveToolContentLayerAt(worldPos);
                if (!hitLayerId.isNull() && !m_panel->m_layerModel->isSelected(hitLayerId)) {
                    m_pendingMoveToolContentHit = true;
                    m_pendingMoveToolContentLayerId = hitLayerId;
                    m_pendingMoveToolContentPressGlobalPos = event->globalPosition().toPoint();
                    m_pendingMoveToolContentPressWorldPos = worldPos;
                    event->accept();
                    return true;
                }
            }

            if (glWidget->enterMoveOnlyTransformMode()) {
                aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
                float zoom = glWidget->viewport().camera().zoom();
                auto& ctrl = glWidget->transformController();
                glWidget->beginTransformUndoStep();
                if (ctrl.mousePress(worldPos, zoom, event->modifiers())) {
                    m_panel->m_transformDragCursorValid = true;
                    m_panel->m_transformDragCursor = Qt::SizeAllCursor;
                    if (auto* cursorManager = m_host->inputCursorManager()) {
                        cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
                        cursorManager->updateCursorPosition(event->globalPosition().toPoint());
                    }
                    event->accept();
                    return true;
                }
                glWidget->discardTransformUndoStep();
                glWidget->cancelTransform();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Text) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            handleTextToolPress(worldPos);
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Zoom) {
            glWidget->viewport().camera().stopAnimation();
            m_panel->m_isZoomDragging = true;
            m_panel->m_zoomDragStartPos = localPos;
            m_panel->m_zoomDragStartValue = glWidget->viewport().camera().zoom();
            QPoint localPos = glWidget->mapFromGlobal(event->globalPosition().toPoint());
            m_panel->m_zoomAnchorScreen = aether::Vector2(localPos.x(), localPos.y());
            m_panel->showZoomInfoOverlay();
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::RotateView) {
            m_panel->m_isRotatingView = true;
            const QPoint widgetPos = glWidget->mapFromGlobal(event->globalPosition().toPoint());
            const QPoint center = glWidget->rect().center();
            m_panel->m_rotateViewLastAngle
                = std::atan2(static_cast<float>(widgetPos.y() - center.y()),
                    static_cast<float>(widgetPos.x() - center.x()));
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Eyedropper) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            const int px = static_cast<int>(std::floor(worldPos.x));
            const int py = static_cast<int>(std::floor(worldPos.y));
            const bool fromLayerOnly = event->modifiers().testFlag(Qt::ControlModifier);
            QColor picked;
            if (fromLayerOnly) {
                picked = detail::sampleColorFromActiveLayer(
                    m_panel->m_layerModel, glWidget->canvas(), px, py);
            } else if (!glWidget->sampleColorFromScene(worldPos.x, worldPos.y, picked)) {
                picked = detail::sampleColorFromLayerModel(
                    m_panel->m_layerModel, glWidget->canvas(), px, py);
            }
            uint8_t r, g, b, alphaToUse;
            if (fromLayerOnly) {
                alphaToUse = static_cast<uint8_t>(picked.alpha());
                if (alphaToUse == 0) {
                    const QColor cur = m_panel->currentBrushColor();
                    r = cur.red();
                    g = cur.green();
                    b = cur.blue();
                } else {
                    r = static_cast<uint8_t>(picked.red());
                    g = static_cast<uint8_t>(picked.green());
                    b = static_cast<uint8_t>(picked.blue());
                }
            } else {
                r = static_cast<uint8_t>(picked.red());
                g = static_cast<uint8_t>(picked.green());
                b = static_cast<uint8_t>(picked.blue());
                alphaToUse = m_panel->brushAlpha();
            }
            m_panel->setBrushColor(r, g, b, alphaToUse);
            if (fromLayerOnly) {
                m_panel->setBrushOpacityNormalized(alphaToUse / 255.0);
            }
            m_panel->colorPicked(picked);
            m_panel->m_isEyedropping = true;
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Fill) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            if (event->button() == Qt::LeftButton) {
                const int px = static_cast<int>(std::floor(worldPos.x));
                const int py = static_cast<int>(std::floor(worldPos.y));
                glWidget->performFill(px, py);
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::ClassicFill) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            if (event->button() == Qt::LeftButton) {
                const int px = static_cast<int>(std::floor(worldPos.x));
                const int py = static_cast<int>(std::floor(worldPos.y));
                glWidget->performClassicFill(px, py);
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::LassoFill) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isLassoFillSelecting = true;
            glWidget->beginLassoFill(worldPos.x, worldPos.y);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Lasso) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isLassoSelecting = true;
            m_panel->m_lassoAdd = event->modifiers().testFlag(Qt::ShiftModifier);
            m_panel->m_lassoSubtract = event->modifiers().testFlag(Qt::AltModifier);
            glWidget->beginLasso(
                worldPos.x, worldPos.y, m_panel->m_lassoAdd, m_panel->m_lassoSubtract);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::SquareSelection) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isRectSelecting = true;
            m_panel->m_rectAdd = event->modifiers().testFlag(Qt::ShiftModifier);
            m_panel->m_rectSubtract = event->modifiers().testFlag(Qt::AltModifier);
            glWidget->beginRectSelection(
                worldPos.x, worldPos.y, m_panel->m_rectAdd, m_panel->m_rectSubtract);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::CircleSelection) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isCircleSelecting = true;
            m_panel->m_circleAdd = event->modifiers().testFlag(Qt::ShiftModifier);
            m_panel->m_circleSubtract = event->modifiers().testFlag(Qt::AltModifier);
            glWidget->beginCircleSelection(
                worldPos.x, worldPos.y, m_panel->m_circleAdd, m_panel->m_circleSubtract);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == CanvasPanel::ToolMode::Brush
            || m_host->currentInputTool() == CanvasPanel::ToolMode::Blur
            || m_host->currentInputTool() == CanvasPanel::ToolMode::Smudge
            || m_host->currentInputTool() == CanvasPanel::ToolMode::Liquify
            || m_host->currentInputTool() == CanvasPanel::ToolMode::Eraser) {
            if (m_panel->m_tabletActive) {
                // An authoritative real-mouse press means the tablet is no longer
                // active (missed TabletRelease). Reset the flag so the mouse can
                // draw. A direct WinTab dispatch remains blocked while its tablet
                // stroke is in progress.
                auto& stylusInput = ruwa::services::input::StylusInputManager::instance();
                if ((!stylusInput.usesNativeUiRouting() || !stylusInput.nativeCursorPosition())
                    && event->deviceType() == QInputDevice::DeviceType::Mouse) {
                    m_panel->m_tabletActive = false;
                    // End the orphaned tablet stroke so beginStroke below starts
                    // clean, with proper undo state.
                    if (m_panel->m_isDrawing) {
                        m_panel->m_isDrawing = false;
                        m_panel->m_glWidget->endStroke();
                        m_panel->canvasContentChanged();
                    }
                } else {
                    event->accept();
                    return true;
                }
            }
            const MousePointerSample pointerSample = sampleMousePointer(glWidget, event);
            if (m_panel->m_brushOverlay) {
                if (auto* packOverlay = m_panel->m_brushOverlay->brushPackOverlay()) {
                    if (!packOverlay->isUserMoved()) {
                        packOverlay->hidePanel();
                    }
                }
            }
            m_panel->setEraseMode(
                pointerSample.isEraser || m_panel->shouldEraseForTool(m_host->currentInputTool()));
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            glWidget->beginStroke(worldPos.x, worldPos.y, pointerSample.pressure,
                strokeInputDeviceForSample(pointerSample));
            // Seed the recovered-point pressure interpolation from the click
            // sample so the very first coalesced batch lerps from real data.
            m_lastRealStrokePressure = pointerSample.pressure;
            m_lastRealStrokeElapsedSec = glWidget->strokeElapsedSecondsNow();
            m_lastRealStrokeSampleValid = true;
            m_panel->m_isDrawing = glWidget->isDrawing();
            if (!m_panel->m_isDrawing) {
                m_panel->showBlockedDrawMessageForSelectedLayer();
            } else if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        return false;
    }
    if (event->button() == Qt::RightButton) {
        beginPendingRightClick(event);
        event->accept();
        return true;
    }
    if (event->button() == Qt::MiddleButton) {
        if (glWidget) {
            m_panel->m_isPanning = true;
            m_panel->m_panButton = event->button();
            m_panel->m_lastMousePos = event->globalPosition();
            glWidget->beginPanSampling();
            m_panel->updateToolCursor();
        }
        event->accept();
        return true;
    }
    return false;
}

bool CanvasMouseInputHandler::handleMouseMove(QMouseEvent* event)
{
    auto* glWidget = m_host->inputGlWidget();
    auto* cursorManager = m_host->inputCursorManager();

    if (m_panel->isPositionPickerActive()) {
        // Keep the "position this click would pick" readout live even while
        // panning (Hand-tool LMB or middle-click drag) — otherwise it freezes
        // at wherever the pan started instead of tracking the cursor.
        if (m_panel->m_positionPickerOverlay) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            m_panel->m_positionPickerOverlay->setDocumentPosition(QPointF(worldPos.x, worldPos.y));
            if (auto* overlayParent = m_panel->m_positionPickerOverlay->parentWidget()) {
                m_panel->m_positionPickerOverlay->followCursor(
                    overlayParent->mapFromGlobal(event->globalPosition().toPoint()));
            }
        }
        if (!m_panel->m_isPanning) {
            event->accept();
            return true;
        }
        // Panning: fall through to the normal pan-drag handling below (see
        // handleMousePress's Hand-tool/middle-click exemptions) so the camera
        // keeps moving; the overlay refresh above already covered this move.
    }

    if (m_brushSizeAdjust) {
        if (event) {
            updateBrushSizeAdjust(event->globalPosition().toPoint());
            event->accept();
        }
        return true;
    }
    if (!glWidget || !glWidget->isInitialized()) {
        if (glWidget)
            glWidget->endPanSampling();
        cancelPendingRightClick();
        m_panel->m_isPanning = false;
        m_panel->m_isZoomDragging = false;
        m_panel->m_isRotatingView = false;
        if (m_panel->m_canvasResizeController) {
            m_panel->m_canvasResizeController->resetInteractionState();
        }
        m_panel->m_isLassoSelecting = false;
        m_panel->m_isLassoFillSelecting = false;
        m_panel->m_isRectSelecting = false;
        m_panel->m_isCircleSelecting = false;
        m_panel->m_isDrawing = false;
        m_panel->hideSelectionSizeOverlay();
        if (m_panel->m_isEyedropping)
            m_panel->persistGlobalToolState();
        m_panel->m_isEyedropping = false;
        return false;
    }
    const QPoint globalPos = event->globalPosition().toPoint();
    const QPoint localPos = panelLocalPos(m_panel, event);
    if (m_panel->temporaryMoveToolUndoCooldownActive()) {
        clearPendingMoveToolContentHit();
        cancelPendingRightClick();
        event->accept();
        return true;
    }
    updatePendingRightClick(event);
    auto& stylusInput = ruwa::services::input::StylusInputManager::instance();

    if (stylusInput.shouldIgnoreCanvasMouseMove(event)) {
        event->accept();
        return true;
    }

    const bool keepTextEditorFocus = m_host->currentInputTool() == CanvasPanel::ToolMode::Text
        && m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing();
    if (!keepTextEditorFocus && !m_host->hasInputFocus() && m_host->isCursorOverCanvas()) {
        m_panel->setFocus();
    }

    if (cursorManager && glWidget && glWidget->isTransformActive()) {
        if (m_panel->m_isPanning) {
            cursorManager->setRequestedCursor(Qt::ClosedHandCursor);
        } else if (glWidget->transformController().isDragging()
            && m_panel->m_transformDragCursorValid) {
            cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
        } else {
            aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
            float zoom = glWidget->viewport().camera().zoom();
            auto& tc = glWidget->transformController();
            const auto hit = tc.hitTestDetailed(worldPos, zoom);
            cursorManager->setRequestedCursor(detail::cursorForTransformHandle(hit, tc.state(),
                tc.cornersActAsRotationHandles(), glWidget->canvasContentFlipHorizontal(),
                glWidget->canvasContentFlipVertical()));
        }
    }
    if (m_host->currentInputTool() == CanvasPanel::ToolMode::CanvasResize && glWidget
        && !glWidget->isTransformActive() && m_panel->m_canvasResizeController && cursorManager) {
        cursorManager->setRequestedCursor(
            m_panel->m_canvasResizeController->cursorForPosition(globalPos));
    }

    if (cursorManager) {
        cursorManager->updateCursorPosition(globalPos);
    }

    if (glWidget && glWidget->isInitialized()) {
        aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
        if (glWidget->isInfiniteCanvas() || glWidget->canvas().contains(worldPos)) {
            m_panel->cursorPositionChanged(
                QPoint(static_cast<int>(worldPos.x), static_cast<int>(worldPos.y)));
        }
    }

    if (m_pendingMoveToolContentHit) {
        if (!(event->buttons() & Qt::LeftButton)
            || m_host->currentInputTool() != CanvasPanel::ToolMode::Move || !m_panel->m_layerModel
            || !glWidget) {
            clearPendingMoveToolContentHit();
            return false;
        }

        if (globalPos == m_pendingMoveToolContentPressGlobalPos) {
            event->accept();
            return true;
        }

        const QUuid targetLayerId = m_pendingMoveToolContentLayerId;
        const aether::Vector2 pressWorldPos = m_pendingMoveToolContentPressWorldPos;
        clearPendingMoveToolContentHit();

        if (m_panel->m_layerModel->contains(targetLayerId)) {
            m_panel->m_layerModel->setSelectedLayer(targetLayerId);
            if (glWidget->enterMoveOnlyTransformMode()) {
                auto& ctrl = glWidget->transformController();
                const auto& viewport = glWidget->viewport();
                const float zoom = viewport.camera().zoom();
                glWidget->beginTransformUndoStep();
                if (ctrl.mousePress(pressWorldPos, zoom, event->modifiers())) {
                    m_panel->m_transformDragCursorValid = true;
                    m_panel->m_transformDragCursor = Qt::SizeAllCursor;
                    if (cursorManager) {
                        cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
                    }

                    const aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
                    aether::TransformSnapContext snapContext;
                    snapContext.canvasSize = glWidget->canvas().size();
                    snapContext.snapToCanvasCenter = true;
                    snapContext.snapToCanvasEdges = glWidget->hasFiniteDocumentBounds();
                    glWidget->latchSelectionCopyMoveTransformIfNeeded(worldPos, event->modifiers());
                    if (ctrl.mouseMove(
                            worldPos, zoom, event->modifiers(), &viewport, &snapContext)) {
                        m_panel->requestRender();
                    }
                    event->accept();
                    return true;
                }
                glWidget->discardTransformUndoStep();
                glWidget->cancelTransform();
            }
        }

        event->accept();
        return true;
    }

    if (m_panel->m_spaceSelectionMoveActive && m_host->isAnySelectionInteractionActive()) {
        m_panel->moveActiveSelectionWithSpace(globalPos);
        event->accept();
        return true;
    }
    if (m_panel->m_spaceStrokeMoveActive && m_host->isInputDrawingActive()) {
        m_panel->moveActiveStrokeWithSpace(globalPos);
        event->accept();
        return true;
    }

    if (m_panel->m_canvasResizeController
        && m_panel->m_canvasResizeController->handleMouseMove(
            m_panel->mapToWorld(event->globalPosition()), event->globalPosition().toPoint(),
            localPos)) {
        m_panel->m_canvasResizePreviewSize = m_panel->m_canvasResizeController->targetCanvasSize();
        m_panel->syncToolStateOverlayContent();
        event->accept();
        return true;
    }
    if (m_textSelecting && m_panel->toolMode() == CanvasPanel::ToolMode::Text
        && m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()
        && (event->buttons() & Qt::LeftButton)) {
        const aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
        m_panel->m_textEditingController->extendSelectionToWorld(worldPos);
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoFillSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->m_glWidget->updateLassoFill(x, y); });
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->m_glWidget->updateLasso(x, y); });
        event->accept();
        return true;
    }
    if (m_panel->m_isRectSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->m_glWidget->updateRectSelection(x, y); });
        m_panel->updateSelectionSizeOverlay();
        event->accept();
        return true;
    }
    if (m_panel->m_isCircleSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->m_glWidget->updateCircleSelection(x, y); });
        event->accept();
        return true;
    }
    if (m_panel->m_isRotatingView) {
        QPoint widgetPos = m_panel->m_glWidget->mapFromGlobal(event->globalPosition().toPoint());
        QPoint center = m_panel->m_glWidget->rect().center();
        const float curAngle = std::atan2(static_cast<float>(widgetPos.y() - center.y()),
            static_cast<float>(widgetPos.x() - center.x()));
        const float deltaAngle
            = detail::normalizeAngleDelta(curAngle - m_panel->m_rotateViewLastAngle);
        auto& cam = m_panel->m_glWidget->viewport().camera();
        cam.addRotation(deltaAngle);
        m_panel->m_rotateViewLastAngle = curAngle;
        m_panel->requestRender();
        if (m_panel->m_canvasResizeController && m_panel->m_canvasResizeController->isActive()) {
            m_panel->m_canvasResizeController->updateOverlay();
        }
        if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
            m_panel->m_textEditingController->refreshFormattingPopup();
        }
        event->accept();
        return true;
    }
    if (m_panel->m_isZoomDragging) {
        const QPoint delta = localPos - m_panel->m_zoomDragStartPos;
        const float dragDistance
            = std::sqrt(static_cast<float>(delta.x() * delta.x() + delta.y() * delta.y()));
        const float direction = delta.y() <= 0 ? 1.0f : -1.0f;
        const float zoomExponent = direction * dragDistance * 0.0025f;
        const float targetZoom = qBound(m_panel->m_glWidget->viewport().camera().minZoom(),
            m_panel->m_zoomDragStartValue * std::exp(zoomExponent),
            m_panel->m_glWidget->viewport().camera().maxZoom());

        auto& cam = m_panel->m_glWidget->viewport().camera();
        const float currentZoom = cam.zoom();
        if (currentZoom > 0.0f) {
            float factor = targetZoom / currentZoom;
            factor = qBound(0.88f, factor, 1.12f);
            const aether::Vector2 viewportSize(
                static_cast<float>(m_panel->m_glWidget->viewport().width()),
                static_cast<float>(m_panel->m_glWidget->viewport().height()));
            cam.zoomAt(factor, m_panel->m_zoomAnchorScreen, viewportSize);
            m_panel->zoomChanged(static_cast<qreal>(cam.zoom()));
            m_panel->showZoomInfoOverlay();
            m_panel->requestRender();
            if (m_panel->m_canvasResizeController
                && m_panel->m_canvasResizeController->isActive()) {
                m_panel->m_canvasResizeController->updateOverlay();
            }
            if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
                m_panel->m_textEditingController->refreshFormattingPopup();
            }
        }
        event->accept();
        return true;
    }
    if (m_panel->m_isEyedropping) {
        const bool fromLayerOnly = event->modifiers().testFlag(Qt::ControlModifier);
        const int intervalMs = fromLayerOnly ? 32 : 8;
        const bool shouldUpdate = [this, intervalMs] {
            if (!m_panel->m_eyedropperUpdateTimer.isValid()) {
                m_panel->m_eyedropperUpdateTimer.start();
                return true;
            }
            if (m_panel->m_eyedropperUpdateTimer.elapsed() >= intervalMs) {
                m_panel->m_eyedropperUpdateTimer.restart();
                return true;
            }
            return false;
        }();
        if (shouldUpdate) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            const int px = static_cast<int>(std::floor(worldPos.x));
            const int py = static_cast<int>(std::floor(worldPos.y));
            const bool fromLayerOnly = event->modifiers().testFlag(Qt::ControlModifier);
            QColor picked;
            if (fromLayerOnly) {
                picked = detail::sampleColorFromActiveLayer(
                    m_panel->m_layerModel, m_panel->m_glWidget->canvas(), px, py);
            } else if (!m_panel->m_glWidget->sampleColorFromScene(worldPos.x, worldPos.y, picked)) {
                picked = detail::sampleColorFromLayerModel(
                    m_panel->m_layerModel, m_panel->m_glWidget->canvas(), px, py);
            }
            uint8_t r, g, b, alphaToUse;
            if (fromLayerOnly) {
                alphaToUse = static_cast<uint8_t>(picked.alpha());
                if (alphaToUse == 0) {
                    const QColor cur = m_panel->currentBrushColor();
                    r = cur.red();
                    g = cur.green();
                    b = cur.blue();
                } else {
                    r = static_cast<uint8_t>(picked.red());
                    g = static_cast<uint8_t>(picked.green());
                    b = static_cast<uint8_t>(picked.blue());
                }
            } else {
                r = static_cast<uint8_t>(picked.red());
                g = static_cast<uint8_t>(picked.green());
                b = static_cast<uint8_t>(picked.blue());
                alphaToUse = m_panel->brushAlpha();
            }
            m_panel->setBrushColor(r, g, b, alphaToUse);
            if (fromLayerOnly) {
                m_panel->setBrushOpacityNormalized(alphaToUse / 255.0);
            }
            m_panel->colorPicked(picked);
        }
        event->accept();
        return true;
    }
    if (m_panel->m_isDrawing) {
        if (m_panel->m_tabletActive) {
            // Real mouse move while tablet supposedly active — tablet probably
            // went away without a proper TabletRelease.  Let the mouse through.
            if ((!stylusInput.usesNativeUiRouting() || !stylusInput.nativeCursorPosition())
                && event->deviceType() == QInputDevice::DeviceType::Mouse) {
                m_panel->m_tabletActive = false;
            } else {
                event->accept();
                return true;
            }
        }

        // While WinTab owns the pointer, the ONLY valid stroke samples are the
        // synthetic events dispatched by StylusInputManager. Other mouse moves
        // are delayed cursor warps or Qt synthesis and would add out-of-order
        // positions to the brush engine.
        if (stylusInput.usesNativeUiRouting() && stylusInput.nativeCursorPosition()
            && !stylusInput.isDispatchingNativeInput()) {
            // Proximity alone is insufficient: a wheel or real mouse event may
            // already have transferred ownership while the pen is still nearby.
            event->accept();
            return true;
        }

        const MousePointerSample pointerSample = sampleMousePointer(m_panel->m_glWidget, event);
        const float currentElapsedSec = m_panel->m_glWidget->strokeElapsedSecondsNow();

        // Recover intermediate OS mouse positions. Skip this only while WinTab
        // owns the pointer; its packet buffer already contains those samples.
        if (!stylusInput.usesNativeUiRouting() || !stylusInput.nativeCursorPosition()) {
            const QPoint currentScreenPos = event->globalPosition().toPoint();
            if (m_panel->isGlobalOverGlViewport(currentScreenPos)) {
                const auto recovered
                    = ruwa::services::input::StylusInputManager::recoverMouseMoveHistory(
                        currentScreenPos);
                if (!recovered.empty()) {
                    // Anchor recovered points in stroke time using their
                    // WM timestamps relative to the current point from the
                    // same GetMouseMovePointsEx batch. Without this they all collapse to the
                    // current wall-clock instant (Δt≈0) and the
                    // stabilizer treats them as a single burst, then
                    // jumps a long way on the next non-recovered event
                    // — visible as polygon edges on small brushes.

                    // Recovered positions carry no pressure of their own.
                    // Feeding them all the current sample's pressure turns
                    // pressure into a per-event step function — the brush
                    // width holds flat across the whole coalesced batch then
                    // jumps at the next real event, i.e. a visible
                    // "staircase". Instead interpolate from the previous real
                    // pointer sample's pressure to the current one, along the
                    // same time axis used to anchor the positions above.
                    const float prevPressure = m_lastRealStrokeSampleValid
                        ? m_lastRealStrokePressure
                        : pointerSample.pressure;
                    const float prevElapsedSec = m_lastRealStrokeSampleValid
                        ? m_lastRealStrokeElapsedSec
                        : currentElapsedSec;
                    const float elapsedSpan = currentElapsedSec - prevElapsedSec;
                    const std::size_t recoveredCount = recovered.size();
                    std::size_t recoveredIndex = 0;
                    for (const auto& rp : recovered) {
                        ++recoveredIndex;
                        if (!m_panel->isGlobalOverGlViewport(rp.pos)) {
                            continue;
                        }
                        const aether::Vector2 wp = m_panel->mapToViewportWorld(rp.pos);
                        // wm timestamps wrap mod 2^32; deltas stay sane.
                        const long deltaMs = static_cast<long>(rp.currentWmTimeMs - rp.wmTimeMs);
                        const float recoveredElapsedSec
                            = currentElapsedSec - static_cast<float>(deltaMs) / 1000.0f;
                        // Time-based fraction when the batch spans real time;
                        // fall back to even index spacing if the timestamps
                        // collapse (Δt≈0).
                        const float t = (elapsedSpan > 1e-4f)
                            ? std::clamp(
                                  (recoveredElapsedSec - prevElapsedSec) / elapsedSpan, 0.0f, 1.0f)
                            : static_cast<float>(recoveredIndex)
                                / static_cast<float>(recoveredCount + 1);
                        const float recoveredPressure
                            = prevPressure + (pointerSample.pressure - prevPressure) * t;
                        m_panel->m_glWidget->continueStrokeAtElapsed(wp.x, wp.y, recoveredPressure,
                            recoveredElapsedSec, strokeInputDeviceForSample(pointerSample));
                    }
                }
            }
        }

        aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
        m_panel->m_glWidget->continueStroke(worldPos.x, worldPos.y, pointerSample.pressure,
            strokeInputDeviceForSample(pointerSample));
        // This real sample becomes the left anchor for the next batch's
        // pressure interpolation.
        m_lastRealStrokePressure = pointerSample.pressure;
        m_lastRealStrokeElapsedSec = currentElapsedSec;
        m_lastRealStrokeSampleValid = true;
        m_panel->canvasContentChanged();
        event->accept();
        return true;
    }
    if (m_panel->m_glWidget->isTransformActive()
        && m_panel->m_glWidget->transformController().isDragging()) {
        aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
        const auto& viewport = m_panel->m_glWidget->viewport();
        float zoom = viewport.camera().zoom();
        aether::TransformSnapContext snapContext;
        snapContext.canvasSize = m_panel->m_glWidget->canvas().size();
        snapContext.snapToCanvasCenter = true;
        snapContext.snapToCanvasEdges = m_panel->m_glWidget->hasFiniteDocumentBounds();
        m_panel->m_glWidget->latchSelectionCopyMoveTransformIfNeeded(worldPos, event->modifiers());
        if (m_panel->m_glWidget->transformController().mouseMove(
                worldPos, zoom, event->modifiers(), &viewport, &snapContext)) {
            m_panel->requestRender();
        }
        event->accept();
        return true;
    }
    if (m_panel->m_isPanning) {
        // Camera pan is applied in paintGL via OpenGLCanvasWidget pan sampling
        // (beginPanSampling/endPanSampling). The widget reads QCursor::pos()
        // once per VSync, so pan is synchronous with the display refresh.
        m_panel->m_lastMousePos = event->globalPosition();
        if (m_panel->m_glWidget && m_panel->m_canvasResizeController
            && m_panel->m_canvasResizeController->isActive()) {
            m_panel->m_canvasResizeController->updateOverlay();
        }
        if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
            m_panel->m_textEditingController->refreshFormattingPopup();
        }
        event->accept();
        return true;
    }
    return false;
}

bool CanvasMouseInputHandler::handleMouseRelease(QMouseEvent* event)
{
    auto* glWidget = m_host->inputGlWidget();
    if (m_brushSizeAdjust) {
        if (event && event->button() == Qt::LeftButton) {
            endBrushSizeAdjust();
            event->accept();
            return true;
        }
        // Keep the adjust mode active for any other button release.
        if (event) {
            event->accept();
        }
        return true;
    }
    if (!glWidget || !glWidget->isInitialized()) {
        if (glWidget)
            glWidget->endPanSampling();
        m_panel->m_isPanning = false;
        m_panel->m_isZoomDragging = false;
        m_panel->m_isRotatingView = false;
        if (m_panel->m_canvasResizeController) {
            m_panel->m_canvasResizeController->resetInteractionState();
        }
        m_panel->m_isLassoSelecting = false;
        m_panel->m_isLassoFillSelecting = false;
        m_panel->m_isRectSelecting = false;
        m_panel->m_isCircleSelecting = false;
        m_panel->m_isDrawing = false;
        m_panel->hideSelectionSizeOverlay();
        if (m_panel->m_isEyedropping)
            m_panel->persistGlobalToolState();
        m_panel->m_isEyedropping = false;
        return false;
    }
    if (m_panel->temporaryMoveToolUndoCooldownActive()) {
        clearPendingMoveToolContentHit();
        cancelPendingRightClick();
        event->accept();
        return true;
    }
    if (consumePendingRightClick(event)) {
        event->accept();
        return true;
    }
    if (m_pendingMoveToolContentHit && event->button() == Qt::LeftButton) {
        clearPendingMoveToolContentHit();
        event->accept();
        return true;
    }
    if (m_textSelecting && event->button() == Qt::LeftButton) {
        m_textSelecting = false;
        // Don't consume — let other handlers run, but selection extension stops here.
    }
    if (m_panel->m_glWidget->isTransformActive()
        && m_panel->m_glWidget->transformController().isDragging()
        && event->button() == Qt::LeftButton) {
        const bool hadTransformGuides
            = m_panel->m_glWidget->transformController().moveAxisGuideActive()
            || m_panel->m_glWidget->transformController().autoSnapGuideState().active();
        m_panel->m_glWidget->transformController().mouseRelease();
        m_panel->m_glWidget->commitTransformUndoStep();
        m_panel->m_transformDragCursorValid = false;
        if (hadTransformGuides) {
            m_panel->requestRender();
        }
        // Move tool: apply the move-only transform immediately on mouse release
        // instead of leaving it live until a single click or tool change.
        if (m_panel->m_glWidget->isMoveOnlyTransform()) {
            m_panel->confirmTransform();
        }
        event->accept();
        return true;
    }

    if (m_panel->m_canvasResizeController
        && m_panel->m_canvasResizeController->handleMouseRelease(
            m_panel->mapToWorld(event->globalPosition().toPoint()),
            event->globalPosition().toPoint(), event->button())) {
        m_panel->m_canvasResizePreviewSize = m_panel->m_canvasResizeController->targetCanvasSize();
        m_panel->syncToolStateOverlayContent();
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoFillSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isLassoFillSelecting = false;
        m_panel->m_glWidget->endLassoFill();
        m_panel->canvasContentChanged();
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isLassoSelecting = false;
        m_panel->m_glWidget->endLasso(m_panel->m_lassoAdd, m_panel->m_lassoSubtract);
        m_panel->m_lassoAdd = false;
        m_panel->m_lassoSubtract = false;
        event->accept();
        return true;
    }
    if (m_panel->m_isRectSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isRectSelecting = false;
        m_panel->m_glWidget->endRectSelection(m_panel->m_rectAdd, m_panel->m_rectSubtract);
        m_panel->m_rectAdd = false;
        m_panel->m_rectSubtract = false;
        m_panel->hideSelectionSizeOverlay();
        event->accept();
        return true;
    }
    if (m_panel->m_isCircleSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isCircleSelecting = false;
        m_panel->m_glWidget->endCircleSelection(m_panel->m_circleAdd, m_panel->m_circleSubtract);
        m_panel->m_circleAdd = false;
        m_panel->m_circleSubtract = false;
        event->accept();
        return true;
    }
    if (m_panel->m_isEyedropping && event->button() == Qt::LeftButton) {
        m_panel->m_isEyedropping = false;
        m_panel->persistGlobalToolState();
        event->accept();
        return true;
    }
    if (m_panel->m_isRotatingView && event->button() == Qt::LeftButton) {
        m_panel->m_isRotatingView = false;
        event->accept();
        return true;
    }
    if (m_panel->m_isZoomDragging && event->button() == Qt::LeftButton) {
        m_panel->m_isZoomDragging = false;
        event->accept();
        return true;
    }
    if (m_panel->m_isDrawing && event->button() == Qt::LeftButton) {
        m_panel->m_isDrawing = false;
        if (m_panel->m_spaceStrokeMoveActive) {
            m_panel->endSpaceStrokeMove();
        }
        m_panel->m_glWidget->endStroke();
        m_panel->setEraseMode(m_panel->shouldEraseForTool(m_panel->toolMode()));
        m_panel->canvasContentChanged();
        event->accept();
        return true;
    }
    if (m_panel->m_isPanning && event->button() == m_panel->m_panButton) {
        m_panel->m_isPanning = false;
        m_panel->m_panButton = Qt::NoButton;
        if (m_panel->m_glWidget)
            m_panel->m_glWidget->endPanSampling();
        m_panel->updateToolCursor();
        event->accept();
        return true;
    }
    if (event->button() == Qt::RightButton) {
        cancelPendingRightClick();
    }
    return false;
}

bool CanvasMouseInputHandler::handleMouseDoubleClick(QMouseEvent* event)
{
    auto* glWidget = m_host->inputGlWidget();
    if (m_brushSizeAdjust) {
        if (event) {
            event->accept();
        }
        return true;
    }
    if (!glWidget || !glWidget->isInitialized() || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_host->currentInputTool() == CanvasPanel::ToolMode::Zoom) {
        auto& cam = glWidget->viewport().camera();
        const float currentZoom = cam.zoom();
        if (currentZoom > 0.0f) {
            const aether::Vector2 viewportSize(static_cast<float>(glWidget->viewport().width()),
                static_cast<float>(glWidget->viewport().height()));
            const aether::Vector2 centerScreen(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
            cam.zoomAtSmooth(1.0f / currentZoom, centerScreen, viewportSize);
            m_panel->zoomChanged(static_cast<qreal>(cam.zoom()));
            m_panel->showZoomInfoOverlay();
            m_panel->requestRender();
        }
        event->accept();
        return true;
    }
    if (m_host->currentInputTool() == CanvasPanel::ToolMode::RotateView) {
        auto& cam = glWidget->viewport().camera();
        cam.setRotationSmooth(0.0f);
        m_panel->requestRender();
        event->accept();
        return true;
    }
    return false;
}

} // namespace ruwa::ui::workspace
