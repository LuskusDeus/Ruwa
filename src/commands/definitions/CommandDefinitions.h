// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   D E F I N I T I O N S
// ======================================================================================
//   File        : CommandDefinitions.h
//   Description : Aggregates all command definitions for easy registration.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_COMMANDDEFINITIONS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_COMMANDDEFINITIONS_H

#include "commands/CommandRegistry.h"

namespace ruwa::core::commands {

// Forward declarations of registration functions
void registerFileCommands(CommandRegistry& registry);
void registerEditCommands(CommandRegistry& registry);
void registerTabCommands(CommandRegistry& registry);
void registerNavigationCommands(CommandRegistry& registry);
void registerViewCommands(CommandRegistry& registry);
void registerToolCommands(CommandRegistry& registry);
void registerLayerCommands(CommandRegistry& registry);

/**
 * @brief Register all built-in commands
 *
 * Call this function during application startup to register
 * all commands with the CommandRegistry.
 */
inline void registerAllCommands()
{
    auto& registry = CommandRegistry::instance();

    registerFileCommands(registry);
    registerEditCommands(registry);
    registerTabCommands(registry);
    registerNavigationCommands(registry);
    registerViewCommands(registry);
    registerToolCommands(registry);
    registerLayerCommands(registry);
}

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_COMMANDDEFINITIONS_H
