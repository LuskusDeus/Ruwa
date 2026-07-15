// SPDX-License-Identifier: MPL-2.0

// ContextMenuCoordinator.cpp
#include "ContextMenuCoordinator.h"

#include <QAction>
namespace ruwa::ui::windows {

ContextMenuCoordinator::ContextMenuCoordinator(QObject* parent)
    : QObject(parent)
{
}

void ContextMenuCoordinator::showContextMenu(
    ContextMenuType type, const QPoint& globalPos, const QVariantMap& context)
{
    QMenu* menu = nullptr;

    switch (type) {
    case ContextMenuType::TabBar:
        menu = createTabBarMenu(context);
        break;
    case ContextMenuType::LayerItem:
        menu = createLayerItemMenu(context);
        break;
    case ContextMenuType::Canvas:
        menu = createCanvasMenu(context);
        break;
    case ContextMenuType::Sidebar:
        menu = createSidebarMenu(context);
        break;
    case ContextMenuType::Generic:
        menu = new QMenu();
        menu->addAction("Context Menu Placeholder");
        break;
    }

    if (menu) {
        menu->exec(globalPos);
        menu->deleteLater();
    }
}

QMenu* ContextMenuCoordinator::createTabBarMenu(const QVariantMap& context)
{
    Q_UNUSED(context);

    auto* menu = new QMenu();
    menu->addAction("Close Tab");
    menu->addAction("Close Other Tabs");
    menu->addAction("Close All Tabs");
    menu->addSeparator();
    menu->addAction("Duplicate Tab");

    return menu;
}

QMenu* ContextMenuCoordinator::createLayerItemMenu(const QVariantMap& context)
{
    Q_UNUSED(context);

    auto* menu = new QMenu();
    menu->addAction("Duplicate Layer");
    menu->addAction("Delete Layer");
    menu->addSeparator();
    menu->addAction("Merge Down");
    menu->addAction("Flatten Image");

    return menu;
}

QMenu* ContextMenuCoordinator::createCanvasMenu(const QVariantMap& context)
{
    Q_UNUSED(context);

    auto* menu = new QMenu();
    menu->addAction("Paste");
    menu->addAction("Select All");
    menu->addSeparator();
    menu->addAction("Fill with Foreground Color");
    menu->addAction("Clear Selection");

    return menu;
}

QMenu* ContextMenuCoordinator::createSidebarMenu(const QVariantMap& context)
{
    Q_UNUSED(context);

    auto* menu = new QMenu();
    menu->addAction("Add to Favorites");
    menu->addAction("Remove from Favorites");

    return menu;
}

} // namespace ruwa::ui::windows
