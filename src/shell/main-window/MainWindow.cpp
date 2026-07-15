// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   MAIN WINDOW - REFACTORED IMPLEMENTATION (FIXED)
// ======================================================================================

#include "MainWindow.h"
#include "app/Application.h"
#include "shell/main-window/WindowSetupCoordinator.h"
#include "shell/main-window/TabSystemCoordinator.h"
#include "shell/main-window/OverlayCoordinator.h"
#include "shell/main-window/CommandCoordinator.h"
#include "shell/main-window/ContextMenuCoordinator.h"

#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "services/discord/DiscordService.h"
#include "shell/top-bar/UnsavedChangesHelper.h"
#include "commands/CommandExecutor.h"
#include "features/home/HomePageTab.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "shell/tab-system/CustomTabBar.h"
#include "shell/top-bar/TopBar.h"
#include "shell/docking/state/DockLayoutPreset.h"
#include "shell/docking/state/DockLayoutPresetStore.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shared/utils/FileDialogMemory.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/layout/AnimatedTabWidget.h"
#include "shell/context-menu/ContextMenuSystem.h"
#include "platform/windows/WindowsInkFeedback.h"
#include "services/input/StylusDebugService.h"
#include "services/input/StylusInputManager.h"
#include "services/updates/UpdateManager.h"
#include "features/settings/SettingsManager.h"
#include "features/canvas/ui/CanvasPanelHelpers.h"
#include <QCoreApplication>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QFileInfo>
#include <QSet>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QSettings>
#include <QShowEvent>
#include <QShortcut>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QtConcurrent>

namespace ruwa::ui::windows {

namespace {

constexpr int kFirstLaunchUpdateAfterOnboardingMs = 1000;
constexpr qint64 kMaxDroppedBrowserImageBytes = 50 * 1024 * 1024;

void persistLastSeenUpdateVersion(
    const QString& organization, const QString& application, const QString& version)
{
    if (organization.isEmpty() || application.isEmpty() || version.isEmpty()) {
        return;
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organization, application);
    settings.setValue(QStringLiteral("Updates/lastSeenVersion"), version);
    settings.sync();
}

/// Hosts TopBar at (0,0) full width; TopBar draws the outer gutter in paint (real hit targets to
/// window edges).
class TopBarClipWidget : public QWidget {
public:
    explicit TopBarClipWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setAutoFillBackground(false);
        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, [this]() { relayout(); });
    }

    void setTrackedChild(QWidget* child)
    {
        if (m_child) {
            m_child->removeEventFilter(this);
        }

        m_child = child;
        if (m_child) {
            m_child->setParent(this);
            m_child->show();
            m_child->installEventFilter(this);
        }
        relayout();
    }

    void relayout()
    {
        if (!m_child) {
            return;
        }

        const int w = width();
        if (w > 0) {
            m_child->resize(w, m_child->height());
        }
        m_child->move(0, 0);

        // Guard against the child's not-yet-laid-out geometry: right after
        // construction/show() height() can still be the default -1, and
        // setFixedHeight(-1) triggers Qt's "Negative sizes" warnings. The
        // child's later Resize/LayoutRequest re-fires relayout() with a real
        // height via the event filter.
        const int childHeight = m_child->height();
        if (childHeight > 0) {
            setFixedHeight(childHeight);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_child) {
            switch (event->type()) {
            case QEvent::Resize:
            case QEvent::LayoutRequest:
            case QEvent::Show:
            case QEvent::StyleChange:
                relayout();
                break;
            default:
                break;
            }
        }

        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), ruwa::ui::core::ThemeManager::instance().colors().background);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        relayout();
    }

    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        relayout();
    }

private:
    QWidget* m_child = nullptr;
};

QStringList extractImportableImagePaths(const QMimeData* mimeData)
{
    return ruwa::ui::workspace::detail::extractImportableImagePathsFromMime(mimeData);
}

bool isProjectFilePath(const QString& filePath)
{
    return filePath.endsWith(QStringLiteral(".rwf"), Qt::CaseInsensitive)
        || filePath.endsWith(QStringLiteral(".uwa"), Qt::CaseInsensitive);
}

QStringList extractProjectFilePaths(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasUrls()) {
        return {};
    }

    QStringList result;
    const QList<QUrl> urls = mimeData->urls();
    result.reserve(urls.size());
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString localPath = url.toLocalFile();
        QFileInfo fileInfo(localPath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }

        const QString absoluteFilePath = fileInfo.absoluteFilePath();
        if (!isProjectFilePath(absoluteFilePath)) {
            continue;
        }

        result.append(absoluteFilePath);
    }
    return result;
}

bool mayContainProjectFilePaths(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasUrls()) {
        return false;
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString filePath = url.toLocalFile();
        if (isProjectFilePath(filePath)) {
            return true;
        }
    }

    return false;
}

