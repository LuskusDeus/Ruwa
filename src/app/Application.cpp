// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   I M P L E M E N T A T I O N
// ==========================================================================

#include "app/Application.h"
#include "app/TabletToMouseEventFilter.h"
#include "services/input/StylusInputManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/FontManager.h"
#include "commands/CommandRegistry.h"
#include "commands/ShortcutManager.h"
#include "commands/definitions/CommandDefinitions.h"
#include "features/settings/SettingsManager.h"
#include "features/brush/manager/BrushManager.h"
#include "services/discord/DiscordService.h"
#include "services/updates/UpdateManager.h"
#include "shared/rendering/GLProgramBinaryCache.h"
#include "shared/rendering/GLShaderWarmup.h"

#include <QEventLoop>
#include <QFileOpenEvent>
#include <QSettings>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QProcess>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
namespace ruwa {

namespace {

void pumpStartupUi()
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

bool s_factoryResetRestartInProgress = false;
int s_currentTabletBackend = 0; // Currently active tablet backend
constexpr auto kFactoryResetArgument = "--factory-reset";

void clearSettingsStore(
    QSettings::Format format, const QString& organization, const QString& application)
{
    QSettings settings(format, QSettings::UserScope, organization, application);
    settings.clear();
    settings.sync();
}

QStringList sanitizedRestartArguments(bool includeFactoryReset)
{
    QStringList arguments;
    const QStringList currentArguments = QCoreApplication::arguments().mid(1);

    for (const QString& argument : currentArguments) {
        if (argument.compare(QLatin1String(kFactoryResetArgument), Qt::CaseInsensitive) == 0) {
            continue;
        }
        arguments.append(argument);
    }

    if (includeFactoryReset) {
        arguments.append(QLatin1String(kFactoryResetArgument));
    }

    return arguments;
}

bool resetWritableLocation(QStandardPaths::StandardLocation location)
{
    const QString path = QStandardPaths::writableLocation(location);
    if (path.isEmpty()) {
        return true;
    }

    const QString appName = QCoreApplication::applicationName().trimmed();
    const QString normalizedPath = QDir::cleanPath(path);
    if (!appName.isEmpty() && !normalizedPath.contains(appName, Qt::CaseInsensitive)) {
        return false;
    }

    QDir dir(path);
    if (!dir.exists()) {
        return true;
    }

    if (!dir.removeRecursively()) {
        return false;
    }

    return QDir().mkpath(path);
}
} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{

    setupDefaultSettings();
    initializeOpenGL(); // Pre-warm OpenGL context

    // Synthesize mouse events from stylus outside canvas
    // (AA_SynthesizeMouseForUnhandledTabletEvents is false)
    installEventFilter(new TabletToMouseEventFilter(this));
    services::input::StylusInputManager::instance().initialize(this);
}

Application::~Application()
{
    // Save shortcuts on exit unless this instance is being replaced by a
    // factory-reset restart. In that path the next process recreates defaults.
    if (!s_factoryResetRestartInProgress) {
        core::ShortcutManager::instance().saveToSettings();
    }

    // Cleanup OpenGL
    delete m_glWarmup;
    delete m_glContext;
    delete m_glSurface;
}

Application* Application::instance()
{
    return qobject_cast<Application*>(QApplication::instance());
}

bool Application::event(QEvent* event)
{
    if (event && event->type() == QEvent::FileOpen) {
        auto* openEvent = static_cast<QFileOpenEvent*>(event);
        const QString filePath = openEvent->file();
        if (!filePath.isEmpty()) {
            emit fileOpenRequested(filePath);
            return true;
        }
    }

    return QApplication::event(event);
}

// ==========================================================================
//   I N I T I A L I Z A T I O N
// ==========================================================================

void Application::initializeManagers()
{

    // 1. F O N T S   &   T H E M I N G
    // ----------------------------------------------------------------------
    // ThemeManager is usually already initialized in StartupController::start();
    // pumpStartupUi() after each progress line so the splash can repaint.
    auto& theme = ui::core::ThemeManager::instance();

    if (!theme.currentPreset()) {
        emit loadingProgress("Loading fonts...", 5);
        pumpStartupUi();
        emit loadingProgress("Initializing theme system...", 12);
        theme.initialize();
        emit loadingProgress("Applying theme...", 20);
        theme.applyTheme();
        pumpStartupUi();
    } else {
        emit loadingProgress("Theme & fonts ready", 10);
        pumpStartupUi();
    }

    // 2. C O M M A N D   S Y S T E M
    // ----------------------------------------------------------------------
    emit loadingProgress("Registering commands...", 28);
    pumpStartupUi();
    core::commands::registerAllCommands();
    pumpStartupUi();

    // 3. S H O R T C U T S
    // ----------------------------------------------------------------------
    emit loadingProgress("Loading shortcuts...", 36);
    pumpStartupUi();
    auto& shortcuts = core::ShortcutManager::instance();
    shortcuts.loadFromSettings();
    pumpStartupUi();

    // 4. D I S C O R D   R I C H   P R E S E N C E
    // ----------------------------------------------------------------------
    emit loadingProgress("Connecting services...", 44);
    pumpStartupUi();
    services::DiscordService::instance()->initialize();
    pumpStartupUi();

    // 5. U P D A T E   M A N A G E R   ( N E T W O R K   T E S T )
    // ----------------------------------------------------------------------
    emit loadingProgress("Checking for updates...", 52);
    pumpStartupUi();
    services::UpdateManager::instance()->initialize();
    pumpStartupUi();

    // Leave headroom for MainWindow construction & transition (StartupController).
    emit loadingProgress("Core modules loaded", 60);
    pumpStartupUi();
}

void Application::warmUpOpenGLShaders()
{
    if (!m_glContext || !m_glSurface) {
        return;
    }

    emit loadingProgress("Preparing OpenGL shader cache...", 62);
    pumpStartupUi();

    auto warmupResult = aether::warmUpOpenGLShaderPrograms(
        m_glContext, m_glSurface, [this](const QString& message, int percentage) {
            emit loadingProgress(message, percentage);
            pumpStartupUi();
        });

    if (!warmupResult) {
        return;
    }

    emit loadingProgress("OpenGL shaders ready", 94);
    pumpStartupUi();
}

void Application::setupDefaultSettings()
{

    // 1. P E R S I S T E N C E
    // ----------------------------------------------------------------------
    QSettings::setDefaultFormat(QSettings::IniFormat);
    setOrganizationName("Ruwa");
    setOrganizationDomain("Ruwa.app");
    setApplicationName("Ruwa");
    // Version is set in main.cpp before Application creation

    // 2. G R A P H I C S   C O N T E X T   ( O p e n G L )
    // ----------------------------------------------------------------------
    // Note: Used for Qt Quick / Widgets mixing, independent of Vulkan engine
    QSurfaceFormat format;
    format.setVersion(4, 5); // OpenGL 4.5
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4); // 4x MSAA Antialiasing

