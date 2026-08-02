// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   W I N D O W   R E S T O R E   M O N I T O R
// ==========================================================================

#include "platform/windows/WindowRestoreMonitor.h"

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>

#include <algorithm>
#endif

namespace aether {
namespace platform {

#if defined(Q_OS_WIN)
namespace {

// Windows decides where to put a window coming back from the taskbar from
// WINDOWPLACEMENT::rcNormalPosition - not from where the window was last seen.
// A maximized window moved with SC_DRAGMOVE (what QWindow::startSystemMove()
// issues, i.e. dragging Ruwa by its custom title bar) is relocated without that
// rectangle ever being updated, so the display the window is drawn on and the
// display it will be restored to drift apart. Un-minimizing then snaps the
// window back to the display it was originally maximized on.
//
// Re-anchoring the restore rectangle to the current display fixes un-minimizing
// and un-maximizing at once, and leaves both to the window manager afterwards.

/// Last display the window was seen on while it was not minimized: a minimized
/// window sits at off-desktop coordinates and can no longer report it itself.
HWND g_trackedWindow = nullptr;
HMONITOR g_lastMonitor = nullptr;

/// SetWindowPlacement() feeds messages back into the window procedure we are
/// called from.
bool g_repairing = false;

void repairRestoreRect(HWND hwnd, HMONITOR monitor)
{
    if (!hwnd || !monitor || g_repairing) {
        return;
    }

    WINDOWPLACEMENT placement {};
    placement.length = sizeof(placement);
    if (!::GetWindowPlacement(hwnd, &placement)) {
        return;
    }

    HMONITOR restoreMonitor
        = ::MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
    if (!restoreMonitor || restoreMonitor == monitor) {
        return;
    }

    MONITORINFO from {};
    from.cbSize = sizeof(from);
    MONITORINFO to {};
    to.cbSize = sizeof(to);
    if (!::GetMonitorInfoW(restoreMonitor, &from) || !::GetMonitorInfoW(monitor, &to)) {
        return;
    }

    // Carry the restored size over to the same spot of the other work area, then
    // clamp it in - the displays rarely have the same size.
    const LONG width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
    const LONG height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
    LONG left = to.rcWork.left + (placement.rcNormalPosition.left - from.rcWork.left);
    LONG top = to.rcWork.top + (placement.rcNormalPosition.top - from.rcWork.top);
    left = std::clamp(left, to.rcWork.left, std::max(to.rcWork.left, to.rcWork.right - width));
    top = std::clamp(top, to.rcWork.top, std::max(to.rcWork.top, to.rcWork.bottom - height));

    placement.rcNormalPosition.left = left;
    placement.rcNormalPosition.top = top;
    placement.rcNormalPosition.right = left + width;
    placement.rcNormalPosition.bottom = top + height;

    // Keep the current show state; only the restore rectangle is being corrected.
    // For a minimized window ask for the non-activating variant so the repair
    // cannot pull the window back to the foreground.
    if (::IsIconic(hwnd)) {
        placement.showCmd = SW_SHOWMINNOACTIVE;
    }
    placement.flags &= ~static_cast<UINT>(WPF_SETMINPOSITION);

    g_repairing = true;
    ::SetWindowPlacement(hwnd, &placement);
    g_repairing = false;
}

} // namespace
#endif // Q_OS_WIN

void trackWindowRestoreMonitor(void* message)
{
#if defined(Q_OS_WIN)
    auto* msg = static_cast<MSG*>(message);
    if (!msg || !msg->hwnd || g_repairing) {
        return;
    }

    switch (msg->message) {
    case WM_WINDOWPOSCHANGED:
        if (!::IsIconic(msg->hwnd)) {
            g_trackedWindow = msg->hwnd;
            g_lastMonitor = ::MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
        }
        break;

    case WM_EXITSIZEMOVE:
        // A move or resize just ended - the common way the window changes display.
        // Repairing here keeps every later restore, from any source, correct.
        if (!::IsIconic(msg->hwnd)) {
            repairRestoreRect(msg->hwnd, ::MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST));
        }
        break;

    case WM_SIZE:
        // Last chance before the restore rectangle starts to matter. The window is
        // already off-desktop, so the display has to come from the tracked value.
        if (msg->wParam == SIZE_MINIMIZED && msg->hwnd == g_trackedWindow) {
            repairRestoreRect(msg->hwnd, g_lastMonitor);
        }
        break;

    default:
        break;
    }
#else
    Q_UNUSED(message);
#endif
}

} // namespace platform
} // namespace aether
