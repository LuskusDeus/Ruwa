// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S T A R T U P   A N I M A T I O N   C O N T R O L L E R
// ======================================================================================
//   File        : StartupAnimationController.h
//   Description : Orchestrates all startup animations (splash expansion, widget cascades)
//   Location    : Core/Startup/
// ======================================================================================

#ifndef RUWA_CORE_STARTUP_STARTUPANIMATIONCONTROLLER_H
#define RUWA_CORE_STARTUP_STARTUPANIMATIONCONTROLLER_H

#include <QObject>

namespace ruwa::ui::windows {
class SplashScreen;
class MainWindow;
} // namespace ruwa::ui::windows

namespace ruwa::core {

/**
 * @brief Manages startup animation sequence
 *
 * Coordinates:
 * 1. Splash screen expansion to window size
 * 2. MainWindow preparation (opacity tricks to prevent flash)
 * 3. Cascade fade-in animations for all widgets
 *
 * This controller knows about both SplashScreen and MainWindow,
 * so neither has to know about the other.
 */
class StartupAnimationController : public QObject {
    Q_OBJECT

public:
    explicit StartupAnimationController(QObject* parent = nullptr);
    ~StartupAnimationController() override = default;

    /**
     * @brief Start expansion animation from splash to main window
     * @param splash Splash screen to expand
     * @param mainWindow Main window to transition to
     * @param durationMs Animation duration
     */
    void expandSplashToWindow(ruwa::ui::windows::SplashScreen* splash,
        ruwa::ui::windows::MainWindow* mainWindow, int durationMs = 500);

signals:
    /**
     * @brief Emitted when all startup animations complete
     */
    void animationsCompleted();
};

} // namespace ruwa::core

#endif // RUWA_CORE_STARTUP_STARTUPANIMATIONCONTROLLER_H
