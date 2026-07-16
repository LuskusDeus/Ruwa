// SPDX-License-Identifier: MPL-2.0

#include "services/input/StylusInputManager.h"
#include "services/input/StylusDebugService.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/layers/ui/LayerListView.h"
#include "features/layers/ui/LayerRowWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <algorithm>
#include <iterator>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointF>
#include <QPointer>
#include <QSettings>
#include <QScopedValueRollback>
#include <QTabletEvent>
#include <QTextEdit>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef GMMP_USE_DISPLAY_POINTS
#define GMMP_USE_DISPLAY_POINTS 1
#endif
#endif // Q_OS_WIN

namespace ruwa::services::input {

struct StylusInputManager::State {
    struct PendingNativeCursorWarp {
        QPoint pos;
        qint64 queuedAtMs = 0;
    };

    QApplication* application = nullptr;
    bool useNativeUiRouting = false;
    quint64 lastHandledPacketSerial = 0;
    Qt::MouseButtons lastButtons = Qt::NoButton;
    QPoint lastGlobalPos;
    bool nativePointerIsActive = false;
    bool nativePointerWasInProximity = false;
    QElapsedTimer cursorWarpClock;
    std::vector<PendingNativeCursorWarp> pendingNativeCursorWarps;
    QPointer<QWidget> hoverTarget;
    QPointer<QWidget> activeMouseTarget;
    QPointer<QWidget> activeCanvasTarget;
    QPointer<ruwa::ui::widgets::SmoothScrollArea> pendingScrollArea;
    QPointer<QWidget> pendingPressTarget;
    QPoint pendingPressGlobalPos;
    Qt::MouseButton pendingPressButton = Qt::NoButton;
    Qt::MouseButtons pendingPressButtons = Qt::NoButton;
    Qt::KeyboardModifiers pendingPressModifiers = Qt::NoModifier;
    bool stylusSwipeDragging = false;
    bool dispatchingToCanvas = false;
    bool dispatchingSyntheticMouseEvent = false;
    float currentDispatchPressure = 0.0f;
    bool hasNativeCanvasDispatch = false;
    QElapsedTimer canvasReleaseDebounce;
    bool canvasReleaseDebounceActive = false;
    bool suppressButtonsUntilRelease = false;

    // UI move throttle: coalesce high-frequency stylus moves for non-canvas targets.
    // The pending move is stored in State so it persists across handleNativeEvent calls.
    QElapsedTimer uiMoveThrottleTimer;
    bool uiMoveThrottleValid = false;
    bool hasCoalescedUiMove = false;
    QPoint coalescedUiMovePos;
    Qt::MouseButtons coalescedUiMoveButtons = Qt::NoButton;
    QPointer<QWidget> coalescedUiMoveTarget;
};

namespace {

constexpr auto kUiDragActiveProperty = "ruwa_ui_drag_active";

bool isUiDragActive()
{
    return qApp && qApp->property(kUiDragActiveProperty).toBool();
}

bool useRuwaWinTabBackend()
{
    QSettings settings(QApplication::organizationName(), QApplication::applicationName());
    return settings.value("Performance/tabletBackend", 2).toInt() == 2;
}

// Maximum dispatch rate for non-canvas UI targets.  ~40 Hz is sufficient for
// drag & drop target updates and hover highlights — the ghost animation runs
// on its own timer and is not affected by this throttle.  This avoids the
// event-loop flooding from high-frequency WinTab packets (200-266+ Hz).
constexpr qint64 kUiMoveThrottleMs = 25; // ~40 Hz
constexpr qint64 kUiDragMoveThrottleMs = 16; // ~60 Hz
constexpr qint64 kCursorWarpLifetimeMs = 1000;

bool isCanvasWidget(QWidget* widget)
{
    if (!widget) {
        return false;
    }

    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<aether::OpenGLCanvasWidget*>(current)) {
            return true;
        }
    }

    return false;
}

bool isPointInsideWidgetGlobalRect(QWidget* widget, const QPoint& globalPos)
{
    if (!widget || !widget->isVisible()) {
        return false;
    }

    return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(globalPos);
}

QWidget* closestCanvasWidget(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<aether::OpenGLCanvasWidget*>(current)) {
            return current;
        }
    }

    return nullptr;
}

QWidget* directChildCanvasAtGlobalPos(QWidget* widget, const QPoint& globalPos)
{
    if (!widget) {
        return nullptr;
    }

    const auto canvasWidgets
        = widget->findChildren<aether::OpenGLCanvasWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* canvasWidget : canvasWidgets) {
        if (!canvasWidget || !canvasWidget->isVisible()) {
            continue;
        }

        const QPoint localPos = canvasWidget->mapFromGlobal(globalPos);
        if (canvasWidget->rect().contains(localPos)) {
            return canvasWidget;
        }
    }

    return nullptr;
}

