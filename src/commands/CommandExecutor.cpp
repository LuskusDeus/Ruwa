// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================

#include "commands/CommandExecutor.h"
#include "commands/CommandRegistry.h"
#include "commands/Command.h"

namespace ruwa::core {

CommandExecutor& CommandExecutor::instance()
{
    static CommandExecutor instance;
    return instance;
}

CommandExecutor::CommandExecutor()
    : QObject(nullptr)
{
}

CommandExecutor::~CommandExecutor() = default;

void CommandExecutor::initialize(TabManager* tabManager, ruwa::ui::windows::MainWindow* mainWindow)
{
    m_context.setTabManager(tabManager);
    m_context.setMainWindow(mainWindow);
    m_initialized = true;
}

bool CommandExecutor::execute(const QString& commandId, const QVariantMap& args)
{
    if (!m_initialized) {
        emit executionFailed(commandId, tr("Command executor not initialized"));
        return false;
    }

    Command* cmd = CommandRegistry::instance().command(commandId);

    if (!cmd) {
        emit executionFailed(commandId, tr("Unknown command: %1").arg(commandId));
        return false;
    }

    if (!cmd->canExecute(m_context)) {
        emit executionFailed(commandId, tr("Command not available: %1").arg(cmd->info().title));
        return false;
    }

    emit aboutToExecute(commandId);

    try {
        cmd->execute(m_context, args);

        emit commandExecuted(commandId);
        return true;
    } catch (const std::exception& e) {
        emit executionFailed(commandId, QString::fromStdString(e.what()));
        return false;
    }
}

bool CommandExecutor::canExecute(const QString& commandId) const
{
    if (!m_initialized) {
        return false;
    }

    Command* cmd = CommandRegistry::instance().command(commandId);
    if (!cmd) {
        return false;
    }

    return cmd->canExecute(m_context);
}

} // namespace ruwa::core
