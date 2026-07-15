// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   |   U N D O / R E D O   C O M M A N D S
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_UNDOREDOCOMMANDS_H
#define RUWA_CORE_COMMANDS_UNDOREDOCOMMANDS_H

#include "commands/Command.h"

#include <functional>

namespace aether {
class UndoManager;
}

namespace ruwa::core {

// ======================================================================================
//   Resolver — returns the active UndoManager* for the current context.
//   Set once during registration; called each time the command executes.
// ======================================================================================

using UndoManagerResolver = std::function<aether::UndoManager*()>;

// ======================================================================================
//   U N D O   C O M M A N D
// ======================================================================================

class UndoActionCommand : public Command {
public:
    explicit UndoActionCommand(UndoManagerResolver resolver)
        : m_resolve(std::move(resolver))
    {
    }

    CommandInfo info() const override
    {
        return { /* id          */ QStringLiteral("edit.undo"),
            /* title       */ QStringLiteral("Undo"),
            /* category    */ QStringLiteral("Edit"),
            /* description */ QStringLiteral("Undo the last action"),
            /* aliases     */ { QStringLiteral("undo"), QStringLiteral("z") },
            /* shortcut    */ QKeySequence::Undo,
            /* icon        */ {} };
    }

    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;

private:
    UndoManagerResolver m_resolve;
};

// ======================================================================================
//   R E D O   C O M M A N D
// ======================================================================================

class RedoActionCommand : public Command {
public:
    explicit RedoActionCommand(UndoManagerResolver resolver)
        : m_resolve(std::move(resolver))
    {
    }

    CommandInfo info() const override
    {
        return { /* id          */ QStringLiteral("edit.redo"),
            /* title       */ QStringLiteral("Redo"),
            /* category    */ QStringLiteral("Edit"),
            /* description */ QStringLiteral("Redo the last undone action"),
            /* aliases     */ { QStringLiteral("redo"), QStringLiteral("y") },
            /* shortcut    */ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), // Photoshop: Redo
            /* icon        */ {} };
    }

    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;

private:
    UndoManagerResolver m_resolve;
};

} // namespace ruwa::core

#endif // RUWA_CORE_COMMANDS_UNDOREDOCOMMANDS_H
