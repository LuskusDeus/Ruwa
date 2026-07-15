// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   N A V I G A T I O N   C O M M A N D S
// ======================================================================================

#include "commands/definitions/NavigationCommands.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandContext.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "shell/main-window/MainWindow.h"
#include "features/settings/shortcuts/ShortcutManagerTab.h"

#include <QMetaObject>
namespace ruwa::core::commands {

// ======================================================================================
//   N E X T   T A B
// ======================================================================================

CommandInfo NextTabCommand::info() const
{
    return CommandInfo { .id = "nav.nextTab",
        .title = "Next Tab",
        .category = "Navigation",
        .description = "Switch to the next tab",
        .aliases = { "next", "nt" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_Tab),
        .icon = QIcon() };
}

bool NextTabCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.tabManager() && ctx.tabManager()->count() > 1;
}

void NextTabCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (!ctx.tabManager()) {
        return;
    }

    ctx.tabManager()->activateNextTab();
}

// ======================================================================================
//   P R E V I O U S   T A B
// ======================================================================================

CommandInfo PreviousTabCommand::info() const
{
    return CommandInfo { .id = "nav.previousTab",
        .title = "Previous Tab",
        .category = "Navigation",
        .description = "Switch to the previous tab",
        .aliases = { "prev", "pt" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
        .icon = QIcon() };
}

bool PreviousTabCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.tabManager() && ctx.tabManager()->count() > 1;
}

void PreviousTabCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (!ctx.tabManager()) {
        return;
    }

    ctx.tabManager()->activatePreviousTab();
}

// ======================================================================================
//   G O   T O   T A B
// ======================================================================================

CommandInfo GoToTabCommand::info() const
{
    return CommandInfo { .id = "nav.goToTab",
        .title = "Go to Tab...",
        .category = "Navigation",
        .description = "Switch to a specific tab by number (1-9)",
        .aliases = { "goto", "gt" },
        .defaultShortcut = QKeySequence(), // Ctrl+1-9 handled separately
        .icon = QIcon() };
}

bool GoToTabCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.tabManager() && ctx.tabManager()->count() > 0;
}

void GoToTabCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    TabManager* manager = ctx.tabManager();
    if (!manager) {
        return;
    }

    // Get tab index from args (1-based for user convenience)
    int tabNumber = args.value("number", 1).toInt();
    int tabIndex = tabNumber - 1; // Convert to 0-based

    if (tabIndex < 0 || tabIndex >= manager->count()) {
        return;
    }

    BaseTab* tab = manager->tabAtIndex(tabIndex);
    if (tab) {
        manager->activateTab(tab);
    }
}

// ======================================================================================
//   G O   T O   S E T T I N G S
// ======================================================================================

CommandInfo GoToSettingsCommand::info() const
{
    return CommandInfo { .id = "nav.settings",
        .title = "Go to Settings",
        .category = "Navigation",
        .description = "Open the settings page",
        .aliases = { "settings", "prefs" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_K), // Photoshop: Preferences
        .icon = QIcon() };
}

void GoToSettingsCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* mainWindow = ctx.mainWindow()) {
        QMetaObject::invokeMethod(mainWindow, "navigateToSettings", Qt::DirectConnection);
    }
}

// ======================================================================================
//   G O   T O   S H O R T C U T   M A N A G E R
// ======================================================================================

CommandInfo GoToShortcutManagerCommand::info() const
{
    return CommandInfo { .id = "settings.shortcuts",
        .title = "Keyboard Shortcuts",
        .category = "Settings",
        .description = "Open keyboard shortcut settings",
        .aliases = { "shortcuts", "keyboard-shortcuts", "hotkeys" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_K),
        .icon = QIcon() };
}

void GoToShortcutManagerCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* tabManager = ctx.tabManager();
    if (!tabManager) {
        return;
    }

    for (BaseTab* tab : tabManager->tabs()) {
        if (auto* shortcutTab = qobject_cast<ruwa::ui::tabs::ShortcutManagerTab*>(tab)) {
            tabManager->activateTab(shortcutTab);
            return;
        }
    }

    tabManager->addTab(new ruwa::ui::tabs::ShortcutManagerTab());
}

// ======================================================================================
//   G O   T O   A B O U T
// ======================================================================================

CommandInfo GoToAboutCommand::info() const
{
    return CommandInfo { .id = "nav.about",
        .title = "Go to About Ruwa",
        .category = "Navigation",
        .description = "Open the About section on the home page",
        .aliases = { "about", "about ruwa", "info", "help" },
        .defaultShortcut = QKeySequence(Qt::Key_F1),
        .icon = QIcon() };
}

void GoToAboutCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* mainWindow = ctx.mainWindow()) {
        QMetaObject::invokeMethod(mainWindow, "navigateToAbout", Qt::DirectConnection);
    }
}

// ======================================================================================
//   G O   T O   H O M E
// ======================================================================================

CommandInfo GoToHomeCommand::info() const
{
    return CommandInfo { .id = "nav.home",
        .title = "Go to Home",
        .category = "Navigation",
        .description = "Go to the home page",
        .aliases = { "home", "start" },
        .defaultShortcut = QKeySequence(Qt::ALT | Qt::Key_Home),
        .icon = QIcon() };
}

void GoToHomeCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* mainWindow = ctx.mainWindow()) {
        QMetaObject::invokeMethod(mainWindow, "navigateToHomeTab", Qt::DirectConnection);
    }
}

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerNavigationCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<NextTabCommand>());
    registry.registerCommand(std::make_unique<PreviousTabCommand>());
    registry.registerCommand(std::make_unique<GoToTabCommand>());
    registry.registerCommand(std::make_unique<GoToHomeCommand>());
    registry.registerCommand(std::make_unique<GoToSettingsCommand>());
    registry.registerCommand(std::make_unique<GoToShortcutManagerCommand>());
    registry.registerCommand(std::make_unique<GoToAboutCommand>());
}

} // namespace ruwa::core::commands
