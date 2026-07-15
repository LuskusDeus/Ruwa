// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S E L E C T I O N   S T A T E
// ==========================================================================

#include "shared/undo/SelectionState.h"
#include "features/layers/model/LayerSelectionManager.h"
#include "features/selection/LassoSelectionManager.h"
#include "features/transform/TransformState.h"

namespace aether {

// ==========================================================================
//   L A Y E R   S E L E C T I O N
// ==========================================================================

LayerSelectionState captureLayerSelection(const ruwa::core::layers::LayerSelectionManager* mgr)
{
    if (!mgr)
        return {};
    LayerSelectionState s;
    s.primaryId = mgr->primaryId();
    s.selectedIds = mgr->selectedIds();
    return s;
}

void applyLayerSelection(ruwa::core::layers::LayerSelectionManager* mgr,
    const LayerSelectionState& state,
    const std::function<bool(const ruwa::core::layers::LayerId&)>& layerExists)
{
    if (!mgr)
        return;

    // Filter out layers that no longer exist (e.g. deleted during undo)
    QSet<ruwa::core::layers::LayerId> validIds;
    for (const auto& id : state.selectedIds) {
        if (layerExists && !layerExists(id))
            continue;
        validIds.insert(id);
    }

    ruwa::core::layers::LayerId validPrimary = state.primaryId;
    if (!validPrimary.isNull() && (!layerExists || !layerExists(validPrimary))) {
        validPrimary = validIds.isEmpty() ? ruwa::core::layers::LayerId() : *validIds.begin();
    }

    mgr->applySelectionState(validPrimary, validIds);
}

// ==========================================================================
//   L A S S O   S E L E C T I O N
// ==========================================================================

LassoSelectionState captureLassoSelection(
    const LassoSelectionManager* mgr, uint32_t canvasWidth, uint32_t canvasHeight)
{
    if (!mgr)
        return {};
    LassoSelectionState s;
    s.regions = mgr->regions();
    s.canvasWidth = canvasWidth;
    s.canvasHeight = canvasHeight;
    if (!mgr->mask().empty()) {
        // shared_ptr from manager's cache: cheap if no mutation occurred since
        // the last capture, deep-copied lazily on first capture after a mutation.
        s.maskTiles = mgr->snapshotMask();
        s.maskHasSoftAlpha = mgr->maskHasSoftAlpha();
    }
    return s;
}

void applyLassoSelection(LassoSelectionManager* mgr, const LassoSelectionState& state)
{
    if (!mgr)
        return;
    if (state.maskTiles) {
        // Verbatim restore — preserves soft-alpha pixel data that polygon
        // replay at strength=255 cannot reproduce.
        mgr->applyMaskSnapshot(state.maskTiles, state.regions, state.maskHasSoftAlpha,
            state.canvasWidth, state.canvasHeight);
    } else {
        // Legacy region-replay path for selections captured before the snapshot
        // mechanism existed, or for unconditionally-empty masks.
        mgr->applyState(state.regions, state.canvasWidth, state.canvasHeight);
    }
}

void applySelectionRestore(const SelectionRestoreContext& ctx, const SelectionState& state)
{
    if (ctx.onBeforeRestore)
        ctx.onBeforeRestore();
    if (ctx.layerSelection) {
        applyLayerSelection(ctx.layerSelection, state.layer, ctx.layerExists);
    }
    if (ctx.lassoSelection && ctx.canvas) {
        applyLassoSelection(ctx.lassoSelection, state.lasso);
    }
    if (ctx.requestRender)
        ctx.requestRender();
    if (ctx.onAfterRestore)
        ctx.onAfterRestore();
}

LassoSelectionState transformLassoRegions(
    const LassoSelectionState& state, const TransformState& transform)
{
    LassoSelectionState result;
    result.canvasWidth = state.canvasWidth;
    result.canvasHeight = state.canvasHeight;
    result.regions.reserve(state.regions.size());
    for (const LassoRegion& r : state.regions) {
        LassoRegion tr;
        tr.mode = r.mode;
        tr.polygon.reserve(r.polygon.size());
        for (const Vector2& p : r.polygon) {
            tr.polygon.push_back(transform.transformPoint(p));
        }
        result.regions.push_back(std::move(tr));
    }
    return result;
}

} // namespace aether