bool mayContainImportableImagePaths(const QMimeData* mimeData)
{
    return ruwa::ui::workspace::detail::mayContainImportableImageFromMime(mimeData);
}

QSize resolveProjectSizeFromImages(const QStringList& filePaths)
{
    for (const QString& filePath : filePaths) {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        QSize imageSize = reader.size();
        if (!imageSize.isValid()) {
            continue;
        }

        if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) {
            imageSize.transpose();
        }

        if (imageSize.width() > 0 && imageSize.height() > 0) {
            return imageSize;
        }
    }
    return QSize(1920, 1080);
}

QString projectNameFromImagePaths(const QStringList& filePaths)
{
    if (filePaths.size() != 1) {
        return QCoreApplication::translate("FileCommands", "Untitled Project");
    }

    const QString name = QFileInfo(filePaths.first()).completeBaseName().trimmed();
    return name.isEmpty() ? QCoreApplication::translate("FileCommands", "Untitled Project") : name;
}

QString projectNameFromImageLayerName(const QString& layerName)
{
    const QString name = layerName.trimmed();
    return name.isEmpty() ? QCoreApplication::translate("FileCommands", "Untitled Project") : name;
}

bool useRuwaWinTabBackend()
{
    QSettings settings(QApplication::organizationName(), QApplication::applicationName());
    return settings.value("Performance/tabletBackend", 2).toInt() == 2;
}

// WinTab packet capture as a *pure data source*, independent of native UI
// routing.
//
// NOTE: capture is NOT enabled in "WinTab (Qt)" mode (0). Opening our own WinTab
// context there collides with the WinTab context Qt itself owns in that mode —
// on real hardware our context starves Qt's, so the pen still feeds our pressure
// readout but Qt stops receiving QTabletEvents (cursor + drawing dead). Two
// WinTab contexts in one process do not coexist reliably. The viable way to get
// WinTab pressure + sample recovery is to have Qt on Windows Ink (winink) for
// position/UI while our WinTab context supplies pressure — exactly the mode-2
// pressure path, which DOES coexist (Ink and WinTab are separate subsystems).
bool useWinTabCapture()
{
    QSettings settings(QApplication::organizationName(), QApplication::applicationName());
    return settings.value("Performance/tabletBackend", 2).toInt() == 2;
}

} // namespace

MainWindow::MainWindow(QWidget* parent, const QStringList& startupOpenFilePaths)
    : QMainWindow(parent)
    , m_theme(this)
{
    // Create native window early
    setAttribute(Qt::WA_NativeWindow);
    winId();
#if defined(Q_OS_WIN)
    configureWindowsInkFeedback();
    attachStylusDebugBackend();
#endif

    // Setup UI
    setupUI();

    // Initialize coordinators
    m_setupCoordinator = new WindowSetupCoordinator(this);
    m_setupCoordinator->setupWindowAgent(this, m_topBar, m_tabBar);
    m_setupCoordinator->setupOpenGLWarmup(this);

    m_tabCoordinator = new TabSystemCoordinator(this);
    m_tabCoordinator->initialize(
        new ruwa::core::TabManager(this), m_tabBar, m_tabContent, startupOpenFilePaths.isEmpty());

    m_overlayCoordinator = new OverlayCoordinator(this);
    m_overlayCoordinator->initialize(m_tabContent, m_centralWidget, m_topBar);

    m_commandCoordinator = new CommandCoordinator(this);
    m_commandCoordinator->initialize(m_tabCoordinator->tabManager(), this);

    m_contextMenuCoordinator = new ContextMenuCoordinator(this);

    // Install context menu system
    widgets::ContextMenuSystem::instance().installOn(m_centralWidget);

    // Connect signals
    connectSignals();

    // Restore window state
    m_setupCoordinator->restoreWindowState(this);
}