QWidget* resolveMouseTarget(QWidget* activeMouseTarget, const QPoint& globalPos)
{
    if (activeMouseTarget && !isCanvasWidget(activeMouseTarget)) {
        return activeMouseTarget;
    }

    if (QWidget* grabber = QWidget::mouseGrabber()) {
        if (!isCanvasWidget(grabber)) {
            return grabber;
        }
    }

    if (QWidget* widgetAtPos = QApplication::widgetAt(globalPos)) {
        if (!isCanvasWidget(widgetAtPos)) {
            return widgetAtPos;
        }
    }

    return nullptr;
}

QWidget* resolveCanvasTarget(QWidget* activeCanvasTarget, const QPoint& globalPos)
{
    if (activeCanvasTarget && isCanvasWidget(activeCanvasTarget)
        && isPointInsideWidgetGlobalRect(activeCanvasTarget, globalPos)) {
        return activeCanvasTarget;
    }

    if (QWidget* grabber = QWidget::mouseGrabber()) {
        if (QWidget* canvasWidget = closestCanvasWidget(grabber)) {
            return canvasWidget;
        }
    }

    if (QWidget* widgetAtPos = QApplication::widgetAt(globalPos)) {
        if (QWidget* canvasWidget = closestCanvasWidget(widgetAtPos)) {
            return canvasWidget;
        }
        return directChildCanvasAtGlobalPos(widgetAtPos, globalPos);
    }

    return nullptr;
}

ruwa::ui::widgets::SmoothScrollArea* findSmoothScrollArea(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (auto* scrollArea = qobject_cast<ruwa::ui::widgets::SmoothScrollArea*>(current)) {
            return scrollArea;
        }
    }
    return nullptr;
}

bool shouldUseStylusSwipeForWidget(QWidget* widget)
{
    if (!widget) {
        return false;
    }

    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<ruwa::ui::widgets::LayerRowWidget*>(current)
            || qobject_cast<ruwa::ui::widgets::LayerListView*>(current)) {
            return false;
        }

        if (qobject_cast<QAbstractButton*>(current) || qobject_cast<QAbstractSlider*>(current)
            || qobject_cast<QAbstractSpinBox*>(current) || qobject_cast<QComboBox*>(current)
            || qobject_cast<QLineEdit*>(current) || qobject_cast<QTextEdit*>(current)
            || qobject_cast<QPlainTextEdit*>(current)
            || qobject_cast<ruwa::ui::widgets::ProgressHandleSlider*>(current)
            || qobject_cast<QAbstractItemView*>(current)) {
            return false;
        }
    }

    return findSmoothScrollArea(widget) != nullptr;
}

void sendSyntheticMouseEvent(QEvent::Type type, QWidget* target, const QPointF& globalPos,
    Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers,
    bool& dispatchingSyntheticMouseEvent)
{
    if (!target) {
        return;
    }

    const QPointF localPos = target->mapFromGlobal(globalPos);
    QMouseEvent event(type, localPos, globalPos, button, buttons, modifiers);
    QScopedValueRollback<bool> dispatchGuard(dispatchingSyntheticMouseEvent, true);
    QCoreApplication::sendEvent(target, &event);
}

void updateHoverTarget(QPointer<QWidget>& hoverTarget, QWidget* target)
{
    if (hoverTarget == target) {
        return;
    }

    if (hoverTarget) {
        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(hoverTarget, &leaveEvent);
    }

    hoverTarget = target;

    if (hoverTarget) {
        const QPoint globalPos = QCursor::pos();
        const QPointF localPos = hoverTarget->mapFromGlobal(globalPos);
        const QPointF windowPos
            = hoverTarget->window() ? hoverTarget->window()->mapFromGlobal(globalPos) : localPos;
        const QPointF screenPos(globalPos);
        QEnterEvent enterEvent(localPos, windowPos, screenPos);
        QCoreApplication::sendEvent(hoverTarget, &enterEvent);
    }
}

Qt::MouseButton changedButton(Qt::MouseButtons before, Qt::MouseButtons after)
{
    const Qt::MouseButtons changed = before ^ after;
    if (changed.testFlag(Qt::LeftButton)) {
        return Qt::LeftButton;
    }
    if (changed.testFlag(Qt::RightButton)) {
        return Qt::RightButton;
    }
    if (changed.testFlag(Qt::MiddleButton)) {
        return Qt::MiddleButton;
    }
    return Qt::NoButton;
}

} // namespace

