// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   F I L E   C O M M A N D S
// ======================================================================================

#include "commands/definitions/FileCommands.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandContext.h"
#include "commands/CommandExecutor.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "features/project/ProjectSerializer.h"
#include "features/project/ProjectData.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "shell/main-window/MainWindow.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shared/utils/FileDialogMemory.h"

#include <QFileDialog>
#include <QTimer>
#include <QSize>
#include <QApplication>
#include <QCoreApplication>
#include <QMetaObject>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QImageReader>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

namespace ruwa::core::commands {

namespace {
constexpr int kDefaultQuickProjectWidth = 4000;
constexpr int kDefaultQuickProjectHeight = 3000;
} // namespace

// ======================================================================================
//   N E W   P R O J E C T
// ======================================================================================

CommandInfo NewProjectCommand::info() const
{
    return CommandInfo { .id = "file.new",
        .title = "New Project",
        .category = "File",
        .description = "Create a new project",
        .aliases = { "new", "create", "fnp" },
        .defaultShortcut = QKeySequence::New,
        .icon = QIcon() };
}

void NewProjectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    // Show new project dialog for user to configure settings
    if (auto* mainWindow = ctx.mainWindow()) {
        QMetaObject::invokeMethod(mainWindow, "showNewProjectDialog", Qt::DirectConnection);
    }
}

// ======================================================================================
//   O P E N   P R O J E C T
// ======================================================================================

CommandInfo OpenProjectCommand::info() const
{
    return CommandInfo { .id = "file.open",
        .title = "Open Project",
        .category = "File",
        .description = "Open an existing project (from recent or file dialog)",
        .aliases = { "open", "load", "fop", "op" },
        .defaultShortcut = QKeySequence::Open,
        .icon = QIcon(),
        .arguments = { ruwa::core::CommandArgument { .name = "path",
            .hint = QCoreApplication::translate("FileCommands", "Project name"),
            .placeholder = QCoreApplication::translate("FileCommands", "Browse..."),
            .useRecentProjects = true } } };
}

namespace {

QString buildOpenFileFilter()
{
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    QStringList imageExtensions;
    for (const QByteArray& fmt : formats) {
        const QString ext = QStringLiteral("*.") + QString::fromLatin1(fmt);
        if (!imageExtensions.contains(ext, Qt::CaseInsensitive)) {
            imageExtensions.append(ext);
        }
    }
    const QString imageFilter = imageExtensions.isEmpty()
        ? QStringLiteral("*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tiff *.tga")
        : imageExtensions.join(QChar(' '));
    // Combined filter first so project files and images are visible by default
    const QString allSupported = QStringLiteral("*.rwf *.uwa ") + imageFilter;
    const QString trAllSupported
        = QCoreApplication::translate("FileCommands", "All supported files (%1)");
    const QString trProjects
        = QCoreApplication::translate("FileCommands", "Ruwa Projects (*.rwf *.uwa)");
    const QString trImages = QCoreApplication::translate("FileCommands", "Images (%1)");
    const QString trAllFiles = QCoreApplication::translate("FileCommands", "All Files (*)");
    return trAllSupported.arg(allSupported) + QStringLiteral(";;") + trProjects
        + QStringLiteral(";;") + trImages.arg(imageFilter) + QStringLiteral(";;") + trAllFiles;
}

QString ensureProjectSaveExtension(QString filePath)
{
    if (filePath.isEmpty()) {
        return filePath;
    }
    if (filePath.endsWith(QStringLiteral(".rwf"), Qt::CaseInsensitive)) {
        return filePath;
    }
    if (filePath.endsWith(QStringLiteral(".uwa"), Qt::CaseInsensitive)) {
        filePath.chop(4);
    }
    filePath += QStringLiteral(".rwf");
    return filePath;
}

QSize projectSizeFromImage(const QString& filePath)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (!image.isNull() && image.width() > 0 && image.height() > 0) {
        return image.size();
    }
    return QSize(1920, 1080);
}

QString projectNameFromImagePath(const QString& filePath)
{
    const QString name = QFileInfo(filePath).completeBaseName().trimmed();
    return name.isEmpty() ? QCoreApplication::translate("FileCommands", "Untitled Project") : name;
}