MainWindow::~MainWindow()
{
    if (m_firstLaunchUpdateDismissSyncFuture.isRunning()) {
        m_firstLaunchUpdateDismissSyncFuture.waitForFinished();
    }
    if (!ruwa::Application::isFactoryResetRestartInProgress()) {
        m_setupCoordinator->saveWindowState(this);
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
#if defined(Q_OS_WIN)
    configureWindowsInkFeedback();
    attachStylusDebugBackend();
#endif
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    if (useWinTabCapture()) {
        // Feed the direct backend with full-resolution WinTab packets. In Ruwa mode
        // StylusInputManager routes this stream and the application filter suppresses
        // the parallel Windows Ink QTabletEvents.
        ruwa::services::input::StylusDebugService::instance()->handleNativeEvent(message);
    }
    if (useRuwaWinTabBackend()) {
        // Native synthetic-mouse UI routing — only in explicit Ruwa mode.
        ruwa::services::input::StylusInputManager::instance().handleNativeEvent(message);
    }
    if (aether::platform::handleWindowsInkNativeEvent(message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::configureWindowsInkFeedback()
{
    aether::platform::configureWindowsInkFeedback(reinterpret_cast<void*>(winId()));
}

void MainWindow::attachStylusDebugBackend()
{
    auto* stylusDebugService = ruwa::services::input::StylusDebugService::instance();
    if (useWinTabCapture()) {
        stylusDebugService->attachToWindow(reinterpret_cast<void*>(winId()));
        return;
    }

    stylusDebugService->detachFromWindow();
}
#endif

void MainWindow::setupUI()
{
    setWindowTitle("Ruwa");
    setMinimumSize(800, 600);

    // Central widget with layout
    m_centralWidget = new QWidget(this);
    auto* layout = new QVBoxLayout(m_centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create TopBar
    createTopBar();
    layout->addWidget(m_topBarClip);

    // Tab content
    m_tabContent = new widgets::AnimatedTabWidget(m_centralWidget);
    m_tabContent->setAnimationDuration(350);
    m_tabContent->setAnimationEasing(QEasingCurve::InOutCubic);
    layout->addWidget(m_tabContent);

    setCentralWidget(m_centralWidget);
    setAcceptDrops(true);
    menuBar()->hide();

    // Enable tablet/stylus tracking for all UI elements (buttons, sliders, etc.)
    setTabletTracking(true);
    m_centralWidget->setTabletTracking(true);
    m_topBar->setTabletTracking(true);
    m_tabContent->setTabletTracking(true);
}

void MainWindow::createTopBar()
{
    m_topBarClip = new TopBarClipWidget(m_centralWidget);
    m_topBarClip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_topBar = new widgets::TopBar(m_topBarClip);
    static_cast<TopBarClipWidget*>(m_topBarClip)->setTrackedChild(m_topBar);
    static_cast<TopBarClipWidget*>(m_topBarClip)->relayout();

    // Create tab bar and add to TopBar's tab container
    m_tabBar = new tabs::CustomTabBar(m_topBar->tabBarContainer());
    if (auto* layout = qobject_cast<QHBoxLayout*>(m_topBar->tabBarContainer()->layout())) {
        layout->addWidget(m_tabBar);
    } else {
        auto* tabLayout = new QHBoxLayout(m_topBar->tabBarContainer());
        tabLayout->setContentsMargins(0, 0, 0, 0);
        tabLayout->setSpacing(0);
        tabLayout->addWidget(m_tabBar);
    }

    applyTopBarTabAlignment();
}

void MainWindow::applyTopBarTabAlignment()
{
    if (!m_tabBar || !m_topBar) {
        return;
    }
    QWidget* container = m_topBar->tabBarContainer();
    if (!container) {
        return;
    }
    auto* lay = qobject_cast<QHBoxLayout*>(container->layout());
    if (!lay) {
        return;
    }
    // Horizontal placement is animated inside CustomTabBar (strip uses full container width).
    lay->setAlignment(m_tabBar, Qt::AlignVCenter);
    lay->invalidate();
}

void MainWindow::connectSignals()
{
    // TopBar system controls/navigation
    connect(m_topBar, &widgets::TopBar::homeRequested, this,
        [this]() { m_tabCoordinator->navigateToHomeTab(); });
    connect(m_topBar, &widgets::TopBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_topBar, &widgets::TopBar::maximizeRequested, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    connect(m_topBar, &widgets::TopBar::closeRequested, this,
        []() { ruwa::core::CommandExecutor::instance().execute("file.exit"); });
    connect(m_topBar, &widgets::TopBar::dockLayoutPresetChosen, this,
        [this](const ruwa::ui::docking::DockLayoutPreset& preset) {
            if (auto* ws = activeWorkspaceTab()) {
                ws->applyLayoutPreset(preset);
            }
        });
    connect(m_topBar, &widgets::TopBar::dockLayoutNewPresetFromCurrentRequested, this, [this]() {
        QString err;
        if (auto* ws = activeWorkspaceTab()) {
            if (ws->captureCurrentLayoutAsPreset(&err)) {
                return;
            }
        } else {
            err = tr("Open a project tab to save a layout.");
        }
        ruwa::ui::widgets::MessagePopupManager::show(this,
            err.isEmpty() ? tr("Could not save layout.") : err, { { tr("OK"), true, []() { } } },
            360);
    });
    connect(m_topBar, &widgets::TopBar::dockLayoutExportRequested, this, [this]() {
        QString err;
        if (auto* ws = activeWorkspaceTab()) {
            ruwa::ui::docking::DockLayoutPreset preset;
            if (ws->createCurrentLayoutPreset(&preset, &err)) {
                const QString suggestedName = preset.name.trimmed().isEmpty()
                    ? QStringLiteral("layout")
                    : preset.name.trimmed();
                const QString path = ruwa::shared::filedialog::getSaveFileName(this,
                    ruwa::shared::filedialog::category::kLayout, tr("Export Layout"),
                    QStringLiteral("%1.json").arg(suggestedName), tr("Layout JSON (*.json)"));
                if (path.isEmpty()) {
                    return;
                }

                QFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    err = tr("Could not write layout file.");
                } else {
                    const qint64 written = file.write(
                        QJsonDocument(preset.toJson()).toJson(QJsonDocument::Indented));
                    file.close();
                    if (written < 0) {
                        err = tr("Could not write layout file.");
                    } else {
                        return;
                    }
                }
            }
        } else {
            err = tr("Open a project tab to export a layout.");
        }

        ruwa::ui::widgets::MessagePopupManager::show(this,
            err.isEmpty() ? tr("Could not export layout.") : err, { { tr("OK"), true, []() { } } },
            360);
    });
    connect(m_topBar, &widgets::TopBar::dockLayoutImportRequested, this, [this]() {
        QString err;
        if (auto* ws = activeWorkspaceTab()) {
            const QString path = ruwa::shared::filedialog::getOpenFileName(this,
                ruwa::shared::filedialog::category::kLayout, tr("Import Layout"),
                tr("Layout JSON (*.json)"));
            if (path.isEmpty()) {
                return;
            }

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                err = tr("Could not open layout file.");
            } else {
                const QByteArray raw = file.readAll();
                file.close();

                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    err = tr("Layout file is not valid JSON.");
                } else {
                    ruwa::ui::docking::DockLayoutPreset preset
                        = ruwa::ui::docking::DockLayoutPreset::fromJson(doc.object());
                    QString importedName = preset.name.trimmed();
                    if (importedName.isEmpty()) {
                        importedName = QFileInfo(path).completeBaseName().trimmed();
                    }
                    if (importedName.isEmpty()) {
                        importedName = tr("Imported layout");
                    }

                    auto& store = ruwa::ui::docking::DockLayoutPresetStore::instance();
                    preset.id = QUuid::createUuid();
                    preset.name = store.suggestUniqueName(importedName);
                    preset.isBuiltIn = false;

                    if (preset.dockState.isEmpty()
                        && (preset.layoutTree.isEmpty()
                            || !preset.layoutTree.value(QStringLiteral("hasRoot")).toBool(false))
                        && preset.placements.isEmpty()) {
                        err = tr("Layout file does not contain a usable layout.");
                    } else {
                        store.addCustomPreset(preset);
                        ws->applyLayoutPreset(preset);
                        return;
                    }
                }
            }
        } else {
            err = tr("Open a project tab to import a layout.");
        }

        ruwa::ui::widgets::MessagePopupManager::show(this,
            err.isEmpty() ? tr("Could not import layout.") : err, { { tr("OK"), true, []() { } } },
            360);
    });

    // Tab changes
    connect(m_tabCoordinator, &TabSystemCoordinator::activeTabChanged, this,
        &MainWindow::onActiveTabChanged);
    // Sync initial Discord state (tab was set before we connected)
    if (auto* tm = m_tabCoordinator->tabManager()) {
        ruwa::services::DiscordService::instance()->onActiveTabChanged(tm->activeTab());
    }
    connect(
        m_tabCoordinator, &TabSystemCoordinator::workspacePanelVisibilityChanged, this, [this]() {
            if (auto* ws = activeWorkspaceTab()) {
                m_topBar->setPanelsVisibilityState(ws->isToolsPanelVisible(),
                    ws->isBrushesPanelVisible(), ws->isLayersPanelVisible(),
                    ws->isLayerPropertiesPanelVisible(), ws->isLayerEffectsPanelVisible(),
                    ws->isColorPanelVisible(), ws->isComposerPanelVisible());
                m_topBar->setCanvasWidgetsVisibilityState(ws->isJoystickVisible(),
                    ws->isBrushControlVisible(), ws->isToolStateOverlayVisible());
            }
        });

    // Color picker requests from tabs (e.g., ThemeEditorTab)
    connect(m_tabCoordinator, &TabSystemCoordinator::colorPickerRequested, this,
        [this](const QColor& color, QWidget* button) {
            m_overlayCoordinator->showColorPicker(color, button);
        });

    // Menu actions (most items call CommandExecutor directly in TopBar; only custom handlers here)
    connect(m_topBar, &widgets::TopBar::fileImportImagesRequested, this,
        &MainWindow::handleImportImagesRequest);
    connect(m_topBar, &widgets::TopBar::helpAboutRequested, this, &MainWindow::onHelpAbout);

    // Panels visibility (View → Panels) - forward to active WorkspaceTab
    connect(m_topBar, &widgets::TopBar::panelsToolsVisibilityChanged, this, [this](bool visible) {
        if (auto* ws = activeWorkspaceTab())
            ws->setToolsPanelVisible(visible);
    });
    connect(m_topBar, &widgets::TopBar::panelsBrushesVisibilityChanged, this, [this](bool visible) {
        if (auto* ws = activeWorkspaceTab())
            ws->setBrushesPanelVisible(visible);
    });
    connect(m_topBar, &widgets::TopBar::panelsLayersVisibilityChanged, this, [this](bool visible) {
        if (auto* ws = activeWorkspaceTab())
            ws->setLayersPanelVisible(visible);
    });
    connect(m_topBar, &widgets::TopBar::panelsLayerPropertiesVisibilityChanged, this,
        [this](bool visible) {
            if (auto* ws = activeWorkspaceTab())
                ws->setLayerPropertiesPanelVisible(visible);
        });
    connect(m_topBar, &widgets::TopBar::panelsLayerEffectsVisibilityChanged, this,
        [this](bool visible) {
            if (auto* ws = activeWorkspaceTab())
                ws->setLayerEffectsPanelVisible(visible);
        });
    connect(m_topBar, &widgets::TopBar::panelsColorVisibilityChanged, this, [this](bool visible) {
        if (auto* ws = activeWorkspaceTab())
            ws->setColorPanelVisible(visible);
    });
    connect(
        m_topBar, &widgets::TopBar::panelsComposerVisibilityChanged, this, [this](bool visible) {
            if (auto* ws = activeWorkspaceTab())
                ws->setComposerPanelVisible(visible);
        });
    // Canvas widgets visibility (View → Canvas widgets submenu)
    connect(m_topBar, &widgets::TopBar::canvasWidgetsJoystickVisibilityChanged, this,
        [this](bool visible) {
            if (auto* ws = activeWorkspaceTab())
                ws->setJoystickVisible(visible);
        });
    connect(m_topBar, &widgets::TopBar::canvasWidgetsBrushControlVisibilityChanged, this,
        [this](bool visible) {
            if (auto* ws = activeWorkspaceTab())
                ws->setBrushControlVisible(visible);
        });
    connect(m_topBar, &widgets::TopBar::canvasWidgetsToolStateOverlayVisibilityChanged, this,
        [this](bool visible) {
            if (auto* ws = activeWorkspaceTab())
                ws->setToolStateOverlayVisible(visible);
        });

    // First-launch update message (when dismissed, save flag)
    connect(m_overlayCoordinator, &OverlayCoordinator::firstLaunchUpdateMessageDismissed, this,
        &MainWindow::onFirstLaunchUpdateMessageDismissed);

    // Command palette shortcut
    auto* paletteShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space), this);
    paletteShortcut->setContext(Qt::ApplicationShortcut);
    connect(paletteShortcut, &QShortcut::activated, this, &MainWindow::showCommandPalette);

    if (m_topBar && m_tabCoordinator && m_tabCoordinator->tabManager()) {
        ruwa::core::BaseTab* at = m_tabCoordinator->tabManager()->activeTab();
        m_topBar->setLayoutSwitchEnabled(
            at != nullptr && qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(at) != nullptr);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    auto* tm = m_tabCoordinator->tabManager();
    if (!tm) {
        event->accept();
        return;
    }

    // First pass: process only modified tabs. Start with active tab if modified.
    QList<ruwa::ui::tabs::WorkspaceTab*> modifiedTabs;
    for (ruwa::core::BaseTab* tab : tm->tabs()) {
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (wsTab && wsTab->isModified()) {
            modifiedTabs.append(wsTab);
        }
    }

    ruwa::core::BaseTab* activeTab = tm->activeTab();
    auto* activeWsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(activeTab);
    if (activeWsTab && modifiedTabs.contains(activeWsTab)) {
        if (!ruwa::ui::widgets::prepareWorkspaceTabForClose(activeWsTab, this)) {
            event->ignore();
            return;
        }
        tm->requestCloseTab(activeTab);
        tm->confirmTabClosed(activeTab->id());
        modifiedTabs.removeOne(activeWsTab);
    }

    for (ruwa::ui::tabs::WorkspaceTab* wsTab : modifiedTabs) {
        if (!tm->hasTab(wsTab))
            continue;
        tm->activateTab(wsTab);
        if (!ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, this)) {
            event->ignore();
            return;
        }
        tm->requestCloseTab(wsTab);
        tm->confirmTabClosed(wsTab->id());
    }

    // Second pass: close all remaining (non-modified) tabs
    while (tm->count() > 0) {
        ruwa::core::BaseTab* tab = tm->activeTab();
        if (!tab)
            break;
        if (!tm->requestCloseTab(tab))
            break;
        tm->confirmTabClosed(tab->id());
    }

    event->accept();
}

void MainWindow::relayoutTopBarInset()
{
    if (m_topBarClip) {
        static_cast<TopBarClipWidget*>(m_topBarClip)->relayout();
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        Qt::WindowStates state = windowState();
        if (state & Qt::WindowMaximized) {
            m_topBar->onWindowStateChanged(Qt::WindowMaximized);
        } else {
            m_topBar->onWindowStateChanged(Qt::WindowNoState);
        }
        relayoutTopBarInset();
        m_topBar->update();
    }
    if (event->type() == QEvent::LanguageChange) {
        // Let Qt process language propagation naturally and update title next cycle.
        QTimer::singleShot(0, this, [this]() {
            if (m_tabCoordinator && m_tabCoordinator->tabManager()) {
                if (auto* tab = m_tabCoordinator->tabManager()->activeTab()) {
                    setWindowTitle(QString("Ruwa - %1").arg(tab->title()));
                    return;
                }
            }
            setWindowTitle("Ruwa");
        });
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    auto* tm = tabManager();
    auto* active = tm ? tm->activeTab() : nullptr;
    const bool isHomeTab = qobject_cast<ruwa::ui::tabs::HomePageTab*>(active) != nullptr;
    if (isHomeTab
        && (mayContainProjectFilePaths(event->mimeData())
            || mayContainImportableImagePaths(event->mimeData()))) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    auto* tm = tabManager();
    auto* active = tm ? tm->activeTab() : nullptr;
    const bool isHomeTab = qobject_cast<ruwa::ui::tabs::HomePageTab*>(active) != nullptr;
    if (isHomeTab
        && (mayContainProjectFilePaths(event->mimeData())
            || mayContainImportableImagePaths(event->mimeData()))) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragMoveEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    auto* tm = tabManager();
    auto* active = tm ? tm->activeTab() : nullptr;
    const bool isHomeTab = qobject_cast<ruwa::ui::tabs::HomePageTab*>(active) != nullptr;
    if (!isHomeTab) {
        QMainWindow::dropEvent(event);
        return;
    }

    const QStringList projectPaths = extractProjectFilePaths(event->mimeData());
    if (!projectPaths.isEmpty()) {
        bool handled = false;
        for (const QString& filePath : projectPaths) {
            handled = ruwa::core::CommandExecutor::instance().execute(
                          QStringLiteral("file.open"), { { QStringLiteral("path"), filePath } })
                || handled;
        }
        if (handled) {
            event->acceptProposedAction();
            return;
        }
    }

    const QStringList filePaths = extractImportableImagePaths(event->mimeData());
    if (!filePaths.isEmpty() && tm) {
        const QSize projectSize = resolveProjectSizeFromImages(filePaths);
        ruwa::ui::tabs::WorkspaceTab::ProjectSettings settings;
        settings.name = projectNameFromImagePaths(filePaths);
        settings.canvasSize = projectSize;
        settings.canvasBoundsMode = ruwa::core::canvas::CanvasBoundsMode::Bounded;
        settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
            QRect(0, 0, projectSize.width(), projectSize.height()) };
        settings.templateType = QCoreApplication::translate("FileCommands", "RGB Color");

        auto* workspaceTab = new ruwa::ui::tabs::WorkspaceTab(settings);
        workspaceTab->seedStartupImageImportPaths(filePaths);
        tm->addTab(workspaceTab);
        event->acceptProposedAction();
        return;
    }

    const auto directImage
        = ruwa::ui::workspace::detail::extractImageFromMime(event->mimeData(), tr("Dropped image"));
    if (directImage.isValid()) {
        createProjectFromDroppedImage(directImage.image, directImage.layerName);
        event->acceptProposedAction();
        return;
    }

    const QList<QUrl> remoteUrls
        = ruwa::ui::workspace::detail::extractRemoteImageUrlsFromMime(event->mimeData());
    if (!remoteUrls.isEmpty()) {
        downloadAndCreateProjectFromDroppedImage(remoteUrls.first());
        event->acceptProposedAction();
        return;
    }

    QMainWindow::dropEvent(event);
}

void MainWindow::createProjectFromDroppedImage(const QImage& image, const QString& layerName)
{
    if (image.isNull()) {
        return;
    }

    auto* tm = tabManager();
    if (!tm) {
        return;
    }

    const QSize projectSize = image.size().isValid() ? image.size() : QSize(1920, 1080);
    ruwa::ui::tabs::WorkspaceTab::ProjectSettings settings;
    settings.name = projectNameFromImageLayerName(layerName);
    settings.canvasSize = projectSize;
    settings.canvasBoundsMode = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
        QRect(0, 0, projectSize.width(), projectSize.height()) };
    settings.templateType = QCoreApplication::translate("FileCommands", "RGB Color");

    auto* workspaceTab = new ruwa::ui::tabs::WorkspaceTab(settings);
    workspaceTab->seedStartupImageImport(image, layerName);
    tm->addTab(workspaceTab);
}