StylusInputManager& StylusInputManager::instance()
{
    static StylusInputManager instance;
    return instance;
}

StylusInputManager::~StylusInputManager()
{
    delete m_state;
    m_state = nullptr;
}

void StylusInputManager::initialize(QApplication* application)
{
    if (!m_state) {
        m_state = new State();
    }

    m_state->application = application;
    m_state->useNativeUiRouting = useRuwaWinTabBackend();
    m_state->lastHandledPacketSerial = 0;
    m_state->lastButtons = Qt::NoButton;
    m_state->nativePointerIsActive = false;
    m_state->nativePointerWasInProximity = false;
    m_state->cursorWarpClock.start();
    m_state->pendingNativeCursorWarps.clear();
    m_state->hoverTarget.clear();
    m_state->activeMouseTarget.clear();
    m_state->activeCanvasTarget.clear();
    m_state->dispatchingToCanvas = false;
    m_state->dispatchingSyntheticMouseEvent = false;
    m_state->currentDispatchPressure = 0.0f;
    m_state->hasNativeCanvasDispatch = false;
    m_state->suppressButtonsUntilRelease = false;
    m_state->hasCoalescedUiMove = false;
    m_state->uiMoveThrottleValid = false;
    clearPendingStylusSwipe();
}

bool StylusInputManager::handleTabletEvent(QWidget* target, QTabletEvent* event)
{
    Q_UNUSED(target);
    Q_UNUSED(event);
    return false;
}

