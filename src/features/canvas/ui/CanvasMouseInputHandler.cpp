// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   M O U S E   I N P U T   H A N D L E R
// ==========================================================================

#include "CanvasMouseInputHandler.h"
#include "CanvasInputHost.h"
#include "CanvasPanel.h"
#include "CanvasPanelHelpers.h"
#include "TextEditingController.h"

#include "features/transform/TransformState.h"
#include "features/layers/model/LayerModel.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushPackOverlay.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/canvas/stroke/StrokeInputQueue.h"
#include "features/canvas/ui/CanvasCursorManager.h"
#include "features/canvas/ui/CanvasPositionPickerOverlay.h"
#include "shared/resources/IconProvider.h"
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
#include <optional>

namespace ruwa::ui::workspace {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRotateViewSnapIncrement = kPi * 0.5f;
constexpr float kRotateViewSnapCaptureDistance = 2.5f * kPi / 180.0f;

struct MousePointerSample {
    bool stylusLike = false;
    bool isEraser = false;
    float pressure = 1.0f;
    QPointF tiltVector;
    bool tiltAvailable = false;
};

QPoint panelLocalPos(const CanvasPanel* panel, const QMouseEvent* event)
{
    if (!panel || !event) {
        return QPoint();
    }

    return panel->mapFromGlobal(event->globalPosition().toPoint());
}

MousePointerSample sampleMousePointer(QMouseEvent* event)
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
            if (const auto tiltVector = stylusInput.dispatchTiltVector()) {
                sample.tiltVector = *tiltVector;
                sample.tiltAvailable = true;
            }
        } else if (stylusInput.nativeCursorPosition()) {
            const auto snapshot = ruwa::services::input::StylusDebugService::instance()->snapshot();
            sample.stylusLike = true;
            sample.pressure = std::clamp(snapshot.winTabPressure, 0.0f, 1.0f);
        }
    }

    return sample;
}

ruwa::core::brushes::BrushInputDynamics pointerInputDynamics(const CanvasInputHost* host,
    const QPointF& globalPos, const MousePointerSample& sample)
{
    ruwa::core::brushes::BrushInputDynamics result;
    if (!host || !sample.tiltAvailable) {
        return result;
    }
    const aether::Vector2 origin = host->mapInputToViewportWorld(globalPos);
    const aether::Vector2 projected
        = host->mapInputToViewportWorld(globalPos + sample.tiltVector);
    const float dx = projected.x - origin.x;
    const float dy = projected.y - origin.y;
    if (std::hypot(dx, dy) <= 0.000001f) {
        return result;
    }
    constexpr float kPi = 3.14159265358979323846f;
    const float degrees = ruwa::core::brushes::normalizeAngleDegrees(
        std::atan2(dy, dx) * 180.0f / kPi);
    result.penTilt = degrees / 360.0f;
    result.penTiltAvailable = true;
    return result;
}

StrokeInputDevice strokeInputDeviceForSample(const MousePointerSample& sample)
{
    return sample.stylusLike ? StrokeInputDevice::Stylus : StrokeInputDevice::Mouse;
}

} // namespace

CanvasMouseInputHandler::CanvasMouseInputHandler(CanvasPanel* panel)
    : m_host(panel)
    , m_panel(panel)
{
}

void CanvasMouseInputHandler::clearPendingMoveToolContentHit()
{
    m_pendingMoveToolContentHit = false;
    m_pendingMoveToolContentLayerId = QUuid();
    m_pendingMoveToolContentPressGlobalPos = {};
    m_pendingMoveToolContentPressWorldPos = {};
    m_pendingMoveToolContentPressModifiers = Qt::NoModifier;
}