QNetworkAccessManager* MainWindow::imageDropNetworkManager()
{
    if (!m_imageDropNetworkManager) {
        m_imageDropNetworkManager = new QNetworkAccessManager(this);
    }
    return m_imageDropNetworkManager;
}

void MainWindow::downloadAndCreateProjectFromDroppedImage(const QUrl& url)
{
    QNetworkAccessManager* manager = imageDropNetworkManager();
    if (!manager || !url.isValid()) {
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(
        QNetworkRequest::UserAgentHeader, QStringLiteral("Ruwa image drag-drop import"));

    const quint64 generation = ++m_imageDropLoadGeneration;
    QNetworkReply* reply = manager->get(request);
    QPointer<MainWindow> self = this;

    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxDroppedBrowserImageBytes) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, self, reply, generation, url]() {
        reply->deleteLater();
        if (!self || m_imageDropLoadGeneration != generation) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        const QByteArray data = reply->readAll();
        if (data.size() > kMaxDroppedBrowserImageBytes) {
            return;
        }

        const QImage image = ruwa::ui::workspace::detail::decodeImageData(data);
        if (image.isNull()) {
            return;
        }

        createProjectFromDroppedImage(
            image, ruwa::ui::workspace::detail::suggestedLayerNameForImageUrl(url));
    });
}