bool isImageFile(const QString& filePath)
{
    return QImageReader(filePath).canRead();
}

QString normalizedOpenProjectPath(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return canonical;
    }
    return QDir::cleanPath(fi.absoluteFilePath());
}

ruwa::ui::tabs::WorkspaceTab* findWorkspaceTabForProjectFile(
    ruwa::core::TabManager* tabManager, const QString& filePath)
{
    if (!tabManager) {
        return nullptr;
    }
    const QString target = normalizedOpenProjectPath(filePath);
    if (target.isEmpty()) {
        return nullptr;
    }
    for (ruwa::core::BaseTab* tab : tabManager->tabs()) {
        auto* ws = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (!ws || !ws->hasFilePath()) {
            continue;
        }
        const QString other = normalizedOpenProjectPath(ws->filePath());
        if (!other.isEmpty() && other == target) {
            return ws;
        }
    }
    return nullptr;
}

struct LoadProjectResult {
    bool success = false;
    QString filePath;
    QString errorMessage;
    ruwa::core::serialization::ProjectData data;
};

LoadProjectResult loadProjectInBackground(const QString& filePath)
{
    LoadProjectResult result;
    result.filePath = filePath;
    ruwa::core::serialization::ProjectSerializer serializer;
    if (!serializer.load(filePath, result.data)) {
        result.errorMessage = serializer.lastError();
        return result;
    }
    result.success = true;
    return result;
}

} // anonymous namespace

void OpenProjectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    QString filePath = args.value("path").toString().trimmed();

    if (filePath.isEmpty()) {
        filePath = ruwa::shared::filedialog::getOpenFileName(ctx.mainWindow(),
            ruwa::shared::filedialog::category::kProject,
            QCoreApplication::translate("FileCommands", "Open Project"), buildOpenFileFilter());
    }

    if (filePath.isEmpty()) {
        return;
    }

    auto* tabManager = ctx.tabManager();
    auto* mainWindow = ctx.mainWindow();
    if (!tabManager || !mainWindow) {
        return;
    }

    // Image file: same behavior as drag & drop — create new project and import image
    if (isImageFile(filePath)) {
        const QSize projectSize = projectSizeFromImage(filePath);
        ruwa::ui::tabs::WorkspaceTab::ProjectSettings settings;
        settings.name = projectNameFromImagePath(filePath);
        settings.canvasSize = projectSize;
        settings.canvasBoundsMode = ruwa::core::canvas::CanvasBoundsMode::Bounded;
        settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
            QRect(0, 0, projectSize.width(), projectSize.height()) };
        settings.templateType = QCoreApplication::translate("FileCommands", "RGB Color");

        auto* workspaceTab = new ruwa::ui::tabs::WorkspaceTab(settings);
        workspaceTab->seedStartupImageImportPaths(QStringList { filePath });
        tabManager->addTab(workspaceTab);
        return;
    }

    if (auto* existingTab = findWorkspaceTabForProjectFile(tabManager, filePath)) {
        tabManager->activateTab(existingTab);
        return;
    }

    // Project file (.rwf or legacy .uwa): load in background
    auto* loadingTab = ruwa::ui::tabs::WorkspaceTab::createLoadingPlaceholder(filePath, mainWindow);
    if (!loadingTab || !tabManager->addTab(loadingTab)) {
        delete loadingTab;
        return;
    }
    QPointer<ruwa::ui::tabs::WorkspaceTab> loadingTabGuard(loadingTab);

    auto* watcher = new QFutureWatcher<LoadProjectResult>(mainWindow);
    QObject::connect(watcher, &QFutureWatcher<LoadProjectResult>::finished, mainWindow,
        [watcher, tabManager, loadingTabGuard, mainWindow]() {
            auto result = watcher->result();
            watcher->deleteLater();

            if (!result.success) {
                if (loadingTabGuard) {
                    tabManager->requestCloseTab(loadingTabGuard);
                }
                if (mainWindow) {
                    const QString message
                        = QCoreApplication::translate("FileCommands", "Failed to open project:\n%1")
                              .arg(result.errorMessage);
                    ruwa::ui::widgets::MessagePopupManager::show(mainWindow, message,
                        { { QCoreApplication::translate("FileCommands", "OK"), false, []() { } } },
                        380);
                }
                return;
            }

            if (loadingTabGuard) {
                loadingTabGuard->acceptLoadedProjectData(std::move(result.data), result.filePath);
            }
        });

    watcher->setFuture(QtConcurrent::run(loadProjectInBackground, filePath));
}

