// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S T R O K E   F I N A L I Z A T I O N   C O N T R O L L E R
// ==========================================================================

#include "features/canvas/stroke/StrokeFinalizationController.h"
#include "features/brush/engine/BrushEngine.h"
#include "features/canvas/scene/Canvas.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TileGrid.h"

#include <cstring>

namespace aether {

void StrokeFinalizationController::finalize(PendingStrokeFinalization& pending, Context& ctx)
{
    if (!pending.active)
        return;

    TileGrid* grid = ctx.getActiveLayerGrid ? ctx.getActiveLayerGrid() : nullptr;
    if (!grid) {
        BrushExecutionBackend* brushExecutionBackend
            = ctx.getBrushExecutionBackend ? ctx.getBrushExecutionBackend() : nullptr;
        if (pending.fence && brushExecutionBackend) {
            if (ctx.makeCurrent)
                ctx.makeCurrent();
            brushExecutionBackend->deleteFence(pending.fence);
            if (ctx.doneCurrent)
                ctx.doneCurrent();
        }
        pending = {};
        return;
    }

    BrushExecutionBackend* brushExecutionBackend
        = ctx.getBrushExecutionBackend ? ctx.getBrushExecutionBackend() : nullptr;
    if (pending.fence && brushExecutionBackend) {
        if (ctx.makeCurrent)
            ctx.makeCurrent();
        brushExecutionBackend->finishReadback(
            pending.fence, *grid, pending.readbackKeysOrdered, true);
        if (ctx.doneCurrent)
            ctx.doneCurrent();
        pending.fence = nullptr;
    }

    StrokeSnapshot snapshot;
    snapshot.layerId = pending.layerId;
    snapshot.maskTarget = pending.maskTarget;
    snapshot.beforeTiles = std::move(pending.beforeTiles);
    snapshot.createdTiles = std::move(pending.createdTiles);
    snapshot.removedTiles = std::move(pending.removedTiles);

    if (pending.eraseMode) {
        for (const auto& key : pending.flattenedKeys) {
            TileData* tile = grid->getTile(key);
            if (tile && tile->isEmpty())
                snapshot.removedTiles.insert(key);
        }
        grid->pruneEmpty();
    }

    const size_t bytesPerTile = tileByteSize(grid->format());
    for (const auto& key : pending.flattenedKeys) {
        if (snapshot.removedTiles.count(key)) {
            snapshot.afterTiles[key].resize(bytesPerTile, 0);
            continue;
        }
        const TileData* tile = grid->getTile(key);
        if (tile) {
            auto& buf = snapshot.afterTiles[key];
            buf.resize(bytesPerTile);
            std::memcpy(buf.data(), tile->pixels(), bytesPerTile);
        }
    }

    if (!pending.flattenedKeys.empty() && ctx.canvas && ctx.layerModel) {
        std::optional<SelectionRestoreContext> selRestore;
        if (ctx.selectionRestore) {
            selRestore = std::move(*ctx.selectionRestore);
        }
        auto cmd = std::make_unique<DrawCommand>(
            ctx.canvas, ctx.layerModel, std::move(snapshot), std::move(selRestore));
        ctx.canvas->undoManager().push(std::move(cmd));
    }

    pending = {};
}

} // namespace aether
