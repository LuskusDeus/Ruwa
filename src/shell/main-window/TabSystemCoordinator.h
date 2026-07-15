// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   T A B   S Y S T E M   C O O R D I N A T O R
// ======================================================================================

#ifndef RUWA_UI_WINDOWS_MAINWINDOW_TABSYSTEMCOORDINATOR_H
#define RUWA_UI_WINDOWS_MAINWINDOW_TABSYSTEMCOORDINATOR_H

#include <QObject>
#include <QColor>
#include <QUuid>
#include <QElapsedTimer>

class QWidget;

namespace ruwa::core {
class TabManager;
class BaseTab;
} // namespace ruwa::core

namespace ruwa::ui::tabs {
class CustomTabBar;
class HomePageTab;
} // namespace ruwa::ui::tabs

namespace ruwa::ui::widgets {
class AnimatedTabWidget;
}

namespace ruwa::ui::windows {

class TabSystemCoordinator : public QObject {
    Q_OBJECT

public:
    explicit TabSystemCoordinator(QObject* parent = nullptr);
    ~TabSystemCoordinator() override = default;

    void initialize(ruwa::core::TabManager* tabManager, ruwa::ui::tabs::CustomTabBar* tabBar,
        ruwa::ui::widgets::AnimatedTabWidget* tabContent, bool createInitialHomeTab = true);

    void createInitialTabs();
    void navigateToHomeTab();
    void navigateToNewProject();
    void navigateToSettings();
    void navigateToAbout();

    ruwa::core::TabManager* tabManager() const { return m_tabManager; }

signals:
    void activeTabChanged(ruwa::core::BaseTab* newTab);
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);
    /// Emitted when active WorkspaceTab's panel visibility changes
    void workspacePanelVisibilityChanged();

private:
    void registerTabFactories();
    void setupConnections();
    tabs::HomePageTab* createHomeTab();
    void connectHomeTabSignals(tabs::HomePageTab* homeTab);
    void onThemeApplyStarted();
    void onThemeChanged();
    void onTabTransitionFinished();
    void applyPendingThemeToTab(ruwa::core::BaseTab* tab, bool showLoading);
    void finishThemeApplyForTab(const QUuid& tabId);

private:
    ruwa::core::TabManager* m_tabManager = nullptr;
    ruwa::ui::tabs::CustomTabBar* m_tabBar = nullptr;
    ruwa::ui::widgets::AnimatedTabWidget* m_tabContent = nullptr;
    bool m_themeApplyInProgress = false;
    bool m_waitingForThemeChanged = false;
    bool m_tabThemeRefreshInProgress = false;
    QUuid m_refreshingThemeTabId;
    bool m_themeLoadingNeedsMinDuration = false; ///< enforce min visible time (no-slide case)
    QElapsedTimer m_themeLoadingShownAt; ///< when the loader overlay became visible
};

} // namespace ruwa::ui::windows

#endif
