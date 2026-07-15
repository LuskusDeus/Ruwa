// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   CONTEXT MENU SYSTEM
// ======================================================================================

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_CONTEXTMENUSYSTEM_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_CONTEXTMENUSYSTEM_H

#include "ContextMenuTypes.h"
#include <QObject>
#include <QPoint>
#include <QVector>
#include <QVariantMap>
#include <QPointer>

class QWidget;

namespace ruwa::ui::widgets {

class BaseContextMenu;

/**
 * @brief Central system for managing context menus
 *
 * Features:
 * - Event filter to detect right-clicks
 * - Automatic detection of IContextMenuProvider widgets
 * - Factory for creating appropriate menu widgets
 * - Single active menu at a time
 */
class ContextMenuSystem : public QObject {
    Q_OBJECT

public:
    static ContextMenuSystem& instance();

    /**
     * @brief Install event filter on a widget tree
     * @param rootWidget Root widget to monitor (e.g., MainWindow's central widget)
     */
    void installOn(QWidget* rootWidget);

    /**
     * @brief Show a context menu at the specified position
     * @param type Type of menu to show
     * @param globalPos Global screen position
     * @param context Context data for the menu
     * @param sourceWidget Widget that triggered the menu (for positioning)
     */
    void showContextMenu(ContextMenuType type, const QPoint& globalPos,
        const QVariantMap& context = {}, QWidget* sourceWidget = nullptr);

    /**
     * @brief Hide the active context menu
     */
    void hideContextMenu();

    /**
     * @brief Hide the active context menu using its closing animation
     */
    void hideContextMenuAnimated();

    /**
     * @brief Check if a context menu is currently active
     */
    bool isMenuActive() const;

    /**
     * @brief True if a menu is currently open (and not animating closed) that was
     * triggered by \p sourceWidget. Used by button-triggered menus to toggle
     * themselves closed on a repeat click.
     */
    bool isMenuActiveFor(QWidget* sourceWidget) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit ContextMenuSystem(QObject* parent = nullptr);
    ~ContextMenuSystem() override = default;

    // Non-copyable
    ContextMenuSystem(const ContextMenuSystem&) = delete;
    ContextMenuSystem& operator=(const ContextMenuSystem&) = delete;

    BaseContextMenu* createMenu(ContextMenuType type, QWidget* rootWidget);
    QWidget* findContextMenuProvider(QWidget* widget) const;
    QWidget* findRegisteredRoot(QWidget* widget) const;
    void pruneDeadRoots();
    static bool isInsideContextMenuShell(QWidget* widget);

private:
    QVector<QPointer<QWidget>> m_rootWidgets;
    QPointer<BaseContextMenu> m_activeMenu;
    QPointer<QWidget> m_activeSourceWidget; // Widget that triggered the active menu
    QPoint m_lastMenuPosition; // Last position where menu was shown
    bool m_appEventFilterInstalled = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_CONTEXTMENUSYSTEM_H
