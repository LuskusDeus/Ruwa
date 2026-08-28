// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   M A I N   W I N D O W   ( R E F A C T O R E D )
// ======================================================================================
//   File        : MainWindow.h
//   Description : Main application window (thin wrapper around coordinators)
// ======================================================================================

#ifndef RUWA_UI_WINDOWS_MAINWINDOW_H
#define RUWA_UI_WINDOWS_MAINWINDOW_H

#include "features/theme/manager/ThemeContext.h"

#include "shell/tab-system/BaseTab.h"

#include <QMainWindow>
#include <QPointer>
#include <QFuture>
#include <QVariantMap>
#include <QByteArray>
#include <QStringList>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QImage;
class QNetworkAccessManager;
class QUrl;
class QShowEvent;

namespace ruwa::core {
class TabManager;
}

namespace ruwa::ui {
namespace tabs {
class CustomTabBar;
class WorkspaceTab;
} // namespace tabs
namespace widgets {
class TopBar;
class AnimatedTabWidget;
} // namespace widgets
} // namespace ruwa::ui

namespace ruwa::ui::windows {

// Forward declarations of coordinators
class WindowSetupCoordinator;
class TabSystemCoordinator;
class OverlayCoordinator;
class CommandCoordinator;
class ContextMenuCoordinator;
class ContextWindow;

/**
 * @brief Main application window (refactored with coordinator pattern)
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, const QStringList& startupOpenFilePaths = {});
    ~MainWindow() override;

    // === Command System Integration ===

    /// Show the command palette (Ctrl+Space)
    Q_INVOKABLE void showCommandPalette();

    /// Navigate to Home tab
    Q_INVOKABLE void navigateToHomeTab();

    /// Navigate to Settings section (Home → Settings)
    Q_INVOKABLE void navigateToSettings();

    /// Navigate to About section (Home → About)
    Q_INVOKABLE void navigateToAbout();

    /// Create a new project with default settings (called from commands)
    Q_INVOKABLE void createProjectWithDefaults();

    /// Navigate to Home tab and show New Project section (creates Home tab if needed)
    Q_INVOKABLE void showNewProjectDialog();

    /// Show color picker
    void showColorPicker(const QColor& initialColor = Qt::white, QWidget* sourceButton = nullptr);

    /// Show first-launch update message if not yet shown (checks QSettings flag)
    void showFirstLaunchUpdateMessageIfNeeded();
    void showReleaseNotesOverlay();

    /// Show context menu
    void showContextMenu(int menuType, const QPoint& globalPos, const QVariantMap& context = {});

    /// Open the modal Fill settings window for the active selection.
    void showFillContextWindow();

    // === Widget Access (for StartupAnimationController) ===

    /// Get TopBar widget (used by StartupAnimationController for animations)
    widgets::TopBar* topBar() const { return m_topBar; }

    /// Clip container for startup topbar animation
    QWidget* topBarClip() const { return m_topBarClip; }

    /// Lays out the inset title bar (margins + child geometry)
    void relayoutTopBarInset();

    /// Get TabManager (for external access)
    ruwa::core::TabManager* tabManager() const;

    /// Build tabs out of a MIME payload the Home tab can turn into projects
    /// (project files, image files, a raw image, a remote image URL). Used by
    /// the drop handler and by Ctrl+V while the Home tab is active.
    /// Returns true when the payload was consumed.
    bool createProjectFromMimeData(const QMimeData* mimeData);

    /// Get ContextMenuCoordinator (for external access)
    ContextMenuCoordinator* contextMenuCoordinator() const { return m_contextMenuCoordinator; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void setupUI();
    void createTopBar();
    void applyTopBarTabAlignment();
    void connectSignals();
    void handleCopyRequest();
    void handlePasteRequest();
    void handleImportImagesRequest();
    void createProjectFromDroppedImage(const QImage& image, const QString& layerName);
    void downloadAndCreateProjectFromDroppedImage(const QUrl& url);
    QNetworkAccessManager* imageDropNetworkManager();
    ruwa::ui::tabs::WorkspaceTab* activeWorkspaceTab() const;
    /// Version gate + overlay (assumes onboarding already completed when relevant).
    void presentFirstLaunchUpdateMessageIfNeeded();
    /// Writes geometry + monitor once per session (first caller wins).
    void persistWindowState();
#if defined(Q_OS_WIN)
    void configureWindowsInkFeedback();
    void attachStylusDebugBackend();
#endif

private slots:
    void onActiveTabChanged(ruwa::core::BaseTab* newTab);
    void updateWindowTitleForActiveTab();
    void onHelpAbout();
    void onFirstLaunchUpdateMessageDismissed();

private:
    // Theme context
    ui::core::ThemeContext m_theme;

    // UI widgets
    widgets::TopBar* m_topBar = nullptr;
    QWidget* m_topBarClip = nullptr;
    tabs::CustomTabBar* m_tabBar = nullptr;
    QWidget* m_centralWidget = nullptr;
    widgets::AnimatedTabWidget* m_tabContent = nullptr;

    // Coordinators (all logic delegated here)
    WindowSetupCoordinator* m_setupCoordinator = nullptr;
    TabSystemCoordinator* m_tabCoordinator = nullptr;
    OverlayCoordinator* m_overlayCoordinator = nullptr;
    CommandCoordinator* m_commandCoordinator = nullptr;
    ContextMenuCoordinator* m_contextMenuCoordinator = nullptr;
    QPointer<ContextWindow> m_contextWindow;

    // Hidden OpenGL widget for pre-warming

    /// Tab we listen to for title / modified (QPointer clears if destroyed without a coordinator
    /// signal).
    QPointer<ruwa::core::BaseTab> m_windowTitleTab;

    /// When first-launch update toast is deferred until onboarding (welcome setup) finishes.
    bool m_deferFirstLaunchUpdateUntilOnboarding = false;
    QFuture<void> m_firstLaunchUpdateDismissSyncFuture;
    QNetworkAccessManager* m_imageDropNetworkManager = nullptr;
    quint64 m_imageDropLoadGeneration = 0;

    /// closeEvent() is the last moment the window still reports its real monitor;
    /// the destructor must not overwrite that with whatever is left by then.
    bool m_windowStateSaved = false;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_MAINWINDOW_H
