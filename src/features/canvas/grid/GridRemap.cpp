// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   G R I D   R E M A P
// ==========================================================================

#include "features/canvas/grid/GridRemap.h"

#include "shared/tiles/TileData.h"
#include "shared/tiles/TileGrid.h"

#include <QtConcurrent>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace ruwa::core::canvas {

namespace {

constexpr int kTS = static_cast<int>(aether::TILE_SIZE);

inline int floorDivTS(int v)
{
    int q = v / kTS;
    int r = v % kTS;
    if (r < 0) {
        q -= 1;
    }
    return q;
}

bool tileFullyInsideKeepRect(
    const aether::TileKey& key, int keepLeft, int keepTop, int keepRight, int keepBottom)
{
    const int tLeft = key.x * kTS;
    const int tTop = key.y * kTS;
    const int tRight = tLeft + kTS;
    const int tBottom = tTop + kTS;
    return tLeft >= keepLeft && tTop >= keepTop && tRight <= keepRight && tBottom <= keepBottom;
}

} // namespace

// --------------------------------------------------------------------------
//   t i l e s C r o p p e d B y R e s i z e
// --------------------------------------------------------------------------

std::vector<aether::TileKey> tilesCroppedByResize(
    const aether::TileGrid& grid, int offsetX, int offsetY, int newWidth, int newHeight)
{
    std::vector<aether::TileKey> result;
    if (newWidth <= 0 || newHeight <= 0) {
        result.reserve(grid.tiles().size());
        for (const auto& [k, _] : grid.tiles()) {
            result.push_back(k);
        }
        return result;
    }

    const int keepLeft = offsetX;
    const int keepTop = offsetY;
    const int keepRight = offsetX + newWidth;
    const int keepBottom = offsetY + newHeight;

    for (const auto& [k, _] : grid.tiles()) {
        if (!tileFullyInsideKeepRect(k, keepLeft, keepTop, keepRight, keepBottom)) {
            result.push_back(k);
        }
    }
    return result;
}

// --------------------------------------------------------------------------
//   F a s t   p a t h :   t i l e - a l i g n e d   o f f s e t
// --------------------------------------------------------------------------

namespace {

bool remapTileAligned(aether::TileGrid& grid, int offsetX, int offsetY, int newWidth, int newHeight)
{
    const int tileOffX = offsetX / kTS;
    const int tileOffY = offsetY / kTS;
    const int maxDstTX = (newWidth - 1) / kTS;
    const int maxDstTY = (newHeight - 1) / kTS;

    aether::TileGrid remapped;
    // Inherit the source grid's pixel format so the grid-level format metadata
    // survives the move-assign below and edge clearing strides correctly for
    // 16F/32F content (moved tiles already carry their own format).
    remapped.setFormat(grid.format());
    const int bpp = static_cast<int>(aether::tileBytesPerPixel(grid.format()));
    std::vector<aether::TileKey> edgeKeys;

    auto& srcMap = grid.tiles();
    for (auto it = srcMap.begin(); it != srcMap.end(); ++it) {
        const aether::TileKey dstKey { it->first.x - tileOffX, it->first.y - tileOffY };
        if (dstKey.x < 0 || dstKey.y < 0 || dstKey.x > maxDstTX || dstKey.y > maxDstTY) {
            continue;
        }
        auto& dstTile = remapped.getOrCreateTile(dstKey);
        dstTile = std::move(it->second);
        dstTile.markDirty();

        const int tileRight = (dstKey.x + 1) * kTS;
        const int tileBottom = (dstKey.y + 1) * kTS;
        if (tileRight > newWidth || tileBottom > newHeight) {
            edgeKeys.push_back(dstKey);
        }
    }

    if (!edgeKeys.empty()) {
        QtConcurrent::blockingMap(edgeKeys, [&](const aether::TileKey& k) {
            aether::TileData* tile = remapped.getTile(k);
            if (!tile)
                return;
            uint8_t* px = tile->pixels();
            const int localMaxX = std::min(kTS, newWidth - k.x * kTS);
            const int localMaxY = std::min(kTS, newHeight - k.y * kTS);
            if (localMaxY < kTS && localMaxY >= 0) {
                std::memset(px + localMaxY * kTS * bpp, 0, (kTS - localMaxY) * kTS * bpp);
            }
            if (localMaxX < kTS && localMaxX >= 0) {
                for (int y = 0; y < localMaxY; ++y) {
                    std::memset(px + (y * kTS + localMaxX) * bpp, 0, (kTS - localMaxX) * bpp);
                }
            }
        });
    }

    const bool anyCopied = !remapped.tiles().empty();
    grid = std::move(remapped);
    return anyCopied;
}

} // namespace

// --------------------------------------------------------------------------
//   r e m a p G r i d F o r C a n v a s R e c t
// --------------------------------------------------------------------------