void CanvasMouseInputHandler::handleTextToolPress(const aether::Vector2& worldPos)
{
    auto* controller = m_panel ? m_panel->m_textEditingController : nullptr;
    if (!controller) {
        return;
    }

    m_textSelecting = false;

    if (auto* hit = controller->hitTextLayerAt(worldPos)) {
        // Only a session that actually opened can be drag-selected in.
        m_textSelecting = controller->startExistingLayer(hit->id, worldPos);
        return;
    }

    if (controller->isEditing()) {
        controller->commit();
        return;
    }
    m_textSelecting = controller->startNewLayerAt(worldPos);
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
        if (m_panel->isGlobalOverViewport(currentGlobal)) {
            const QPoint currentScreenPos = currentGlobal.toPoint();
            const auto recovered
                = ruwa::services::input::StylusInputManager::recoverMouseMoveHistory(
                    currentScreenPos);
            for (const auto& rp : recovered) {
                if (!m_panel->isGlobalOverViewport(rp.pos)) {
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
    return tool == ToolId::Brush || tool == ToolId::Eraser || tool == ToolId::Blur
        || tool == ToolId::Smudge || tool == ToolId::Liquify;
}

bool CanvasMouseInputHandler::beginBrushSizeAdjust(QMouseEvent* event)
{
    auto* hostWidget = m_host ? m_host->inputViewportHostWidget() : nullptr;
    auto* view = m_host ? m_host->inputView() : nullptr;
    if (!event || !m_panel || !hostWidget || !view || !m_host->inputRenderReady()) {
        return false;
    }

    const QPoint globalPos = event->globalPosition().toPoint();

    const QSizeF viewportExtent = view->viewportExtent();
    const qreal scaleX = hostWidget->width() > 0
        ? viewportExtent.width() / static_cast<qreal>(hostWidget->width())
        : 1.0;
    const qreal scaleY = hostWidget->height() > 0
        ? viewportExtent.height() / static_cast<qreal>(hostWidget->height())
        : 1.0;
    const QPoint localPos = hostWidget->mapFromGlobal(globalPos);

    m_brushSizeAdjust = true;
    // The ring is parked on the anchor for the whole drag, so the frame must
    // stop following the pointer with it.
    m_host->inputPresentation()->setCursorPositionPinned(true);
    m_brushSizeAnchorGlobal = globalPos;
    m_brushSizeLastGlobal = globalPos;
    m_brushSizeAnchorVx = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
    m_brushSizeAnchorVy = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
    m_brushSizeCursorScale = static_cast<float>((scaleX + scaleY) * 0.5);
    m_brushSizeStartRadius = m_host->inputPainting()->brushRadius();

    // Suppress the cursor manager so it stops moving the brush ring with the
    // mouse; then force the OS cursor to a horizontal resize arrow on top of
    // the fixed custom brush ring.
    if (auto* cursorManager = m_host->inputCursorManager()) {
        cursorManager->setSuppressed(true);
    }
    const QCursor resizeCursor(Qt::SizeHorCursor);
    hostWidget->setCursor(resizeCursor);
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
    auto* view = m_host ? m_host->inputView() : nullptr;
    if (!m_brushSizeAdjust || !m_panel || !view) {
        return;
    }
    m_brushSizeLastGlobal = globalPos;

    // Horizontal drag only: the radius grows to the right and shrinks to the
    // left, starting from the size the brush had when the drag began. The ring
    // stays centred on the anchor, so the pointer is a slider, not a rim grip.
    const float dx = static_cast<float>(globalPos.x() - m_brushSizeAnchorGlobal.x());

    const float zoom = static_cast<float>(view->zoom());
    const float deltaWorld = (zoom > 0.0f) ? (dx / zoom) : dx;
    const float worldRadius = std::max(0.0f, m_brushSizeStartRadius + deltaWorld);

    const QSize canvas = m_panel->canvasSize();
    const qreal normalized = ruwa::ui::widgets::normalizedSizeFromRadiusPxForCanvasMode(
        worldRadius, canvas.width(), canvas.height(), m_panel->hasFiniteDocumentBounds());
    m_panel->setBrushSizeNormalized(normalized);

    applyBrushSizeAdjustOverlay();
}

void CanvasMouseInputHandler::applyBrushSizeAdjustOverlay()
{
    auto* view = m_host ? m_host->inputView() : nullptr;
    auto* painting = m_host ? m_host->inputPainting() : nullptr;
    auto* presentation = m_host ? m_host->inputPresentation() : nullptr;
    if (!m_panel || !view || !painting || !presentation) {
        return;
    }
    const float zoom = static_cast<float>(view->zoom());
    const float radiusViewport = painting->brushRadius() * zoom * m_brushSizeCursorScale;
    presentation->setBrushCursorState(true, m_brushSizeAnchorVx, m_brushSizeAnchorVy, radiusViewport);
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
        if (auto* hostWidget = m_host->inputViewportHostWidget()) {
            hostWidget->unsetCursor();
        }
        if (auto* presentation = m_host->inputPresentation()) {
            presentation->setCursorPositionPinned(false);
            presentation->setBrushCursorState(false, 0, 0, 0);
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
    if (!m_host->inputRenderReady()) {
        return false;
    }
    auto* view = m_host->inputView();
    auto* editing = m_host->inputEditing();
    auto* transform = m_host->inputTransform();

    if (m_panel->isPositionPickerActive()) {
        const bool handToolActive = m_host->currentInputTool() == ToolId::Hand;
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
        event->accept();
        return true;
    }

    // Shift+Alt drags the brush size; plain Shift is the stroke axis lock, so
    // the size gesture needs both modifiers to stay out of its way.
    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier)
        && event->modifiers().testFlag(Qt::AltModifier) && !m_brushSizeAdjust
        && !m_host->isInputDrawingActive() && !m_panel->m_tabletActive
        && !transform->isActive() && isPaintingLikeTool()) {
        return beginBrushSizeAdjust(event);
    }

    const bool keepTextEditorFocus = m_host->currentInputTool() == ToolId::Text
        && m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing();
    if (!keepTextEditorFocus) {
        m_panel->setFocus();
    }
    const QPoint localPos = panelLocalPos(m_panel, event);

    // Transform mode mouse handling
    if (transform->isActive()) {
        if ((transform->isAutoApplying() || transform->hasPendingDiscreteActionAnimation())
            && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
            event->accept();
            return true;
        }
        if (event->button() == Qt::LeftButton) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            const QPointF documentPos(worldPos.x, worldPos.y);
            const float zoom = static_cast<float>(view->zoom());
            const auto hit = transform->hitTest(documentPos, zoom);

            transform->beginUndoStep();
            if (transform->pointerPress(documentPos, zoom, event->modifiers())) {
                transform->beginSnapSession();
                m_panel->m_transformDragCursorValid = true;
                m_panel->m_transformDragCursor = detail::cursorForTransformHandle(hit,
                    transform->cornersActAsRotationHandles(),
                    transform->isScaleMirroredHorizontally(),
                    transform->isScaleMirroredVertically(), view->contentFlipHorizontal(),
                    view->contentFlipVertical());
                if (auto* cursorManager = m_host->inputCursorManager()) {
                    cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
                    cursorManager->updateCursorPosition(event->globalPosition().toPoint());
                }
                event->accept();
                return true;
            }
            transform->discardUndoStep();
            m_panel->confirmTransform();
            event->accept();
            return true;
        } else if (event->button() == Qt::RightButton) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            if (!transform->isMoveOnly()
                && transform->containsDocumentPoint(QPointF(worldPos.x, worldPos.y))) {
                QVariantList actions;
                const auto currentMode = transform->interactionMode();

                QVariantMap classicAction;
                classicAction.insert(QStringLiteral("id"),
                    static_cast<int>(CanvasPanel::TransformActionModeClassic));
                classicAction.insert(QStringLiteral("text"), QObject::tr("Classic"));
                classicAction.insert(QStringLiteral("checked"),
                    currentMode == TransformInteractionMode::Classic);
                classicAction.insert(QStringLiteral("enabled"),
                    currentMode != TransformInteractionMode::Classic);
                actions.push_back(classicAction);

                QVariantMap deformAction;
                deformAction.insert(
                    QStringLiteral("id"), static_cast<int>(CanvasPanel::TransformActionModeDeform));
                deformAction.insert(QStringLiteral("text"), QObject::tr("Warp"));
                deformAction.insert(QStringLiteral("checked"),
                    currentMode == TransformInteractionMode::Deform);
                deformAction.insert(QStringLiteral("enabled"),
                    currentMode != TransformInteractionMode::Deform);
                actions.push_back(deformAction);

                // Mirroring shares the eased flip the selection popup plays, so it
                // rides along here as an icon-only pair under a separator.
                const bool canFlip = transform->canFlipContent();
                QVariantMap separator;
                separator.insert(QStringLiteral("separator"), true);
                actions.push_back(separator);

                QVariantList iconActions;
                QVariantMap flipHorizontalAction;
                flipHorizontalAction.insert(QStringLiteral("id"),
                    static_cast<int>(CanvasPanel::TransformActionFlipHorizontal));
                flipHorizontalAction.insert(QStringLiteral("standardIcon"),
                    static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::FlipHorizontal));
                flipHorizontalAction.insert(
                    QStringLiteral("tooltip"), QObject::tr("Flip horizontally"));
                flipHorizontalAction.insert(QStringLiteral("enabled"), canFlip);
                iconActions.push_back(flipHorizontalAction);

                QVariantMap flipVerticalAction;
                flipVerticalAction.insert(QStringLiteral("id"),
                    static_cast<int>(CanvasPanel::TransformActionFlipVertical));
                flipVerticalAction.insert(QStringLiteral("standardIcon"),
                    static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::FlipVertical));
                flipVerticalAction.insert(
                    QStringLiteral("tooltip"), QObject::tr("Flip vertically"));
                flipVerticalAction.insert(QStringLiteral("enabled"), canFlip);
                iconActions.push_back(flipVerticalAction);

                QVariantMap context;
                context.insert(QStringLiteral("simpleActions"), actions);
                context.insert(QStringLiteral("simpleIconActions"), iconActions);
                ruwa::ui::widgets::ContextMenuSystem::instance().showContextMenu(
                    ruwa::ui::widgets::ContextMenuType::SimpleActions,
                    event->globalPosition().toPoint(), context, m_panel);
                event->accept();
                return true;
            }

            event->accept();
            return true;
        }
        if (event->button() == Qt::MiddleButton) {
            m_panel->m_isPanning = true;
            m_panel->m_panButton = event->button();
            m_panel->m_lastMousePos = event->globalPosition();
            view->beginPanSampling();
            m_panel->updateToolCursor();
            event->accept();
            return true;
        }
    }

    if (event->button() == Qt::LeftButton) {
        m_panel->markTemporaryToolUsed();

        if (m_host->currentInputTool() == ToolId::CanvasResize) {
            if (m_panel->m_canvasResizeAwaitingRotationReset) {
                if (view->isCameraAnimating()) {
                    event->accept();
                    return true;
                }
                m_panel->m_canvasResizeAwaitingRotationReset = false;
            }
            if (!detail::isAngleEffectivelyZero(static_cast<float>(view->rotationRadians()))) {
                m_panel->m_canvasResizeAwaitingRotationReset = true;
                view->setRotationSmoothRadians(0.0);
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

        if (m_host->currentInputTool() == ToolId::Hand) {
            m_panel->m_isPanning = true;
            m_panel->m_panButton = Qt::LeftButton;
            m_panel->m_lastMousePos = event->globalPosition();
            view->beginPanSampling();
            m_panel->updateToolCursor();
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::Move) {
            // Move tool: move layer/selection content only (no pan)
            const bool pickLayerByContent = event->modifiers().testFlag(Qt::ControlModifier);
            if (pickLayerByContent && m_panel->m_layerModel) {
                const aether::Vector2 worldPos
                    = m_panel->mapToWorld(event->globalPosition().toPoint());
                const QUuid hitLayerId
                    = m_host->inputHitTesting()->movableContentLayerAt(QPointF(worldPos.x, worldPos.y));
                if (!hitLayerId.isNull() && !m_panel->m_layerModel->isSelected(hitLayerId)) {
                    m_pendingMoveToolContentHit = true;
                    m_pendingMoveToolContentLayerId = hitLayerId;
                    m_pendingMoveToolContentPressGlobalPos = event->globalPosition().toPoint();
                    m_pendingMoveToolContentPressWorldPos = worldPos;
                    m_pendingMoveToolContentPressModifiers = event->modifiers();
                    event->accept();
                    return true;
                }
            }

            if (transform->enterMoveOnly()) {
                aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
                const QPointF documentPos(worldPos.x, worldPos.y);
                const float zoom = static_cast<float>(view->zoom());
                transform->beginUndoStep();
                if (transform->pointerPress(documentPos, zoom, event->modifiers())) {
                    transform->beginSnapSession();
                    m_panel->m_transformDragCursorValid = true;
                    m_panel->m_transformDragCursor = Qt::SizeAllCursor;
                    if (auto* cursorManager = m_host->inputCursorManager()) {
                        cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
                        cursorManager->updateCursorPosition(event->globalPosition().toPoint());
                    }
                    event->accept();
                    return true;
                }
                transform->discardUndoStep();
                transform->cancel();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::Text) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            handleTextToolPress(worldPos);
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::Zoom) {
            view->stopCameraAnimation();
            m_panel->m_isZoomDragging = true;
            m_panel->m_zoomDragStartPos = localPos;
            m_panel->m_zoomDragStartValue = static_cast<float>(view->zoom());
            QPoint localPos
                = m_host->inputViewportHostWidget()->mapFromGlobal(event->globalPosition().toPoint());
            m_panel->m_zoomAnchorScreen = aether::Vector2(localPos.x(), localPos.y());
            m_panel->showZoomInfoOverlay();
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::RotateView) {
            view->stopCameraAnimation();
            m_panel->m_isRotatingView = true;
            auto* hostWidget = m_host->inputViewportHostWidget();
            const QPoint widgetPos = hostWidget->mapFromGlobal(event->globalPosition().toPoint());
            const QPoint center = hostWidget->rect().center();
            m_panel->m_rotateViewLastAngle
                = std::atan2(static_cast<float>(widgetPos.y() - center.y()),
                    static_cast<float>(widgetPos.x() - center.x()));
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::Eyedropper) {
            aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
            const int px = static_cast<int>(std::floor(worldPos.x));
            const int py = static_cast<int>(std::floor(worldPos.y));
            const bool fromLayerOnly = event->modifiers().testFlag(Qt::ControlModifier);
            QColor picked;
            if (fromLayerOnly) {
                picked = detail::sampleColorFromActiveLayer(
                    m_panel->m_layerModel, m_panel->accessCanvas(), px, py);
            } else if (!editing->sampleSceneColor(QPointF(worldPos.x, worldPos.y), picked)) {
                picked = detail::sampleColorFromLayerModel(
                    m_panel->m_layerModel, m_panel->accessCanvas(), px, py);
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
        if (m_host->currentInputTool() == ToolId::Fill) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            if (event->button() == Qt::LeftButton) {
                const int px = static_cast<int>(std::floor(worldPos.x));
                const int py = static_cast<int>(std::floor(worldPos.y));
                m_panel->requestFillAt(px, py);
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::ClassicFill) {
            const aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition().toPoint());
            if (event->button() == Qt::LeftButton) {
                const int px = static_cast<int>(std::floor(worldPos.x));
                const int py = static_cast<int>(std::floor(worldPos.y));
                m_panel->requestClassicFillAt(px, py);
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::LassoFill) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isLassoFillSelecting = true;
            editing->beginLassoFill(worldPos.x, worldPos.y);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::Lasso) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isLassoSelecting = true;
            m_panel->m_lassoAdd = event->modifiers().testFlag(Qt::ShiftModifier);
            m_panel->m_lassoSubtract = event->modifiers().testFlag(Qt::AltModifier);
            editing->beginLasso(
                worldPos.x, worldPos.y, m_panel->m_lassoAdd, m_panel->m_lassoSubtract);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::SquareSelection) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isRectSelecting = true;
            m_panel->m_rectAdd = event->modifiers().testFlag(Qt::ShiftModifier);
            m_panel->m_rectSubtract = event->modifiers().testFlag(Qt::AltModifier);
            editing->beginRectSelection(
                worldPos.x, worldPos.y, m_panel->m_rectAdd, m_panel->m_rectSubtract);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::CircleSelection) {
            ruwa::services::input::StylusInputManager::resetMouseMoveHistory();
            aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            m_panel->m_isCircleSelecting = true;
            m_panel->m_circleAdd = event->modifiers().testFlag(Qt::ShiftModifier);
            m_panel->m_circleSubtract = event->modifiers().testFlag(Qt::AltModifier);
            editing->beginCircleSelection(
                worldPos.x, worldPos.y, m_panel->m_circleAdd, m_panel->m_circleSubtract);
            if (QWidget::mouseGrabber() != m_panel) {
                m_panel->grabMouse();
            }
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::MagicWand) {
            const aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
            const bool addSelection = event->modifiers().testFlag(Qt::ShiftModifier);
            const bool subtractSelection = event->modifiers().testFlag(Qt::AltModifier);
            editing->performMagicWandSelection(static_cast<int>(std::floor(worldPos.x)),
                static_cast<int>(std::floor(worldPos.y)), addSelection, subtractSelection);
            event->accept();
            return true;
        }
        if (m_host->currentInputTool() == ToolId::Brush
            || m_host->currentInputTool() == ToolId::Blur
            || m_host->currentInputTool() == ToolId::Smudge
            || m_host->currentInputTool() == ToolId::Liquify
            || m_host->currentInputTool() == ToolId::Eraser) {
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
                        m_host->inputPainting()->endStroke();
                        m_panel->notifyStrokeSessionEnded();
                        m_panel->canvasContentChanged();
                    }
                } else {
                    event->accept();
                    return true;
                }
            }
            const MousePointerSample pointerSample = sampleMousePointer(event);
            const auto inputDynamics
                = pointerInputDynamics(m_panel, event->globalPosition(), pointerSample);
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
            m_host->inputPainting()->beginStroke(worldPos.x, worldPos.y, pointerSample.pressure,
                strokeInputDeviceForSample(pointerSample),
                event->modifiers().testFlag(Qt::ShiftModifier), inputDynamics);
            // Seed the recovered-point pressure interpolation from the click
            // sample so the very first coalesced batch lerps from real data.
            m_lastRealStrokePressure = pointerSample.pressure;
            m_lastRealStrokeElapsedSec = m_host->inputPainting()->strokeElapsedSecondsNow();
            m_lastRealStrokeInputDynamics = inputDynamics;
            m_lastRealStrokeSampleValid = true;
            m_panel->m_isDrawing = m_host->inputPainting()->isDrawing();
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
        // The radial menu opens on the press so the same right-click can be
        // held and dragged onto a seat; the widget claims the release.
        m_panel->showRadialMenu(event->globalPosition().toPoint(), true);
        event->accept();
        return true;
    }
    if (event->button() == Qt::MiddleButton) {
        m_panel->m_isPanning = true;
        m_panel->m_panButton = event->button();
        m_panel->m_lastMousePos = event->globalPosition();
        view->beginPanSampling();
        m_panel->updateToolCursor();
        event->accept();
        return true;
    }
    return false;
}

