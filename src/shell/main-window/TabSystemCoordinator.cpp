// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   T A B   S Y S T E M   C O O R D I N A T O R
// ======================================================================================

#include "TabSystemCoordinator.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "shell/tab-system/CustomTabBar.h"
#include "features/home/HomePageTab.h"
#include "shell/tab-system/EmptyStateTab.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/theme/editor/ThemeEditorTab.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/settings/shortcuts/ShortcutManagerTab.h"
#include "shared/widgets/layout/AnimatedTabWidget.h"
#include "features/settings/SettingsContent.h"
#include "features/theme/editor/ThemeSelectorWidget.h"
#include "shared/widgets/PresetMenuListWidget.h"
#include "shared/widgets/inputs/ColorInputButton.h"

#include <QVariantMap>
#include <QSize>
#include <QTimer>

namespace ruwa::ui::windows {

TabSystemCoordinator::TabSystemCoordinator(QObject* parent)
    : QObject(parent)
{
}

void TabSystemCoordinator::initialize(ruwa::core::TabManager* tabManager,
    ruwa::ui::tabs::CustomTabBar* tabBar, ruwa::ui::widgets::AnimatedTabWidget* tabContent,
    bool createInitialHomeTab)
{
    m_tabManager = tabManager;
    m_tabBar = tabBar;
    m_tabContent = tabContent;

    if (!m_tabManager) {
        return;
    }

    m_tabManager->setEmptyStateTab(new tabs::EmptyStateTab());

    // Connect tab manager to UI
    if (m_tabBar) {
        m_tabBar->setTabManager(m_tabManager);
    }

    if (m_tabContent) {
        m_tabContent->setTabManager(m_tabManager);
    }

    // Setup tab system
    registerTabFactories();
    setupConnections();
    if (createInitialHomeTab) {
        createInitialTabs();
    }
}

void TabSystemCoordinator::registerTabFactories()
{
    if (!m_tabManager)
        return;

    // HomePage factory
    m_tabManager->registerTabFactory(ruwa::core::BaseTab::TabType::HomePage,
        [](const QVariantMap&) -> ruwa::core::BaseTab* { return new tabs::HomePageTab(); });

    // Workspace factory
    m_tabManager->registerTabFactory(ruwa::core::BaseTab::TabType::Workspace,
        [](const QVariantMap& data) -> ruwa::core::BaseTab* {
            tabs::WorkspaceTab::ProjectSettings settings;
            settings.name = data.value("name", "Untitled Project").toString();
            settings.canvasSize = data.value("canvasSize", QSize(1920, 1080)).toSize();
            settings.canvasBoundsMode = static_cast<ruwa::core::canvas::CanvasBoundsMode>(
                data.value("canvasBoundsMode",
                        data.value("infiniteCanvasEnabled", false).toBool() ? 1 : 0)
                    .toInt());
            const QRect exportFrame = data.value("exportFrame").toRect();
            settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
                exportFrame.isValid()
                    ? exportFrame
                    : QRect(0, 0, settings.canvasSize.width(), settings.canvasSize.height()) };
            settings.templateType = data.value("templateType", "RGB Color").toString();
            settings.backgroundColor
                = data.value("backgroundColor", QColor(Qt::white)).value<QColor>();
            auto* tab = new tabs::WorkspaceTab(settings);
            tabs::WorkspaceTab::WorkspaceColorState colorState;
            colorState.foreground
                = data.value("foregroundColor", QColor(Qt::black)).value<QColor>();
            colorState.background
                = data.value("workspaceBackgroundColor", settings.backgroundColor).value<QColor>();
            colorState.editingForeground = data.value("editingForegroundColor", true).toBool();
            tab->setWorkspaceColorState(colorState);
            return tab;
        });
}