bool StylusInputManager::handleNativeEvent(void* message)
{
    Q_UNUSED(message);

    if (!m_state || !m_state->useNativeUiRouting || !m_state->application) {
        return false;
    }

    // Drain ALL packets buffered since the last call. WinTabBackend::handleNativeEvent was already
    // called (by StylusDebugService) and populated the pending-packet list via WTPacketsGet.
    // Processing all of them here (instead of just the latest snapshot) recovers intermediate
    // pen positions that would otherwise be lost when WinTab's message queue is slow.
    auto packets = StylusDebugService::instance()->drainWinTabQueue();
    const StylusDebugService::Snapshot snapshot = StylusDebugService::instance()->snapshot();

    if (packets.empty()) {
        // No new packets from WTPacketsGet — fall back to the snapshot-based path.
        if (snapshot.winTabPacketSerial == 0
            || snapshot.winTabPacketSerial == m_state->lastHandledPacketSerial) {
            return false;
        }
        WinTabBackend::PenSample fallback;
        fallback.globalPos = snapshot.winTabGlobalPos;
        fallback.pressure = snapshot.winTabPressure;
        fallback.buttons = snapshot.winTabButtons;
        packets.push_back(fallback);
    }

    // Update the serial so we don't re-process on the next call.
    m_state->lastHandledPacketSerial = snapshot.winTabPacketSerial;

    // WinTabBackend appends a zero-button terminal sample when proximity is lost.
    // Process it below before clearing routing state; otherwise release-driven Qt
    // controls remain pressed and lasso interactions never receive their final point.
    const bool proximityLost = !snapshot.winTabInProximity;
    const bool proximityGained
        = snapshot.winTabInProximity && !m_state->nativePointerWasInProximity;
    m_state->nativePointerWasInProximity = snapshot.winTabInProximity;

    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();

    // ---- UI move coalescing ----
    // Non-canvas mouse-move events are throttled to ~40 Hz normally and
    // ~60 Hz during UI drags to prevent event-loop flooding from
    // high-frequency WinTab packets (200-266+ Hz).
    // The latest non-canvas move position is accumulated in m_state and only
    // dispatched when the throttle interval has elapsed.  Button changes
    // (press/release) are always dispatched immediately and flush any
    // pending coalesced move first.  The pending state persists across
    // handleNativeEvent calls so that throttling works even when WinTab
    // generates one WM_PACKET per sample.

    auto flushCoalescedUiMove = [&]() {
        if (!m_state->hasCoalescedUiMove || !m_state->coalescedUiMoveTarget)
            return;
        // Do NOT call QCursor::setPos here — the system cursor was already moved
        // to the live pen position when this move packet was first processed
        // (the move-only branch calls setPos every packet).  Re-positioning here
        // with the older coalesced position would yank the cursor backward and
        // cause visible oscillation.  This only sends the synthetic MouseMove so
        // the throttled widget event processing (hover/drag) catches up.
        updateHoverTarget(m_state->hoverTarget, m_state->coalescedUiMoveTarget);
        sendSyntheticMouseEvent(QEvent::MouseMove, m_state->coalescedUiMoveTarget,
            m_state->coalescedUiMovePos, Qt::NoButton, m_state->coalescedUiMoveButtons, modifiers,
            m_state->dispatchingSyntheticMouseEvent);
        m_state->lastGlobalPos = m_state->coalescedUiMovePos;
        m_state->hasCoalescedUiMove = false;
        m_state->uiMoveThrottleTimer.start();
        m_state->uiMoveThrottleValid = true;
    };

    auto shouldThrottleUiMove = [&]() -> bool {
        if (!m_state->uiMoveThrottleValid)
            return false;
        const qint64 throttleMs = isUiDragActive() ? kUiDragMoveThrottleMs : kUiMoveThrottleMs;
        return m_state->uiMoveThrottleTimer.isValid()
            && m_state->uiMoveThrottleTimer.elapsed() < throttleMs;
    };

    // If there's a pending coalesced move from a previous call and the
    // throttle interval has now elapsed, dispatch it before processing
    // new packets.
    if (m_state->hasCoalescedUiMove && !shouldThrottleUiMove()) {
        flushCoalescedUiMove();
    }

    // Dispatch one synthetic mouse event per buffered packet.
    for (size_t pktIdx = 0; pktIdx < packets.size(); ++pktIdx) {
        const auto& pkt = packets[pktIdx];
        const QPointF globalPosF = pkt.globalPos;
        const QPoint globalPos = globalPosF.toPoint();
        const Qt::MouseButtons physicalButtons = pkt.buttons;
        Qt::MouseButtons currentButtons = physicalButtons;

        if (m_state->suppressButtonsUntilRelease) {
            if (physicalButtons == Qt::NoButton) {
                m_state->suppressButtonsUntilRelease = false;
            } else {
                currentButtons = Qt::NoButton;
            }
        }

        // Debounce: after a canvas release, suppress LeftButton re-engagement
        // for a brief period.  Digitiser sensors can "bounce" pressure during
        // pen lift (e.g. 200→0→50→0), producing phantom press/release pairs
        // that appear as tiny dot strokes or duplicate clicks.
        if (m_state->canvasReleaseDebounceActive) {
            if (m_state->canvasReleaseDebounce.isValid()
                && m_state->canvasReleaseDebounce.elapsed() < 60) {
                currentButtons &= ~Qt::LeftButton;
            } else {
                m_state->canvasReleaseDebounceActive = false;
            }
        }

        const Qt::MouseButtons previousButtons = m_state->lastButtons;
        if (!proximityLost
            && (m_state->nativePointerIsActive || globalPos != m_state->lastGlobalPos
                || currentButtons != previousButtons || proximityGained)) {
            // Stationary hover packets are not user intent and must not steal
            // ownership back immediately after a mouse move or wheel event.
            // Pen movement and button transitions are authoritative WinTab input.
            m_state->nativePointerIsActive = true;
        }

        if (!m_state->nativePointerIsActive) {
            // The mouse currently owns the pointer. WinTab may continue reporting
            // identical hover samples at tablet report rate; forwarding them would
            // immediately revive the stale canvas cursor after mouse-wheel input.
            continue;
        }

        // Windows exposes one shared pointer for mouse and pen. Keep its physical
        // position synchronized on every native packet, including a canvas that
        // currently uses BlankCursor for GL rendering. BlankCursor controls only
        // visibility; freezing QCursor here leaves a second arrow over the UI and
        // makes the next real mouse move resume from the canvas-entry boundary.
        syncSystemCursorFromNative(globalPos);

        const Qt::MouseButton button = changedButton(previousButtons, currentButtons);
        const Qt::MouseButtons addedButtons = currentButtons & ~previousButtons;
        const Qt::MouseButtons removedButtons = previousButtons & ~currentButtons;
        QWidget* target = resolveMouseTarget(m_state->activeMouseTarget, globalPos);
        QWidget* canvasTarget = resolveCanvasTarget(m_state->activeCanvasTarget, globalPos);

        const bool canvasCaptureActive
            = m_state->activeCanvasTarget && previousButtons != Qt::NoButton;
        const bool uiCaptureActive
            = (m_state->activeMouseTarget || m_state->pendingPressTarget)
            && previousButtons != Qt::NoButton;
        if (canvasCaptureActive) {
            // A stroke that began on canvas owns the complete press/move/release
            // sequence, even after crossing over UI.
            canvasTarget = m_state->activeCanvasTarget;
        } else if (uiCaptureActive) {
            // Symmetrically, a press that began on a UI control must not turn into
            // a canvas stroke merely because the pen crossed the viewport.
            canvasTarget = nullptr;
        }

        // Pen-down stroke continuation: once a stroke starts on a canvas, keep
        // routing every packet to that canvas until the pen lifts — even after
        // the pen leaves the canvas bounds. This matches the mouse path, which
        // grabs the mouse on press so a stroke ends only on release, not when
        // the cursor leaves the canvas. Without this the stroke was cut short
        // (a synthetic release was injected) the instant the pen crossed the
        // edge. previousButtons (not currentButtons) is tested so the final
        // release packet — where Left has just dropped — still routes here and
        // ends the stroke on the canvas rather than leaking into a UI widget.
        if (!canvasTarget && m_state->activeCanvasTarget
            && previousButtons.testFlag(Qt::LeftButton)) {
            canvasTarget = m_state->activeCanvasTarget;
        }

        if (canvasTarget) {
            recordNativeCanvasDispatch();
        }

        // ---- Canvas target: dispatch every packet at full rate ----
        if (canvasTarget) {
            // Flush any pending UI move before switching to canvas.
            flushCoalescedUiMove();

            clearPendingStylusSwipe();
            updateHoverTarget(m_state->hoverTarget, nullptr);

            // Set flag and per-packet pressure so the canvas mouse handler can
            // distinguish our synthetic events from real WM_MOUSEMOVE and read
            // the correct pressure for this specific packet (not the snapshot).
            m_state->dispatchingToCanvas = true;
            m_state->currentDispatchPressure = pkt.pressure;

            if (previousButtons == currentButtons) {
                sendSyntheticMouseEvent(QEvent::MouseMove, canvasTarget, globalPosF, Qt::NoButton,
                    currentButtons, modifiers, m_state->dispatchingSyntheticMouseEvent);
                m_state->dispatchingToCanvas = false;
                m_state->lastButtons = currentButtons;
                m_state->lastGlobalPos = globalPos;
                continue;
            }

            if (addedButtons != Qt::NoButton && button != Qt::NoButton) {
                m_state->activeCanvasTarget = canvasTarget;
                sendSyntheticMouseEvent(QEvent::MouseButtonPress, canvasTarget, globalPosF, button,
                    currentButtons, modifiers, m_state->dispatchingSyntheticMouseEvent);
            } else if (removedButtons != Qt::NoButton && button != Qt::NoButton) {
                QWidget* releaseTarget
                    = resolveCanvasTarget(m_state->activeCanvasTarget, globalPos);
                if (!releaseTarget) {
                    // Pen lifted outside the canvas it was drawing on — still
                    // deliver the release there so the stroke ends cleanly.
                    releaseTarget = m_state->activeCanvasTarget;
                }
                if (releaseTarget) {
                    sendSyntheticMouseEvent(QEvent::MouseButtonRelease, releaseTarget, globalPosF,
                        button, currentButtons & ~button, modifiers,
                        m_state->dispatchingSyntheticMouseEvent);
                }
                if (currentButtons == Qt::NoButton) {
                    m_state->activeCanvasTarget.clear();
                }
                // Start debounce window to suppress phantom re-engagement
                // from digitiser pressure bounce during pen lift.
                if (button == Qt::LeftButton) {
                    m_state->canvasReleaseDebounceActive = true;
                    m_state->canvasReleaseDebounce.start();
                }
            }

            QWidget* moveTarget = resolveCanvasTarget(m_state->activeCanvasTarget, globalPos);
            if (moveTarget) {
                sendSyntheticMouseEvent(QEvent::MouseMove, moveTarget, globalPosF, Qt::NoButton,
                    currentButtons, modifiers, m_state->dispatchingSyntheticMouseEvent);
            }

            m_state->dispatchingToCanvas = false;
            m_state->lastButtons = currentButtons;
            m_state->lastGlobalPos = globalPos;
            continue;
        }

        // ---- Non-canvas (UI) target ----

        if (currentButtons == Qt::NoButton) {
            m_state->activeCanvasTarget.clear();
        }

        // Move-only packet (no button change) — coalesce instead of
        // dispatching immediately to avoid flooding the event loop.
        if (previousButtons == currentButtons) {
            if (m_state->pendingScrollArea) {
                if (!m_state->stylusSwipeDragging
                    && (globalPos - m_state->pendingPressGlobalPos).manhattanLength()
                        >= QApplication::startDragDistance()) {
                    m_state->pendingScrollArea->beginStylusSwipe(m_state->pendingPressGlobalPos);
                    m_state->stylusSwipeDragging = true;
                }
                if (m_state->stylusSwipeDragging) {
                    m_state->pendingScrollArea->updateStylusSwipe(globalPos);
                }
                m_state->lastGlobalPos = globalPos;
                continue;
            }

            // Coalesce: record latest position but only dispatch when
            // the throttle interval has elapsed.
            QWidget* moveTarget = resolveMouseTarget(m_state->activeMouseTarget, globalPos);
            if (moveTarget) {
                m_state->coalescedUiMovePos = globalPos;
                m_state->coalescedUiMoveButtons = currentButtons;
                m_state->coalescedUiMoveTarget = moveTarget;
                m_state->hasCoalescedUiMove = true;

                if (!shouldThrottleUiMove()) {
                    flushCoalescedUiMove();
                }
            }
            m_state->lastGlobalPos = globalPos;
            continue;
        }

        // Button change — flush any coalesced move first so the target
        // receives the latest position before the press/release.
        flushCoalescedUiMove();

        updateHoverTarget(m_state->hoverTarget, target);

        if (addedButtons != Qt::NoButton) {
            QWidget* pressTarget = target;
            if (pressTarget && button != Qt::NoButton) {
                if (auto* scrollArea = findSmoothScrollArea(pressTarget);
                    scrollArea && shouldUseStylusSwipeForWidget(pressTarget)) {
                    clearPendingStylusSwipe();
                    m_state->pendingScrollArea = scrollArea;
                    m_state->pendingPressTarget = pressTarget;
                    m_state->pendingPressGlobalPos = globalPos;
                    m_state->pendingPressButton = button;
                    m_state->pendingPressButtons = currentButtons;
                    m_state->pendingPressModifiers = modifiers;
                    m_state->stylusSwipeDragging = false;
                    m_state->lastButtons = currentButtons;
                    m_state->lastGlobalPos = globalPos;
                    continue;
                }

                m_state->activeMouseTarget = pressTarget;
                sendSyntheticMouseEvent(QEvent::MouseButtonPress, pressTarget, globalPos, button,
                    currentButtons, modifiers, m_state->dispatchingSyntheticMouseEvent);
            }
        } else if (removedButtons != Qt::NoButton) {
            if (m_state->pendingScrollArea) {
                if (m_state->stylusSwipeDragging) {
                    m_state->pendingScrollArea->endStylusSwipe(globalPos);
                } else {
                    QWidget* pressTarget
                        = m_state->pendingPressTarget ? m_state->pendingPressTarget.data() : target;
                    m_state->activeMouseTarget = pressTarget;
                    sendSyntheticMouseEvent(QEvent::MouseButtonPress, pressTarget,
                        m_state->pendingPressGlobalPos, m_state->pendingPressButton,
                        m_state->pendingPressButtons, m_state->pendingPressModifiers,
                        m_state->dispatchingSyntheticMouseEvent);
                    sendSyntheticMouseEvent(QEvent::MouseButtonRelease, pressTarget, globalPos,
                        m_state->pendingPressButton, Qt::NoButton, modifiers,
                        m_state->dispatchingSyntheticMouseEvent);
                    m_state->activeMouseTarget.clear();
                }
                clearPendingStylusSwipe();
                m_state->lastButtons = currentButtons;
                m_state->lastGlobalPos = globalPos;
                continue;
            }

            QWidget* releaseTarget = resolveMouseTarget(m_state->activeMouseTarget, globalPos);
            if (releaseTarget && button != Qt::NoButton) {
                sendSyntheticMouseEvent(QEvent::MouseButtonRelease, releaseTarget, globalPos,
                    button, currentButtons & ~button, modifiers,
                    m_state->dispatchingSyntheticMouseEvent);
            }
            if (currentButtons == Qt::NoButton) {
                m_state->activeMouseTarget.clear();
            }
        }

        QWidget* moveTarget = resolveMouseTarget(m_state->activeMouseTarget, globalPos);
        if (moveTarget) {
            sendSyntheticMouseEvent(QEvent::MouseMove, moveTarget, globalPos, Qt::NoButton,
                currentButtons, modifiers, m_state->dispatchingSyntheticMouseEvent);
        }

        m_state->lastButtons = currentButtons;
        m_state->lastGlobalPos = globalPos;
    }

    if (proximityLost) {
        // Mouse and pen share one Windows pointer, so ownership changes here but
        // its position intentionally remains at the final pen sample.
        activateMousePointer();

        // The terminal packet above has already delivered the release through the
        // regular target-capture path. Discard only delayed hover movement and reset
        // the routing session after that release has completed.
        m_state->hasCoalescedUiMove = false;
        m_state->coalescedUiMoveTarget.clear();
        updateHoverTarget(m_state->hoverTarget, nullptr);
        m_state->activeMouseTarget.clear();
        m_state->activeCanvasTarget.clear();
        m_state->lastButtons = Qt::NoButton;
        m_state->hasNativeCanvasDispatch = false;
        m_state->canvasReleaseDebounceActive = false;
        m_state->suppressButtonsUntilRelease = false;
        m_state->uiMoveThrottleValid = false;
        clearPendingStylusSwipe();
    }

    // Note: we intentionally do NOT flush the coalesced UI move at the end of
    // the batch.  The pending move will be dispatched at the start of the next
    // handleNativeEvent call once the throttle interval has elapsed, or flushed
    // immediately if a button change occurs. This ensures the UI throttle is
    // respected across calls (WinTab often sends one WM_PACKET per sample).

    return false;
}