    QSurfaceFormat::setDefaultFormat(format);
}

void Application::initializeOpenGL()
{

    // Pre-warm OpenGL context at startup to avoid delay when creating canvas
    // This also ensures context sharing works properly

    m_glSurface = new QOffscreenSurface;
    m_glSurface->setFormat(QSurfaceFormat::defaultFormat());
    m_glSurface->create();

    m_glContext = new QOpenGLContext;
    m_glContext->setFormat(QSurfaceFormat::defaultFormat());

    if (m_glContext->create()) {
        m_glContext->makeCurrent(m_glSurface);

        // Log OpenGL info
        const char* vendor
            = reinterpret_cast<const char*>(m_glContext->functions()->glGetString(GL_VENDOR));
        const char* renderer
            = reinterpret_cast<const char*>(m_glContext->functions()->glGetString(GL_RENDERER));
        const char* version
            = reinterpret_cast<const char*>(m_glContext->functions()->glGetString(GL_VERSION));

        m_glContext->doneCurrent();
    }

    // Create hidden OpenGL widget to force Qt to initialize OpenGL subsystem
    // This prevents window recreation when first visible OpenGL widget is created
    m_glWarmup = new QOpenGLWidget;
    m_glWarmup->setFixedSize(1, 1);
    m_glWarmup->setAttribute(Qt::WA_DontShowOnScreen);
    m_glWarmup->setAttribute(Qt::WA_QuitOnClose, false);
    m_glWarmup->show(); // Triggers initializeGL()
}

