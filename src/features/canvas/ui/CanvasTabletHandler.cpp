// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   T A B L E T   H A N D L E R
// ==========================================================================

#include "CanvasTabletHandler.h"
#include "CanvasInputHost.h"
#include "CanvasPanel.h"

#include "services/input/StylusDebugService.h"
#include "services/input/StylusInputManager.h"
#include "features/layers/model/LayerModel.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushPackOverlay.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasStylusJoystickWidget.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "features/selection/SelectionActionPopup.h"
#include "features/color/ColorPicker.h"
#include "features/color/ColorPickerOverlay.h"
#include "features/canvas/ui/CanvasCursorManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QRect>
#include <QString>
#include <QTabletEvent>
#include <QWidget>

#include <cmath>

namespace ruwa::ui::workspace {
namespace {

QString tabletEventTypeName(QEvent::Type type)
{
    switch (type) {
    case QEvent::TabletPress:
        return QStringLiteral("TabletPress");
    case QEvent::TabletMove:
        return QStringLiteral("TabletMove");
    case QEvent::TabletRelease:
        return QStringLiteral("TabletRelease");
    default:
        return QStringLiteral("TabletOther");
    }
}

bool nearlySameScreenPoint(const QPointF& a, const QPointF& b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return dx * dx + dy * dy < 0.0001;
}

ruwa::core::brushes::BrushInputDynamics inputDynamicsFromScreenTilt(
    const CanvasInputHost* host, const QPointF& globalPos, const QPointF& screenDirection)
{
    ruwa::core::brushes::BrushInputDynamics result;
    if (!host || std::hypot(screenDirection.x(), screenDirection.y()) <= 0.000001) {
        return result;
    }

    constexpr double kPi = 3.14159265358979323846;
    const aether::Vector2 origin = host->mapInputToViewportWorld(globalPos);
    const aether::Vector2 projected
        = host->mapInputToViewportWorld(globalPos + screenDirection);
    const float worldDx = projected.x - origin.x;
    const float worldDy = projected.y - origin.y;
    if (std::hypot(worldDx, worldDy) <= 0.000001f) {
        return result;
    }

    float degrees = static_cast<float>(std::atan2(worldDy, worldDx) * 180.0 / kPi);
    degrees = ruwa::core::brushes::normalizeAngleDegrees(degrees);
    result.penTilt = degrees / 360.0f;
    result.penTiltAvailable = true;
    return result;
}

ruwa::core::brushes::BrushInputDynamics tabletInputDynamics(
    const CanvasInputHost* host, const QTabletEvent* event, const QPointF& globalPos)
{
    if (!host || !event) {
        return {};
    }

    // QTabletEvent reports angles in two perpendicular planes. Their tangents,
    // not the angles themselves, are the components of the pen's projected
    // direction. This remains geometrically correct at the large tilts where a
    // direct atan2(yTilt, xTilt) is noticeably biased.
    constexpr double kPi = 3.14159265358979323846;
    const double tiltX = std::tan(static_cast<double>(event->xTilt()) * kPi / 180.0);
    const double tiltY = std::tan(static_cast<double>(event->yTilt()) * kPi / 180.0);
    const double projectedLength = std::hypot(tiltX, tiltY);
    constexpr double kMinimumReliableTiltDegrees = 0.5;
    if (!std::isfinite(projectedLength)
        || projectedLength < std::tan(kMinimumReliableTiltDegrees * kPi / 180.0)) {
        return {};
    }

    // Transform the direction through the same viewport mapping as the stroke.
    // That makes tilt follow canvas rotation and mirror transforms instead of
    // remaining incorrectly locked to the physical screen.
    return inputDynamicsFromScreenTilt(
        host, globalPos, QPointF(tiltX / projectedLength, tiltY / projectedLength));
}

} // namespace

bool CanvasTabletHandler::globalPosOverViewport(
    const CanvasInputHost* host, const QPointF& globalPos)
{
    return host && host->isGlobalOverInputViewport(globalPos);
}

CanvasTabletHandler::CanvasTabletHandler(CanvasPanel* panel)
    : m_host(panel)
    , m_panel(panel)
{
}

void CanvasTabletHandler::handleTabletEvent(QTabletEvent* event)
{
    if (ruwa::services::input::StylusInputManager::instance().handleTabletEvent(m_panel, event)) {
        return;
    }

    if (!m_host->inputRenderReady()) {
        event->ignore();
        return;
    }

    // Grab keyboard focus on hover so hotkeys work without clicking
    if (!m_host->hasInputFocus() && m_host->isCursorOverCanvas()) {
        m_panel->setFocus();
    }

    // Use high-precision coordinates for smoother strokes
    const QPointF globalPosF = event->globalPosition();
    const float pressure
        = ruwa::services::input::StylusInputManager::instance().effectivePressure(event);
    const auto inputDynamics = tabletInputDynamics(m_host, event, globalPosF);
    ruwa::services::input::StylusDebugService::instance()->updateQtTabletState(
        static_cast<float>(event->pressure()), pressure, true, tabletEventTypeName(event->type()));
    const Qt::MouseButtons tabletButtons = event->buttons();

    // Save previous button state for side-button detection, then update immediately
    // so that all early-return paths still record the latest state.
    const Qt::MouseButtons prevTabletButtons = m_host->previousTabletButtons();
    m_host->setPreviousTabletButtons(tabletButtons);
    const bool physicalPenContact
        = (event->pointerType() == QPointingDevice::PointerType::Pen
              || event->pointerType() == QPointingDevice::PointerType::Eraser)
        && pressure > 0.0f;
    const bool tabletContact = event->type() != QEvent::TabletRelease
        && (tabletButtons.testFlag(Qt::LeftButton) || event->button() == Qt::LeftButton
            || physicalPenContact
            || ((event->type() == QEvent::TabletPress || m_host->isInputTabletActive())
                && (event->pointerType() == QPointingDevice::PointerType::Pen
                    || event->pointerType() == QPointingDevice::PointerType::Eraser)));

    const auto effectiveTabletButton = [&]() -> Qt::MouseButton {
        if (event->button() != Qt::NoButton) {
            return event->button();
        }
        // Some tablet drivers report button() == NoButton for side-button
        // presses.  Infer the newly-pressed button by diffing current
        // buttons() against the previous state we saved.
        const Qt::MouseButtons newlyPressed = tabletButtons & ~prevTabletButtons;
        if (newlyPressed.testFlag(Qt::RightButton))
            return Qt::RightButton;
        if (newlyPressed.testFlag(Qt::MiddleButton))
            return Qt::MiddleButton;
        if (newlyPressed.testFlag(Qt::LeftButton))
            return Qt::LeftButton;
        // For Release: infer the newly-released button.
        const Qt::MouseButtons newlyReleased = prevTabletButtons & ~tabletButtons;
        if (newlyReleased.testFlag(Qt::RightButton))
            return Qt::RightButton;
        if (newlyReleased.testFlag(Qt::MiddleButton))
            return Qt::MiddleButton;
        if (newlyReleased.testFlag(Qt::LeftButton))
            return Qt::LeftButton;
        // Fallback: just pick the first button that's held.
        if (tabletButtons.testFlag(Qt::LeftButton))
            return Qt::LeftButton;
        if (tabletButtons.testFlag(Qt::RightButton))
            return Qt::RightButton;
        if (tabletButtons.testFlag(Qt::MiddleButton))
            return Qt::MiddleButton;
        if (tabletContact) {
            return Qt::LeftButton;
        }
        if (event->type() == QEvent::TabletRelease
            && (prevTabletButtons.testFlag(Qt::LeftButton) || m_host->isInputTabletActive())) {
            return Qt::LeftButton;
        }
        return Qt::NoButton;
    };
    const auto beginTemporaryStylusToolHold = [&](Qt::MouseButton heldButton, ToolId tool) {
        m_host->beginTemporaryToolHoldFromButton(heldButton, tool);
    };
    const auto endTemporaryStylusToolHold = [&](Qt::MouseButton heldButton) {
        if (!m_host->temporaryToolHoldActive() || !m_host->temporaryToolHeldButtonIs(heldButton)) {
            return;
        }

        m_host->endTemporaryTool();
    };
    const bool wasPositionPickerActive = m_panel->isPositionPickerActive();
    const auto notifyToolInteraction = [&]() {
        if (!wasPositionPickerActive && !m_host->isInputPanningActive()) {
            m_host->notifyCanvasToolInteractionStarted();
        }
    };
    // A selection drag (lasso, lasso fill, rectangular/elliptical selection) owns
    // the canvas exactly like a brush stroke does: it holds the panel's mouse grab
    // until release. Without counting it here, dragging the pen across the brush
    // overlay or the stylus joystick would hand those packets to the overlay and
    // stall the selection halfway through.
    const bool activeTabletStroke = (m_host->isInputTabletActive() && m_host->isDrawingActive())
        || m_host->isAnySelectionInteractionActive();

    // When stylus is over BrushControlOverlay, let Qt synthesize mouse events for it
    if (m_host->shouldIgnoreTabletInputForOverlay(globalPosF, activeTabletStroke)) {
        event->ignore();
        return;
    }
    if (m_host->routeTabletInputToStylusJoystick(
            event, globalPosF, effectiveTabletButton(), activeTabletStroke)) {
        return;
    }
    // Same behavior for selection action popup, confirmation popup, and embedded color picker.

    // In transform mode stylus input must follow transform mouse logic,
    // regardless of the currently selected drawing tool.
    if (m_host->isTransformInputActive()) {
        const QPoint localPos = m_panel->mapFromGlobal(globalPosF.toPoint());
        switch (event->type()) {
        case QEvent::TabletPress: {
            const Qt::MouseButton btn = event->button();
            if (btn == Qt::NoButton) {
                event->ignore();
                return;
            }
            QMouseEvent syntheticPress(QEvent::MouseButtonPress, localPos, globalPosF.toPoint(),
                btn, event->buttons(), event->modifiers());
            m_host->dispatchSyntheticMousePress(&syntheticPress);
            notifyToolInteraction();
            event->accept();
            return;
        }
        case QEvent::TabletMove: {
            QMouseEvent syntheticMove(QEvent::MouseMove, localPos, globalPosF.toPoint(),
                Qt::NoButton, event->buttons(), event->modifiers());
            m_host->dispatchSyntheticMouseMove(&syntheticMove);
            event->accept();
            return;
        }
        case QEvent::TabletRelease: {
            const Qt::MouseButton btn = event->button();
            if (btn == Qt::NoButton) {
                event->ignore();
                return;
            }
            QMouseEvent syntheticRelease(QEvent::MouseButtonRelease, localPos, globalPosF.toPoint(),
                btn, event->buttons(), event->modifiers());
            m_host->dispatchSyntheticMouseRelease(&syntheticRelease);
            event->accept();
            return;
        }
        default:
            event->ignore();
            return;
        }
    }

    // Physical eraser end of stylus: use erase mode regardless of current tool
    const bool isEraserPointer = (event->pointerType() == QPointingDevice::PointerType::Eraser);

    switch (event->type()) {
    case QEvent::TabletPress: {
        const Qt::MouseButton tabletButton = effectiveTabletButton();

        // RightButton / MiddleButton only (no pen touch): temporary switch to Eraser
        if ((tabletButton == Qt::RightButton || tabletButton == Qt::MiddleButton)
            && !tabletButtons.testFlag(Qt::LeftButton)) {
            beginTemporaryStylusToolHold(tabletButton, ToolId::Eraser);
            event->accept();
            return;
        }

        if (tabletButton == Qt::LeftButton) {
            // Pen touch while holding side button: switch to eraser BEFORE starting stroke
            if ((m_host->currentInputTool() == ToolId::Brush
                    || m_host->currentInputTool() == ToolId::Blur
                    || m_host->currentInputTool() == ToolId::Smudge
                    || m_host->currentInputTool() == ToolId::Liquify
                    || m_host->currentInputTool() == ToolId::Eraser)
                && (tabletButtons.testFlag(Qt::RightButton)
                    || tabletButtons.testFlag(Qt::MiddleButton))
                && !m_host->temporaryToolHoldActive()) {
                const Qt::MouseButton sideBtn
                    = tabletButtons.testFlag(Qt::RightButton) ? Qt::RightButton : Qt::MiddleButton;
                beginTemporaryStylusToolHold(sideBtn, ToolId::Eraser);
            }
            // Mark temporary tool as used
            m_host->markTemporaryToolUsed();

            // Physical eraser end: always draw (erase) regardless of current tool
            if (isEraserPointer) {
                m_host->hideBrushPackOverlayIfNotUserMoved();
                m_host->setInputTabletActive(true);
                m_host->setEraseMode(true);
                aether::Vector2 worldPos = m_host->mapInputToViewportWorld(globalPosF);
                m_strokeTimestampBaseMs = event->timestamp();
                m_lastTabletElapsedSec = 0.0f;
                m_host->inputPainting()->beginStroke(worldPos.x, worldPos.y, pressure,
                    StrokeInputDevice::Stylus,
                    event->modifiers().testFlag(Qt::ShiftModifier), inputDynamics);
                m_host->setInputDrawingActive(m_host->inputPainting()->isDrawing());
                if (!m_host->isInputDrawingActive()) {
                    m_host->showBlockedDrawMessageForSelectedLayer();
                }
                notifyToolInteraction();
                event->accept();
                return;
            }

            // Hand / Move tool: panning
            if (m_host->currentInputTool() == ToolId::Hand
                || m_host->currentInputTool() == ToolId::Move) {
                QPoint localPos = m_panel->mapFromGlobal(globalPosF.toPoint());
                QMouseEvent syntheticPress(QEvent::MouseButtonPress, localPos, globalPosF.toPoint(),
                    Qt::LeftButton, tabletButtons, event->modifiers());
                m_host->dispatchSyntheticMousePress(&syntheticPress);
                notifyToolInteraction();
                event->accept();
                return;
            }
            if (m_host->currentInputTool() == ToolId::Brush
                || m_host->currentInputTool() == ToolId::Blur
                || m_host->currentInputTool() == ToolId::Smudge
                || m_host->currentInputTool() == ToolId::Liquify
                || m_host->currentInputTool() == ToolId::Eraser) {
                m_host->hideBrushPackOverlayIfNotUserMoved();
                m_host->setInputTabletActive(true);
                m_host->setEraseMode(
                    isEraserPointer || m_host->shouldEraseForTool(m_host->currentInputTool()));
                aether::Vector2 worldPos = m_host->mapInputToViewportWorld(globalPosF);
                m_strokeTimestampBaseMs = event->timestamp();
                m_lastTabletElapsedSec = 0.0f;
                m_host->inputPainting()->beginStroke(worldPos.x, worldPos.y, pressure,
                    StrokeInputDevice::Stylus,
                    event->modifiers().testFlag(Qt::ShiftModifier), inputDynamics);
                m_host->setInputDrawingActive(m_host->inputPainting()->isDrawing());
                if (!m_host->isInputDrawingActive()) {
                    m_host->showBlockedDrawMessageForSelectedLayer();
                }
                notifyToolInteraction();
                event->accept();
                return;
            }
        }

        if ((m_host->currentInputTool() == ToolId::Brush
                || m_host->currentInputTool() == ToolId::Blur
                || m_host->currentInputTool() == ToolId::Smudge
                || m_host->currentInputTool() == ToolId::Liquify
                || m_host->currentInputTool() == ToolId::Eraser)
            && (tabletButton == Qt::RightButton || tabletButton == Qt::MiddleButton)) {
            // Side stylus buttons should behave like mouse buttons without delayed brush start.
            QPoint localPos = m_panel->mapFromGlobal(globalPosF.toPoint());
            QMouseEvent syntheticPress(QEvent::MouseButtonPress, localPos, globalPosF.toPoint(),
                tabletButton, tabletButtons, event->modifiers());
            m_host->dispatchSyntheticMousePress(&syntheticPress);
            notifyToolInteraction();
            event->accept();
            return;
        }

        // Forward tablet to mouse logic for non-drawing tools (avoids Qt's synthesis issues)
        if (tabletButton != Qt::NoButton) {
            QPoint localPos = m_panel->mapFromGlobal(globalPosF.toPoint());
            QMouseEvent syntheticPress(QEvent::MouseButtonPress, localPos, globalPosF.toPoint(),
                tabletButton, tabletButtons, event->modifiers());
            m_host->dispatchSyntheticMousePress(&syntheticPress);
            notifyToolInteraction();
            event->accept();
            return;
        }
        event->ignore();
        break;
    }
    case QEvent::TabletMove: {
        m_host->updateInputCursorPosition(globalPosF.toPoint());
        // Side button pressed during move (driver often reports button change in TabletMove, not
        // TabletPress)
        const Qt::MouseButton moveTabletButton = effectiveTabletButton();
        if (moveTabletButton == Qt::RightButton || moveTabletButton == Qt::MiddleButton) {
            const Qt::MouseButtons newlyPressed = tabletButtons & ~prevTabletButtons;
            if (newlyPressed.testFlag(moveTabletButton) && !m_host->temporaryToolHoldActive()) {
                // Switch to eraser; don't end stroke — current stroke finishes on pen release,
                // next touch will be eraser
                beginTemporaryStylusToolHold(moveTabletButton, ToolId::Eraser);
            }
        }
        const Qt::MouseButtons newlyReleased = prevTabletButtons & ~tabletButtons;
        if (newlyReleased.testFlag(Qt::RightButton)) {
            endTemporaryStylusToolHold(Qt::RightButton);
        } else if (newlyReleased.testFlag(Qt::MiddleButton)) {
            endTemporaryStylusToolHold(Qt::MiddleButton);
        }
        // Space+stylus panning: forward move when panning
        if (m_host->isInputPanningActive()) {
            const QPointF localPosF = m_panel->mapFromGlobal(globalPosF);
            QMouseEvent syntheticMove(QEvent::MouseMove, localPosF, globalPosF, Qt::NoButton,
                tabletButtons, event->modifiers());
            m_host->dispatchSyntheticMouseMove(&syntheticMove);
            event->accept();
            return;
        }
        if (m_host->isSpaceStrokeMoveActive() && m_host->isInputTabletActive()
            && m_host->isInputDrawingActive()) {
            m_host->moveActiveStrokeWithSpace(globalPosF.toPoint());
            event->accept();
            return;
        }
        if (m_host->isInputTabletActive() && m_host->isInputDrawingActive()) {
            if (!tabletContact) {
                m_host->setInputTabletActive(false);
                m_host->setInputDrawingActive(false);
                if (m_host->isSpaceStrokeMoveActive()) {
                    m_host->endSpaceStrokeMove();
                }
                m_host->inputPainting()->endStroke();
                // Finalize a still-draining stroke before any mode flag moves:
                // endStroke() flattens the whole buffer and reads those flags then.
                m_host->notifyStrokeSessionEnded();
                m_host->setEraseMode(m_host->shouldEraseForTool(m_host->currentInputTool()));
                m_host->notifyCanvasContentChanged();
                event->accept();
                return;
            }

            // Recover intermediate pen positions from the WinTab packet
            // buffer.  When the Qt tablet path is active (not native WinTab
            // routing), the OS may coalesce WT_PACKET messages, dropping
            // intermediate samples.  Our WinTab backend buffers all packets
            // independently, so we can inject them here.
            // Real pen-sample time for this move, relative to stroke begin.
            // event->timestamp() is the OS event clock; falling back to the
            // last value guards against a non-advancing/odd clock.
            const quint64 tsNow = event->timestamp();
            const float curElapsedSec = (tsNow >= m_strokeTimestampBaseMs)
                ? static_cast<float>(static_cast<double>(tsNow - m_strokeTimestampBaseMs) / 1000.0)
                : m_lastTabletElapsedSec;

            if (!ruwa::services::input::StylusInputManager::instance().usesNativeUiRouting()) {
                auto packets
                    = ruwa::services::input::StylusDebugService::instance()->drainWinTabQueue();
                const QPoint currentScreenPos = globalPosF.toPoint();
                if (m_host->isGlobalOverInputViewport(currentScreenPos)) {
                    // Recovered WinTab packets carry no Qt clock; spread them
                    // evenly in time between the previous move and this one so
                    // the stream stays monotonic.
                    const float prevElapsed = m_lastTabletElapsedSec;
                    const float span = curElapsedSec - prevElapsed;
                    const int total = static_cast<int>(packets.size()) + 1;
                    int idx = 0;
                    for (const auto& pkt : packets) {
                        ++idx;
                        if (nearlySameScreenPoint(pkt.globalPos, globalPosF)
                            || !m_host->isGlobalOverInputViewport(pkt.globalPos)) {
                            continue; // current point handled below; off-viewport history is noisy
                        }
                        const aether::Vector2 wp = m_host->mapInputToViewportWorld(pkt.globalPos);
                        const float pktElapsed
                            = prevElapsed + span * (static_cast<float>(idx) / total);
                        const auto packetInputDynamics = pkt.tiltAvailable
                            ? inputDynamicsFromScreenTilt(
                                  m_host, pkt.globalPos, pkt.tiltVector)
                            : inputDynamics;
                        // Queued, not rasterized inline: this burst is however
                        // many packets the driver buffered since the last Qt
                        // move, so drawing it here would put device-rate work in
                        // the event handler. The queue applies the frame budget;
                        // the real sample below drains it in the same call, so
                        // nothing is postponed.
                        m_host->inputPainting()->queueStrokeAtElapsed(
                            wp.x, wp.y, pkt.pressure, pktElapsed,
                            StrokeInputDevice::Stylus,
                            packetInputDynamics);
                    }
                }
            }

            aether::Vector2 worldPos = m_host->mapInputToViewportWorld(globalPosF);
            m_host->inputPainting()->continueStrokeAtElapsed(
                worldPos.x, worldPos.y, pressure, curElapsedSec,
                StrokeInputDevice::Stylus, inputDynamics);
            m_lastTabletElapsedSec = curElapsedSec;
            m_host->notifyCanvasContentChanged();
            event->accept();
            return;
        }
        // Forward to mouse logic for non-drawing tools (Lasso, Zoom, Eyedropper, Transform, Pan)
        if (event->buttons() != Qt::NoButton) {
            const QPoint currentGlobal = globalPosF.toPoint();
            // Same WinTab packet recovery as brush/eraser: Qt may coalesce tablet moves;
            // inject buffered samples before the current position.
            if (!ruwa::services::input::StylusInputManager::instance().usesNativeUiRouting()
                && m_host->isAnySelectionInteractionActive()) {
                auto packets
                    = ruwa::services::input::StylusDebugService::instance()->drainWinTabQueue();
                for (const auto& pkt : packets) {
                    if (nearlySameScreenPoint(pkt.globalPos, globalPosF)) {
                        continue;
                    }
                    if (!CanvasTabletHandler::globalPosOverViewport(m_host, pkt.globalPos)) {
                        continue;
                    }
                    const QPointF localPkt = m_panel->mapFromGlobal(pkt.globalPos);
                    QMouseEvent syntheticPrior(QEvent::MouseMove, localPkt, pkt.globalPos,
                        Qt::NoButton, tabletButtons, event->modifiers());
                    m_host->dispatchSyntheticMouseMove(&syntheticPrior);
                }
            }
            const QPoint localPos = m_panel->mapFromGlobal(currentGlobal);
            QMouseEvent syntheticMove(QEvent::MouseMove, localPos, globalPosF.toPoint(),
                Qt::NoButton, tabletButtons, event->modifiers());
            m_host->dispatchSyntheticMouseMove(&syntheticMove);
            event->accept();
            return;
        }
        event->ignore();
        break;
    }
    case QEvent::TabletRelease: {
        const Qt::MouseButton tabletButton = effectiveTabletButton();

        if (m_host->isInputTabletActive()
            && (event->button() == Qt::LeftButton || !tabletButtons.testFlag(Qt::LeftButton))) {
            m_host->setInputTabletActive(false);
            if (m_host->isInputDrawingActive()) {
                m_host->setInputDrawingActive(false);
                if (m_host->isSpaceStrokeMoveActive()) {
                    m_host->endSpaceStrokeMove();
                }
                m_host->inputPainting()->endStroke();
                // Finalize a still-draining stroke before any mode flag moves:
                // endStroke() flattens the whole buffer and reads those flags then.
                m_host->notifyStrokeSessionEnded();
                m_host->setEraseMode(m_host->shouldEraseForTool(m_host->currentInputTool()));
                m_host->notifyCanvasContentChanged();
            }
            event->accept();
            return;
        }
        // RightButton / MiddleButton release: end temporary Eraser hold
        if ((tabletButton == Qt::RightButton || tabletButton == Qt::MiddleButton)
            && m_host->temporaryToolHoldActive()
            && m_host->temporaryToolHeldButtonIs(tabletButton)) {
            endTemporaryStylusToolHold(tabletButton);
            event->accept();
            return;
        }
        // Space+stylus: forward release to end panning
        if (m_host->isInputPanningActive() && tabletButton == m_host->inputPanButton()) {
            QPoint localPos = m_panel->mapFromGlobal(globalPosF.toPoint());
            QMouseEvent syntheticRelease(QEvent::MouseButtonRelease, localPos, globalPosF.toPoint(),
                tabletButton, tabletButtons, event->modifiers());
            m_host->dispatchSyntheticMouseRelease(&syntheticRelease);
            event->accept();
            return;
        }
        // Forward to mouse logic for non-drawing tools
        if (tabletButton != Qt::NoButton) {
            QPoint localPos = m_panel->mapFromGlobal(globalPosF.toPoint());
            QMouseEvent syntheticRelease(QEvent::MouseButtonRelease, localPos, globalPosF.toPoint(),
                tabletButton, tabletButtons, event->modifiers());
            m_host->dispatchSyntheticMouseRelease(&syntheticRelease);
            event->accept();
            return;
        }
        event->ignore();
        break;
    }
    default:
        event->ignore();
        break;
    }
}

} // namespace ruwa::ui::workspace
