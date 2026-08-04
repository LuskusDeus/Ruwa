// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C O M P O S I T E   U N D O   C O M M A N D
// ==========================================================================

#include "shared/undo/CompositeUndoCommand.h"

#include <QSet>

#include <utility>

namespace aether {

CompositeUndoCommand::CompositeUndoCommand(QString text)
    : m_text(std::move(text))
{
}

void CompositeUndoCommand::append(std::unique_ptr<IUndoCommand> command)
{
    if (!command) {
        return;
    }
    m_commands.push_back(std::move(command));
}

std::unique_ptr<IUndoCommand> CompositeUndoCommand::takeOnly()
{
    if (m_commands.size() != 1) {
        return nullptr;
    }
    auto only = std::move(m_commands.front());
    m_commands.clear();
    return only;
}

void CompositeUndoCommand::undo()
{
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo();
    }
}

void CompositeUndoCommand::redo()
{
    for (auto& command : m_commands) {
        command->redo();
    }
}

QString CompositeUndoCommand::text() const
{
    return m_text;
}

qint64 CompositeUndoCommand::memorySize() const
{
    qint64 size = sizeof(CompositeUndoCommand);
    size += static_cast<qint64>(m_commands.capacity() * sizeof(std::unique_ptr<IUndoCommand>));
    for (const auto& command : m_commands) {
        size += command->memorySize();
    }
    return size;
}

bool CompositeUndoCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    // Every child must survive the remap: a partially remapped step would undo
    // into a mix of old and new canvas space. Keep remapping the rest even after
    // a failure so no child is left holding stale coordinates, then report it.
    bool allRemapped = true;
    for (auto& command : m_commands) {
        if (!command->remapForCanvasResize(offsetX, offsetY, newWidth, newHeight)) {
            allRemapped = false;
        }
    }
    return allRemapped;
}

bool CompositeUndoCommand::requiresAsyncPreparationForUndo() const
{
    for (const auto& command : m_commands) {
        if (command->requiresAsyncPreparationForUndo()) {
            return true;
        }
    }
    return false;
}

bool CompositeUndoCommand::requiresAsyncPreparationForRedo() const
{
    for (const auto& command : m_commands) {
        if (command->requiresAsyncPreparationForRedo()) {
            return true;
        }
    }
    return false;
}

void CompositeUndoCommand::prepareUndo()
{
    // Preparation only decompresses/loads state, so the order is irrelevant;
    // every child that wants it gets prepared before the step runs.
    for (auto& command : m_commands) {
        if (command->requiresAsyncPreparationForUndo()) {
            command->prepareUndo();
        }
    }
}

void CompositeUndoCommand::prepareRedo()
{
    for (auto& command : m_commands) {
        if (command->requiresAsyncPreparationForRedo()) {
            command->prepareRedo();
        }
    }
}

QList<QPoint> CompositeUndoCommand::affectedTilePositions() const
{
    QSet<QPoint> positions;
    for (const auto& command : m_commands) {
        const QList<QPoint> childPositions = command->affectedTilePositions();
        for (const QPoint& position : childPositions) {
            positions.insert(position);
        }
    }
    return positions.values();
}

} // namespace aether
