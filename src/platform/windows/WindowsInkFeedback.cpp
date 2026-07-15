// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   W I N D O W S   I N K   F E E D B A C K
// ==========================================================================

#include "WindowsInkFeedback.h"

#if defined(Q_OS_WIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
#if __has_include(<tpcshrd.h>)
#include <tpcshrd.h>
#endif
#ifndef WM_TABLET_QUERYSYSTEMGESTURESTATUS
#define WM_TABLET_QUERYSYSTEMGESTURESTATUS 0x02CC
#endif
#ifndef TABLET_DISABLE_PRESSANDHOLD
#define TABLET_DISABLE_PRESSANDHOLD 0x00000001
#endif
#ifndef TABLET_DISABLE_PENTAPFEEDBACK
#define TABLET_DISABLE_PENTAPFEEDBACK 0x00000008
#endif
#ifndef TABLET_DISABLE_PENBARRELFEEDBACK
#define TABLET_DISABLE_PENBARRELFEEDBACK 0x00000010
#endif
#ifndef TABLET_DISABLE_FLICKS
#define TABLET_DISABLE_FLICKS 0x00010000
#endif

#include <unordered_set>

namespace aether {
namespace platform {

namespace {

std::unordered_set<void*> s_configuredHwnds;

} // namespace

bool configureWindowsInkFeedback(void* hwnd)
{
    if (!hwnd) {
        return false;
    }
    if (s_configuredHwnds.count(hwnd)) {
        return true;
    }

    const HWND winHwnd = reinterpret_cast<HWND>(hwnd);

#if defined(FEEDBACK_PEN_TAP) && defined(SetWindowFeedbackSetting)
    BOOL enabled = FALSE;
    SetWindowFeedbackSetting(winHwnd, FEEDBACK_PEN_TAP, 0, sizeof(enabled), &enabled);
    SetWindowFeedbackSetting(winHwnd, FEEDBACK_PEN_DOUBLETAP, 0, sizeof(enabled), &enabled);
    SetWindowFeedbackSetting(winHwnd, FEEDBACK_PEN_PRESSANDHOLD, 0, sizeof(enabled), &enabled);
    SetWindowFeedbackSetting(winHwnd, FEEDBACK_TOUCH_TAP, 0, sizeof(enabled), &enabled);
    SetWindowFeedbackSetting(winHwnd, FEEDBACK_TOUCH_DOUBLETAP, 0, sizeof(enabled), &enabled);
    SetWindowFeedbackSetting(winHwnd, FEEDBACK_TOUCH_PRESSANDHOLD, 0, sizeof(enabled), &enabled);
    SetWindowFeedbackSetting(
        winHwnd, FEEDBACK_TOUCH_CONTACTVISUALIZATION, 0, sizeof(enabled), &enabled);
#endif

    s_configuredHwnds.insert(hwnd);
    return true;
}

bool handleWindowsInkNativeEvent(void* message, qintptr* result)
{
    MSG* msg = static_cast<MSG*>(message);
    if (!msg) {
        return false;
    }

    if (msg->message == WM_TABLET_QUERYSYSTEMGESTURESTATUS) {
        const DWORD disableMask = TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK
            | TABLET_DISABLE_PENBARRELFEEDBACK | TABLET_DISABLE_FLICKS;
        if (result) {
            *result = static_cast<qintptr>(disableMask);
        }
        return true;
    }

    return false;
}

} // namespace platform
} // namespace aether

#endif // Q_OS_WIN
