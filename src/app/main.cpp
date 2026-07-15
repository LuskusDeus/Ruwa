// SPDX-License-Identifier: MPL-2.0

#include "app/Application.h"
#include "commands/CommandExecutor.h"
#include "features/effects/plugin/EffectPluginManager.h"
#include "features/startup/StartupController.h"
#include "services/updates/UpdateManager.h"
#include "shared/resources/IconProvider.h"
#include "shell/main-window/MainWindow.h"
#include "RuwaBuildConfig.h"

#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr auto kFactoryResetArgument = "--factory-reset";
constexpr auto kSkipElevationRestartArgument = "--skip-elevation-restart";

#ifdef Q_OS_WIN
bool hasCommandLineArgument(int argc, char* argv[], const char* argument)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]).compare(QLatin1String(argument), Qt::CaseInsensitive)
            == 0) {
            return true;
        }
    }
    return false;
}

bool isCurrentProcessElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation {};
    DWORD size = 0;
    const BOOL ok
        = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);

    return ok && elevation.TokenIsElevated != 0;
}

QString quoteWindowsCommandLineArgument(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString currentExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length
        = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return QCoreApplication::applicationFilePath();
    }

    buffer.resize(length);
    return QString::fromStdWString(buffer);
}

QString buildUnelevatedRelaunchCommandLine(int argc, char* argv[])
{
    QStringList parts;
    parts << quoteWindowsCommandLineArgument(currentExecutablePath());
    parts << QString::fromLatin1(kSkipElevationRestartArgument);

    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument.compare(QLatin1String(kSkipElevationRestartArgument), Qt::CaseInsensitive)
            == 0) {
            continue;
        }
        parts << quoteWindowsCommandLineArgument(argument);
    }

    return parts.join(QLatin1Char(' '));
}

bool relaunchUnelevatedFromShellToken(int argc, char* argv[])
{
    const HWND shellWindow = GetShellWindow();
    if (!shellWindow) {
        return false;
    }

    DWORD shellPid = 0;
    GetWindowThreadProcessId(shellWindow, &shellPid);
    if (shellPid == 0) {
        return false;
    }

    HANDLE shellProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shellPid);
    if (!shellProcess) {
        return false;
    }

    HANDLE shellToken = nullptr;
    if (!OpenProcessToken(shellProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &shellToken)) {
        CloseHandle(shellProcess);
        return false;
    }

    HANDLE primaryToken = nullptr;
    const BOOL duplicated = DuplicateTokenEx(shellToken,
        TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT
            | TOKEN_ADJUST_SESSIONID,
        nullptr, SecurityImpersonation, TokenPrimary, &primaryToken);
    CloseHandle(shellToken);
    CloseHandle(shellProcess);

    if (!duplicated) {
        return false;
    }

    QString commandLine = buildUnelevatedRelaunchCommandLine(argc, argv);
    std::wstring mutableCommandLine = commandLine.toStdWString();
    const QString workingDirectory = QFileInfo(currentExecutablePath()).absolutePath();
    const std::wstring workingDirectoryW
        = QDir::toNativeSeparators(workingDirectory).toStdWString();

    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo {};

    const BOOL created
        = CreateProcessWithTokenW(primaryToken, 0, nullptr, mutableCommandLine.data(), 0, nullptr,
            workingDirectoryW.c_str(), &startupInfo, &processInfo);
    CloseHandle(primaryToken);

    if (created) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }

    return created;
}
#endif

QString normalizedStartupFilePath(QString argument)
{
    argument = argument.trimmed();
    if (argument.isEmpty()) {
        return {};
    }
    if (argument.compare(QLatin1String(kFactoryResetArgument), Qt::CaseInsensitive) == 0
        || argument.compare(QLatin1String(kSkipElevationRestartArgument), Qt::CaseInsensitive)
            == 0) {
        return {};
    }

    const QUrl url(argument);
    if (url.isValid() && url.isLocalFile()) {
        argument = url.toLocalFile();
    }

    const QFileInfo fileInfo(argument);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return {};
    }

    const QString canonicalPath = fileInfo.canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        return canonicalPath;
    }

    return QDir::cleanPath(fileInfo.absoluteFilePath());
}

QStringList collectStartupFilePaths()
{
    QStringList result;
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    for (const QString& argument : arguments) {
        const QString filePath = normalizedStartupFilePath(argument);
        if (!filePath.isEmpty() && !result.contains(filePath, Qt::CaseInsensitive)) {
            result.append(filePath);
        }
    }
    return result;
}

void openFilePaths(const QStringList& filePaths)
{
    for (const QString& filePath : filePaths) {
        ruwa::core::CommandExecutor::instance().execute(
            QStringLiteral("file.open"), { { QStringLiteral("path"), filePath } });
    }
}

} // namespace

