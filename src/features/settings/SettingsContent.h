// SPDX-License-Identifier: MPL-2.0

// SettingsContent.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCONTENT_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCONTENT_H

#include "features/home/HomePageContent.h"
#include <QString>
#include <QVector>

class QLabel;
class QEvent;
class QVBoxLayout;
class QHBoxLayout;

namespace ruwa::ui::widgets {

class BaseAnimatedButton;
class SettingsCategory;
class ThemeSelectorWidget;
class WelcomeBannerSelectorWidget;
class SmoothScrollArea;
class SearchBar;
class SettingsToggle;
class SettingsChoice;
class SettingsComboBox;
class UpdatesSettingsWidget;
class ShortcutsNavigatorWidget;

/**
 * @brief Settings content panel
 *
 * Features:
 * - Theme selector with visual previews
 * - Multiple categories (always expanded)
 * - Smooth scrolling
 * - Search bar with auto-scroll to found settings
 */
class SettingsContent : public HomePageContent {
    Q_OBJECT

public:
    explicit SettingsContent(QWidget* parent = nullptr);
    ~SettingsContent() override = default;

    QString title() const override { return tr("Settings"); }

    /// Get theme selector widget for external connections
    ThemeSelectorWidget* themeSelectorWidget() const { return m_themeSelector; }

signals:
    void customThemesRequested();
    void shortcutManagerRequested();

protected:
    void setupContent() override;
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void updateScaledSizes();
    void createUpdatesCategory();
    void createAppearanceCategory();
    void createEditorCategory();
    void createShortcutsCategory();
    void createPerformanceCategory();
    void updateThemeColors();
    void onSearchTextChanged(const QString& text);
    void scrollToSetting(QWidget* settingWidget);
    void loadSettings();
    void applyUpdateCheckResult(bool hasUpdate, const QString& versionInfo);
    void finishPendingUpdateRecheck();

private slots:
    void onResetSettingsClicked();
    void onThemeChanged();

private:
    QVector<SettingsCategory*> m_categories;
    ThemeSelectorWidget* m_themeSelector { nullptr };
    WelcomeBannerSelectorWidget* m_welcomeBannerSelector { nullptr };
    UpdatesSettingsWidget* m_updatesSettingsWidget { nullptr };
    ShortcutsNavigatorWidget* m_shortcutsNavigator { nullptr };
    QLabel* m_titleLabel { nullptr };
    SmoothScrollArea* m_scrollArea { nullptr };
    QWidget* m_scrollContent { nullptr };
    SearchBar* m_searchBar { nullptr };
    QVBoxLayout* m_mainLayout { nullptr };
    QHBoxLayout* m_headerLayout { nullptr };
    QVBoxLayout* m_scrollLayout { nullptr };

    // Settings widgets for persistence
    SettingsChoice* m_uiScaleChoice { nullptr };
    SettingsComboBox* m_languageChoice { nullptr };
    SettingsChoice* m_topBarTabAlignmentChoice { nullptr };
    SettingsChoice* m_autoSaveChoice { nullptr };
    SettingsToggle* m_quickshapesToggle { nullptr };
    SettingsChoice* m_undoMemoryChoice { nullptr };
    SettingsChoice* m_tabletBackendChoice { nullptr };

    BaseAnimatedButton* m_resetButton { nullptr };
    bool m_updateRecheckInProgress { false };
    bool m_updateRecheckDelayElapsed { false };
    bool m_updateRecheckResultReady { false };
    bool m_pendingUpdateHasUpdate { false };
    QString m_pendingUpdateVersionInfo;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCONTENT_H
