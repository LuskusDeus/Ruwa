// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S T A R T U P   S E Q U E N C E
// ======================================================================================
//   File        : StartupSequence.h
//   Description : Enumeration of startup phases
// ======================================================================================

#ifndef RUWA_CORE_STARTUP_STARTUPSEQUENCE_H
#define RUWA_CORE_STARTUP_STARTUPSEQUENCE_H

namespace ruwa::core {

/**
 * @brief Startup sequence phases
 *
 * Defines the ordered stages of application startup:
 *
 * 1. AppCreate      - QApplication construction
 * 2. ThemePreInit   - Early theme initialization (for splash screen)
 * 3. SplashShow     - Show splash screen
 * 4. SplashAppear   - Splash appearance animation
 * 5. ManagersInit   - Initialize fonts, themes, commands, shortcuts
 * 6. WindowCreate   - Create main window (hidden)
 * 7. Expand         - Expand splash to window size
 * 8. WidgetsAnimate - Cascade fade-in animations
 * 9. Complete       - Startup finished
 */
enum class StartupPhase {
    AppCreate,
    ThemePreInit,
    SplashShow,
    SplashAppear,
    ManagersInit,
    WindowCreate,
    Expand,
    WidgetsAnimate,
    Complete
};

} // namespace ruwa::core

#endif // RUWA_CORE_STARTUP_STARTUPSEQUENCE_H