float StylusInputManager::effectivePressure(const QTabletEvent* event) const
{
    if (!event) {
        return 1.0f;
    }

    return StylusDebugService::instance()->effectivePressureOrFallback(
        static_cast<float>(event->pressure()));
}

bool StylusInputManager::usesNativeUiRouting() const
{
    return m_state && m_state->useNativeUiRouting
        && StylusDebugService::instance()->snapshot().winTabAttached;
}

std::optional<QPoint> StylusInputManager::nativeCursorPosition() const
{
    if (!usesNativeUiRouting() || !m_state->nativePointerIsActive) {
        return std::nullopt;
    }

    const auto snapshot = StylusDebugService::instance()->snapshot();
    if (!snapshot.winTabInProximity || snapshot.winTabPacketSerial == 0) {
        return std::nullopt;
    }

    return snapshot.winTabGlobalPos.toPoint();
}

bool StylusInputManager::isDispatchingNativeInput() const
{
    return m_state && (m_state->dispatchingToCanvas || m_state->dispatchingSyntheticMouseEvent);
}

bool StylusInputManager::hasActiveNativePointerButtons() const
{
    if (!m_state || !m_state->useNativeUiRouting) {
        return false;
    }

    const auto snapshot = StylusDebugService::instance()->snapshot();
    return snapshot.winTabAttached && snapshot.winTabInProximity
        && snapshot.winTabButtons != Qt::NoButton;
}

