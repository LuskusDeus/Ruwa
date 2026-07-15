// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   W I N D O W S   I N K   F E E D B A C K
// ==========================================================================
// Platform-specific Windows Ink feedback configuration and native event
// handling. Isolates WM_TABLET_QUERYSYSTEMGESTURESTATUS and
// SetWindowFeedbackSetting from the main canvas widget.
// ==========================================================================

#ifndef RUWA_PLATFORM_WINDOWS_WINDOWSINKFEEDBACK_H
#define RUWA_PLATFORM_WINDOWS_WINDOWSINKFEEDBACK_H

#include <QtCore/qglobal.h>

namespace aether {
namespace platform {

/// Configure Windows Ink feedback for the given window handle.
/// Disables press-and-hold, pen tap feedback, touch feedback, etc.
/// Returns true if configuration was applied (or already done for this hwnd).
bool configureWindowsInkFeedback(void* hwnd);

/// Handle Windows-specific native events (e.g. WM_TABLET_QUERYSYSTEMGESTURESTATUS).
/// Returns true if the event was handled; caller should not pass to base class.
/// \a message is the MSG* from QWidget::nativeEvent.
/// \a result receives the response value when handling WM_TABLET_QUERYSYSTEMGESTURESTATUS.
bool handleWindowsInkNativeEvent(void* message, qintptr* result);

} // namespace platform
} // namespace aether

#endif // RUWA_PLATFORM_WINDOWS_WINDOWSINKFEEDBACK_H
