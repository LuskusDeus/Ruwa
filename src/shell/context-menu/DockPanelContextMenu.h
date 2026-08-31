// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_DOCKPANELCONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_DOCKPANELCONTEXTMENU_H

#include "shell/context-menu/BaseContextMenu.h"
#include "features/layers/ui/LayersPanel.h"
#include "shared/resources/IconProvider.h"
#include "shared/types/ToolId.h"

#include <QPointer>
#include <QVector>

class QLabel;

namespace ruwa::ui::docking {
class DockPanel;
}

namespace ruwa::ui::workspace {
class ToolsPanel;
class LayersPanel;
class BrushesPanel;
} // namespace ruwa::ui::workspace

namespace ruwa::ui::widgets {

class HorizontalSeparator;
class ToggleSwitch;
class ProgressHandleSlider;
class SegmentedOptionSelector;

class DockPanelContextMenu : public StandardContextMenu {
    Q_OBJECT

public:
    explicit DockPanelContextMenu(QWidget* parent = nullptr);
    ContextMenuType menuType() const override { return ContextMenuType::DockPanelTitle; }

protected:
    void rebuildStandardMenu() override;
    QSize expandMenuContentHint(const QSize& hint) const override;

private:
    void buildUi();
    void applyChrome();
    void updateToggleRowsChrome();
    void updateControlColumns();
    QWidget* createToggleRow(QWidget* parent, ruwa::ui::core::IconProvider::StandardIcon iconKind,
        const QString& text, ToggleSwitch*& outToggle);

    struct BehaviorToggleRowDesc {
        QWidget* rowWidget = nullptr;
        QLabel* iconLabel = nullptr;
        QLabel* textLabel = nullptr;
        ToggleSwitch* toggle = nullptr;
        ruwa::ui::core::IconProvider::StandardIcon iconKind
            = ruwa::ui::core::IconProvider::StandardIcon::Move;
    };

    struct ToolToggleDesc {
        ruwa::ui::workspace::ToolId tool = ruwa::ui::workspace::ToolId::Hand;
        ToggleSwitch* toggle = nullptr;
    };

    struct LayerButtonToggleDesc {
        ruwa::ui::workspace::LayersPanel::ToolbarItem item
            = ruwa::ui::workspace::LayersPanel::ToolbarItem::AddLayer;
        ToggleSwitch* toggle = nullptr;
    };

private:
    QPointer<ruwa::ui::docking::DockPanel> m_panel;
    QPointer<ruwa::ui::workspace::ToolsPanel> m_toolsPanel;
    QPointer<ruwa::ui::workspace::LayersPanel> m_layersPanel;
    QPointer<ruwa::ui::workspace::BrushesPanel> m_brushesPanel;
    ProgressHandleSlider* m_hudSizeSlider = nullptr;
    QLabel* m_hudSizeLabel = nullptr;
    SegmentedOptionSelector* m_brushViewSelector = nullptr;
    QLabel* m_brushViewLabel = nullptr;
    SegmentedOptionSelector* m_packOrientationSelector = nullptr;
    QLabel* m_packOrientationLabel = nullptr;
    QWidget* m_brushesSectionHost = nullptr;
    HorizontalSeparator* m_sepBeforeBrushes = nullptr;
    ToggleSwitch* m_movableToggle = nullptr;
    ToggleSwitch* m_resizableToggle = nullptr;
    ToggleSwitch* m_dockableToggle = nullptr;

    QLabel* m_sectionLabel = nullptr;
    QLabel* m_toolsSectionLabel = nullptr;
    QLabel* m_layerButtonsSectionLabel = nullptr;
    QVector<BehaviorToggleRowDesc> m_toggleRows;
    QVector<ToolToggleDesc> m_toolToggles;
    QVector<LayerButtonToggleDesc> m_layerButtonToggles;
    QWidget* m_toolsSectionHost = nullptr;
    QWidget* m_layerButtonsSectionHost = nullptr;
    HorizontalSeparator* m_sepBeforeFloat = nullptr;
    HorizontalSeparator* m_sepBeforeClose = nullptr;
    HorizontalSeparator* m_sepBeforeTools = nullptr;
    HorizontalSeparator* m_sepBeforeLayerButtons = nullptr;
    BaseStyledWidget* m_floatAction = nullptr;
    BaseStyledWidget* m_ungroupAction = nullptr;
    BaseStyledWidget* m_closeAction = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_DOCKPANELCONTEXTMENU_H