void StylusInputManager::activateMousePointer()
{
    if (!m_state) {
        return;
    }

    m_state->nativePointerIsActive = false;
    m_state->hasCoalescedUiMove = false;
    m_state->coalescedUiMoveTarget.clear();
}

bool StylusInputManager::consumeNativeCursorWarpAt(const QPoint& globalPos)
{
    if (!m_state) {
        return false;
    }

    auto& warps = m_state->pendingNativeCursorWarps;
    const qint64 nowMs = m_state->cursorWarpClock.elapsed();
    warps.erase(std::remove_if(warps.begin(), warps.end(), [nowMs](const auto& warp) {
        return nowMs - warp.queuedAtMs > kCursorWarpLifetimeMs;
    }), warps.end());

    const auto it = std::find_if(warps.begin(), warps.end(), [&globalPos](const auto& warp) {
        return warp.pos == globalPos;
    });
    if (it == warps.end()) {
        return false;
    }

    // Windows may coalesce several SetCursorPos-generated WM_MOUSEMOVE messages.
    // A match therefore acknowledges this warp and every older queued position.
    warps.erase(warps.begin(), std::next(it));
    return true;
}

void StylusInputManager::syncSystemCursorFromNative(const QPoint& globalPos)
{
    if (!m_state || QCursor::pos() == globalPos) {
        return;
    }

    auto& warps = m_state->pendingNativeCursorWarps;
    warps.push_back({ globalPos, m_state->cursorWarpClock.elapsed() });
    constexpr size_t kMaxPendingCursorWarps = 64;
    if (warps.size() > kMaxPendingCursorWarps) {
        warps.erase(warps.begin(), warps.begin() + kMaxPendingCursorWarps / 2);
    }
    QCursor::setPos(globalPos);
}

