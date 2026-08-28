// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/engine/CanvasEngineSession.h"
#include "RadialMenuController.h"

#include "RadialMenuConfig.h"

#include "commands/Command.h"
#include "commands/CommandExecutor.h"
#include "commands/CommandRegistry.h"
#include "commands/ShortcutManager.h"
#include "features/canvas/engine/CanvasEngineQtEvents.h"
#include "features/canvas/ui/CanvasCursorManager.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "shared/i18n/CommandLocalization.h"
#include "shared/resources/IconProvider.h"

#include <QCoreApplication>

namespace ruwa::ui::workspace {

using ruwa::core::CommandExecutor;
using ruwa::core::CommandRegistry;
using ruwa::core::ShortcutManager;
using ruwa::ui::core::IconProvider;
using ruwa::ui::widgets::RadialMenuConfig;
using ruwa::ui::widgets::RadialMenuItem;
using ruwa::ui::widgets::RadialMenuLayout;
using ruwa::ui::widgets::RadialMenuPage;
using ruwa::ui::widgets::RadialMenuWidget;

namespace {

/// Localized command title, falling back to the source string.
QString commandTitle(const ruwa::core::Command* command)
{
    if (!command) {
        return QString();
    }
    const ruwa::core::CommandInfo info = command->info();
    const QString localized = ruwa::i18n::CommandLocalization::instance().title(info.id);
    return localized.isEmpty() ? info.title : localized;
}

} // namespace

RadialMenuController::RadialMenuController(CanvasPanel* panel)
    : QObject(panel)
    , m_panel(panel)
{
}

// ======================================================================================
//   L I F E T I M E
// ======================================================================================

void RadialMenuController::ensureWidget()
{
    if (!m_panel || m_panel->m_radialMenu || !m_panel->m_contentWidget) {
        return;
    }

    auto* menu = new RadialMenuWidget(m_panel->m_contentWidget);
    m_panel->m_radialMenu = menu;
    menu->hide();
    menu->raise();

    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->addCursorExclusionWidget(menu);
    }

    // Same frosted backdrop as the on-canvas overlays. The widget is built on
    // the first right-click, long after the canvas set the others up, so it
    // subscribes here instead.
    if (m_panel->m_engineBinding) {
        menu->setBackdropSource(m_panel->m_engineBinding->backdropSource());
        connect(&m_panel->m_engineBinding->events(),
            &CanvasEngineQtEvents::backdropAvailabilityChanged, menu,
            QOverload<>::of(&QWidget::update));
        connect(m_panel->m_engineBinding->viewportHostWidget(), &QObject::destroyed, menu,
            [menu]() { menu->setBackdropSource(nullptr); });
    }

    connect(
        menu, &RadialMenuWidget::slotTriggered, this, &RadialMenuController::handleSlotTriggered);
    connect(menu, &RadialMenuWidget::backRequested, this, &RadialMenuController::goBack);
    connect(menu, &RadialMenuWidget::dismissed, this, [this]() { m_pageStack.clear(); });

    // A layout edit while the menu is open should be visible immediately —
    // the configuration UI previews itself through this path.
    connect(&RadialMenuConfig::instance(), &RadialMenuConfig::layoutChanged, this, [this]() {
        if (isRadialMenuVisible() && !m_pageStack.isEmpty()) {
            openPage(m_pageStack.last(), false);
        }
    });
}

void RadialMenuController::showRadialMenu(const QPoint& globalPos, bool armReleaseSelect)
{
    ensureWidget();
    if (!m_panel || !m_panel->m_radialMenu || !m_panel->m_contentWidget) {
        return;
    }

    m_pageStack.clear();
    openPage(RadialMenuLayout::rootPageId(), true);
    if (m_pageStack.isEmpty()) {
        return;
    }

    m_panel->m_radialMenu->showAt(
        m_panel->m_contentWidget->mapFromGlobal(globalPos), armReleaseSelect);
}

void RadialMenuController::hideRadialMenu()
{
    if (m_panel && m_panel->m_radialMenu) {
        m_panel->m_radialMenu->hideMenu();
    }
    m_pageStack.clear();
}

bool RadialMenuController::isRadialMenuVisible() const
{
    return m_panel && m_panel->m_radialMenu && m_panel->m_radialMenu->isMenuVisible();
}

// ======================================================================================
//   N A V I G A T I O N
// ======================================================================================

