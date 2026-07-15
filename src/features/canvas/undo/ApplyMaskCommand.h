// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   A P P L Y   M A S K   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_APPLYMASKCOMMAND_H
#define RUWA_CORE_UNDO_APPLYMASKCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/canvas/undo/DrawCommand.h"

#include <QUuid>

#include <memory>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class Canvas;

// ==========================================================================
//   A P P L Y   M A S K   C O M M A N D
// ==========================================================================
//
//   Bakes a layer's mask into its pixels and removes the mask, as a single
//   undoable step.
//
//   Internally composes two DrawCommands: one for the pixel bake and one for
//   the mask tiles, so the heavy machinery (background compression and
//   canvas-resize remap) is reused as-is. On top of that this command also
//   toggles the layer's mask-grid presence and flags, which a plain
//   DrawCommand does not track.
//

class ApplyMaskCommand : public IUndoCommand {
public:
    ApplyMaskCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
        const QUuid& layerId, StrokeSnapshot&& pixelSnapshot, StrokeSnapshot&& maskSnapshot,
        bool maskEnabled, bool maskLinked);

    ~ApplyMaskCommand() override;

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
    // Notify the model that the mask grid appeared/disappeared so the canvas
    // rebuilds its layer stack and the layers panel refreshes.
    void notifyMaskStructureChanged();

    ruwa::core::layers::LayerModel* m_layerModel;
    QUuid m_layerId;

    // Pixel bake (before = original pixels, after = masked pixels).
    std::unique_ptr<DrawCommand> m_pixelCmd;
    // Mask tiles (before = original mask, after = empty). Only ever undone:
    // redo drops the mask grid entirely instead of emptying it.
    std::unique_ptr<DrawCommand> m_maskCmd;

    // Mask flags to restore on undo.
    bool m_maskEnabled;
    bool m_maskLinked;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_APPLYMASKCOMMAND_H
