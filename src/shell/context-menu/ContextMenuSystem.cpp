// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   CONTEXT MENU SYSTEM - IMPLEMENTATION
// ======================================================================================

#include "ContextMenuSystem.h"
#include "IContextMenuProvider.h"
#include "shell/context-menu/BaseContextMenu.h"
#include "shell/context-menu/NoActionsContextMenu.h"
#include "shell/context-menu/TabContextMenu.h"
#include "shell/context-menu/DockPanelContextMenu.h"
#include "shell/context-menu/SimpleActionsContextMenu.h"
#include "shell/tab-system/CustomTabBar.h"
#include "features/theme/editor/ThemePreviewWidget.h"
#include "features/home/welcome/WelcomeBannerPreviewWidget.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"
#include "features/layers/ui/LayerRowWidget.h"

#include <QWidget>
#include <QMouseEvent>
#include <QApplication>
#include <utility>

namespace ruwa::ui::widgets {

namespace {

constexpr const char* kContextMenuSystemBypassProperty = "ruwaContextMenuSystemBypass";

bool hasContextMenuSystemBypass(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (current->property(kContextMenuSystemBypassProperty).toBool()) {
            return true;
        }
    }
    return false;
}

} // namespace

ContextMenuSystem& ContextMenuSystem::instance()
{
    static ContextMenuSystem instance;
    return instance;
}

ContextMenuSystem::ContextMenuSystem(QObject* parent)
    : QObject(parent)
{
}

void ContextMenuSystem::installOn(QWidget* rootWidget)
{
    if (!rootWidget) {
        return;
    }

    pruneDeadRoots();

    for (const QPointer<QWidget>& existingRoot : std::as_const(m_rootWidgets)) {
        if (existingRoot == rootWidget) {
            return;
        }
    }

    m_rootWidgets.append(rootWidget);

    // Install once on the application: workspace tabs, dock panels, etc. are created after startup,
    // so a one-time recursive install on the central widget would miss most of the UI tree.
    if (!m_appEventFilterInstalled) {
        if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
            app->installEventFilter(this);
            m_appEventFilterInstalled = true;
        }
    }
}

bool ContextMenuSystem::isInsideContextMenuShell(QWidget* widget)
{
    for (QWidget* w = widget; w; w = w->parentWidget()) {
        if (qobject_cast<BaseContextMenu*>(w)) {
            return true;
        }
    }
    return false;
}

