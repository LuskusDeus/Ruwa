// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   T A B   C O M M A N D S
// ======================================================================================

#include "commands/definitions/TabCommands.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandContext.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "shell/top-bar/UnsavedChangesHelper.h"
#include "shell/main-window/MainWindow.h"
#include <QWidget>

namespace ruwa::core::commands {

// ======================================================================================
//   C L O S E   T A B
// ======================================================================================

CommandInfo CloseTabCommand::info() const
{
    return CommandInfo { .id = "tab.close",
        .title = "Close Tab",
        .category = "Tab",
        .description = "Close the current tab",
        .aliases = { "close", "tc" },
        .defaultShortcut = QKeySequence::Close,
        .icon = QIcon() };
}

bool CloseTabCommand::canExecute(const CommandContext& ctx) const
{
    if (!ctx.tabManager())
        return false;
    BaseTab* tab = ctx.activeTab();
    return tab != nullptr;
}

void CloseTabCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (!ctx.tabManager()) {
        return;
    }

    BaseTab* tab = ctx.activeTab();
    if (!tab) {
        return;
    }

    auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
    if (wsTab && ctx.mainWindow()
        && !ruwa::ui::widgets::prepareWorkspaceTabForClose(
            wsTab, static_cast<QWidget*>(ctx.mainWindow()))) {
        return; // User cancelled
    }

    ctx.tabManager()->requestCloseTab(tab);
}

// ======================================================================================
//   C L O S E   A L L   T A B S
// ======================================================================================

CommandInfo CloseAllTabsCommand::info() const
{
    return CommandInfo { .id = "tab.closeAll",
        .title = "Close All Tabs",
        .category = "Tab",
        .description = "Close all open tabs",
        .aliases = { "closeall", "tca" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
        .icon = QIcon() };
}

bool CloseAllTabsCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.tabManager() && ctx.tabManager()->count() > 0;
}

void CloseAllTabsCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    TabManager* manager = ctx.tabManager();
    if (!manager) {
        return;
    }

    QWidget* context = static_cast<QWidget*>(ctx.mainWindow());
    if (!context)
        return;

    for (BaseTab* tab : manager->tabs()) {
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (wsTab && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
    }

    manager->closeAllTabs();
}

// ======================================================================================
//   C L O S E   O T H E R   T A B S
// ======================================================================================

CommandInfo CloseOtherTabsCommand::info() const
{
    return CommandInfo { .id = "tab.closeOthers",
        .title = "Close Other Tabs",
        .category = "Tab",
        .description = "Close all tabs except the current one",
        .aliases = { "closeothers", "tco" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool CloseOtherTabsCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.tabManager() && ctx.tabManager()->count() > 1;
}

void CloseOtherTabsCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    TabManager* manager = ctx.tabManager();
    if (!manager) {
        return;
    }

    BaseTab* currentTab = ctx.activeTab();
    if (!currentTab) {
        return;
    }

    QWidget* context = static_cast<QWidget*>(ctx.mainWindow());
    if (!context)
        return;

    QList<BaseTab*> tabsToClose;
    for (BaseTab* tab : manager->tabs()) {
        if (tab != currentTab) {
            tabsToClose.append(tab);
        }
    }

    for (BaseTab* tab : tabsToClose) {
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (wsTab && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
        manager->requestCloseTab(tab);
    }
}

// ======================================================================================
//   D U P L I C A T E   T A B
// ======================================================================================

CommandInfo DuplicateTabCommand::info() const
{
    return CommandInfo { .id = "tab.duplicate",
        .title = "Duplicate Tab",
        .category = "Tab",
        .description = "Create a copy of the current tab",
        .aliases = { "duplicate", "td" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool DuplicateTabCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void DuplicateTabCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab) {
        return;
    }

    TabManager* manager = ctx.tabManager();
    if (!manager) {
        return;
    }

    auto* newTab = ruwa::ui::tabs::WorkspaceTab::duplicate(wsTab);
    if (newTab) {
        manager->addTab(newTab);
        manager->activateTab(newTab);
    }
}

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerTabCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<CloseTabCommand>());
    registry.registerCommand(std::make_unique<CloseAllTabsCommand>());
    registry.registerCommand(std::make_unique<CloseOtherTabsCommand>());
    registry.registerCommand(std::make_unique<DuplicateTabCommand>());
}

} // namespace ruwa::core::commands