QOpenGLContext* Application::sharedGLContext()
{
    return m_glContext;
}

bool Application::restart()
{
    const QString program = QCoreApplication::applicationFilePath();
    const QStringList arguments = sanitizedRestartArguments(false);
    const QString workingDirectory = QDir::currentPath();

    QWidget* windowToClose = QApplication::activeWindow();
    if (windowToClose) {
        windowToClose = windowToClose->window();
    }

    if (!windowToClose) {
        const QWidgetList topLevel = QApplication::topLevelWidgets();
        for (QWidget* widget : topLevel) {
            if (widget && widget->isVisible()) {
                windowToClose = widget;
                break;
            }
        }
    }

    if (windowToClose) {
        QPointer<QWidget> guard = windowToClose;
        windowToClose->close();

        // Closing can be rejected (e.g. unsaved projects confirmation).
        if (guard && guard->isVisible()) {
            return false;
        }
    }

    const bool started = QProcess::startDetached(program, arguments, workingDirectory);

    if (started) {
        QCoreApplication::quit();
    }

    return started;
}

bool Application::restartWithFactoryReset()
{
    const QString program = QCoreApplication::applicationFilePath();
    const QStringList arguments = sanitizedRestartArguments(true);
    const QString workingDirectory = QDir::currentPath();
    s_factoryResetRestartInProgress = true;

    QWidget* windowToClose = QApplication::activeWindow();
    if (windowToClose) {
        windowToClose = windowToClose->window();
    }

    if (!windowToClose) {
        const QWidgetList topLevel = QApplication::topLevelWidgets();
        for (QWidget* widget : topLevel) {
            if (widget && widget->isVisible()) {
                windowToClose = widget;
                break;
            }
        }
    }

    if (windowToClose) {
        QPointer<QWidget> guard = windowToClose;
        windowToClose->close();

        if (guard && guard->isVisible()) {
            s_factoryResetRestartInProgress = false;
            return false;
        }
    }

    const bool started = QProcess::startDetached(program, arguments, workingDirectory);

    if (started) {
        QCoreApplication::quit();
    } else {
        s_factoryResetRestartInProgress = false;
    }

    return started;
}

bool Application::isFactoryResetRestartInProgress()
{
    return s_factoryResetRestartInProgress;
}

bool Application::restartWithUpdate()
{
    auto* updateManager = services::UpdateManager::instance();
    if (!updateManager->hasPendingDownloadedUpdate()) {
        return false;
    }

    QWidget* windowToClose = QApplication::activeWindow();
    if (windowToClose) {
        windowToClose = windowToClose->window();
    }

    if (!windowToClose) {
        const QWidgetList topLevel = QApplication::topLevelWidgets();
        for (QWidget* widget : topLevel) {
            if (widget && widget->isVisible()) {
                windowToClose = widget;
                break;
            }
        }
    }

    if (windowToClose) {
        QPointer<QWidget> guard = windowToClose;
        windowToClose->close();

        // Closing can be rejected (e.g. unsaved projects confirmation).
        if (guard && guard->isVisible()) {
            return false;
        }
    }

    return updateManager->applyUpdateAndRestart();
}

bool Application::runFactoryResetIfRequested(const QStringList& arguments)
{
    if (!arguments.contains(QLatin1String(kFactoryResetArgument), Qt::CaseInsensitive)) {
        return false;
    }

    const QString organization = QStringLiteral("Ruwa");
    const QString application = QStringLiteral("Ruwa");

    clearSettingsStore(QSettings::NativeFormat, organization, application);
    clearSettingsStore(QSettings::IniFormat, organization, application);

    bool ok = true;
    QString shaderCacheError;
    if (!aether::GLProgramBinaryCache::clearPersistentCache(&shaderCacheError)) {
        ok = false;
    }
    ok = resetWritableLocation(QStandardPaths::CacheLocation) && ok;
    ok = resetWritableLocation(QStandardPaths::AppDataLocation) && ok;
    ok = resetWritableLocation(QStandardPaths::AppLocalDataLocation) && ok;

    if (!ok) { }

    return true;
}

int Application::currentTabletBackend()
{
    return s_currentTabletBackend;
}

void Application::setCurrentTabletBackend(int backend)
{
    s_currentTabletBackend = backend;
}

} // namespace ruwa