void TabSystemCoordinator::setupConnections()
{
    if (!m_tabManager)
        return;

    auto& themeManager = ruwa::ui::core::ThemeManager::instance();
    connect(&themeManager, &ruwa::ui::core::ThemeManager::themeApplyStarted, this,
        &TabSystemCoordinator::onThemeApplyStarted);
    connect(&themeManager, &ruwa::ui::core::ThemeManager::themeChanged, this,
        &TabSystemCoordinator::onThemeChanged);

    if (m_tabContent) {
        connect(m_tabContent, &widgets::AnimatedTabWidget::transitionFinished, this,
            &TabSystemCoordinator::onTabTransitionFinished);
    }

    connect(m_tabManager, &ruwa::core::TabManager::activeTabChanged, this,
        [this](ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab) {
            Q_UNUSED(oldTab);
            emit activeTabChanged(newTab);
        });

    // Connect color picker requests from ThemeEditorTab
    connect(
        m_tabManager, &ruwa::core::TabManager::tabAdded, this, [this](ruwa::core::BaseTab* tab) {
            // Check if this is a ThemeEditorTab
            if (auto* themeEditor = qobject_cast<tabs::ThemeEditorTab*>(tab)) {
                connect(themeEditor, &tabs::ThemeEditorTab::colorPickerRequested, this,
                    [this](const QColor& color, widgets::ColorInputButton* button) {
                        emit colorPickerRequested(color, static_cast<QWidget*>(button));
                    });
            }
            if (auto* workspaceTab = qobject_cast<tabs::WorkspaceTab*>(tab)) {
                connect(workspaceTab, &tabs::WorkspaceTab::colorPickerRequested, this,
                    [this](const QColor& color, QWidget* sourceButton) {
                        emit colorPickerRequested(color, sourceButton);
                    });
                connect(workspaceTab, &tabs::WorkspaceTab::panelsVisibilityChanged, this,
                    [this, workspaceTab]() {
                        if (m_tabManager && m_tabManager->activeTab() == workspaceTab) {
                            emit workspacePanelVisibilityChanged();
                        }
                    });
            }
        });

    connect(m_tabManager, &ruwa::core::TabManager::tabReplaced, this,
        [this](ruwa::core::BaseTab* oldTab, ruwa::core::BaseTab* newTab) {
            Q_UNUSED(oldTab);
            // A replacement tab is a brand-new object — re-establish the same signal
            // wiring tabAdded() set up for the original (it was lost with the old tab).
            if (auto* themeEditor = qobject_cast<tabs::ThemeEditorTab*>(newTab)) {
                connect(themeEditor, &tabs::ThemeEditorTab::colorPickerRequested, this,
                    [this](const QColor& color, widgets::ColorInputButton* button) {
                        emit colorPickerRequested(color, static_cast<QWidget*>(button));
                    });
            }
            if (auto* workspaceTab = qobject_cast<tabs::WorkspaceTab*>(newTab)) {
                connect(workspaceTab, &tabs::WorkspaceTab::colorPickerRequested, this,
                    [this](const QColor& color, QWidget* sourceButton) {
                        emit colorPickerRequested(color, sourceButton);
                    });
                connect(workspaceTab, &tabs::WorkspaceTab::panelsVisibilityChanged, this,
                    [this, workspaceTab]() {
                        if (m_tabManager && m_tabManager->activeTab() == workspaceTab) {
                            emit workspacePanelVisibilityChanged();
                        }
                    });
            }

            if (m_tabManager && m_tabManager->activeTab() == newTab) {
                emit activeTabChanged(newTab);
                emit workspacePanelVisibilityChanged();
            }
        });
}

void TabSystemCoordinator::onThemeApplyStarted()
{
    if (!m_tabManager) {
        return;
    }

    m_themeApplyInProgress = true;
    m_waitingForThemeChanged = true;
    m_tabManager->setActivationBlocked(true);

    for (auto* tab : m_tabManager->tabs()) {
        if (tab) {
            tab->setNeedsThemeRefresh(true);
        }
    }

    if (auto* emptyTab = m_tabManager->emptyStateTab()) {
        emptyTab->setNeedsThemeRefresh(true);
    }
}

void TabSystemCoordinator::onThemeChanged()
{
    m_waitingForThemeChanged = false;

    if (!m_tabManager) {
        return;
    }

    if (auto* active = m_tabManager->activeTab()) {
        // The selected tab gets the theme first, with the loading screen shown
        // over its content while it is applied (per spec).
        applyPendingThemeToTab(active, true);
        return;
    }

    m_themeApplyInProgress = false;
    m_tabManager->setActivationBlocked(false);
    if (m_tabContent) {
        m_tabContent->setThemeLoadingVisible(false);
    }
}

void TabSystemCoordinator::onTabTransitionFinished()
{
    if (!m_tabManager || m_waitingForThemeChanged || m_tabThemeRefreshInProgress) {
        return;
    }

    auto* active = m_tabManager->activeTab();
    if (!active || !active->needsThemeRefresh()) {
        return;
    }

    applyPendingThemeToTab(active, true);
}

