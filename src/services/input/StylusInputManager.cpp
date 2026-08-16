// SPDX-License-Identifier: MPL-2.0

#include "services/input/StylusInputManager.h"
#include "services/input/StylusDebugService.h"
#include "services/input/StylusInputTrace.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/layers/ui/LayerListView.h"
#include "features/layers/ui/LayerRowWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/SyntheticMouseFocus.h"

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
    // Evidence that the pen is being used again, gathered while the mouse owns the
    // shared system pointer. A pen left lying inside the tablet's hover range keeps
    // reporting forever, so ownership has to be won by movement rather than by the
    // arrival of a packet. See kPenReclaimDistancePx.
    QPoint penReclaimBaseline;
    bool hasPenReclaimBaseline = false;
    QPoint penReclaimCandidate;
    int penReclaimCandidateCount = 0;
    QElapsedTimer cursorWarpClock;
    std::vector<PendingNativeCursorWarp> pendingNativeCursorWarps;
    // Oscillation guard for the system-cursor warp. Warping to a position the
    // cursor is already at is a no-op that returns early, so REPEATING a warp to
    // the same position can only mean something outside this process keeps
    // pulling the cursor back off it. Left unchecked that is a feedback loop at
    // tablet report rate — the cursor ping-pongs between two points and no
    // physical input can escape it.
    QPoint lastCursorWarpTarget;
    int repeatedCursorWarpCount = 0;
    QElapsedTimer cursorWarpBackoffClock;
    bool cursorWarpBackoffActive = false;
    int cursorWarpLoopCount = 0;
    // Set once the loop has been confirmed repeatedly. Giving up entirely is the
    // safe degradation: the WinTab context is opened with CXO_SYSTEM, so the
    // driver moves the system cursor anyway — all that is lost is our own
    // sub-pixel alignment of it, which is worth far less than a machine whose
    // pointer cannot be moved.
    bool cursorWarpDisabled = false;
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
    float currentDispatchStrokeElapsedSeconds = 0.0f;
    bool currentDispatchHasStrokeElapsed = false;
    bool nativeStrokePacketClockActive = false;
    bool nativeStrokePacketClockRejected = false;
    quint32 nativeStrokePacketTimeBaseMs = 0;
    quint32 nativeStrokePacketTimeLastMs = 0;
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
// Consecutive warps to one position before the cursor is declared contested.
// Healthy input never reaches 2: the first warp lands and every later packet
// for the same position returns early.
constexpr int kMaxRepeatedCursorWarps = 6;
// How long to leave the system cursor alone once a loop is detected. Long
// enough that the user regains control of the machine, short enough that a
// one-off collision costs nothing noticeable.
constexpr qint64 kCursorWarpBackoffMs = 750;
// Backoffs before the warp is abandoned for the rest of the session.
constexpr int kMaxCursorWarpLoops = 3;

// How far the pen must travel, while the mouse owns the shared system pointer,
// before it is read as deliberate input rather than as the noise of a pen left
// lying on the tablet. Deliberate hover movement crosses this within a packet or
// two; a resting digitiser's jitter never does.
constexpr int kPenReclaimDistancePx = 12;
// Consecutive coherent packets required on top of that distance. At 200-266 Hz
// this is under 20 ms, so it cannot be felt — but a driver that alternates a real
// position with a bogus one clears the distance test on every other packet and
// would otherwise reclaim the pointer on the strength of the bogus one alone.
constexpr int kPenReclaimSampleCount = 3;
// How far apart two consecutive packets may be and still belong to the same
// continuous movement. Generous enough for a fast hover sweep, far short of the
// distance to a screen corner.
constexpr int kPenReclaimCoherencePx = 96;

// Whether this process owns the window the user is currently working in.
//
// A WinTab system context keeps delivering packets while the app sits in the
// background, so without this test a background Ruwa still drags the system
// cursor around — and two live instances (an old one that has not exited while
// its replacement starts up) fight over it forever, each undoing the other's
// warp. Qt's applicationState is checked as well because it is the portable
// half of the answer, but the foreground-window test is what is authoritative
// during startup, when a window can exist before Qt reports it active.
bool applicationOwnsForegroundWindow()
{
#ifdef Q_OS_WIN
    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }
    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foreground, &foregroundProcessId);
    return foregroundProcessId == GetCurrentProcessId();
#else
    return QApplication::applicationState() == Qt::ApplicationActive;
