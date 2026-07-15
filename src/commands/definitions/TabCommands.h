// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   T A B   C O M M A N D S
// ======================================================================================
//   File        : TabCommands.h
//   Description : Commands for tab management (Close, Close All, etc.)
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_TABCOMMANDS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_TABCOMMANDS_H

#include "commands/Command.h"

namespace ruwa::core {
class CommandRegistry;
}

namespace ruwa::core::commands {

// ======================================================================================
//   C L O S E   T A B
// ======================================================================================

class CloseTabCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   C L O S E   A L L   T A B S
// ======================================================================================

class CloseAllTabsCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   C L O S E   O T H E R   T A B S
// ======================================================================================

class CloseOtherTabsCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   D U P L I C A T E   T A B
// ======================================================================================

class DuplicateTabCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;

    /// Hidden until tab duplication is implemented
    bool showInPalette() const override { return false; }
};

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerTabCommands(CommandRegistry& registry);

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_TABCOMMANDS_H