void MainWindow::onActiveTabChanged(ruwa::core::BaseTab* newTab)
{
    ruwa::services::DiscordService::instance()->onActiveTabChanged(newTab);

    if (m_topBar) {
        m_topBar->setLayoutSwitchEnabled(
            newTab != nullptr && qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(newTab) != nullptr);
    }

    if (ruwa::core::BaseTab* prev = m_windowTitleTab.data()) {
        QObject::disconnect(prev, nullptr, this, nullptr);
    }
    m_windowTitleTab = newTab;

    if (newTab) {
        setWindowTitle(QStringLiteral("Ruwa - %1").arg(newTab->title()));
        connect(newTab, &ruwa::core::BaseTab::titleChanged, this,
            &MainWindow::updateWindowTitleForActiveTab);
        connect(newTab, &ruwa::core::BaseTab::modifiedChanged, this,
            &MainWindow::updateWindowTitleForActiveTab);
        if (auto* ws = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(newTab)) {
            m_topBar->setPanelsVisibilityState(ws->isToolsPanelVisible(),
                ws->isBrushesPanelVisible(), ws->isLayersPanelVisible(),
                ws->isLayerPropertiesPanelVisible(), ws->isLayerEffectsPanelVisible(),
                ws->isColorPanelVisible(), ws->isComposerPanelVisible());
            m_topBar->setCanvasWidgetsVisibilityState(ws->isJoystickVisible(),
                ws->isBrushControlVisible(), ws->isToolStateOverlayVisible());
        }
    } else {
        setWindowTitle(QStringLiteral("Ruwa"));
    }
}

