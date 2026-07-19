// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_INPUT_STYLUSINPUTMANAGER_H
#define RUWA_SERVICES_INPUT_STYLUSINPUTMANAGER_H

#include <QPoint>
#include <optional>
#include <vector>

class QApplication;
class QMouseEvent;
class QTabletEvent;
class QWidget;

namespace ruwa::services::input {

class StylusInputManager {
public:
    static StylusInputManager& instance();

    void initialize(QApplication* application);

    bool handleTabletEvent(QWidget* target, QTabletEvent* event);
    bool handleNativeEvent(void* message);
    float effectivePressure(const QTabletEvent* event) const;
    bool usesNativeUiRouting() const;
    /// True only during synchronous mouse-event dispatch from the Ruwa WinTab stream.
    /// Cursor-warp and other OS mouse events remain false and can be filtered safely.
    bool isDispatchingNativeInput() const;
    /// Whether the direct WinTab pointer currently has at least one pressed button.
    bool hasActiveNativePointerButtons() const;
    /// Marks authoritative mouse input and hands ownership to the shared system cursor.
    void activateMousePointer();
    /// Consumes a delayed MouseMove known to have been posted by a native QCursor warp.
    bool consumeNativeCursorWarpAt(const QPoint& globalPos);
    float dispatchPressure() const;
    /// Hardware-timestamped elapsed time for the WinTab stroke currently being
    /// dispatched. Empty for ordinary mouse/Qt tablet input and for drivers
    /// that do not provide a usable PK_TIME stream.
    std::optional<float> dispatchStrokeElapsedSeconds() const;
    bool shouldIgnoreCanvasMouseMove(const QMouseEvent* event) const;

    /// Returns the direct WinTab position while it is the active pointer source.
    std::optional<QPoint> nativeCursorPosition() const;

    /// One recovered mouse position from the OS coalescing buffer.
    /// `wmTimeMs` is the WM_MOUSEMOVE timestamp (GetMouseMovePointsEx
    /// MOUSEMOVEPOINT.time, in ms since boot — only deltas are meaningful).
    /// `currentWmTimeMs` is the matched current point's timestamp from the
    /// same history batch, so consumers do not mix WinAPI and Qt clocks.
    struct RecoveredMousePoint {
        QPoint pos;
        unsigned long wmTimeMs = 0;
        unsigned long currentWmTimeMs = 0;
    };

    /// Recover intermediate mouse positions lost to OS-level WM_MOUSEMOVE coalescing.
    /// Returns points between the last processed position and @p currentScreenPos
    /// (oldest-first).  The current position itself is NOT included.
    static std::vector<RecoveredMousePoint> recoverMouseMoveHistory(const QPoint& currentScreenPos);

    /// Reset mouse history tracking (call when a stroke begins or ends).
    static void resetMouseMoveHistory();

private:
    StylusInputManager() = default;
    ~StylusInputManager();
    void clearPendingStylusSwipe();
    void recordNativeCanvasDispatch();
    void syncSystemCursorFromNative(const QPoint& globalPos);

    StylusInputManager(const StylusInputManager&) = delete;
    StylusInputManager& operator=(const StylusInputManager&) = delete;

    struct State;
    State* m_state = nullptr;
};

} // namespace ruwa::services::input

#endif // RUWA_SERVICES_INPUT_STYLUSINPUTMANAGER_H
