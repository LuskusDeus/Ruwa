// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_TABS_CONTENT_THEMEEDITORTAB_H
#define RUWA_UI_TABS_CONTENT_THEMEEDITORTAB_H

#include "shell/tab-system/BaseTab.h"
#include "features/theme/manager/ThemePreset.h"

#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <array>
#include <cstddef>

#include <QPaintEvent>
#include <QResizeEvent>

namespace ruwa::ui::widgets {
class CapsuleButton;
class DetailedThemePreview;
class ColorInputButton;
class SmoothScrollArea;
class PresetMenuListWidget;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::tabs {

/**
 * @brief Theme editor tab for creating and editing color themes
 *
 * Features:
 * - List of built-in and custom themes
 * - Live preview of theme changes
 * - Color property editing with ColorInputButton
 * - Proper theme system integration
 */
class ThemeEditorTab : public ruwa::core::BaseTab {
    Q_OBJECT

public:
    explicit ThemeEditorTab(QWidget* parent = nullptr);
    ~ThemeEditorTab() override;

    // BaseTab implementation
    ruwa::core::BaseTab::TabType type() const override { return TabType::Custom; }
    QString title() const override { return tr("Theme Editor"); }
    QString tabKindLabel() const override { return tr("Theme Editor"); }

    // This tab uses explicit palettes / colours cached at construction in many
    // sub-widgets, so a repolish isn't enough — it fully rebuilds itself on a
    // theme change (behind the loading overlay).
    bool wantsThemeLoadingScreen() const override { return true; }

    // === Public API for external control ===

    /// Select theme by ID (can be called from MainWindow for synchronization)
    void selectThemeById(const QUuid& id) { selectTheme(id); }

protected:
    void onInitialize() override;
    void onApplyThemeRefresh(std::function<void()> finished, bool showLoading) override;
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Internal ctor preserving the tab id across a theme-refresh rebuild.
    explicit ThemeEditorTab(const QUuid& id, QWidget* parent);

signals:
    void colorPickerRequested(
        const QColor& initialColor, ruwa::ui::widgets::ColorInputButton* button);

    /// Emitted when theme is applied (for ThemeSelectorWidget sync)
    void themeApplied(const QUuid& themeId);

private:
    // === Color field identifiers ===
    enum class ColorField {
        Primary,
        Background,
        Surface,
        SurfaceAlt,
        Border,
        Text,
        TextMuted,
        TextOnPrimary,
        Success,
        Warning,
        Error,
        Info,

        Count // Must be last
    };

    static constexpr size_t ColorFieldCount = static_cast<size_t>(ColorField::Count);

    // === UI Construction ===
    void createLayout();
    void setupThemeListPanel(QWidget* container);
    void setupColorEditorPanel(QWidget* container);
    void setupPreviewPanel(QWidget* container);

    // === Scaling & Theming ===
    void updateScaledSizes();
    void updateThemeColors();

    // === Logic ===
    void loadThemesList();
    void exportCurrentThemePreset();
    void importThemePreset();
    void retranslateUi();
    void selectTheme(const QUuid& id);
    void createNewTheme();
    void saveCurrentTheme();
    void deleteCurrentTheme();

    // === Color field helpers ===
    void createColorFields();
    ruwa::ui::widgets::ColorInputButton* createColorField(
        ColorField field, const QString& label, QWidget* parent);
    void updateColorFieldsFromTheme();
    void onColorFieldChanged(ColorField field, const QColor& color);
    QColor& getThemeColorRef(ColorField field);

private slots:
    void onThemeChanged();

private:
    // === Data ===
    ruwa::ui::core::ThemePreset m_currentEditingTheme;
    bool m_isInternalChange { false };

    // === Widgets ===
    QWidget* m_sidebarContainer { nullptr };
    QWidget* m_propertiesContainer { nullptr };
    QWidget* m_previewContainer { nullptr };

    QLabel* m_sidebarHeader { nullptr };
    QLabel* m_propertiesHeader { nullptr };
    QLabel* m_previewHeader { nullptr };

    QPushButton* m_addBtn { nullptr };
    ruwa::ui::widgets::CapsuleButton* m_saveBtn { nullptr };

    ruwa::ui::widgets::PresetMenuListWidget* m_themePresetList { nullptr };
    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea { nullptr };
    QWidget* m_scrollContent { nullptr };
    ruwa::ui::widgets::DetailedThemePreview* m_preview { nullptr };

    // === Color inputs (indexed by ColorField enum) ===
    std::array<ruwa::ui::widgets::ColorInputButton*, ColorFieldCount> m_colorInputs {};

    // === Category labels for styling ===
    QVector<QLabel*> m_categoryLabels;
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_CONTENT_THEMEEDITORTAB_H
