// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_PANELS_LAYERPROPERTIESPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_LAYERPROPERTIESPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include <QColor>

namespace ruwa::core::layers {
class LayerModel;
}

namespace ruwa::ui::widgets {
class ColorInputButton;
class ToggleSwitch;
} // namespace ruwa::ui::widgets

class QLabel;
class QWidget;

namespace ruwa::ui::workspace {

class LayerPropertiesPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit LayerPropertiesPanel(QWidget* parent = nullptr);
    ~LayerPropertiesPanel() override;
    void setLayerModel(ruwa::core::layers::LayerModel* model);

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;

private slots:
    void refreshUi();
    void onBackgroundColorRequested(const QColor& initialColor);
    void onTransparentToggled(bool checked);

signals:
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);

private:
    void applyTheme();

private:
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    QWidget* m_contentWidget = nullptr;
    QLabel* m_hintLabel = nullptr;
    QWidget* m_backgroundSection = nullptr;
    ruwa::ui::widgets::ColorInputButton* m_backgroundColorInput = nullptr;
    ruwa::ui::widgets::ToggleSwitch* m_transparentSwitch = nullptr;
    bool m_syncingUi = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_LAYERPROPERTIESPANEL_H