void ContextMenuSystem::showContextMenu(ContextMenuType type, const QPoint& globalPos,
    const QVariantMap& context, QWidget* sourceWidget)
{
    QWidget* menuRoot = findRegisteredRoot(sourceWidget);
    if (!menuRoot) {
        return;
    }

    if (m_activeMenu) { }

    // Check if we can reuse existing menu (only if it's truly active, not hiding).
    // SimpleActions is always recreated: its actions are wired to the specific source widget.
    if (m_activeMenu && m_activeMenu->menuType() == type && m_activeMenu->isActive()
        && type != ContextMenuType::SimpleActions) {
        // Reuse menu - showAt will handle context update and repositioning
        m_activeSourceWidget = sourceWidget;
        m_activeMenu->showAt(globalPos, context, sourceWidget);
        m_lastMenuPosition = globalPos; // Update position
        return;
    }

    // Force-delete existing menu immediately (even if it's animating)
    if (m_activeMenu) {
        m_activeMenu->hide(); // Stop all animations and remove event filter
        m_activeMenu->deleteLater();
        m_activeMenu = nullptr;
    }

    // Create new menu
    BaseContextMenu* menu = createMenu(type, menuRoot);
    if (!menu) {
        return;
    }

    m_activeMenu = menu;

    // Connect menu signals if it's a TabContextMenu
    if (type == ContextMenuType::TabBar) {
        if (auto* tabMenu = qobject_cast<TabContextMenu*>(menu)) {
            // Find the CustomTabBar that triggered this menu
            if (auto* tabBar = qobject_cast<ruwa::ui::tabs::CustomTabBar*>(sourceWidget)) {
                connect(tabMenu, &TabContextMenu::renameRequested, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onRenameRequested);
                connect(tabMenu, &TabContextMenu::changeIconRequested, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onChangeIconRequested);
                connect(tabMenu, &TabContextMenu::closeTabRequested, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onCloseTabRequested);
                connect(tabMenu, &TabContextMenu::closeOtherTabsRequested, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onCloseOtherTabsRequested);
                connect(tabMenu, &TabContextMenu::closeAllTabsRequested, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onCloseAllTabsRequested);
                connect(tabMenu, &TabContextMenu::tabRenamed, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onTabRenamed);
                connect(tabMenu, &TabContextMenu::tabIconChanged, tabBar,
                    &ruwa::ui::tabs::CustomTabBar::onTabIconChanged);
            }
        }
    }

    if (type == ContextMenuType::SimpleActions) {
        if (auto* simple = qobject_cast<SimpleActionsContextMenu*>(menu)) {
            if (auto* themePreview = qobject_cast<ThemePreviewWidget*>(sourceWidget)) {
                connect(simple, &SimpleActionsContextMenu::actionTriggered, themePreview,
                    &ThemePreviewWidget::onSimpleContextAction);
            } else if (auto* bannerPreview
                = qobject_cast<WelcomeBannerPreviewWidget*>(sourceWidget)) {
                connect(simple, &SimpleActionsContextMenu::actionTriggered, bannerPreview,
                    &WelcomeBannerPreviewWidget::onSimpleContextAction);
            } else if (auto* canvasPanel
                = qobject_cast<ruwa::ui::workspace::CanvasPanel*>(sourceWidget)) {
                connect(simple, &SimpleActionsContextMenu::actionTriggered, canvasPanel,
                    &ruwa::ui::workspace::CanvasPanel::onSimpleContextAction);
            } else if (qobject_cast<BrushControlOverlay*>(sourceWidget)
                || qobject_cast<CanvasToolStateOverlay*>(sourceWidget)
                || qobject_cast<CanvasStylusJoystickContainerWidget*>(sourceWidget)) {
                for (QWidget* w = sourceWidget; w; w = w->parentWidget()) {
                    if (auto* panel = qobject_cast<ruwa::ui::workspace::CanvasPanel*>(w)) {
                        connect(simple, &SimpleActionsContextMenu::actionTriggered, panel,
                            &ruwa::ui::workspace::CanvasPanel::onSimpleContextAction);
                        break;
                    }
                }
            } else if (auto* layerRow = qobject_cast<LayerRowWidget*>(sourceWidget)) {
                connect(simple, &SimpleActionsContextMenu::actionTriggered, layerRow,
                    &LayerRowWidget::onSimpleContextAction);
            } else if (auto* provider = dynamic_cast<IContextMenuProvider*>(sourceWidget)) {
                QPointer<QWidget> safeSourceWidget(sourceWidget);
                connect(simple, &SimpleActionsContextMenu::actionTriggered, menu,
                    [safeSourceWidget](int actionId) {
                        if (!safeSourceWidget) {
                            return;
                        }
                        if (auto* safeProvider
                            = dynamic_cast<IContextMenuProvider*>(safeSourceWidget.data())) {
                            safeProvider->handleContextMenuAction(actionId);
                        }
                    });
            }
        }
    }

    // Show menu (showAt will set context)
    m_activeSourceWidget = sourceWidget;
    menu->showAt(globalPos, context, sourceWidget);

    // Save position for toggle detection
    m_lastMenuPosition = globalPos;
}

void ContextMenuSystem::hideContextMenu()
{
    if (m_activeMenu) {
        m_activeMenu->hide();
        m_activeMenu->deleteLater();
        m_activeMenu = nullptr;
    }
    m_activeSourceWidget = nullptr;
}

bool ContextMenuSystem::isMenuActiveFor(QWidget* sourceWidget) const
{
    return m_activeMenu && m_activeMenu->isActive() && m_activeSourceWidget == sourceWidget;
}

void ContextMenuSystem::hideContextMenuAnimated()
{
    if (m_activeMenu && m_activeMenu->isActive()) {
        m_activeMenu->hideAnimated();
    }
}

bool ContextMenuSystem::isMenuActive() const
{
    return m_activeMenu && m_activeMenu->isVisible();
}

BaseContextMenu* ContextMenuSystem::createMenu(ContextMenuType type, QWidget* rootWidget)
{
    if (!rootWidget) {
        return nullptr;
    }

    switch (type) {
    case ContextMenuType::TabBar:
        return new TabContextMenu(rootWidget);
    case ContextMenuType::DockPanelTitle:
        return new DockPanelContextMenu(rootWidget);

    case ContextMenuType::SimpleActions:
        return new SimpleActionsContextMenu(rootWidget);

    case ContextMenuType::LayerItem:
    case ContextMenuType::Canvas:
    case ContextMenuType::Sidebar:
    case ContextMenuType::Generic:
        return new NoActionsContextMenu(rootWidget);

    case ContextMenuType::None:
    default:
        return nullptr;
    }
}

QWidget* ContextMenuSystem::findRegisteredRoot(QWidget* widget) const
{
    if (!widget) {
        return nullptr;
    }

    QWidget* bestMatch = nullptr;
    int bestDepth = -1;
    for (const QPointer<QWidget>& rootWidget : m_rootWidgets) {
        if (!rootWidget) {
            continue;
        }
        if (rootWidget == widget || rootWidget->isAncestorOf(widget)) {
            int depth = 0;
            for (QWidget* current = widget; current && current != rootWidget;
                current = current->parentWidget()) {
                ++depth;
            }
            if (bestDepth < 0 || depth < bestDepth) {
                bestDepth = depth;
                bestMatch = rootWidget;
            }
        }
    }
    return bestMatch;
}

