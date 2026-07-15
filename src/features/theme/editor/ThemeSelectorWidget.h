// SPDX-License-Identifier: MPL-2.0

// ThemeSelectorWidget.h
#ifndef RUWA_UI_WIDGETS_SETTINGS_THEMESELECTORWIDGET_H
#define RUWA_UI_WIDGETS_SETTINGS_THEMESELECTORWIDGET_H

#include "features/settings/BaseSettingsWidget.h"
#include "features/theme/manager/ThemePreset.h"

#include <QVector>

class QEvent;
class QHBoxLayout;
class QLabel;

namespace ruwa::ui::widgets {

class ThemePreviewWidget;
class CustomThemesNavigatorWidget;

class ThemeSelectorWidget : public BaseSettingsWidget {
    Q_OBJECT

public:
    /// When \p showCustomThemesEntry is false, only favorite presets are shown (e.g. first-run
    /// setup).
    explicit ThemeSelectorWidget(QWidget* parent = nullptr, bool showCustomThemesEntry = true);
    ~ThemeSelectorWidget() override = default;

    void addCustomTheme(const ruwa::ui::core::ThemePreset& preset);
    void removeCustomTheme(const QUuid& id);
    void reloadThemes(); // Reload all themes from ThemeManager

    const ruwa::ui::core::ThemePreset* selectedTheme() const;
    void setSelectedTheme(const QUuid& id);
    void updateFromThemeEditor(
        const QUuid& appliedThemeId); // Update when theme is applied in editor

signals:
    void themeSelected(const ruwa::ui::core::ThemePreset& preset);
    void customThemesRequested();

protected:
    void setupContent() override;
    void updateThemeColors() override;
    void changeEvent(QEvent* event) override;

public:
    void retranslateUi();

private:
    void updateScaledSizes();
    void rebuildPreviews();

private:
    QVector<ruwa::ui::core::ThemePreset> m_themes;
    QVector<ThemePreviewWidget*> m_previews;
    QUuid m_selectedId;
    bool m_showCustomThemesEntry { true };
    QWidget* m_previewContainer { nullptr };
    QHBoxLayout* m_previewLayout { nullptr };
    QLabel* m_separatorLabel { nullptr };
    CustomThemesNavigatorWidget* m_customNavigator { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SETTINGS_THEMESELECTORWIDGET_H
