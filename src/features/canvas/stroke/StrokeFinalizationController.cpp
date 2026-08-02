// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S T R O K E   F I N A L I Z A T I O N   C O N T R O L L E R
// ==========================================================================

#include "features/canvas/stroke/StrokeFinalizationController.h"
#include "features/brush/engine/BrushEngine.h"
#include "features/canvas/scene/Canvas.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TileGrid.h"

#include <algorithm>
#include <cstring>

namespace aether {
namespace {

// Each finalized tile is copied once from the persistent PBO into TileData and
// once into the undo snapshot. Limit one event-loop pass by transport bytes so
// wider document formats automatically use smaller batches.
constexpr size_t kFinalizationCopyBudgetBytes = 4u * 1024u * 1024u;

TileGrid* resolvePendingGrid(
    const PendingStrokeFinalization& pending, StrokeFinalizationController::Context& ctx)
{
    if (ctx.layerModel) {
        if (auto* layer = ctx.layerModel->layerById(pending.layerId)) {
            return pending.maskTarget ? layer->maskTileGrid() : layer->pixelGrid();
        }
        return nullptr;
    }
    return ctx.getActiveLayerGrid ? ctx.getActiveLayerGrid() : nullptr;
}

} // namespace

void StrokeFinalizationController::finalize(PendingStrokeFinalization& pending, Context& ctx)
{
    if (!pending.active)
        return;

    TileGrid* grid = resolvePendingGrid(pending, ctx);
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

    if (!pending.selectionRestoreCaptured) {
        pending.selectionRestoreCaptured = true;
        if (ctx.selectionRestore) {
            pending.selectionRestore = std::move(*ctx.selectionRestore);
        }
    }
    if (pending.afterTiles.empty()) {
        pending.afterTiles.reserve(pending.flattenedKeys.size());
    }

    const size_t bytesPerTile = tileByteSize(grid->format());
    const size_t maxKeys = std::max<size_t>(1, kFinalizationCopyBudgetBytes / bytesPerTile);
    const size_t firstKey = pending.nextKey;
    const size_t requestedEndKey
        = std::min(pending.finalizationKeysOrdered.size(), firstKey + maxKeys);
    size_t endKey = requestedEndKey;

    BrushExecutionBackend* brushExecutionBackend
        = ctx.getBrushExecutionBackend ? ctx.getBrushExecutionBackend() : nullptr;
    if (pending.readbackActive && firstKey < requestedEndKey) {
        if (pending.readbackBatchKeys.empty()) {
            pending.readbackBatchKeys.assign(pending.finalizationKeysOrdered.begin() + firstKey,
                pending.finalizationKeysOrdered.begin() + requestedEndKey);
            if (brushExecutionBackend) {
                if (ctx.makeCurrent)
                    ctx.makeCurrent();
                pending.fence = brushExecutionBackend->startAsyncReadback(
                    *grid, pending.readbackBatchKeys, true);
                if (ctx.doneCurrent)
                    ctx.doneCurrent();
            }
            if (!pending.fence) {
                // Preserve the existing allocation-failure behavior: finish
                // from the current CPU tiles instead of keeping a dead pending
                // operation forever.
                pending.readbackActive = false;
                pending.readbackBatchKeys.clear();
            } else {
                // PBO submission is itself O(tile count). Yield after one small
                // batch; a later tick polls and consumes its fence.
                return;
            }
        }
    }

    if (pending.readbackActive && !pending.readbackBatchKeys.empty() && brushExecutionBackend) {
        endKey = std::min(
            pending.finalizationKeysOrdered.size(), firstKey + pending.readbackBatchKeys.size());
        if (ctx.makeCurrent)
            ctx.makeCurrent();
        const size_t consumed = brushExecutionBackend->finishReadbackBatch(pending.fence, *grid,
            pending.readbackBatchKeys, 0, pending.readbackBatchKeys.size(), true);
        if (ctx.doneCurrent)
            ctx.doneCurrent();
        if (pending.fence) {
            // A forced flush may reach the finite client-wait timeout. Keep the
            // batch intact and retry its fence rather than consuming an
            // incomplete PBO or starting another readback over the same storage.
            return;
        }
        endKey = std::min(endKey, firstKey + consumed);
    }

    for (size_t keyIndex = firstKey; keyIndex < endKey; ++keyIndex) {
        const TileKey& key = pending.finalizationKeysOrdered[keyIndex];
        TileData* tile = grid->getTile(key);
        if (pending.removeEmptyTiles && tile && tile->isEmpty()) {
            pending.removedTiles.insert(key);
            grid->removeTile(key);
            if (!pending.maskTarget && ctx.canvas) {
                ctx.canvas->tilePositionIndex().removeEntry(key, pending.layerId);
            }
            tile = nullptr;
        }

        if (pending.removedTiles.count(key)) {
            pending.afterTiles[key].resize(bytesPerTile, 0);
            continue;
        }
        if (tile) {
            auto& buf = pending.afterTiles[key];
            buf.resize(bytesPerTile);
            std::memcpy(buf.data(), tile->pixels(), bytesPerTile);
        }
    }
    pending.nextKey = endKey;
    pending.readbackBatchKeys.clear();

    if (pending.nextKey < pending.finalizationKeysOrdered.size()) {
        return;
    }

    StrokeSnapshot snapshot;
    snapshot.layerId = pending.layerId;
    snapshot.maskTarget = pending.maskTarget;
    snapshot.beforeTiles = std::move(pending.beforeTiles);
    snapshot.afterTiles = std::move(pending.afterTiles);
    snapshot.createdTiles = std::move(pending.createdTiles);
    snapshot.removedTiles = std::move(pending.removedTiles);

    if (!pending.flattenedKeys.empty() && ctx.canvas && ctx.layerModel) {
        auto cmd = std::make_unique<DrawCommand>(
            ctx.canvas, ctx.layerModel, std::move(snapshot), std::move(pending.selectionRestore));
        ctx.canvas->undoManager().push(std::move(cmd));
    }

    pending = {};
}

} // namespace aether