// ======================================================================================
//   S A V E   P R O J E C T
// ======================================================================================

CommandInfo SaveProjectCommand::info() const
{
    return CommandInfo { .id = "file.save",
        .title = "Save",
        .category = "File",
        .description = "Save the current project",
        .aliases = { "save", "sv" },
        .defaultShortcut = QKeySequence::Save,
        .icon = QIcon() };
}

bool SaveProjectCommand::canExecute(const CommandContext& ctx) const
{
    // Can only save if there's an active workspace tab
    return ctx.activeTab() != nullptr;
}

void SaveProjectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (!canExecute(ctx)) {
        return;
    }

    auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(ctx.activeTab());
    if (!wsTab) {
        return;
    }

    if (wsTab->hasFilePath()) {
        if (!wsTab->saveProjectAsync()) { }
    } else {
        // No path yet — delegate to Save As
        auto& registry = CommandRegistry::instance();
        if (auto* saveAsCmd = registry.command("file.saveAs")) {
            saveAsCmd->execute(ctx);
        }
    }
}

// ======================================================================================
//   S A V E   P R O J E C T   A S
// ======================================================================================

CommandInfo SaveProjectAsCommand::info() const
{
    return CommandInfo { .id = "file.saveAs",
        .title = "Save As...",
        .category = "File",
        .description = "Save the current project to a new file",
        .aliases = { "saveas", "fsa" },
        .defaultShortcut = QKeySequence::SaveAs,
        .icon = QIcon() };
}

bool SaveProjectAsCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeTab() != nullptr;
}

void SaveProjectAsCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (!canExecute(ctx)) {
        return;
    }

    auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(ctx.activeTab());
    if (!wsTab) {
        return;
    }

    // Suggest a filename based on the current path or project name
    QString suggestedPath = wsTab->filePath();
    if (suggestedPath.isEmpty()) {
        suggestedPath
            = ruwa::core::serialization::ProjectSerializer::defaultFileName(wsTab->baseTitle());
    }

    QString filePath = ruwa::shared::filedialog::getSaveFileName(ctx.mainWindow(),
        ruwa::shared::filedialog::category::kProject, "Save Project As", suggestedPath,
        "Ruwa Projects (*.rwf);;All Files (*)");

    if (filePath.isEmpty()) {
        return;
    }

    filePath = ensureProjectSaveExtension(filePath);

    if (!wsTab->saveProjectAsAsync(filePath)) { }
}

// ======================================================================================
//   E X P O R T   P R O J E C T
// ======================================================================================

CommandInfo ExportProjectCommand::info() const
{
    return CommandInfo { .id = "file.export",
        .title = "Export",
        .category = "File",
        .description = "Toggle export mode for the current project",
        .aliases = { "export", "save-for-web" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S),
        .icon = QIcon() };
}

bool ExportProjectCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ExportProjectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab) {
        return;
    }

    wsTab->toggleExportMode();
}

// ======================================================================================
//   F A S T   E X P O R T   A S   P N G
// ======================================================================================

CommandInfo FastExportPngCommand::info() const
{
    return CommandInfo { .id = "file.fastExportPng",
        .title = "Fast Export as PNG",
        .category = "File",
        .description = "Export the canvas straight to a PNG file",
        .aliases = { "export png", "quick export", "fast export", "png" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_P),
        .icon = QIcon() };
}

bool FastExportPngCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void FastExportPngCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab) {
        return;
    }

    wsTab->fastExportPng();
}

// ======================================================================================
//   Q U I C K   N E W   P R O J E C T
// ======================================================================================