void TabSystemCoordinator::applyPendingThemeToTab(ruwa::core::BaseTab* tab, bool showLoading)
{
    if (!m_tabManager || !tab || m_tabThemeRefreshInProgress) {
        return;
    }

    if (!tab->needsThemeRefresh()) {
        // An overlay may have slid in for a switch that turns out not to need a
        // refresh after all — make sure it doesn't get stuck.
        if (m_tabContent && m_tabContent->isThemeLoadingVisible()) {
            m_tabContent->setThemeLoadingVisible(false);
        }
        m_themeLoadingNeedsMinDuration = false;
        if (m_themeApplyInProgress) {
            m_themeApplyInProgress = false;
            m_tabManager->setActivationBlocked(false);
        }
        return;
    }

    m_tabThemeRefreshInProgress = true;
    m_refreshingThemeTabId = tab->id();
    m_tabManager->setActivationBlocked(true);

    // The loading screen masks heavy refreshes (workspace re-theme, or tabs that
    // fully rebuild themselves). Lightweight tabs re-theme instantly via repolish
    // and must NOT flash a loader.
    const bool showLoadingEffective = showLoading && tab->wantsThemeLoadingScreen();

    // Two ways the overlay gets shown:
    //  - Switch to a workspace tab: AnimatedTabWidget::slideToTab already slid the
    //    overlay in with the tab, so the slide itself provided the visible time —
    //    no extra minimum needed here.
    //  - Theme changed while the workspace tab is already active (no slide): show
    //    the overlay now and hold it a minimum time so it doesn't flash.
    const bool overlayAlreadyVisible = m_tabContent && m_tabContent->isThemeLoadingVisible();
    m_themeLoadingNeedsMinDuration = false;
    if (showLoadingEffective && !overlayAlreadyVisible && m_tabContent) {
        m_tabContent->setThemeLoadingVisible(true);
        m_themeLoadingShownAt.restart();
        m_themeLoadingNeedsMinDuration = true;
    }

    const QUuid tabId = tab->id();
    tab->applyThemeRefresh([this, tabId]() { finishThemeApplyForTab(tabId); }, showLoading);
}

void TabSystemCoordinator::finishThemeApplyForTab(const QUuid& tabId)
{
    if (m_refreshingThemeTabId != tabId) {
        return;
    }

    // For the no-slide case the non-destructive refresh is near-instant, so hold
    // the spinner a minimum time; otherwise it flashes for a single frame and the
    // theme appears to "snap" without feedback. (For the slide case the slide
    // already gave the overlay its visible time.)
    constexpr qint64 kMinThemeLoadingMs = 350;
    if (m_themeLoadingNeedsMinDuration) {
        const qint64 remaining = kMinThemeLoadingMs - m_themeLoadingShownAt.elapsed();
        if (remaining > 0) {
            QTimer::singleShot(static_cast<int>(remaining), this,
                [this, tabId]() { finishThemeApplyForTab(tabId); });
            return;
        }
    }

    m_themeLoadingNeedsMinDuration = false;

    // Clear state + unblock once the overlay has shown its confirmation checkmark
    // and faded out. Kept blocked through the fade so a switch can't race it.
    auto finalize = [this]() {
        m_tabThemeRefreshInProgress = false;
        m_refreshingThemeTabId = QUuid();
        m_themeApplyInProgress = false;

        if (m_tabManager) {
            m_tabManager->setActivationBlocked(false);

            // A new theme change may have arrived while we held the overlay;
            // applyPendingThemeToTab() would have been refused then. Re-apply so
            // the active tab never ends up stale.
            if (auto* active = m_tabManager->activeTab(); active && active->needsThemeRefresh()) {
                applyPendingThemeToTab(active, true);
            }
        }
    };

    if (m_tabContent) {
        m_tabContent->finishThemeLoadingWithConfirmation(finalize);
    } else {
        finalize();
    }
}

void TabSystemCoordinator::createInitialTabs()
{
    if (!m_tabManager)
        return;

    auto* homeTab = createHomeTab();
    if (homeTab) {
        m_tabManager->addTab(homeTab);
    }
}

