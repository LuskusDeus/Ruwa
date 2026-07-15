// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_DOCKPANELCONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_DOCKPANELCONTEXTMENU_H

#include "shell/context-menu/BaseContextMenu.h"
#include "shared/resources/IconProvider.h"

#include <QPointer>
#include <QVector>
#include <QVBoxLayout>

class QLabel;

namespace ruwa::ui::docking {
class DockPanel;
}

namespace ruwa::ui::widgets {

class HorizontalSeparator;
class ToggleSwitch;

class DockPanelContextMenu : public StandardContextMenu {
    Q_OBJECT

public:
    explicit DockPanelContextMenu(QWidget* parent = nullptr);
    ContextMenuType menuType() const override { return ContextMenuType::DockPanelTitle; }

protected:
    void rebuildStandardMenu() override;

private:
    void buildUi();
    void applyChrome();
    void updateBehaviorToggleRowsChrome();
    QWidget* addBehaviorToggleRow(QVBoxLayout* column,
        ruwa::ui::core::IconProvider::StandardIcon iconKind, const QString& text,
        ToggleSwitch*& outToggle);

    struct BehaviorToggleRowDesc {
        QWidget* rowWidget = nullptr;
        QLabel* iconLabel = nullptr;
        QLabel* textLabel = nullptr;
        ToggleSwitch* toggle = nullptr;
        ruwa::ui::core::IconProvider::StandardIcon iconKind
            = ruwa::ui::core::IconProvider::StandardIcon::Move;
    };

private:
    QPointer<ruwa::ui::docking::DockPanel> m_panel;
    ToggleSwitch* m_movableToggle = nullptr;
    ToggleSwitch* m_resizableToggle = nullptr;
    ToggleSwitch* m_dockableToggle = nullptr;

    QLabel* m_sectionLabel = nullptr;
    QVector<BehaviorToggleRowDesc> m_behaviorToggleRows;
    HorizontalSeparator* m_sepBeforeFloat = nullptr;
    HorizontalSeparator* m_sepBeforeClose = nullptr;
    BaseStyledWidget* m_floatAction = nullptr;
    BaseStyledWidget* m_closeAction = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_DOCKPANELCONTEXTMENU_H
