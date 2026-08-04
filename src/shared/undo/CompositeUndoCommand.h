// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C O M P O S I T E   U N D O   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_COMPOSITEUNDOCOMMAND_H
#define RUWA_CORE_UNDO_COMPOSITEUNDOCOMMAND_H

#include "shared/undo/UndoManager.h"

#include <QString>

#include <cstddef>
#include <memory>
#include <vector>

namespace aether {

/**
 * @brief Several commands that undo and redo as one step.
 *
 * Children are stored in the order they were applied: redo replays them
 * forwards, undo runs them backwards. That ordering is what makes a compound
 * operation replayable — e.g. merging a smart layer first rasterizes it, then
 * bakes its mask, then composites; a redo that skipped the preparation would
 * merge different pixels than the original operation did.
 *
 * Built by UndoManager::beginTransaction / endTransaction rather than directly,
 * so that commands pushed by code that knows nothing about the transaction
 * (the rasterize and apply-mask paths on the canvas) are captured too.
 */
class CompositeUndoCommand final : public IUndoCommand {
public:
    explicit CompositeUndoCommand(QString text);

    /// Append an already-applied command.
    void append(std::unique_ptr<IUndoCommand> command);

    bool isEmpty() const { return m_commands.empty(); }
    std::size_t size() const { return m_commands.size(); }

    /// Hand back the single child, leaving this command empty. Lets a
    /// transaction that captured exactly one command push it unwrapped so the
    /// undo entry keeps its own name.
    std::unique_ptr<IUndoCommand> takeOnly();

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;
    bool requiresAsyncPreparationForUndo() const override;
    bool requiresAsyncPreparationForRedo() const override;
    void prepareUndo() override;
    void prepareRedo() override;
    QList<QPoint> affectedTilePositions() const override;

private:
    QString m_text;
    std::vector<std::unique_ptr<IUndoCommand>> m_commands;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_COMPOSITEUNDOCOMMAND_H
