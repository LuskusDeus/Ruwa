// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   F I L E   C O M M A N D S
// ======================================================================================
//   File        : FileCommands.h
//   Description : Commands for file operations (New, Open, Save, Export, etc.)
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_FILECOMMANDS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_FILECOMMANDS_H

#include "commands/Command.h"

namespace ruwa::core {
class CommandRegistry;
}

namespace ruwa::core::commands {

// ======================================================================================
//   N E W   P R O J E C T
// ======================================================================================

class NewProjectCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   O P E N   P R O J E C T
// ======================================================================================

class OpenProjectCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   S A V E   P R O J E C T
// ======================================================================================

class SaveProjectCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   S A V E   P R O J E C T   A S
// ======================================================================================

class SaveProjectAsCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   E X P O R T   P R O J E C T
// ======================================================================================

class ExportProjectCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   F A S T   E X P O R T   A S   P N G
// ======================================================================================

class FastExportPngCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   Q U I C K   N E W   P R O J E C T
// ======================================================================================

class QuickNewProjectCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   E X I T   A P P L I C A T I O N
// ======================================================================================

class ExitCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerFileCommands(CommandRegistry& registry);

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_FILECOMMANDS_H
