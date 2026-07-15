// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   E D I T   C O M M A N D S
// ======================================================================================
//   File        : EditCommands.h
//   Description : Commands for edit and selection operations.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_EDITCOMMANDS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_EDITCOMMANDS_H

#include "commands/Command.h"

namespace ruwa::core {
class CommandRegistry;
}

namespace ruwa::core::commands {

class CopyCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class CutCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class PasteCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class DeselectCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class FillSelectionCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

void registerEditCommands(CommandRegistry& registry);

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_EDITCOMMANDS_H