void ContextMenuSystem::pruneDeadRoots()
{
    for (int i = m_rootWidgets.size() - 1; i >= 0; --i) {
        if (m_rootWidgets[i].isNull()) {
            m_rootWidgets.remove(i);
        }
    }
}

QWidget* ContextMenuSystem::findContextMenuProvider(QWidget* widget) const
{
    // Walk up the widget hierarchy looking for IContextMenuProvider
    QWidget* current = widget;
    int depth = 0;
    while (current) {
        if (auto* provider = dynamic_cast<IContextMenuProvider*>(current)) {
            ContextMenuType type = provider->contextMenuType();
            if (type != ContextMenuType::None) {
                return current;
            }
        }
        current = current->parentWidget();
        depth++;
    }

    return nullptr;
}

bool ContextMenuSystem::eventFilter(QObject* watched, QEvent* event)
{
    // Only process mouse button press events
    if (event->type() != QEvent::MouseButtonPress) {
        return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);

    // Only handle right-click
    if (mouseEvent->button() != Qt::RightButton) {
        return false;
    }

    pruneDeadRoots();

    if (m_rootWidgets.isEmpty()) {
        return false;
    }

    // Get the widget that was clicked
    QWidget* clickedWidget = qobject_cast<QWidget*>(watched);
    if (!clickedWidget) {
        return false;
    }

    if (!findRegisteredRoot(clickedWidget)) {
        return false;
    }

    if (isInsideContextMenuShell(clickedWidget)) {
        return false;
    }

    // Drill down to the deepest child widget at the click position.
    // Events may arrive at a container widget (e.g., m_tabBarContainer)
    // when the actual target (e.g., CustomTabBar) is a child widget
    // that should provide the context menu.
    {
        QPoint localPos = mouseEvent->pos();
        QWidget* deepest = clickedWidget;
        while (QWidget* child = deepest->childAt(localPos)) {
            localPos = child->mapFrom(deepest, localPos);
            deepest = child;
        }
        if (deepest != clickedWidget) {
            clickedWidget = deepest;
        }
    }

    if (hasContextMenuSystemBypass(clickedWidget)) {
        return false;
    }

    QPoint globalPos = mouseEvent->globalPosition().toPoint();

    // Check if clicking on the same position while menu exists
    if (m_activeMenu) {
        // Calculate distance from last menu position
        int dx = globalPos.x() - m_lastMenuPosition.x();
        int dy = globalPos.y() - m_lastMenuPosition.y();
        int distanceSq = dx * dx + dy * dy;

        // If clicked within 10 pixels of last position
        if (distanceSq <= 100) { // 10*10 = 100
            if (m_activeMenu->isActive()) {
                // Menu is active - hide it
                m_activeMenu->hideAnimated();
            } else {
                // Menu is already hiding - don't reopen, just consume event
            }
            return true; // Consume event
        }
    }

    // Find context menu provider
    QWidget* providerWidget = findContextMenuProvider(clickedWidget);

    // Collect debug info
    QVariantMap debugContext;
    debugContext["clickedWidget"] = clickedWidget->metaObject()->className();
    debugContext["clickedObjectName"] = clickedWidget->objectName();
    debugContext["globalPos"] = mouseEvent->globalPosition().toPoint();
    debugContext["localPos"] = mouseEvent->pos();

    if (providerWidget) {
        if (auto* layerRow = qobject_cast<LayerRowWidget*>(providerWidget)) {
            layerRow->prepareContextMenuInteraction();
        }

        auto* provider = dynamic_cast<IContextMenuProvider*>(providerWidget);
        ContextMenuType menuType = provider->contextMenuType();
        QVariantMap providerContext = provider->contextMenuContext();

        debugContext["providerWidget"] = providerWidget->metaObject()->className();
        debugContext["providerObjectName"] = providerWidget->objectName();
        debugContext["menuType"] = static_cast<int>(menuType);
        debugContext["providerContext"] = providerContext;

        // Merge provider context with debug context
        for (auto it = providerContext.begin(); it != providerContext.end(); ++it) {
            debugContext[it.key()] = it.value();
        }

        showContextMenu(
            menuType, mouseEvent->globalPosition().toPoint(), debugContext, providerWidget);
    } else {
        // No provider found - show generic menu with debug info
        debugContext["providerWidget"] = "None";
        debugContext["menuType"] = static_cast<int>(ContextMenuType::Generic);

        showContextMenu(ContextMenuType::Generic, mouseEvent->globalPosition().toPoint(),
            debugContext, clickedWidget);
    }

    // Consume the event — we handled the right-click.
    // This prevents event propagation to parent widgets,
    // which would cause duplicate menu processing.
    return true;
}

} // namespace ruwa::ui::widgets
