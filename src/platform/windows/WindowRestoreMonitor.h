// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   W I N D O W   R E S T O R E   M O N I T O R
// ==========================================================================
// Keeps the Win32 restore rectangle of the main window on the display the
// window is actually shown on, so un-minimizing does not throw it back onto
// the display it was started on.
// ==========================================================================

#ifndef RUWA_PLATFORM_WINDOWS_WINDOWRESTOREMONITOR_H
#define RUWA_PLATFORM_WINDOWS_WINDOWRESTOREMONITOR_H

#include <QtCore/qglobal.h>

namespace aether {
namespace platform {

/// Observes the window messages that change which display a top-level window
/// lives on and repairs its restore rectangle when the two drift apart.
/// Never consumes the message - call it from nativeEvent() and keep going.
/// \a message is the MSG* from QWidget::nativeEvent.
void trackWindowRestoreMonitor(void* message);

} // namespace platform
} // namespace aether

#endif // RUWA_PLATFORM_WINDOWS_WINDOWRESTOREMONITOR_H
