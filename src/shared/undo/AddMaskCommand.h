// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   A D D   M A S K   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_ADDMASKCOMMAND_H
#define RUWA_CORE_UNDO_ADDMASKCOMMAND_H

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
 * @brief Undo command for adding (creating) an empty layer mask.
 *
 * redo() creates the mask grid (reveal-all when empty, or pre-filled from an
 * active selection captured at construction) and makes it the active paint
 * target; undo() removes the mask grid again and restores the previous paint
 * target. Mask strokes painted afterwards live in their own DrawCommands on the
 * stack, so they are always undone before this command and replayed after it.
 *
 * The mask's initial tile content (e.g. a selection baked into white/black) is
 * snapshotted at construction so redo can rebuild it after undo dropped the grid.
 */
class AddMaskCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    AddMaskCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, bool prevMaskEditActive,
        RequestRenderFn requestRender, OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    void notify();

    // One mask tile captured at creation time. A uniform-color ("solid") tile is
    // stored as just its packed color (no buffer); a painted tile keeps its raw
    // RGBA bytes (TILE_BYTE_SIZE). Lets redo restore selection-baked content
    // (including memory-free solid tiles) after undo dropped the grid.
    struct MaskTileSnapshot {
        TileKey key;
        bool solid = false;
        uint32_t solidColor = 0;
        std::vector<uint8_t> bytes;
    };

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    bool m_prevMaskEditActive = false;
    std::vector<MaskTileSnapshot> m_initialMaskTiles;
    // Default-fill background of the mask at creation (e.g. opaque black for an
    // Alt+Add "hide-all" mask). Restored by redo after undo dropped the grid.
    uint32_t m_initialDefaultFill = 0;

    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_ADDMASKCOMMAND_H