void MainWindow::updateWindowTitleForActiveTab()
{
    ruwa::core::TabManager* tm = tabManager();
    ruwa::core::BaseTab* at = tm ? tm->activeTab() : nullptr;
    if (!at) {
        setWindowTitle(QStringLiteral("Ruwa"));
        return;
    }
    // During requestCloseTab the tab is removed from TabManager before m_activeTab is reassigned;
    // avoid calling title() on that closing tab.
    if (!tm->hasTab(at->id())) {
        setWindowTitle(QStringLiteral("Ruwa"));
        return;
    }
    setWindowTitle(QStringLiteral("Ruwa - %1").arg(at->title()));
}

void MainWindow::showCommandPalette()
{
    m_overlayCoordinator->showCommandPalette();
}

void MainWindow::navigateToHomeTab()
{
    if (m_tabCoordinator) {
        m_tabCoordinator->navigateToHomeTab();
    }
}

void MainWindow::navigateToSettings()
{
    if (m_tabCoordinator) {
        m_tabCoordinator->navigateToSettings();
    }
}

void MainWindow::navigateToAbout()
{
    if (m_tabCoordinator) {
        m_tabCoordinator->navigateToAbout();
    }
}

void MainWindow::createProjectWithDefaults()
{
    auto* tm = m_tabCoordinator->tabManager();
    if (!tm)
        return;

    ruwa::core::CommandExecutor::instance().execute("file.quickNewProject");
}