#endif
}

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
    m_state->hasPenReclaimBaseline = false;
    m_state->penReclaimCandidateCount = 0;
    m_state->cursorWarpClock.start();
    m_state->pendingNativeCursorWarps.clear();
    m_state->lastCursorWarpTarget = QPoint();
    m_state->repeatedCursorWarpCount = 0;
    m_state->cursorWarpBackoffActive = false;
    m_state->cursorWarpLoopCount = 0;
    m_state->cursorWarpDisabled = false;
    m_state->hoverTarget.clear();
    m_state->activeMouseTarget.clear();
    m_state->activeCanvasTarget.clear();
    m_state->dispatchingToCanvas = false;
    m_state->dispatchingSyntheticMouseEvent = false;
    m_state->currentDispatchPressure = 0.0f;
    m_state->currentDispatchStrokeElapsedSeconds = 0.0f;
    m_state->currentDispatchHasStrokeElapsed = false;
    m_state->nativeStrokePacketClockActive = false;
    m_state->nativeStrokePacketClockRejected = false;
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

    if (proximityGained) {
        // The pen physically came back. Measure its movement from wherever it
        // re-enters, not from the stale position it was left at.
        m_state->hasPenReclaimBaseline = false;
        m_state->penReclaimCandidateCount = 0;
    }

    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();

    auto grantPenOwnership = [&](const QString& reason, const QPoint& pos) {
        m_state->nativePointerIsActive = true;
        m_state->hasPenReclaimBaseline = false;
        m_state->penReclaimCandidateCount = 0;
        if (trace::enabled()) {
            trace::write(QStringLiteral("OWNERSHIP -> pen (") + reason + QStringLiteral(") at (")
                + QString::number(pos.x()) + QStringLiteral(",") + QString::number(pos.y())
                + QStringLiteral(")"));
        }
    };

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
        if (!proximityLost && !m_state->nativePointerIsActive) {
            // The mouse owns the shared system pointer, so the pen has to earn it
            // back. None of the events that merely prove the tablet is alive count:
            // a packet arriving, proximity being regained, or a position that
            // differs from wherever the last stroke ended are all things a pen left
            // lying inside hover range produces on its own, and any of them
            // granting ownership lets the driver yank the cursor out from under the
            // mouse the user is actually holding.
            if (currentButtons != previousButtons) {
                // A button transition is unambiguous intent whatever the position,
                // and it must stay immediate: it is the first packet of a stroke.
                grantPenOwnership(QStringLiteral("button"), globalPos);
            } else if (!m_state->hasPenReclaimBaseline) {
                // First packet since the mouse took over. Remember where the pen is
                // resting; it has to travel away from here to be believed.
                m_state->penReclaimBaseline = globalPos;
                m_state->penReclaimCandidateCount = 0;
                m_state->hasPenReclaimBaseline = true;
            } else if ((globalPos - m_state->penReclaimBaseline).manhattanLength()
                < kPenReclaimDistancePx) {
                // Still resting. Jitter around the baseline restarts the streak so
                // that noise cannot accumulate its way into ownership.
                m_state->penReclaimCandidateCount = 0;
            } else if (m_state->penReclaimCandidateCount > 0
                && (globalPos - m_state->penReclaimCandidate).manhattanLength()
                    <= kPenReclaimCoherencePx) {
                ++m_state->penReclaimCandidateCount;
                m_state->penReclaimCandidate = globalPos;
            } else {
                m_state->penReclaimCandidateCount = 1;
                m_state->penReclaimCandidate = globalPos;
            }

            if (m_state->penReclaimCandidateCount >= kPenReclaimSampleCount) {
                grantPenOwnership(QStringLiteral("movement"), globalPos);
            }
        }

        if (!m_state->nativePointerIsActive) {
            // The mouse still owns the pointer. Forwarding this packet would revive
            // the stale canvas cursor and route UI hover to a pen nobody is holding.
            continue;
        }

        // Windows exposes one shared pointer for mouse and pen. Keep it aligned
        // during hover/UI interaction and on the press/release boundaries.
        // Between those boundaries a captured canvas stroke already routes and
        // renders from WinTab coordinates while the system cursor is blank, so
        // warping it for every hardware packet only generates redundant
        // asynchronous WM_MOUSEMOVE traffic. The release packet synchronizes
        // the final position before normal UI routing can resume.
        const bool capturedCanvasStrokeMove = m_state->activeCanvasTarget
            && previousButtons.testFlag(Qt::LeftButton) && currentButtons.testFlag(Qt::LeftButton);
        if (!capturedCanvasStrokeMove) {
            syncSystemCursorFromNative(globalPos);
        }

        const Qt::MouseButton button = changedButton(previousButtons, currentButtons);
        const Qt::MouseButtons addedButtons = currentButtons & ~previousButtons;
        const Qt::MouseButtons removedButtons = previousButtons & ~currentButtons;

        const bool canvasCaptureActive
            = m_state->activeCanvasTarget && previousButtons != Qt::NoButton;
        const bool uiCaptureActive = (m_state->activeMouseTarget || m_state->pendingPressTarget)
            && previousButtons != Qt::NoButton;
        QWidget* canvasTarget = nullptr;
        if (canvasCaptureActive) {
            // A stroke that began on canvas owns the complete press/move/release
            // sequence, even after crossing over UI. Do not run a topmost-widget
            // lookup first: capture already makes its result irrelevant.
            canvasTarget = m_state->activeCanvasTarget;
        } else if (!uiCaptureActive) {
            canvasTarget = resolveCanvasTarget(nullptr, globalPos);
        } else {
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
            // the correct pressure/time for this specific packet (not the snapshot).
            m_state->dispatchingToCanvas = true;
            m_state->currentDispatchPressure = pkt.pressure;
            m_state->currentDispatchHasStrokeElapsed = false;

            const bool leftPressedNow = currentButtons.testFlag(Qt::LeftButton);
            const bool leftWasPressed = previousButtons.testFlag(Qt::LeftButton);
            if (leftPressedNow && !leftWasPressed) {
                m_state->nativeStrokePacketClockRejected = false;
                m_state->nativeStrokePacketClockActive = pkt.hasPacketTime;
                if (pkt.hasPacketTime) {
                    m_state->nativeStrokePacketTimeBaseMs = pkt.packetTimeMs;
                    m_state->nativeStrokePacketTimeLastMs = pkt.packetTimeMs;
                }
            } else if ((leftPressedNow || leftWasPressed) && pkt.hasPacketTime
                && m_state->nativeStrokePacketClockActive) {
                // Unsigned subtraction intentionally handles GetTickCount-style
                // 32-bit wraparound. A jump over one hour cannot be a normal
                // adjacent packet; reject this driver's time stream for the
                // remainder of the stroke instead of injecting a huge delta.
                constexpr quint32 kMaxAdjacentPacketGapMs = 60u * 60u * 1000u;
                const quint32 adjacentDelta
                    = pkt.packetTimeMs - m_state->nativeStrokePacketTimeLastMs;
                if (adjacentDelta > kMaxAdjacentPacketGapMs) {
                    m_state->nativeStrokePacketClockActive = false;
                    m_state->nativeStrokePacketClockRejected = true;
                } else {
                    m_state->nativeStrokePacketTimeLastMs = pkt.packetTimeMs;
                }
            }

            if (m_state->nativeStrokePacketClockActive && !m_state->nativeStrokePacketClockRejected
                && pkt.hasPacketTime) {
                const quint32 elapsedMs = pkt.packetTimeMs - m_state->nativeStrokePacketTimeBaseMs;
                m_state->currentDispatchStrokeElapsedSeconds
                    = static_cast<float>(elapsedMs) / 1000.0f;
                m_state->currentDispatchHasStrokeElapsed = true;
            }

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
            if (leftWasPressed && !leftPressedNow) {
                m_state->nativeStrokePacketClockActive = false;
                m_state->nativeStrokePacketClockRejected = false;
                m_state->currentDispatchHasStrokeElapsed = false;
            }
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

        // Resolved lazily: only this button-change branch needs it. The canvas
        // branch and the coalesced move-only branch above never read it, and
        // resolveMouseTarget ends in QApplication::widgetAt — a full
        // widget-tree hit test that is far too expensive to run for every
        // 200-266 Hz WinTab packet of a canvas stroke.
        QWidget* target = resolveMouseTarget(m_state->activeMouseTarget, globalPos);

        updateHoverTarget(m_state->hoverTarget, target);

        if (addedButtons != Qt::NoButton) {
            QWidget* pressTarget = target;
            if (pressTarget && button != Qt::NoButton) {
                ruwa::ui::widgets::applySyntheticMousePressFocus(pressTarget);

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
        m_state->nativeStrokePacketClockActive = false;
        m_state->nativeStrokePacketClockRejected = false;
        m_state->currentDispatchHasStrokeElapsed = false;
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

    if (trace::enabled() && m_state->nativePointerIsActive) {
        trace::write(QStringLiteral("OWNERSHIP -> mouse"));
    }

    m_state->nativePointerIsActive = false;
    m_state->hasCoalescedUiMove = false;
    m_state->coalescedUiMoveTarget.clear();
    // The pen must re-earn the pointer from wherever it sits now, not from the
    // position it held when the mouse took over.
    m_state->hasPenReclaimBaseline = false;
    m_state->penReclaimCandidateCount = 0;
}

bool StylusInputManager::consumeNativeCursorWarpAt(const QPoint& globalPos)
{
    if (!m_state) {
        return false;
    }

    auto& warps = m_state->pendingNativeCursorWarps;
    const qint64 nowMs = m_state->cursorWarpClock.elapsed();
    warps.erase(
        std::remove_if(warps.begin(), warps.end(),
            [nowMs](const auto& warp) { return nowMs - warp.queuedAtMs > kCursorWarpLifetimeMs; }),
        warps.end());

    const auto it = std::find_if(warps.begin(), warps.end(),
        [&globalPos](const auto& warp) { return warp.pos == globalPos; });
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
    if (!m_state || m_state->cursorWarpDisabled) {
        return;
    }

    // Never move the pointer on behalf of a background process. The user is
    // working somewhere else, and a WinTab system context goes on delivering
    // packets regardless of focus — so this is also what stops two instances of
    // Ruwa (an exiting one and its replacement) from tearing the cursor between
    // them during a restart.
    if (!applicationOwnsForegroundWindow()) {
        m_state->repeatedCursorWarpCount = 0;
        m_state->cursorWarpBackoffActive = false;
        return;
    }

    if (QCursor::pos() == globalPos) {
        // The warp landed (or was never needed). Whatever ran before was not a
        // loop, so let the guard forget it.
        m_state->repeatedCursorWarpCount = 0;
        m_state->cursorWarpBackoffActive = false;
        return;
    }

    if (m_state->cursorWarpBackoffActive) {
        if (m_state->cursorWarpBackoffClock.isValid()
            && m_state->cursorWarpBackoffClock.elapsed() < kCursorWarpBackoffMs) {
            return;
        }
        // Try again from a clean slate. If the contention is still there the
        // count rebuilds in a few packets and the cursor is released once more,
        // so the machine stays usable either way.
        m_state->cursorWarpBackoffActive = false;
        m_state->repeatedCursorWarpCount = 0;
    }

    if (globalPos == m_state->lastCursorWarpTarget) {
        ++m_state->repeatedCursorWarpCount;
    } else {
        m_state->lastCursorWarpTarget = globalPos;
        m_state->repeatedCursorWarpCount = 0;
    }
    if (m_state->repeatedCursorWarpCount >= kMaxRepeatedCursorWarps) {
        // The pen is not moving (same target every time) yet the cursor keeps
        // leaving that position. Something else owns the pointer; stop pulling.
        m_state->cursorWarpBackoffActive = true;
        m_state->cursorWarpBackoffClock.start();
        m_state->pendingNativeCursorWarps.clear();
        if (++m_state->cursorWarpLoopCount >= kMaxCursorWarpLoops) {
            // Backing off repeatedly means the contention is not transient. Stop
            // for good rather than handing the pointer back for 22 ms out of
            // every second — that reads as a machine-wide fault to the user.
            m_state->cursorWarpDisabled = true;
        }
        return;
    }

    auto& warps = m_state->pendingNativeCursorWarps;
    warps.push_back({ globalPos, m_state->cursorWarpClock.elapsed() });
    constexpr size_t kMaxPendingCursorWarps = 64;
    if (warps.size() > kMaxPendingCursorWarps) {
        warps.erase(warps.begin(), warps.begin() + kMaxPendingCursorWarps / 2);
    }
    if (trace::enabled()) {
        const QPoint from = QCursor::pos();
        trace::write(QStringLiteral("SETPOS (") + QString::number(from.x()) + QStringLiteral(",")
            + QString::number(from.y()) + QStringLiteral(") -> (")
            + QString::number(globalPos.x()) + QStringLiteral(",")
            + QString::number(globalPos.y()) + QStringLiteral(")"));
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

std::optional<float> StylusInputManager::dispatchStrokeElapsedSeconds() const
{
    if (m_state && m_state->dispatchingToCanvas && m_state->currentDispatchHasStrokeElapsed) {
        return m_state->currentDispatchStrokeElapsedSeconds;
    }
    return std::nullopt;
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
