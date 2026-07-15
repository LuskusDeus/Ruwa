// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================
//   File        : CommandExecutor.h
//   Description : Central command execution service.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_COMMANDEXECUTOR_H
#define RUWA_CORE_COMMANDS_COMMANDEXECUTOR_H

#include "commands/CommandContext.h"

#include <QObject>
#include <QVariantMap>

namespace ruwa::core {

class TabManager;

} // namespace ruwa::core

namespace ruwa::ui::windows {
class MainWindow;
}

namespace ruwa::core {

/**
 * @brief Singleton service for executing commands
 *
 * All command execution should go through this service.
 * It maintains the execution context and handles error reporting.
 */
class CommandExecutor : public QObject {
    Q_OBJECT

public:
    // === Singleton ===

    static CommandExecutor& instance();

    // === Context Setup ===

    /// Initialize the executor with application components
    void initialize(TabManager* tabManager, ruwa::ui::windows::MainWindow* mainWindow);

    /// Get the current context (read-only)
    const CommandContext& context() const { return m_context; }

    // === Execution ===

    /// Execute a command by ID with optional arguments
    /// Returns true if command was executed successfully
    bool execute(const QString& commandId, const QVariantMap& args = {});

    /// Check if a command can be executed in current context
    bool canExecute(const QString& commandId) const;

signals:
    /// Emitted after successful command execution
    void commandExecuted(const QString& commandId);

    /// Emitted when command execution fails
    void executionFailed(const QString& commandId, const QString& reason);

    /// Emitted before command execution (for logging/debugging)
    void aboutToExecute(const QString& commandId);

private:
    CommandExecutor();
    ~CommandExecutor() override;

    Q_DISABLE_COPY_MOVE(CommandExecutor)

private:
    CommandContext m_context;
    bool m_initialized = false;
};

} // namespace ruwa::core

#endif // RUWA_CORE_COMMANDS_COMMANDEXECUTOR_H
