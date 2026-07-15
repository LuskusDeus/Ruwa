// SPDX-License-Identifier: MPL-2.0

// ContextMenuCoordinator.h
#ifndef RUWA_UI_WINDOWS_MAINWINDOW_CONTEXTMENUCOORDINATOR_H
#define RUWA_UI_WINDOWS_MAINWINDOW_CONTEXTMENUCOORDINATOR_H

#include <QObject>
#include <QMenu>
#include <QPoint>
#include <QString>

namespace ruwa::ui::windows {

enum class ContextMenuType { TabBar, LayerItem, Canvas, Sidebar, Generic };

class ContextMenuCoordinator : public QObject {
    Q_OBJECT

public:
    explicit ContextMenuCoordinator(QObject* parent = nullptr);

    void showContextMenu(
        ContextMenuType type, const QPoint& globalPos, const QVariantMap& context = {});

private:
    QMenu* createTabBarMenu(const QVariantMap& context);
    QMenu* createLayerItemMenu(const QVariantMap& context);
    QMenu* createCanvasMenu(const QVariantMap& context);
    QMenu* createSidebarMenu(const QVariantMap& context);
};

} // namespace ruwa::ui::windows

#endif