bool CanvasMouseInputHandler::handleMouseMove(QMouseEvent* event)
{
    auto* view = m_host->inputView();
    auto* editing = m_host->inputEditing();
    auto* transform = m_host->inputTransform();
    auto* painting = m_host->inputPainting();
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
    if (!m_host->inputRenderReady()) {
        if (view)
            view->endPanSampling();
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
        event->accept();
        return true;
    }
    auto& stylusInput = ruwa::services::input::StylusInputManager::instance();

    if (stylusInput.shouldIgnoreCanvasMouseMove(event)) {
        event->accept();
        return true;
    }

    const bool keepTextEditorFocus = m_host->currentInputTool() == ToolId::Text
        && m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing();
    if (!keepTextEditorFocus && !m_host->hasInputFocus() && m_host->isCursorOverCanvas()) {
        m_panel->setFocus();
    }

    if (cursorManager && transform->isActive()) {
        if (m_panel->m_isPanning) {
            cursorManager->setRequestedCursor(Qt::ClosedHandCursor);
        } else if (transform->isDragging() && m_panel->m_transformDragCursorValid) {
            cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
        } else {
            aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
            const float zoom = static_cast<float>(view->zoom());
            const auto hit = transform->hitTest(QPointF(worldPos.x, worldPos.y), zoom);
            cursorManager->setRequestedCursor(detail::cursorForTransformHandle(hit,
                transform->cornersActAsRotationHandles(),
                transform->isScaleMirroredHorizontally(),
                transform->isScaleMirroredVertically(), view->contentFlipHorizontal(),
                view->contentFlipVertical()));
        }
    }
    if (m_host->currentInputTool() == ToolId::CanvasResize && !transform->isActive()
        && m_panel->m_canvasResizeController && cursorManager) {
        cursorManager->setRequestedCursor(
            m_panel->m_canvasResizeController->cursorForPosition(globalPos));
    }

    if (cursorManager) {
        cursorManager->updateCursorPosition(globalPos);
    }

    {
        aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
        if (m_panel->isInfiniteCanvas() || m_panel->accessCanvas().contains(worldPos)) {
            m_panel->cursorPositionChanged(
                QPoint(static_cast<int>(worldPos.x), static_cast<int>(worldPos.y)));
        }
    }

    if (m_pendingMoveToolContentHit) {
        if (!(event->buttons() & Qt::LeftButton) || m_host->currentInputTool() != ToolId::Move
            || !m_panel->m_layerModel) {
            clearPendingMoveToolContentHit();
            return false;
        }

        if (globalPos == m_pendingMoveToolContentPressGlobalPos) {
            event->accept();
            return true;
        }

        const QUuid targetLayerId = m_pendingMoveToolContentLayerId;
        const aether::Vector2 pressWorldPos = m_pendingMoveToolContentPressWorldPos;
        const Qt::KeyboardModifiers pressModifiers = m_pendingMoveToolContentPressModifiers;
        clearPendingMoveToolContentHit();

        if (m_panel->m_layerModel->contains(targetLayerId)) {
            m_panel->m_layerModel->setSelectedLayer(targetLayerId);
            if (transform->enterMoveOnly()) {
                const float zoom = static_cast<float>(view->zoom());
                transform->beginUndoStep();
                if (transform->pointerPress(QPointF(pressWorldPos.x, pressWorldPos.y), zoom,
                        pressModifiers)) {
                    transform->beginSnapSession();
                    m_panel->m_transformDragCursorValid = true;
                    m_panel->m_transformDragCursor = Qt::SizeAllCursor;
                    if (cursorManager) {
                        cursorManager->setRequestedCursor(m_panel->m_transformDragCursor);
                    }

                    const aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
                    transform->latchSelectionCopyMove(QPointF(worldPos.x, worldPos.y));
                    if (transform->pointerMove(
                            QPointF(worldPos.x, worldPos.y), zoom, event->modifiers())) {
                        transform->syncMetricOverlays();
                        m_panel->requestRender();
                    }
                    event->accept();
                    return true;
                }
                transform->discardUndoStep();
                transform->cancel();
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
    if (m_textSelecting && m_panel->toolMode() == ToolId::Text && m_panel->m_textEditingController
        && m_panel->m_textEditingController->isEditing() && (event->buttons() & Qt::LeftButton)) {
        const aether::Vector2 worldPos = m_panel->mapToWorld(globalPos);
        m_panel->m_textEditingController->extendSelectionToWorld(worldPos);
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoFillSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->inputEditing()->updateLassoFill(x, y); });
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->inputEditing()->updateLasso(x, y); });
        event->accept();
        return true;
    }
    if (m_panel->m_isRectSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->inputEditing()->updateRectSelection(x, y); });
        m_panel->updateSelectionSizeOverlay();
        event->accept();
        return true;
    }
    if (m_panel->m_isCircleSelecting) {
        dispatchUncoalescedWorldMoves(
            event, [this](float x, float y) { m_panel->inputEditing()->updateCircleSelection(x, y); });
        event->accept();
        return true;
    }
    if (m_panel->m_isRotatingView) {
        QPoint widgetPos
            = m_panel->inputViewportHostWidget()->mapFromGlobal(event->globalPosition().toPoint());
        QPoint center = m_panel->inputViewportHostWidget()->rect().center();
        const float curAngle = std::atan2(static_cast<float>(widgetPos.y() - center.y()),
            static_cast<float>(widgetPos.x() - center.x()));
        const float deltaAngle
            = detail::normalizeAngleDelta(curAngle - m_panel->m_rotateViewLastAngle);
        m_panel->inputView()->addRotationRadians(static_cast<qreal>(deltaAngle));
        m_panel->m_rotateViewLastAngle = curAngle;
        m_panel->requestRender();
        if (m_panel->m_canvasResizeController && m_panel->m_canvasResizeController->isActive()) {
            m_panel->m_canvasResizeController->updateOverlay();
        }
        if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
            m_panel->m_textEditingController->notifyFormattingStateChanged();
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
        const float targetZoom = qBound(static_cast<float>(view->minZoom()),
            m_panel->m_zoomDragStartValue * std::exp(zoomExponent),
            static_cast<float>(view->maxZoom()));

        const float currentZoom = static_cast<float>(view->zoom());
        if (currentZoom > 0.0f) {
            float factor = targetZoom / currentZoom;
            factor = qBound(0.88f, factor, 1.12f);
            view->zoomAtViewportPoint(static_cast<qreal>(factor),
                QPointF(m_panel->m_zoomAnchorScreen.x, m_panel->m_zoomAnchorScreen.y));
            m_panel->zoomChanged(view->zoom());
            m_panel->showZoomInfoOverlay();
            m_panel->requestRender();
            if (m_panel->m_canvasResizeController
                && m_panel->m_canvasResizeController->isActive()) {
                m_panel->m_canvasResizeController->updateOverlay();
            }
            if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
                m_panel->m_textEditingController->notifyFormattingStateChanged();
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
                    m_panel->m_layerModel, m_panel->accessCanvas(), px, py);
            } else if (!editing->sampleSceneColor(QPointF(worldPos.x, worldPos.y), picked)) {
                picked = detail::sampleColorFromLayerModel(
                    m_panel->m_layerModel, m_panel->accessCanvas(), px, py);
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

        const MousePointerSample pointerSample = sampleMousePointer(event);
        const auto inputDynamics
            = pointerInputDynamics(m_panel, event->globalPosition(), pointerSample);
        const bool nativeDispatch = stylusInput.isDispatchingNativeInput();
        const std::optional<float> nativeElapsed
            = nativeDispatch ? stylusInput.dispatchStrokeElapsedSeconds() : std::nullopt;
        const float currentElapsedSec
            = nativeElapsed.value_or(painting->strokeElapsedSecondsNow());

        // Recover intermediate OS mouse positions. Skip this only while WinTab
        // owns the pointer; its packet buffer already contains those samples.
        if (!stylusInput.usesNativeUiRouting() || !stylusInput.nativeCursorPosition()) {
            const QPoint currentScreenPos = event->globalPosition().toPoint();
            if (m_panel->isGlobalOverViewport(currentScreenPos)) {
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
                        if (!m_panel->isGlobalOverViewport(rp.pos)) {
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
                        const auto recoveredInputDynamics
                            = ruwa::core::brushes::interpolateBrushInputDynamics(
                                m_lastRealStrokeSampleValid ? m_lastRealStrokeInputDynamics
                                                            : inputDynamics,
                                inputDynamics, t);
                        // Queued rather than rasterized on the spot: one
                        // GetMouseMovePointsEx batch can carry dozens of points,
                        // and rasterizing a burst inline puts work proportional
                        // to the DEVICE's report rate straight into the event
                        // handler. The queue applies the same frame budget the
                        // native path gets, and the real sample below drains it
                        // immediately, so nothing is deferred by doing this.
                        painting->queueStrokeAtElapsed(wp.x, wp.y, recoveredPressure,
                            recoveredElapsedSec, strokeInputDeviceForSample(pointerSample),
                            recoveredInputDynamics);
                    }
                }
            }
        }

        aether::Vector2 worldPos = m_panel->mapToViewportWorld(event->globalPosition());
        if (nativeDispatch) {
            // A single WT_PACKET notification can contain a large recovered
            // burst. Feed it into the engine's existing time-budgeted
            // queue so the native routing loop remains cheap and painting can
            // interleave with rasterization.
            painting->queueStrokeAtElapsed(worldPos.x, worldPos.y,
                pointerSample.pressure, currentElapsedSec,
                strokeInputDeviceForSample(pointerSample), inputDynamics);
        } else {
            painting->continueStroke(worldPos.x, worldPos.y, pointerSample.pressure,
                strokeInputDeviceForSample(pointerSample), inputDynamics);
        }
        // This real sample becomes the left anchor for the next batch's
        // pressure interpolation.
        m_lastRealStrokePressure = pointerSample.pressure;
        m_lastRealStrokeElapsedSec = currentElapsedSec;
        m_lastRealStrokeInputDynamics = inputDynamics;
        m_lastRealStrokeSampleValid = true;
        m_panel->canvasContentChanged();
        event->accept();
        return true;
    }
    if (transform->isActive() && transform->isDragging()) {
        aether::Vector2 worldPos = m_panel->mapToWorld(event->globalPosition());
        const QPointF documentPos(worldPos.x, worldPos.y);
        const float zoom = static_cast<float>(view->zoom());
        transform->latchSelectionCopyMove(documentPos);
        if (transform->pointerMove(documentPos, zoom, event->modifiers())) {
            transform->syncMetricOverlays();
            m_panel->requestRender();
        }
        event->accept();
        return true;
    }
    if (m_panel->m_isPanning) {
        // Camera pan is applied by the engine's frame-sampled pan
        // (beginPanSampling/endPanSampling): it reads the live pointer once
        // per VSync, so pan is synchronous with the display refresh.
        m_panel->m_lastMousePos = event->globalPosition();
        if (m_panel->m_canvasResizeController
            && m_panel->m_canvasResizeController->isActive()) {
            m_panel->m_canvasResizeController->updateOverlay();
        }
        if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
            m_panel->m_textEditingController->notifyFormattingStateChanged();
        }
        event->accept();
        return true;
    }
    return false;
}

