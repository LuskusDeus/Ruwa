// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S T A R T U P   C O N T R O L L E R   I M P L E M E N T A T I O N
// ======================================================================================

#include "features/startup/StartupController.h"
#include "features/startup/StartupAnimationController.h"
#include "app/Application.h"
#include "shell/main-window/SplashScreen.h"
#include "shell/main-window/MainWindow.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"

#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QtConcurrent>

namespace {

void prewarmSettingsSync(const QString& organization, const QString& application)
{
    if (organization.isEmpty() || application.isEmpty()) {
        return;
    }
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organization, application);
    settings.sync();
}

} // namespace

namespace ruwa::core {

StartupController::StartupController(Application* app, QObject* parent)
    : QObject(parent)
    , m_app(app)
{
    if (!m_app) {
        qFatal("StartupController: Application instance is null");
    }

    // Create animation controller
    m_animationController = new StartupAnimationController(this);

    // Connect to animation completion
    connect(m_animationController, &StartupAnimationController::animationsCompleted, this,
        &StartupController::onAnimationsCompleted);

    connect(m_app, &Application::loadingProgress, this,
        &StartupController::onApplicationLoadingProgress);
}

StartupController::~StartupController()
{
    // Cleanup handled by Qt parent-child ownership
}

void StartupController::onApplicationLoadingProgress(const QString& message, int percentage)
{
    if (m_splash) {
        m_splash->setStatus(message);
        m_splash->setProgress(percentage);
    }
    emit progressUpdate(message, percentage);
}

void StartupController::start()
{
    // Phase 1: ThemePreInit (before splash screen)
    m_currentPhase = StartupPhase::ThemePreInit;
    ui::core::ThemeManager::instance().initialize();
    ui::core::TranslationManager::instance().initialize();

    // Phase 2: Show splash screen
    m_currentPhase = StartupPhase::SplashShow;
    showSplash();
}

void StartupController::setStartupOpenFilePaths(const QStringList& filePaths)
{
    m_startupOpenFilePaths = filePaths;
}

void StartupController::showSplash()
{
    // Create splash screen
    m_splash = new ui::windows::SplashScreen();
    m_splash->show();

    // Warm QSettings I/O off the GUI thread so the first sync on the main thread
    // (or heavy reads during window setup) does not hitch the UI.
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    (void) QtConcurrent::run(prewarmSettingsSync, org, app);

    // Phase 3: Splash appearance animation
    m_currentPhase = StartupPhase::SplashAppear;
    m_splash->animateAppearance(400);

    // Connect to appearance finished
    connect(m_splash, &ui::windows::SplashScreen::appearanceFinished, this,
        &StartupController::onSplashAppearanceFinished);
}

void StartupController::onSplashAppearanceFinished()
{
    m_splash->setStatus("Loading workspace...");
    m_splash->setProgress(0);

    // Small delay before starting initialization
    QTimer::singleShot(100, this, &StartupController::initializeManagers);
}

void StartupController::initializeManagers()
{
    m_currentPhase = StartupPhase::ManagersInit;

    // Initialize all managers (fonts, themes, commands, shortcuts)
    m_app->initializeManagers();
    m_app->warmUpOpenGLShaders();

    // Managers initialized
    QTimer::singleShot(100, this, &StartupController::onManagersInitialized);
}

void StartupController::onManagersInitialized()
{
    if (m_splash) {
        m_splash->setStatus("Building interface...");
        m_splash->setProgress(96);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Phase 5: Create main window
    QTimer::singleShot(300, this, &StartupController::createMainWindow);
}

void StartupController::createMainWindow()
{
    m_currentPhase = StartupPhase::WindowCreate;

    if (m_splash) {
        m_splash->setStatus("Creating main window...");
        m_splash->setProgress(97);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Create main window (hidden)
    m_mainWindow = new ui::windows::MainWindow(nullptr, m_startupOpenFilePaths);
    m_startupOpenFilePaths.clear();

    if (m_splash) {
        m_splash->setStatus("Preparing workspace...");
        m_splash->setProgress(98);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Phase 6: Expand to main window
    QTimer::singleShot(100, this, &StartupController::expandToMainWindow);
}

void StartupController::expandToMainWindow()
{
    m_currentPhase = StartupPhase::Expand;

    if (m_splash) {
        m_splash->setStatus("Ready");
        m_splash->setProgress(100);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Start expansion animation (handled by StartupAnimationController)
    m_animationController->expandSplashToWindow(m_splash, m_mainWindow, 500);

    // Phase 7 (WidgetsAnimate) starts automatically after expansion
    m_currentPhase = StartupPhase::WidgetsAnimate;
}

void StartupController::onAnimationsCompleted()
{
    // Phase 8: Complete
    m_currentPhase = StartupPhase::Complete;
    // Emit completion signal
    emit startupComplete(m_mainWindow);

    // Note: m_splash is already deleted by StartupAnimationController
    m_splash = nullptr;

    // Self-delete after completion (optional)
    // this->deleteLater();
}

} // namespace ruwa::core
