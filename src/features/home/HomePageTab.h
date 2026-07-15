// SPDX-License-Identifier: MPL-2.0

// HomePageTab.h
#ifndef RUWA_UI_TABS_HOMEPAGETAB_H
#define RUWA_UI_TABS_HOMEPAGETAB_H

#include "shell/tab-system/BaseTab.h"
#include "features/theme/manager/ThemeContext.h"
#include "features/home/HomePageSidebar.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/tiles/TileFormat.h"
#include <QColor>
#include <QPaintEvent>
#include <QResizeEvent>

class QWidget;

namespace ruwa::ui::widgets {
class HomePageContent;
class AboutContent;
class WelcomeContent;
class NewProjectContent;
class SettingsContent;
class RecentProjectEditOverlay;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::first_run_integration {
class FirstRunIntegrationWidget;
}

namespace ruwa::ui::tabs {

/**
 * @brief Home page tab displaying application start screen
 *
 * The home page is split into two sections:
 * - Left sidebar: Navigation buttons (Home, New Project, Settings, About)
 * - Right content area: Content panels that change based on sidebar selection
 *
 * This tab cannot be closed and is always available.
 * Content is lazily initialized on first display.
 */
class HomePageTab : public ruwa::core::BaseTab {
    Q_OBJECT

public:
    explicit HomePageTab(QWidget* parent = nullptr);
    ~HomePageTab() override;

    // === BaseTab Interface ===

    ruwa::core::BaseTab::TabType type() const override { return TabType::HomePage; }
    QString title() const override { return m_firstRunCompleted ? tr("Home") : tr("Welcome"); }
    QIcon icon() const override;

    /// Home page has no unsaved changes, always safe to close programmatically
    /// Note: UI layer should prevent user from manually closing this tab
    bool canClose() override { return true; }

    // === Navigation (called from commands via Q_INVOKABLE) ===

    /// Navigate to Welcome section
    Q_INVOKABLE void navigateToWelcome();

    /// Navigate to New Project section
    Q_INVOKABLE void navigateToNewProject();

    /// Navigate to Settings section
    Q_INVOKABLE void navigateToSettings();

    /// Navigate to About section
    Q_INVOKABLE void navigateToAbout();

    /// Navigate to a specific section
    void navigateToSection(ruwa::ui::widgets::HomePageSidebar::Section section);

    /// Get settings content for external connections (e.g., theme synchronization)
    ruwa::ui::widgets::SettingsContent* settingsContent() const { return m_settingsContent; }

signals:
    /// Emitted when user requests to create a new project
    void projectCreateRequested(const QString& name, const QSize& size, bool infiniteCanvasEnabled,
        const QString& colorMode, const QColor& backgroundColor,
        aether::TilePixelFormat tileFormat);
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);

    /// Emitted when user requests to open theme editor
    void themeEditorRequested();

    /// Emitted when user requests to open keyboard shortcuts editor
    void shortcutManagerRequested();

protected:
    void onInitialize() override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSectionChanged(ruwa::ui::widgets::HomePageSidebar::Section section);
    void onFirstRunCompletedChanged(bool completed);
    void onFirstRunCompletedRequested();
    void onFirstRunCustomizeRequested();

private:
    void setupUI();
    void setupHomeContent();
    void updateContentSideMargins();
    void updateFirstRunPageState();

private:
    ruwa::ui::core::ThemeContext m_theme;

    // Widgets
    ruwa::ui::widgets::AnimatedStackedWidget* m_rootStack { nullptr };
    QWidget* m_firstRunContainer { nullptr };
    ruwa::ui::first_run_integration::FirstRunIntegrationWidget* m_firstRunIntegration { nullptr };
    QWidget* m_homeContainer { nullptr };
    QWidget* m_sidebarClip { nullptr };
    ruwa::ui::widgets::HomePageSidebar* m_sidebar { nullptr };
    ruwa::ui::widgets::AnimatedStackedWidget* m_contentStack { nullptr };

    // Content panels
    ruwa::ui::widgets::AboutContent* m_aboutContent { nullptr };
    ruwa::ui::widgets::WelcomeContent* m_welcomeContent { nullptr };
    ruwa::ui::widgets::NewProjectContent* m_newProjectContent { nullptr };
    ruwa::ui::widgets::SettingsContent* m_settingsContent { nullptr };
    ruwa::ui::widgets::RecentProjectEditOverlay* m_recentProjectEditOverlay { nullptr };
    bool m_firstRunCompleted { false };
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_HOMEPAGETAB_H