float StylusInputManager::dispatchPressure() const
{
    if (m_state && m_state->dispatchingToCanvas) {
        return m_state->currentDispatchPressure;
    }
    return 0.0f;
}

bool StylusInputManager::shouldIgnoreCanvasMouseMove(const QMouseEvent* event) const
{
    if (!m_state || !m_state->useNativeUiRouting || !m_state->nativePointerIsActive || !event
        || m_state->dispatchingToCanvas) {
        return false;
    }

    if (!m_state->hasNativeCanvasDispatch) {
        return false;
    }

    const auto snapshot = StylusDebugService::instance()->snapshot();
    if (!snapshot.winTabAttached || !snapshot.winTabInProximity) {
        return false;
    }

    // QCursor::setPos produces ordinary mouse moves asynchronously. They can arrive
    // after newer WinTab packets and must never be added to the native canvas path.
    // While the native pen itself is down, no parallel mouse move is authoritative,
    // regardless of the button flags Windows attaches to the cursor-warp message.
    // A real mouse drag remains available while the pen is only hovering.
    return m_state->lastButtons != Qt::NoButton || event->buttons() == Qt::NoButton;
}

void StylusInputManager::clearPendingStylusSwipe()
{
    if (!m_state) {
        return;
    }

    if (m_state->pendingScrollArea && m_state->pendingScrollArea->isStylusSwipeActive()) {
        m_state->pendingScrollArea->cancelStylusSwipe();
    }

    m_state->pendingScrollArea.clear();
    m_state->pendingPressTarget.clear();
    m_state->pendingPressGlobalPos = QPoint();
    m_state->pendingPressButton = Qt::NoButton;
    m_state->pendingPressButtons = Qt::NoButton;
    m_state->pendingPressModifiers = Qt::NoModifier;
    m_state->stylusSwipeDragging = false;
}

