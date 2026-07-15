// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================
//   File        : CommandRegistry.h
//   Description : Central registry for all application commands.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_COMMANDREGISTRY_H
#define RUWA_CORE_COMMANDS_COMMANDREGISTRY_H

#include "commands/Command.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <memory>
#include <unordered_map>

namespace ruwa::core {

/**
 * @brief Singleton registry for all commands
 *
 * Commands register themselves here at application startup.
 * The registry provides lookup by ID and search functionality
 * for the command palette.
 */
class CommandRegistry : public QObject {
    Q_OBJECT

public:
    // === Singleton ===

    static CommandRegistry& instance();

    // === Registration ===

    /// Register a command. Takes ownership.
    void registerCommand(std::unique_ptr<Command> command);

    /// Register multiple commands at once
    template <typename... Commands> void registerCommands()
    {
        (registerCommand(std::make_unique<Commands>()), ...);
    }

    // === Lookup ===

    /// Get command by ID (returns nullptr if not found)
    Command* command(const QString& id) const;

    /// Check if command exists
    bool hasCommand(const QString& id) const;

    /// Get all registered commands
    QList<Command*> allCommands() const;

    /// Get commands in a specific category
    QList<Command*> commandsInCategory(const QString& category) const;

    /// Get all unique categories
    QStringList categories() const;

    // === Search (for command palette) ===

    /// Search commands by query (matches id, title, aliases)
    /// Results are sorted by relevance
    QList<Command*> search(const QString& query, int maxResults = 20) const;

    /// Find command by exact alias match (e.g. "qnp" -> QuickNewProject)
    /// Returns nullptr if no command has this alias
    Command* findCommandByAlias(const QString& alias) const;

signals:
    /// Emitted when a new command is registered
    void commandRegistered(const QString& id);

private:
    CommandRegistry();
    ~CommandRegistry() override;

    Q_DISABLE_COPY_MOVE(CommandRegistry)

    /// Calculate search score for a command against a query
    int calculateSearchScore(const Command* cmd, const QString& query) const;

private:
    // Use std::unordered_map for unique_ptr storage (QHash doesn't support move-only types)
    std::unordered_map<std::string, std::unique_ptr<Command>> m_commands;
    QHash<QString, QList<Command*>> m_byCategory;
};

} // namespace ruwa::core

#endif // RUWA_CORE_COMMANDS_COMMANDREGISTRY_H