bool remapGridForCanvasRect(
    aether::TileGrid& grid, int offsetX, int offsetY, int newWidth, int newHeight)
{
    if (newWidth <= 0 || newHeight <= 0) {
        grid.clear();
        return true;
    }

    if (grid.tiles().empty()) {
        return false;
    }

    if ((offsetX % kTS == 0) && (offsetY % kTS == 0)) {
        return remapTileAligned(grid, offsetX, offsetY, newWidth, newHeight);
    }

    // ----- General path: arbitrary pixel-offset -----

    const auto& srcTiles = grid.tiles();

    // Collect the set of dst tile keys that receive any pixels.
    std::unordered_set<aether::TileKey, aether::TileKeyHash> dstKeySet;
    dstKeySet.reserve(srcTiles.size() * 2);
    for (const auto& [srcKey, _] : srcTiles) {
        const int dxMin = srcKey.x * kTS - offsetX;
        const int dyMin = srcKey.y * kTS - offsetY;
        const int dxMax = dxMin + kTS - 1;
        const int dyMax = dyMin + kTS - 1;
        const int cxMin = std::max(0, dxMin);
        const int cyMin = std::max(0, dyMin);
        const int cxMax = std::min(newWidth - 1, dxMax);
        const int cyMax = std::min(newHeight - 1, dyMax);
        if (cxMin > cxMax || cyMin > cyMax) {
            continue;
        }
        const int tyMin = floorDivTS(cyMin);
        const int tyMax = floorDivTS(cyMax);
        const int txMin = floorDivTS(cxMin);
        const int txMax = floorDivTS(cxMax);
        for (int ty = tyMin; ty <= tyMax; ++ty) {
            for (int tx = txMin; tx <= txMax; ++tx) {
                dstKeySet.insert({ tx, ty });
            }
        }
    }

    if (dstKeySet.empty()) {
        grid.clear();
        return false;
    }

    std::vector<aether::TileKey> dstKeys(dstKeySet.begin(), dstKeySet.end());

    // Pre-create empty dst tile slots so the map is stable during parallel fill.
    // Inherit the source format so dst tiles allocate at the correct byte size
    // (and the grid-level format survives the move-assign below).
    aether::TileGrid remapped;
    remapped.setFormat(grid.format());
    const int bpp = static_cast<int>(aether::tileBytesPerPixel(grid.format()));
    for (const auto& k : dstKeys) {
        remapped.getOrCreateTile(k);
    }

    // Parallel fill: each worker owns one dst tile. No shared writes.
    QtConcurrent::blockingMap(dstKeys, [&](const aether::TileKey& dstKey) {
        aether::TileData* dstTile = remapped.getTile(dstKey);
        if (!dstTile)
            return;
        uint8_t* dstPixels = dstTile->pixels(); // lazy format-sized alloc + zero-init

        const int dstTileLeft = dstKey.x * kTS;
        const int dstTileTop = dstKey.y * kTS;
        const int dstXMin = std::max(dstTileLeft, 0);
        const int dstYMin = std::max(dstTileTop, 0);
        const int dstXMax = std::min(dstTileLeft + kTS, newWidth);
        const int dstYMax = std::min(dstTileTop + kTS, newHeight);
        if (dstXMin >= dstXMax || dstYMin >= dstYMax)
            return;

        const int srcXMin = dstXMin + offsetX;
        const int srcXMax = dstXMax + offsetX;
        const int srcYMin = dstYMin + offsetY;
        const int srcYMax = dstYMax + offsetY;

        const int stxMin = floorDivTS(srcXMin);
        const int stxMax = floorDivTS(srcXMax - 1);
        const int styMin = floorDivTS(srcYMin);
        const int styMax = floorDivTS(srcYMax - 1);

        for (int sty = styMin; sty <= styMax; ++sty) {
            for (int stx = stxMin; stx <= stxMax; ++stx) {
                auto it = srcTiles.find({ stx, sty });
                if (it == srcTiles.end())
                    continue;
                const uint8_t* srcPixels = it->second.pixels();

                const int srcTileLeft = stx * kTS;
                const int srcTileTop = sty * kTS;
                const int clipSXMin = std::max(srcTileLeft, srcXMin);
                const int clipSYMin = std::max(srcTileTop, srcYMin);
                const int clipSXMax = std::min(srcTileLeft + kTS, srcXMax);
                const int clipSYMax = std::min(srcTileTop + kTS, srcYMax);
                const int w = clipSXMax - clipSXMin;
                const int h = clipSYMax - clipSYMin;
                if (w <= 0 || h <= 0)
                    continue;

                const int srcLocalX = clipSXMin - srcTileLeft;
                const int srcLocalY = clipSYMin - srcTileTop;
                const int dstLocalX = (clipSXMin - offsetX) - dstTileLeft;
                const int dstLocalY = (clipSYMin - offsetY) - dstTileTop;

                const int rowBytes = w * bpp;
                const int stride = kTS * bpp;

                for (int y = 0; y < h; ++y) {
                    const uint8_t* srcRow = srcPixels + (srcLocalY + y) * stride + srcLocalX * bpp;
                    uint8_t* dstRow = dstPixels + (dstLocalY + y) * stride + dstLocalX * bpp;
                    std::memcpy(dstRow, srcRow, rowBytes);
                }
            }
        }
        dstTile->markDirty();
    });

    grid = std::move(remapped);
    return true;
}

} // namespace ruwa::core::canvas