void StylusInputManager::recordNativeCanvasDispatch()
{
    if (!m_state) {
        return;
    }

    m_state->hasNativeCanvasDispatch = true;
}

// ==========================================================================
//   M O U S E   H I S T O R Y   R E C O V E R Y   (GetMouseMovePointsEx)
// ==========================================================================

#ifdef Q_OS_WIN
namespace {

struct MouseHistoryState {
    DWORD lastTime = 0;
    int lastX = 0;
    int lastY = 0;
    bool valid = false;
};

static MouseHistoryState s_mouseHistory;

} // namespace
#endif

std::vector<StylusInputManager::RecoveredMousePoint> StylusInputManager::recoverMouseMoveHistory(
    const QPoint& currentScreenPos)
{
    std::vector<RecoveredMousePoint> result;

#ifdef Q_OS_WIN
    MOUSEMOVEPOINT mp {};
    mp.x = currentScreenPos.x();
    mp.y = currentScreenPos.y();
    mp.time = 0; // match by position only

    MOUSEMOVEPOINT history[64];
    const int count
        = GetMouseMovePointsEx(sizeof(MOUSEMOVEPOINT), &mp, history, 64, GMMP_USE_DISPLAY_POINTS);
    if (count <= 0) {
        // Match failed — just record current position and move on.
        s_mouseHistory.lastX = currentScreenPos.x();
        s_mouseHistory.lastY = currentScreenPos.y();
        s_mouseHistory.lastTime = GetMessageTime();
        s_mouseHistory.valid = true;
        return result;
    }

    if (!s_mouseHistory.valid) {
        // First call after reset — record the matched point, no intermediates.
        s_mouseHistory.lastX = history[0].x;
        s_mouseHistory.lastY = history[0].y;
        s_mouseHistory.lastTime = history[0].time;
        s_mouseHistory.valid = true;
        return result;
    }

    // history[] is newest-first (index 0 == current position's match).
    // Walk backwards to find where we stopped last time.
    int lastIdx = -1;
    for (int i = 0; i < count; ++i) {
        if (history[i].time == s_mouseHistory.lastTime && history[i].x == s_mouseHistory.lastX
            && history[i].y == s_mouseHistory.lastY) {
            lastIdx = i;
            break;
        }
        // If timestamp is older, everything after is also older — stop.
        if (history[i].time < s_mouseHistory.lastTime) {
            lastIdx = i;
            break;
        }
    }

    if (lastIdx <= 0) {
        // No intermediate points (only the current one, or nothing found).
        s_mouseHistory.lastX = history[0].x;
        s_mouseHistory.lastY = history[0].y;
        s_mouseHistory.lastTime = history[0].time;
        return result;
    }

    // Collect intermediate points: from oldest new (lastIdx-1) to just before
    // the current event (index 1). Index 0 is the current position itself and
    // will be processed by the caller. Carry the per-point WM timestamp so
    // downstream consumers can space them correctly in time — without this
    // the stabilizer sees them as bursts at Δt≈0 and the Bezier smoother
    // turns the resulting big stab jumps into visible polygon edges.
    result.reserve(static_cast<size_t>(lastIdx - 1));
    for (int i = lastIdx - 1; i >= 1; --i) {
        RecoveredMousePoint pt;
        pt.pos = QPoint(history[i].x, history[i].y);
        pt.wmTimeMs = history[i].time;
        pt.currentWmTimeMs = history[0].time;
        result.push_back(pt);
    }

    s_mouseHistory.lastX = history[0].x;
    s_mouseHistory.lastY = history[0].y;
    s_mouseHistory.lastTime = history[0].time;

#else
    Q_UNUSED(currentScreenPos);
#endif

    return result;
}

void StylusInputManager::resetMouseMoveHistory()
{
#ifdef Q_OS_WIN
    s_mouseHistory.valid = false;
#endif
}

} // namespace ruwa::services::input