bool CanvasMouseInputHandler::handleMouseRelease(QMouseEvent* event)
{
    auto* view = m_host->inputView();
    auto* editing = m_host->inputEditing();
    auto* transform = m_host->inputTransform();
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
    if (!m_host->inputRenderReady()) {
        if (view)
            view->endPanSampling();
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
    if (transform->isActive() && transform->isDragging()
        && event->button() == Qt::LeftButton) {
        const bool hadTransformGuides
            = transform->isMoveAxisGuideActive() || transform->isSnapGuideActive();
        transform->pointerRelease();
        transform->syncMetricOverlays();
        transform->commitUndoStep();
        m_panel->m_transformDragCursorValid = false;
        if (hadTransformGuides) {
            m_panel->requestRender();
        }
        // Move tool: apply the move-only transform immediately on mouse release
        // instead of leaving it live until a single click or tool change.
        if (transform->isMoveOnly()) {
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
        editing->endLassoFill();
        m_panel->canvasContentChanged();
        event->accept();
        return true;
    }
    if (m_panel->m_isLassoSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isLassoSelecting = false;
        editing->endLasso(m_panel->m_lassoAdd, m_panel->m_lassoSubtract);
        m_panel->m_lassoAdd = false;
        m_panel->m_lassoSubtract = false;
        event->accept();
        return true;
    }
    if (m_panel->m_isRectSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isRectSelecting = false;
        editing->endRectSelection(m_panel->m_rectAdd, m_panel->m_rectSubtract);
        m_panel->m_rectAdd = false;
        m_panel->m_rectSubtract = false;
        m_panel->hideSelectionSizeOverlay();
        event->accept();
        return true;
    }
    if (m_panel->m_isCircleSelecting && event->button() == Qt::LeftButton) {
        m_panel->m_isCircleSelecting = false;
        editing->endCircleSelection(m_panel->m_circleAdd, m_panel->m_circleSubtract);
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
        if (view->snapRotationRadiansSmooth(
                static_cast<qreal>(kRotateViewSnapIncrement),
                static_cast<qreal>(kRotateViewSnapCaptureDistance))) {
            m_panel->requestRender();
        }
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
        m_host->inputPainting()->endStroke();
        // Finalize a still-draining stroke before any mode flag moves: endStroke()
        // flattens the whole buffer and reads those flags when it does.
        m_panel->notifyStrokeSessionEnded();
        m_panel->setEraseMode(m_panel->shouldEraseForTool(m_panel->toolMode()));
        m_panel->canvasContentChanged();
        event->accept();
        return true;
    }
    if (m_panel->m_isPanning && event->button() == m_panel->m_panButton) {
        m_panel->m_isPanning = false;
        m_panel->m_panButton = Qt::NoButton;
        if (view)
            view->endPanSampling();
        m_panel->updateToolCursor();
        event->accept();
        return true;
    }
    return false;
}

bool CanvasMouseInputHandler::handleMouseDoubleClick(QMouseEvent* event)
{
    auto* view = m_host->inputView();
    if (m_brushSizeAdjust) {
        if (event) {
            event->accept();
        }
        return true;
    }
    if (!view || !m_host->inputRenderReady() || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_host->currentInputTool() == ToolId::Zoom) {
        const qreal currentZoom = view->zoom();
        if (currentZoom > 0.0) {
            const QSizeF viewportSize = view->viewportExtent();
            const QPointF centerScreen(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
            view->zoomAtViewportPointSmooth(1.0 / currentZoom, centerScreen);
            m_panel->zoomChanged(view->zoom());
            m_panel->showZoomInfoOverlay();
            m_panel->requestRender();
        }
        event->accept();
        return true;
    }
    if (m_host->currentInputTool() == ToolId::RotateView) {
        view->setRotationSmoothRadians(0.0);
        m_panel->requestRender();
        event->accept();
        return true;
    }
    return false;
}

} // namespace ruwa::ui::workspace