void RadialMenuController::openPage(const QString& pageId, bool pushHistory,
    RadialMenuWidget::PageTransition transition, int pivotIndex)
{
    if (!m_panel || !m_panel->m_radialMenu) {
        return;
    }

    const RadialMenuPage* page = RadialMenuConfig::instance().layout().page(pageId);
    if (!page) {
        return;
    }

    if (pushHistory) {
        m_pageStack.append(pageId);
    }

    RadialMenuWidget::Page resolved = buildPage(*page);
    resolved.canGoBack = m_pageStack.size() > 1;
    m_panel->m_radialMenu->setPage(resolved, transition, pivotIndex);
}

void RadialMenuController::goBack()
{
    if (m_pageStack.size() <= 1) {
        hideRadialMenu();
        return;
    }

    const QString leaving = m_pageStack.takeLast();
    const QString parentId = m_pageStack.last();

    // The hub folds back into the seat that opened the page being left, so the
    // parent page has to say which seat that is.
    int pivotIndex = -1;
    if (const RadialMenuPage* parent = RadialMenuConfig::instance().layout().page(parentId)) {
        for (int index = 0; index < parent->items.size(); ++index) {
            const RadialMenuItem& item = parent->items.at(index);
            if (item.opensPage() && item.pageId == leaving) {
                pivotIndex = index;
                break;
            }
        }
    }

    openPage(parentId, false, RadialMenuWidget::PageTransition::OutOfGroup, pivotIndex);
}

void RadialMenuController::handleSlotTriggered(int index)
{
    const RadialMenuPage* page = currentPage();
    if (!page || index < 0 || index >= page->items.size()) {
        return;
    }

    const RadialMenuItem& item = page->items.at(index);
    if (item.opensPage()) {
        openPage(item.pageId, true, RadialMenuWidget::PageTransition::IntoGroup, index);
        return;
    }
    if (item.commandId.isEmpty()) {
        return;
    }

    m_pageStack.clear();
    CommandExecutor::instance().execute(item.commandId);
}

const RadialMenuPage* RadialMenuController::currentPage() const
{
    if (m_pageStack.isEmpty()) {
        return nullptr;
    }
    return RadialMenuConfig::instance().layout().page(m_pageStack.last());
}

// ======================================================================================
//   R E S O L U T I O N
// ======================================================================================

RadialMenuWidget::Page RadialMenuController::buildPage(const RadialMenuPage& page) const
{
    auto& registry = CommandRegistry::instance();
    auto& executor = CommandExecutor::instance();
    auto& shortcuts = ShortcutManager::instance();
    const auto& icons = IconProvider::instance();

    RadialMenuWidget::Page resolved;
    resolved.title = page.title.isEmpty()
        ? QString()
        : QCoreApplication::translate("RadialMenu", page.title.toUtf8().constData());
    resolved.items.reserve(page.items.size());

    for (const RadialMenuItem& item : page.items) {
        RadialMenuWidget::Slot slot;

        if (item.isEmpty()) {
            slot.empty = true;
            resolved.items.append(slot);
            continue;
        }

        if (!item.iconName.isEmpty()) {
            slot.icon = icons.getIcon(item.iconName);
        }

        if (item.opensPage()) {
            const RadialMenuPage* target = RadialMenuConfig::instance().layout().page(item.pageId);
            slot.opensPage = true;
            slot.title = item.label.isEmpty()
                ? (target && !target->title.isEmpty() ? target->title
                                                      : RadialMenuWidget::tr("More"))
                : item.label;
            if (slot.icon.isNull()) {
                slot.icon = icons.getIcon(IconProvider::StandardIcon::Dots);
            }
            slot.enabled = target != nullptr;
            resolved.items.append(slot);
            continue;
        }

        const ruwa::core::Command* command = registry.command(item.commandId);
        slot.title = item.label.isEmpty() ? commandTitle(command) : item.label;
        if (slot.title.isEmpty()) {
            // An id that no longer exists: keep the seat, show what it points
            // at, and leave it disabled rather than silently dropping it.
            slot.title = item.commandId;
        }
        slot.shortcut = shortcuts.shortcutFor(item.commandId);
        if (slot.icon.isNull() && command) {
            slot.icon = command->info().icon;
        }
        slot.enabled = command != nullptr && executor.canExecute(item.commandId);
        resolved.items.append(slot);
    }

    return resolved;
}

} // namespace ruwa::ui::workspace