void MainWindow::showNewProjectDialog()
{
    if (m_tabCoordinator) {
        m_tabCoordinator->navigateToNewProject();
    }
}

void MainWindow::showColorPicker(const QColor& initialColor, QWidget* sourceButton)
{
    m_overlayCoordinator->showColorPicker(initialColor, sourceButton);
}

void MainWindow::showFirstLaunchUpdateMessageIfNeeded()
{
    auto& sm = ruwa::core::SettingsManager::instance();
    if (!sm.isFirstRunIntegrationCompleted()) {
        if (!m_deferFirstLaunchUpdateUntilOnboarding) {
            m_deferFirstLaunchUpdateUntilOnboarding = true;
            connect(
                &sm, &ruwa::core::SettingsManager::firstRunIntegrationCompletedChanged, this,
                [this](bool completed) {
                    m_deferFirstLaunchUpdateUntilOnboarding = false;
                    if (completed) {
                        QTimer::singleShot(kFirstLaunchUpdateAfterOnboardingMs, this,
                            [this]() { presentFirstLaunchUpdateMessageIfNeeded(); });
                    }
                },
                Qt::SingleShotConnection);
        }
        return;
    }

    presentFirstLaunchUpdateMessageIfNeeded();
}

void MainWindow::showReleaseNotesOverlay()
{
    if (m_overlayCoordinator) {
        m_overlayCoordinator->showReleaseNotesOverlay();
    }
}