void TabSystemCoordinator::navigateToHomeTab()
{
    if (!m_tabManager)
        return;

    for (auto* tab : m_tabManager->tabs()) {
        if (tab && tab->type() == ruwa::core::BaseTab::TabType::HomePage) {
            m_tabManager->activateTab(tab);
            return;
        }
    }

    auto* homeTab = createHomeTab();
    if (homeTab) {
        m_tabManager->addTab(homeTab);
    }
}

void TabSystemCoordinator::navigateToNewProject()
{
    navigateToHomeTab();

    auto* homeTab = qobject_cast<tabs::HomePageTab*>(m_tabManager->activeTab());
    if (homeTab) {
        homeTab->navigateToNewProject();
    }
}

void TabSystemCoordinator::navigateToSettings()
{
    navigateToHomeTab();

    auto* homeTab = qobject_cast<tabs::HomePageTab*>(m_tabManager->activeTab());
    if (homeTab) {
        homeTab->navigateToSettings();
    }
}

void TabSystemCoordinator::navigateToAbout()
{
    navigateToHomeTab();

    auto* homeTab = qobject_cast<tabs::HomePageTab*>(m_tabManager->activeTab());
    if (homeTab) {
        homeTab->navigateToAbout();
    }
}

tabs::HomePageTab* TabSystemCoordinator::createHomeTab()
{
    auto* homeTab = new tabs::HomePageTab();
    connectHomeTabSignals(homeTab);
    return homeTab;
}

void TabSystemCoordinator::connectHomeTabSignals(tabs::HomePageTab* homeTab)
{
    if (!homeTab || !m_tabManager)
        return;

    connect(homeTab, &tabs::HomePageTab::projectCreateRequested, this,
        [this](const QString& name, const QSize& size, bool infiniteCanvasEnabled,
            const QString& colorMode, const QColor& backgroundColor,
            aether::TilePixelFormat tileFormat) {
            if (!m_tabManager)
                return;

            tabs::WorkspaceTab::ProjectSettings settings;
            settings.name = name;
            settings.canvasSize = size;
            settings.canvasBoundsMode = infiniteCanvasEnabled
                ? ruwa::core::canvas::CanvasBoundsMode::Infinite
                : ruwa::core::canvas::CanvasBoundsMode::Bounded;
            settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
                QRect(0, 0, size.width(), size.height()) };
            settings.templateType = colorMode;
            settings.backgroundColor = backgroundColor;
            settings.tileFormat = tileFormat;
            m_tabManager->addTab(new tabs::WorkspaceTab(settings));
        });

    connect(homeTab, &tabs::HomePageTab::colorPickerRequested, this,
        [this](const QColor& color, QWidget* sourceButton) {
            emit colorPickerRequested(color, sourceButton);
        });

    connect(homeTab, &tabs::HomePageTab::shortcutManagerRequested, this, [this]() {
        if (!m_tabManager)
            return;

        for (auto* tab : m_tabManager->tabs()) {
            if (qobject_cast<tabs::ShortcutManagerTab*>(tab)) {
                m_tabManager->activateTab(tab);
                return;
            }
        }

        m_tabManager->addTab(new tabs::ShortcutManagerTab());
    });

    // Connect theme editor request
    connect(homeTab, &tabs::HomePageTab::themeEditorRequested, this, [this, homeTab]() {
        if (!m_tabManager)
            return;

        auto* editor = new tabs::ThemeEditorTab();
        m_tabManager->addTab(editor);

        // Get ThemeSelectorWidget from HomePageTab
        auto* themeSelector = homeTab->settingsContent()
            ? homeTab->settingsContent()->themeSelectorWidget()
            : nullptr;

        if (themeSelector) {
            // 1. Editor -> Selector: when theme is applied in editor, update selector
            connect(editor, &tabs::ThemeEditorTab::themeApplied, themeSelector,
                &widgets::ThemeSelectorWidget::updateFromThemeEditor);

            // 2. Selector -> Editor: when theme is selected in settings, update editor
            connect(themeSelector, &widgets::ThemeSelectorWidget::themeSelected, editor,
                [editor](const ruwa::ui::core::ThemePreset& preset) {
                    editor->selectThemeById(preset.id);

                    // Mark as active in editor's theme list
                    if (auto* list = editor->findChild<widgets::PresetMenuListWidget*>()) {
                        list->setActiveUserData(preset.id.toString());
                    }
                });
        }

        // NOTE: colorPickerRequested is handled by OverlayCoordinator
        // We'll emit a signal that MainWindow can connect
    });
}

} // namespace ruwa::ui::windows