int main(int argc, char* argv[])
{

#ifdef Q_OS_WIN
    if (isCurrentProcessElevated()
        && !hasCommandLineArgument(argc, argv, kSkipElevationRestartArgument)
        && relaunchUnelevatedFromShellToken(argc, argv)) {
        return 0;
    }
#endif

    // 0. D I S A B L E   W I N D O W S   D P I   S C A L I N G
    // ----------------------------------------------------------------------
    // Force Qt to ignore the OS display scale factor (125%, 150%, etc.).
    // After this every API (widgets, OpenGL, input events) operates in
    // physical pixels and devicePixelRatio() returns 1.0.
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");

    // 1. M E T A D A T A
    // ----------------------------------------------------------------------
    QApplication::setApplicationName("Ruwa");
    QApplication::setApplicationVersion(QStringLiteral(RUWA_APPLICATION_VERSION));
    QApplication::setOrganizationName("Ruwa");
    QApplication::setOrganizationDomain("Ruwa.app");
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QStringList startupArguments;
    startupArguments.reserve(argc > 1 ? argc - 1 : 0);
    for (int i = 1; i < argc; ++i) {
        startupArguments.append(QString::fromLocal8Bit(argv[i]));
    }
    ruwa::Application::runFactoryResetIfRequested(startupArguments);

    // Tablet input backend (must be configured before creating QApplication instance):
    // 0 = WinTab (Qt), 1 = Windows Ink, 2 = WinTab (Ruwa)
    QSettings startupSettings(QApplication::organizationName(), QApplication::applicationName());
    const int tabletBackend = startupSettings.value("Performance/tabletBackend", 2).toInt();

    // Store the currently active backend so settings can compare against it
    ruwa::Application::setCurrentTabletBackend(tabletBackend);

    if (tabletBackend == 1 || tabletBackend == 2) {
        // Keep Qt's native tablet integration on Windows Ink. In "WinTab (Ruwa)"
        // mode the application filter consumes those QTabletEvents because the direct
        // WinTab backend is the canonical source for coordinates, buttons and pressure.
        qputenv("QT_TABLET_INTERFACE", "winink");
        qunsetenv("QT_WINTAB_ENABLED");
#ifdef Q_OS_WIN
        // Without this WM_POINTER events don't reach Qt window, pressure = 0
        EnableMouseInPointer(TRUE);
#endif
    } else {
        // Qt WinTab path
        qputenv("QT_TABLET_INTERFACE", "wintab");
        // Qt 5 legacy — kept for compatibility, harmless
        qputenv("QT_WINTAB_ENABLED", "1");
    }

    // In Ruwa WinTab mode we fully control the input pipeline and must prevent
    // Qt from synthesizing mouse events that would conflict with our synthetic
    // dispatch.  For the other backends, keep the default (true) so that
    // unhandled tablet events still produce fallback mouse events.
    if (tabletBackend == 2) {
        QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents, false);
    }
    // Must be set before QApplication construction or Qt may still compress
    // high-frequency input events when the UI thread is under load.
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);

    // 2. C R E A T E   A P P L I C A T I O N
    // ----------------------------------------------------------------------
    ruwa::Application app(argc, argv);
    QStringList pendingOpenFilePaths = collectStartupFilePaths();
    QStringList startupOpenFilePaths;
    QPointer<ruwa::ui::windows::MainWindow> startupMainWindow;
    app.setWindowIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::OpaqueLogoIcon));
    QObject::connect(&app, &ruwa::Application::fileOpenRequested, &app,
        [&pendingOpenFilePaths, &startupOpenFilePaths, &startupMainWindow](
            const QString& filePath) {
            const QString normalizedPath = normalizedStartupFilePath(filePath);
            if (normalizedPath.isEmpty()
                || pendingOpenFilePaths.contains(normalizedPath, Qt::CaseInsensitive)
                || (!startupMainWindow
                    && startupOpenFilePaths.contains(normalizedPath, Qt::CaseInsensitive))) {
                return;
            }

            if (startupMainWindow) {
                openFilePaths(QStringList { normalizedPath });
                return;
            }

            pendingOpenFilePaths.append(normalizedPath);
        });

    // Load effect plugins before the effects UI or the first GL renderer is
    // built, so plugin effects populate the same registries the built-ins use.
    // No-op until effect plugin DLLs are present (they arrive as standard
    // packages once ported).
    ruwa::core::effects::plugin::EffectPluginManager::instance().loadStandardAndUserPlugins();

    // 3. S T A R T U P   S E Q U E N C E
    // ----------------------------------------------------------------------
    ruwa::core::StartupController startup(&app);
    startupOpenFilePaths = pendingOpenFilePaths;
    pendingOpenFilePaths.clear();
    startup.setStartupOpenFilePaths(startupOpenFilePaths);
    QObject::connect(&startup, &ruwa::core::StartupController::startupComplete,
        [&pendingOpenFilePaths, &startupOpenFilePaths, &startupMainWindow](
            ruwa::ui::windows::MainWindow* mainWindow) {
            startupMainWindow = mainWindow;
            if (mainWindow) {
                ruwa::services::UpdateManager::acknowledgeSuccessfulUpdateStartup();
            }
            if (mainWindow && !startupOpenFilePaths.isEmpty()) {
                const QStringList filePaths = startupOpenFilePaths;
                startupOpenFilePaths.clear();
                QTimer::singleShot(0, mainWindow, [filePaths]() { openFilePaths(filePaths); });
            }
            if (mainWindow && !pendingOpenFilePaths.isEmpty()) {
                const QStringList filePaths = pendingOpenFilePaths;
                pendingOpenFilePaths.clear();
                QTimer::singleShot(0, mainWindow, [filePaths]() { openFilePaths(filePaths); });
            }
            if (mainWindow) {
                QTimer::singleShot(50, mainWindow,
                    [mainWindow]() { mainWindow->showFirstLaunchUpdateMessageIfNeeded(); });
            }
        });
    startup.start();

    // 4. E X E C   L O O P
    // ----------------------------------------------------------------------
    return app.exec();
}