CommandInfo QuickNewProjectCommand::info() const
{
    using ruwa::core::CommandArgument;
    using ruwa::core::CommandSuggestionPreset;

    auto tr = [](const char* text) { return QCoreApplication::translate("FileCommands", text); };

    return CommandInfo { .id = "file.quickNewProject",
        .title = "Quick New Project",
        .category = "File",
        .description = "Create a new project (preset or 1920×1080 default)",
        .aliases = { "quicknew", "qnp" },
        // Ctrl+Shift+N reassigned to "New Layer" for Photoshop compatibility
        .defaultShortcut = QKeySequence(),
        .icon = QIcon(),
        .arguments = { CommandArgument { .name = "preset",
            .hint = tr("Preset"),
            .placeholder = tr("Landscape Canvas"),
            .suggestionPresets = {
                { tr("Landscape Canvas"), { { "width", 4000 }, { "height", 3000 } } },
                { tr("Portrait Canvas"), { { "width", 3000 }, { "height", 4000 } } },
                { tr("Square Canvas"), { { "width", 3000 }, { "height", 3000 } } },
                { tr("4K UHD"), { { "width", 3840 }, { "height", 2160 } } },
                { tr("Ultrawide"), { { "width", 3440 }, { "height", 1440 } } },
                { tr("Illustration Portrait"), { { "width", 4000 }, { "height", 5000 } } },
                { tr("US Comic Page"), { { "width", 2550 }, { "height", 3900 } } },
                { tr("Webtoon Episode"), { { "width", 1600 }, { "height", 12000 } } },
                { tr("A4"), { { "width", 2480 }, { "height", 3508 } } },
                { tr("Book Cover"), { { "width", 3000 }, { "height", 4500 } } },
                { tr("Pixel Scene"), { { "width", 512 }, { "height", 512 } } },
            } } } };
}

void QuickNewProjectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    int width = args.value("width", kDefaultQuickProjectWidth).toInt();
    int height = args.value("height", kDefaultQuickProjectHeight).toInt();
    QString name = args.value("name").toString().trimmed();

    width = qBound(1, width, 100000);
    height = qBound(1, height, 100000);

    if (name.isEmpty()) {
        name = QCoreApplication::translate("FileCommands", "Untitled Project");
    }

    auto* tabManager = ctx.tabManager();
    if (!tabManager) {
        return;
    }

    ruwa::ui::tabs::WorkspaceTab::ProjectSettings settings;
    settings.name = name;
    settings.canvasSize = QSize(width, height);
    settings.canvasBoundsMode = args.value("infiniteCanvasEnabled", false).toBool()
        ? ruwa::core::canvas::CanvasBoundsMode::Infinite
        : ruwa::core::canvas::CanvasBoundsMode::Bounded;
    settings.exportFrame
        = ruwa::core::serialization::ProjectData::ExportFrame { true, QRect(0, 0, width, height) };
    settings.templateType = QCoreApplication::translate("FileCommands", "RGB Color");

    tabManager->addTab(new ruwa::ui::tabs::WorkspaceTab(settings));
}

// ======================================================================================
//   E X I T   A P P L I C A T I O N
// ======================================================================================

CommandInfo ExitCommand::info() const
{
    return CommandInfo { .id = "file.exit",
        .title = "Exit",
        .category = "File",
        .description = "Exit the application",
        .aliases = { "quit", "exit", "close app" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_Q), // Photoshop: Quit
        .icon = QIcon() };
}

void ExitCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* mainWindow = ctx.mainWindow()) {
        mainWindow->close();
        if (mainWindow->isVisible()) {
            // Close was cancelled (e.g. user rejected unsaved-changes prompt).
            return;
        }
    }

    // Ensure the process exits even if hidden helper widgets remain alive.
    QApplication::quit();
}

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerFileCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<NewProjectCommand>());
    registry.registerCommand(std::make_unique<OpenProjectCommand>());
    registry.registerCommand(std::make_unique<SaveProjectCommand>());
    registry.registerCommand(std::make_unique<SaveProjectAsCommand>());
    registry.registerCommand(std::make_unique<ExportProjectCommand>());
    registry.registerCommand(std::make_unique<FastExportPngCommand>());
    registry.registerCommand(std::make_unique<QuickNewProjectCommand>());
    registry.registerCommand(std::make_unique<ExitCommand>());
}

} // namespace ruwa::core::commands
