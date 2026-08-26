// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_TABS_THEMEEDITORTAB_H
#define RUWA_UI_TABS_THEMEEDITORTAB_H

#include "features/theme/manager/ThemePreset.h"
#include "shell/tab-system/BaseTab.h"

#include <QColor>
#include <QVector>
#include <QUuid>
#include <array>
#include <cstddef>
#include <memory>

class QButtonGroup;
class QEvent;
class QLabel;
class QPaintEvent;
class QPushButton;
class QResizeEvent;

namespace ruwa::ui::widgets {
class AnimatedStackedWidget;
class CapsuleButton;
class ColorInputButton;
class FontDropdownSelector;
class NumericInputField;
class PropertyRowLayout;
class StyledInputField;
class ThemeEditorAnimationsPreview;
class ThemeEditorSidebar;
class ThemeEditorThemesPreview;
class ToggleSwitch;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::tabs {

/**
 * @brief Shell for the multi-section appearance editor.
 *
 * The tab owns a context-sensitive preview area above a lower settings area.
 * Each editor section owns its own preview page; implementations can therefore
 * evolve independently without coupling their scene contents.
 */
class ThemeEditorTab final : public ruwa::core::BaseTab {
    Q_OBJECT

public:
    explicit ThemeEditorTab(QWidget* parent = nullptr);
    ~ThemeEditorTab() override;

    ruwa::core::BaseTab::TabType type() const override { return TabType::Custom; }
    QString title() const override { return tr("Theme Editor"); }
    QString tabKindLabel() const override { return tr("Theme Editor"); }

    // Kept for the existing Settings -> editor synchronization contract.
    void selectThemeById(const QUuid& id);

signals:
    void colorPickerRequested(
        const QColor& initialColor, ruwa::ui::widgets::ColorInputButton* button);
    void themeApplied(const QUuid& themeId);

protected:
    void onInitialize() override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class SettingsPage { ThemeColors, ThemeFont, Animations, Count };

    enum class ColorField {
        Primary,
        Accent,
        Background,
        Surface,
        SurfaceAlt,
        Border,
        Overlay,
        Text,
        TextMuted,
        TextOnPrimary,
        Success,
        Warning,
        Error,
        Info,
        Count
    };

    struct SettingsSectionDefinition {
        bool hasSubTabs { false };
        QVector<SettingsPage> pages;
    };

    struct SettingsSectionUi {
        QWidget* container { nullptr };
        QWidget* headerRow { nullptr };
        QWidget* tabsBar { nullptr };
        QButtonGroup* tabGroup { nullptr };
        ruwa::ui::widgets::AnimatedStackedWidget* contentStack { nullptr };
        QVector<ruwa::ui::widgets::CapsuleButton*> tabButtons;
        QPushButton* favoriteButton { nullptr };
        ruwa::ui::widgets::StyledInputField* nameInput { nullptr };
        ruwa::ui::widgets::CapsuleButton* applyButton { nullptr };
        ruwa::ui::widgets::CapsuleButton* saveButton { nullptr };
        QVector<SettingsPage> pages;
    };

    static constexpr std::size_t SectionCount = 2;
    static constexpr std::size_t SettingsPageCount = static_cast<std::size_t>(SettingsPage::Count);
    static constexpr std::size_t ColorFieldCount = static_cast<std::size_t>(ColorField::Count);
    static constexpr std::size_t ColorCategoryCount = 3;
    static constexpr std::size_t FontFamilyCount = 2;
    static constexpr std::size_t FontSizeFieldCount = 16;
    static constexpr std::size_t FontCategoryCount = 3;
    static constexpr std::size_t AnimationCategoryCount = 2;
    static constexpr std::size_t AnimationSettingCount = 3;

