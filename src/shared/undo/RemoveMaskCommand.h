// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   R E M O V E   M A S K   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_REMOVEMASKCOMMAND_H
#define RUWA_CORE_UNDO_REMOVEMASKCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileTypes.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

/**
 * @brief Undo command for discarding a layer mask without baking it in.
 *
 * The inverse of AddMaskCommand: redo() drops the mask grid, undo() recreates it
 * and replays the tiles captured at construction. Unlike ApplyMaskCommand the
 * layer's pixels are untouched — the mask simply stops gating them.
 *
 * Construct it BEFORE removing the mask; the constructor snapshots the grid.
 */
class RemoveMaskCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    RemoveMaskCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, RequestRenderFn requestRender,
        OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    void notify();

    // One captured mask tile. Uniform-color ("solid") tiles keep no pixel buffer,
    // so they are stored as just their packed color — mirrors AddMaskCommand.
    struct MaskTileSnapshot {
        TileKey key;
        bool solid = false;
        uint32_t solidColor = 0;
        std::vector<uint8_t> bytes;
    };

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    std::vector<MaskTileSnapshot> m_maskTiles;
    uint32_t m_defaultFill = 0;
    bool m_maskEnabled = true;
    bool m_maskLinked = true;
    bool m_maskEditActive = false;

    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_REMOVEMASKCOMMAND_H
