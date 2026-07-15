// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S T A R T U P   C O N T R O L L E R
// ======================================================================================
//   File        : StartupController.h
//   Description : Orchestrates entire application startup sequence
// ======================================================================================

#ifndef RUWA_CORE_STARTUP_STARTUPCONTROLLER_H
#define RUWA_CORE_STARTUP_STARTUPCONTROLLER_H

#include "features/startup/StartupSequence.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace ruwa {
class Application;

namespace ui::windows {
class SplashScreen;
class MainWindow;
} // namespace ui::windows

namespace core {

class StartupAnimationController;

/**
 * @brief Main controller for application startup sequence
 *
 * Responsibilities:
 * - Coordinate all startup phases
 * - Create and manage splash screen
 * - Initialize managers (fonts, themes, commands)
 * - Create main window
 * - Orchestrate animations
 * - Handle platform-specific setup
 *
 * This is the ONLY class that knows about the entire startup flow.
 * SplashScreen, MainWindow, and Application know nothing about each other.
 */
class StartupController : public QObject {
    Q_OBJECT

public:
    explicit StartupController(Application* app, QObject* parent = nullptr);
    ~StartupController() override;

    /**
     * @brief Start the full startup sequence
     *
     * Phases:
     * 1. Show splash screen with appearance animation
     * 2. Initialize managers (fonts, themes, commands)
     * 3. Create main window (hidden)
     * 4. Expand splash to window size
     * 5. Cascade fade-in animations for widgets
     * 6. Complete and clean up
     */
    void start();

    /// Files requested by the OS command line at process startup.
    void setStartupOpenFilePaths(const QStringList& filePaths);

signals:
    /**
     * @brief Emitted when startup is complete
     * @param mainWindow Created main window (ready to use)
     */
    void startupComplete(ruwa::ui::windows::MainWindow* mainWindow);

    /**
     * @brief Progress update during initialization
     * @param message Status message
     * @param percentage Progress (0-100)
     */
    void progressUpdate(const QString& message, int percentage);

private slots:
    void onApplicationLoadingProgress(const QString& message, int percentage);
    void onSplashAppearanceFinished();
    void onManagersInitialized();
    void onAnimationsCompleted();

private:
    void showSplash();
    void initializeManagers();
    void createMainWindow();
    void expandToMainWindow();

private:
    Application* m_app = nullptr;
    StartupPhase m_currentPhase = StartupPhase::AppCreate;
    QStringList m_startupOpenFilePaths;

    ui::windows::SplashScreen* m_splash = nullptr;
    ui::windows::MainWindow* m_mainWindow = nullptr;
    StartupAnimationController* m_animationController = nullptr;
};

} // namespace core
} // namespace ruwa

#endif // RUWA_CORE_STARTUP_STARTUPCONTROLLER_H