    void setupUi();
    QWidget* createThemesPreviewPage(QWidget* parent);
    QWidget* createAnimationsPreviewPage(QWidget* parent);
    QWidget* createSettingsSection(
        const SettingsSectionDefinition& definition, std::size_t sectionIndex, QWidget* parent);
    QWidget* createSettingsPlaceholder(SettingsPage settingsPage, QWidget* parent);
    QWidget* createColorsSettingsPage(QWidget* parent);
    QWidget* createFontSettingsPage(QWidget* parent);
    QWidget* createAnimationsSettingsPage(QWidget* parent);
    QWidget* createColorCategory(
        const QVector<ColorField>& fields, std::size_t categoryIndex, QWidget* parent);
    ruwa::ui::widgets::ColorInputButton* createColorInput(ColorField field, QWidget* parent);
    QWidget* createFontCategory(std::size_t categoryIndex, QWidget* parent);
    QWidget* createAnimationCategory(std::size_t categoryIndex, QWidget* parent);
    void alignAnimationPropertyColumns();
    ruwa::ui::widgets::FontDropdownSelector* createFontFamilyInput(
        std::size_t familyIndex, QWidget* parent);
    ruwa::ui::widgets::NumericInputField* createFontSizeInput(
        std::size_t sizeIndex, QWidget* parent);
    std::array<SettingsSectionDefinition, SectionCount> settingsSectionDefinitions() const;
    QString settingsPageTitle(SettingsPage settingsPage) const;
    QString settingsPageDescription(SettingsPage settingsPage) const;
    QString saveButtonText() const;
    QString colorFieldLabel(ColorField field) const;
    QString fontFamilyLabel(std::size_t familyIndex) const;
    QString fontSizeFieldLabel(std::size_t sizeIndex) const;
    QString animationCategoryTitle(std::size_t categoryIndex) const;
    QString animationSettingLabel(std::size_t settingIndex) const;
    QColor& editingColor(ColorField field);
    const QColor& savedColor(ColorField field) const;
    void syncColorInputs();
    void syncFontInputs();
    void syncAnimationInputs();
    void syncThemeIdentityInputs();
    void refreshPreviews();
    void updateDirtyState();
    void setDirtyState(bool dirty);
    void applyEditingTheme();
    void saveEditingTheme();
    void retranslateUi();
    void updateContentSideMargins();
    void updateScaledSizes();
    void updateThemeColors();

    ruwa::ui::widgets::AnimatedStackedWidget* m_previewStack { nullptr };
    ruwa::ui::widgets::ThemeEditorThemesPreview* m_themesPreview { nullptr };
    ruwa::ui::widgets::ThemeEditorAnimationsPreview* m_animationsPreview { nullptr };
    QWidget* m_settingsFrame { nullptr };
    ruwa::ui::widgets::ThemeEditorSidebar* m_sidebar { nullptr };
    ruwa::ui::widgets::AnimatedStackedWidget* m_settingsStack { nullptr };
    std::array<SettingsSectionUi, SectionCount> m_settingsSections {};
    std::array<QLabel*, SettingsPageCount> m_settingsTitles {};
    std::array<QLabel*, SettingsPageCount> m_settingsDescriptions {};
    std::array<QLabel*, ColorCategoryCount> m_colorCategoryTitles {};
    std::array<ruwa::ui::widgets::ColorInputButton*, ColorFieldCount> m_colorInputs {};
    std::array<QLabel*, FontCategoryCount> m_fontCategoryTitles {};
    std::array<QLabel*, FontFamilyCount> m_fontFamilyLabels {};
    std::array<QLabel*, FontSizeFieldCount> m_fontSizeLabels {};
    std::array<ruwa::ui::widgets::FontDropdownSelector*, FontFamilyCount> m_fontFamilyInputs {};
    std::array<ruwa::ui::widgets::NumericInputField*, FontSizeFieldCount> m_fontSizeInputs {};
    std::array<std::unique_ptr<ruwa::ui::widgets::PropertyRowLayout>, FontCategoryCount>
        m_fontPropertyLayouts;
    std::array<QLabel*, AnimationCategoryCount> m_animationCategoryTitles {};
    std::array<QLabel*, AnimationSettingCount> m_animationSettingLabels {};
    std::array<std::unique_ptr<ruwa::ui::widgets::PropertyRowLayout>, AnimationCategoryCount>
        m_animationPropertyLayouts;
    ruwa::ui::widgets::ToggleSwitch* m_animationsToggle { nullptr };
    ruwa::ui::widgets::ToggleSwitch* m_canvasAnimationsToggle { nullptr };
    ruwa::ui::widgets::NumericInputField* m_animationSpeedInput { nullptr };
    std::array<QString, FontFamilyCount> m_fontPreviewOriginalFamilies;
    ruwa::ui::core::ThemePreset m_editingTheme;
    ruwa::ui::core::ThemePreset m_savedTheme;
    QUuid m_pendingThemeId;
    bool m_syncingColorInputs { false };
    bool m_syncingFontInputs { false };
    bool m_syncingAnimationInputs { false };
    bool m_syncingThemeIdentityInputs { false };
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_THEMEEDITORTAB_H
