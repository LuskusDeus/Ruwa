// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   N A V I G A T I O N   C O M M A N D S
// ======================================================================================
//   File        : NavigationCommands.h
//   Description : Commands for navigation between tabs and views.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_NAVIGATIONCOMMANDS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_NAVIGATIONCOMMANDS_H

#include "commands/Command.h"

namespace ruwa::core {
class CommandRegistry;
}

namespace ruwa::core::commands {

// ======================================================================================
//   N E X T   T A B
// ======================================================================================

class NextTabCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   P R E V I O U S   T A B
// ======================================================================================

class PreviousTabCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   G O   T O   T A B
// ======================================================================================

class GoToTabCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   G O   T O   S E T T I N G S
// ======================================================================================

class GoToSettingsCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   G O   T O   S H O R T C U T   M A N A G E R
// ======================================================================================

class GoToShortcutManagerCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   G O   T O   A B O U T
// ======================================================================================

class GoToAboutCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   G O   T O   H O M E
// ======================================================================================

class GoToHomeCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerNavigationCommands(CommandRegistry& registry);

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_NAVIGATIONCOMMANDS_H
