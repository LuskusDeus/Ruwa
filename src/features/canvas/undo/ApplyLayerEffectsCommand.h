// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   A P P L Y   L A Y E R   E F F E C T S   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_APPLYLAYEREFFECTSCOMMAND_H
#define RUWA_CORE_UNDO_APPLYLAYEREFFECTSCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/canvas/undo/DrawCommand.h"
#include "features/effects/LayerEffectTypes.h"

#include <QList>
#include <QUuid>

#include <memory>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class Canvas;

// ==========================================================================
//   A P P L Y   L A Y E R   E F F E C T S   C O M M A N D
// ==========================================================================
//
//   Bakes a layer's effect chain into its pixels and clears the chain, as a
//   single undoable step. Composes a DrawCommand for the pixel bake (before =
//   original pixels, after = effected pixels) and separately restores the
//   effect list through LayerModel::replaceLayerEffects.
//

class ApplyLayerEffectsCommand : public IUndoCommand {
public:
    ApplyLayerEffectsCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
        const QUuid& layerId, StrokeSnapshot&& pixelSnapshot,
        QList<ruwa::core::effects::LayerEffectState> beforeEffects);

    ~ApplyLayerEffectsCommand() override;

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
    ruwa::core::layers::LayerModel* m_layerModel;
    QUuid m_layerId;

    // Pixel bake (before = original pixels, after = effected pixels).
    std::unique_ptr<DrawCommand> m_pixelCmd;

    // Effect chain to restore on undo; redo clears it.
    QList<ruwa::core::effects::LayerEffectState> m_beforeEffects;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_APPLYLAYEREFFECTSCOMMAND_H
