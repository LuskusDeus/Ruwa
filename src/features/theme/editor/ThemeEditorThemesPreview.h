// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_THEMEEDITORTHEMESPREVIEW_H
#define RUWA_UI_WIDGETS_THEMEEDITORTHEMESPREVIEW_H

#include "features/theme/manager/ThemeColors.h"

#include <QPixmap>
#include <QRect>
#include <QWidget>

class QEvent;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

namespace ruwa::ui::docking {
class DockGroupHeader;
}

namespace ruwa::ui::core {
struct ThemePreset;
}

namespace ruwa::ui::workspace {
class LayerEffectsPanel;
class LayersPanel;
} // namespace ruwa::ui::workspace

namespace ruwa::ui::widgets {

class FontDropdownSelector;
class SettingsChoice;
class SettingsComboBox;
class SettingsToggle;
class WelcomeBannerButton;

/**
 * @brief Passive Themes-page preview built from the real workspace widgets.
 *
 * The dock group is deliberately larger than the banner. QWidget's native
 * child clipping cuts its right and bottom sides at the banner boundary.
 */
class ThemeEditorThemesPreview final : public QWidget {
    Q_OBJECT

public:
    explicit ThemeEditorThemesPreview(QWidget* parent = nullptr);
    ~ThemeEditorThemesPreview() override;

    /// Re-render immediately from the editor's working copy, without applying it globally.
    void setPreset(const ruwa::ui::core::ThemePreset& preset);

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupLeftPreview();
    void setupWidgetExamples();
    void setupDockPreview();
    void populatePreviewLayers();
    void retranslatePreview();
    void updatePreviewGeometry();
    void updateTheme();
    void rebuildSnapshot();
    void makePreviewPassive();

private:
    QWidget* m_leftContent { nullptr };
    QLabel* m_titleLabel { nullptr };
    QLabel* m_subtitleLabel { nullptr };
    WelcomeBannerButton* m_primaryButton { nullptr };
    WelcomeBannerButton* m_secondaryButton { nullptr };
    QWidget* m_widgetExamples { nullptr };
    FontDropdownSelector* m_fontDropdown { nullptr };
    SettingsToggle* m_toggleSetting { nullptr };
    SettingsChoice* m_switcherSetting { nullptr };
    SettingsComboBox* m_dropdownSetting { nullptr };
    ruwa::ui::docking::DockGroupHeader* m_groupHeader { nullptr };
    ruwa::ui::workspace::LayersPanel* m_layersPanel { nullptr };
    ruwa::ui::workspace::LayerEffectsPanel* m_layerEffectsPanel { nullptr };
    ruwa::ui::core::ThemeColors m_previewColors;
    QPixmap m_snapshot;
    QRect m_leftContentTarget;
    QRect m_widgetExamplesTarget;
    QRect m_groupHeaderTarget;
    QRect m_layersPanelTarget;
    bool m_snapshotDirty { true };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_THEMEEDITORTHEMESPREVIEW_H