void MainWindow::presentFirstLaunchUpdateMessageIfNeeded()
{
    const QString currentVersion = QApplication::applicationVersion();
    const QString lastSeenVersion
        = QSettings().value(QStringLiteral("Updates/lastSeenVersion")).toString();

    // Show overlay when: first run (lastSeen empty) or current version is newer than last seen
    const bool shouldShow = lastSeenVersion.isEmpty()
        || ruwa::services::UpdateManager::isVersionNewer(lastSeenVersion, currentVersion);
    if (!shouldShow) {
        return;
    }

    m_overlayCoordinator->showFirstLaunchUpdateMessage();
}

void MainWindow::onFirstLaunchUpdateMessageDismissed()
{
    const QString version = QApplication::applicationVersion();
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    m_firstLaunchUpdateDismissSyncFuture
        = QtConcurrent::run(persistLastSeenUpdateVersion, organization, application, version);
}

void MainWindow::showContextMenu(int menuType, const QPoint& globalPos, const QVariantMap& context)
{
    m_contextMenuCoordinator->showContextMenu(
        static_cast<ContextMenuType>(menuType), globalPos, context);
}

ruwa::core::TabManager* MainWindow::tabManager() const
{
    return m_tabCoordinator ? m_tabCoordinator->tabManager() : nullptr;
}

ruwa::ui::tabs::WorkspaceTab* MainWindow::activeWorkspaceTab() const
{
    auto* tm = m_tabCoordinator ? m_tabCoordinator->tabManager() : nullptr;
    return tm ? qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tm->activeTab()) : nullptr;
}

void MainWindow::onHelpAbout()
{
    navigateToAbout();
}

void MainWindow::handleCopyRequest()
{
    auto* tm = tabManager();
    auto* workspaceTab
        = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tm ? tm->activeTab() : nullptr);
    if (!workspaceTab) {
        return;
    }
    workspaceTab->handleCopyRequest();
}

void MainWindow::handlePasteRequest()
{
    auto* tm = tabManager();
    auto* workspaceTab
        = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tm ? tm->activeTab() : nullptr);
    if (!workspaceTab) {
        return;
    }
    workspaceTab->handlePasteRequest();
}

void MainWindow::handleImportImagesRequest()
{
    auto* workspaceTab = activeWorkspaceTab();
    if (!workspaceTab) {
        return;
    }

    QStringList patterns;
    for (const QByteArray& fmt : QImageReader::supportedImageFormats()) {
        const QString pattern = QStringLiteral("*.") + QString::fromLatin1(fmt);
        if (!patterns.contains(pattern, Qt::CaseInsensitive)) {
            patterns.append(pattern);
        }
    }
    const QString imageGlob = patterns.isEmpty()
        ? QStringLiteral("*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tiff *.tga")
        : patterns.join(QChar(' '));
    const QString filter
        = tr("Images (%1)").arg(imageGlob) + QStringLiteral(";;") + tr("All Files (*)");

    const QStringList paths = ruwa::shared::filedialog::getOpenFileNames(
        this, ruwa::shared::filedialog::category::kImageImport, tr("Import images"), filter);
    if (paths.isEmpty()) {
        return;
    }
    workspaceTab->promptImportImageFiles(paths);
}

} // namespace ruwa::ui::windows
