// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S E L E C T I O N   S T A T E
// ==========================================================================
// Snapshot types for undo/redo of layer and canvas (lasso) selection.
// ==========================================================================

#ifndef RUWA_CORE_UNDO_SELECTIONSTATE_H
#define RUWA_CORE_UNDO_SELECTIONSTATE_H

#include "features/layers/model/LayerData.h"
#include "features/selection/LassoSelectionManager.h"
#include "features/transform/TransformState.h"

#include <QSet>

#include <functional>
#include <vector>

namespace aether {
class Canvas;
}

namespace ruwa::core::layers {
class LayerSelectionManager;
}

namespace aether {

class LassoSelectionManager;

// ==========================================================================
//   L A Y E R   S E L E C T I O N   S T A T E
// ==========================================================================

struct LayerSelectionState {
    ruwa::core::layers::LayerId primaryId;
    QSet<ruwa::core::layers::LayerId> selectedIds;

    bool isEmpty() const { return selectedIds.isEmpty(); }
};

// ==========================================================================
//   L A S S O   S E L E C T I O N   S T A T E
// ==========================================================================

struct LassoSelectionState {
    std::vector<LassoRegion> regions;
    uint32_t canvasWidth = 0;
    uint32_t canvasHeight = 0;

    /// Optional verbatim snapshot of mask tile pixel data. When present, undo
    /// restores the mask from this snapshot directly instead of replaying
    /// `regions` at full strength — required to faithfully restore soft-alpha
    /// masks (e.g. selectActiveLayerContent stores a full-canvas region as a
    /// placeholder while the real mask carries soft alpha from layer content).
    /// Multiple states captured back-to-back without intervening mask mutation
    /// share the same underlying snapshot via shared_ptr, so capture cost is
    /// O(1) for unchanged masks.
    std::shared_ptr<const MaskTileSnapshot> maskTiles;

    /// Soft-alpha flag for the mask snapshot. Honored only when `maskTiles` is set.
    bool maskHasSoftAlpha = false;

    bool isEmpty() const { return regions.empty(); }
};

// ==========================================================================
//   C O M B I N E D   S E L E C T I O N   S T A T E
// ==========================================================================

struct SelectionState {
    LayerSelectionState layer;
    LassoSelectionState lasso;

    bool isEmpty() const { return layer.isEmpty() && lasso.isEmpty(); }
};

// ==========================================================================
//   S E L E C T I O N   R E S T O R E   C O N T E X T
// ==========================================================================
// Optional context for DrawCommand/TransformCommand to restore selection on undo/redo.
// ==========================================================================

struct SelectionRestoreContext {
    ruwa::core::layers::LayerSelectionManager* layerSelection = nullptr;
    LassoSelectionManager* lassoSelection = nullptr;
    Canvas* canvas = nullptr;
    SelectionState before;
    SelectionState after;
    std::function<bool(const ruwa::core::layers::LayerId&)> layerExists;
    std::function<void()> requestRender;
    std::function<void()> onBeforeRestore;
    std::function<void()> onAfterRestore;
};

// ==========================================================================
//   C A P T U R E   /   A P P L Y   H E L P E R S
// ==========================================================================

LayerSelectionState captureLayerSelection(const ruwa::core::layers::LayerSelectionManager* mgr);
void applyLayerSelection(ruwa::core::layers::LayerSelectionManager* mgr,
    const LayerSelectionState& state,
    const std::function<bool(const ruwa::core::layers::LayerId&)>& layerExists);

LassoSelectionState captureLassoSelection(
    const LassoSelectionManager* mgr, uint32_t canvasWidth, uint32_t canvasHeight);
void applyLassoSelection(LassoSelectionManager* mgr, const LassoSelectionState& state);

void applySelectionRestore(const SelectionRestoreContext& ctx, const SelectionState& state);

/// Transform lasso regions by the given transform state (for selection that moves with transform).
LassoSelectionState transformLassoRegions(
    const LassoSelectionState& state, const TransformState& transform);

} // namespace aether

#endif // RUWA_CORE_UNDO_SELECTIONSTATE_H
