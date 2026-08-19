// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_TABS_THEMEEDITORTAB_H
#define RUWA_UI_TABS_THEMEEDITORTAB_H

#include "features/theme/manager/ThemePreset.h"
#include "shell/tab-system/BaseTab.h"

#include <QColor>
#include <QUuid>
#include <array>

class QEvent;
class QLabel;
class QPaintEvent;
class QResizeEvent;

namespace ruwa::ui::widgets {
class AnimatedStackedWidget;
class ColorInputButton;
class ThemeEditorSidebar;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::tabs {

/**
 * @brief Shell for the multi-section appearance editor.
 *
 * The tab owns a context-sensitive preview area above a lower settings area.
 * Individual preview and settings pages are intentionally placeholders for now.
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
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    QWidget* createPreviewPlaceholder(QWidget* parent);
    QWidget* createSettingsPlaceholder(QWidget* parent);
    void retranslateUi();
    void updateContentSideMargins();
    void updateScaledSizes();
    void updateThemeColors();

    ruwa::ui::widgets::AnimatedStackedWidget* m_previewStack { nullptr };
    QWidget* m_sectionDivider { nullptr };
    QWidget* m_settingsFrame { nullptr };
    ruwa::ui::widgets::ThemeEditorSidebar* m_sidebar { nullptr };
    ruwa::ui::widgets::AnimatedStackedWidget* m_settingsStack { nullptr };
    std::array<QLabel*, 3> m_previewTitles {};
    std::array<QLabel*, 3> m_previewDescriptions {};
    std::array<QLabel*, 3> m_settingsTitles {};
    std::array<QLabel*, 3> m_settingsDescriptions {};
    ruwa::ui::core::ThemePreset m_editingTheme;
    QUuid m_pendingThemeId;
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_THEMEEDITORTAB_H
