// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S E T   M A S K   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_SETMASKCOMMAND_H
#define RUWA_CORE_UNDO_SETMASKCOMMAND_H

#include "features/layers/model/LayerData.h"
#include "shared/undo/UndoManager.h"

#include <functional>
#include <memory>

namespace aether {
class TileGrid;
}

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

/**
 * @brief Undo command for replacing a layer's whole mask with another one.
 *
 * Used by mask paste, where the target may or may not already carry a mask:
 * both the previous and the new state are captured as complete mask snapshots,
 * so undo/redo simply reinstates one of them. AddMaskCommand/RemoveMaskCommand
 * cover the two degenerate cases (nothing → mask, mask → nothing) when the
 * caller knows which one it is; this one covers the general swap.
 *
 * Grids are shared, never mutated: each apply installs a fresh deep copy.
 */
class SetMaskCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    /// One complete mask state. A null @ref grid means "layer has no mask".
    struct MaskState {
        std::shared_ptr<const TileGrid> grid;
        bool enabled = true;
        bool linked = true;
        bool editActive = false;
    };

    /// Capture the layer's current mask state (for use as the `before` state).
    static MaskState captureState(const ruwa::core::layers::LayerData* layer);

    /// Install @p state on @p layer. Public so callers can apply the new state
    /// and push the matching command without duplicating the logic.
    static void applyState(ruwa::core::layers::LayerData* layer, const MaskState& state);

    SetMaskCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, MaskState before, MaskState after,
        RequestRenderFn requestRender, OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    void apply(const MaskState& state);

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    MaskState m_before;
    MaskState m_after;

    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_SETMASKCOMMAND_H
