// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   F L O O D   F I L L
// ==========================================================================

#ifndef RUWA_CORE_FILL_FLOODFILL_H
#define RUWA_CORE_FILL_FLOODFILL_H

#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TilePixelAccess.h"
#include "shared/types/Types.h"

#include <algorithm>
#include <cstdint>

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

namespace aether {

inline int32_t floodFillFloorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

inline uint32_t floodFillFloorMod(int32_t a, int32_t b)
{
    const int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

/// Result of flood fill: tile snapshots for Undo and affected tile keys
struct FloodFillResult {
    using RawTileMap = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;

    RawTileMap beforeTiles;
    RawTileMap afterTiles;
    RawTileMap fillMaskTiles;
    std::unordered_set<TileKey, TileKeyHash> createdTiles;
    std::unordered_set<TileKey, TileKeyHash> removedTiles;
    int pixelsFilled = 0;
};

/// Sample alpha from selection mask at (x, y). Returns 0 if outside or no mask.
inline uint8_t fillMaskAlphaAt(const TileGrid* grid, int32_t x, int32_t y)
{
    if (!grid)
        return 0;

    const int32_t tx = floodFillFloorDiv(x, static_cast<int32_t>(TILE_SIZE));
    const int32_t ty = floodFillFloorDiv(y, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localX = floodFillFloorMod(x, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localY = floodFillFloorMod(y, static_cast<int32_t>(TILE_SIZE));
    const TileData* tile = grid->getTile(TileKey { tx, ty });
    if (!tile)
        return 0;

    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return tile->pixels()[idx + 3];
}

/// Sample pixel from grid at (x, y). Returns false if outside bounds.
/// If tile is missing, returns true with r=g=b=a=0 (transparent).
inline bool samplePixelAt(
    const TileGrid* grid, int32_t x, int32_t y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a)
{
    if (!grid)
        return false;

    const int32_t tx = floodFillFloorDiv(x, static_cast<int32_t>(TILE_SIZE));
    const int32_t ty = floodFillFloorDiv(y, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localX = floodFillFloorMod(x, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localY = floodFillFloorMod(y, static_cast<int32_t>(TILE_SIZE));
    const TileData* tile = grid->getTile(TileKey { tx, ty });
    if (!tile) {
        r = g = b = a = 0;
        return true;
    }

    // CONTENT read: format-aware, quantized to 8-bit premultiplied for the fill
    // algorithm (round-trips RGBA8 exactly).
    float f[4];
    readTilePixelF(*tile, localX, localY, f);
    auto q = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    r = q(f[0]);
    g = q(f[1]);
    b = q(f[2]);
    a = q(f[3]);
    return true;
}

/// Flood fill: replace contiguous region of matching seed color with fill color.
/// Respects selection mask if provided. Returns snapshots for Undo.
FloodFillResult floodFill(TileGrid& grid, int seedX, int seedY, uint8_t fillR, uint8_t fillG,
    uint8_t fillB, uint8_t fillA, const TileGrid* selectionMask, int canvasWidth, int canvasHeight);

/// Classic flood fill: exact-match 4-connected region replacement without soft-edge preservation.
FloodFillResult classicFloodFill(TileGrid& grid, int seedX, int seedY, uint8_t fillR, uint8_t fillG,
    uint8_t fillB, uint8_t fillA, const TileGrid* selectionMask, int canvasWidth, int canvasHeight);

/// Flood fill against raw snapshot tiles without mutating a live TileGrid.
/// `contentFormat` is the pixel format of the CONTENT (source/result) raw tiles —
/// the format of the grid that was snapshotted (document layer vs RGBA8 mask).
FloodFillResult floodFillRawTiles(const FloodFillResult::RawTileMap& sourceTiles, int seedX,
    int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const FloodFillResult::RawTileMap& selectionMaskTiles, int canvasWidth, int canvasHeight,
    TilePixelFormat contentFormat = kDefaultTileFormat);

/// Classic flood fill against raw snapshot tiles without soft-edge semantics.
FloodFillResult classicFloodFillRawTiles(const FloodFillResult::RawTileMap& sourceTiles, int seedX,
    int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const FloodFillResult::RawTileMap& selectionMaskTiles, int canvasWidth, int canvasHeight,
    TilePixelFormat contentFormat = kDefaultTileFormat);

/// Fill polygon interior with color (scanline algorithm). Returns snapshots for Undo.
///
/// If `selectionMask` is non-null the result is gated by the selection mask
/// alpha as a per-pixel **alpha cap**: pre-existing layer alpha already above
/// the cap is left untouched; otherwise the fill blends normally and the
/// resulting alpha is clamped to the cap (RGB scaled to preserve color under
/// premultiplied storage). Pass `nullptr` for unrestricted polygon fill.
FloodFillResult fillPolygon(TileGrid& grid, const std::vector<Vector2>& polygon, uint8_t fillR,
    uint8_t fillG, uint8_t fillB, uint8_t fillA, int canvasWidth, int canvasHeight,
    const TileGrid* selectionMask = nullptr);

/// Apply an already-rasterized fill mask to the grid using the same stroke fill
/// compositing semantics as `fillPolygon`.
FloodFillResult fillMaskTiles(TileGrid& grid, const FloodFillResult::RawTileMap& maskTiles,
    uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const TileGrid* selectionMask = nullptr);

/// Build polygon fill preview without mutating the live grid.
FloodFillResult previewFillPolygon(const TileGrid& grid, const std::vector<Vector2>& polygon,
    uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA, int canvasWidth, int canvasHeight,
    const TileGrid* selectionMask = nullptr);

/// Build polygon fill preview against raw snapshot tiles without mutating a live grid.
///
/// `selectionMaskTiles` follows the same alpha-cap semantics as `fillPolygon`'s
/// `selectionMask` and may be null for unrestricted fill.
FloodFillResult previewFillPolygonRawTiles(const FloodFillResult::RawTileMap& sourceTiles,
    const std::vector<Vector2>& polygon, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    int canvasWidth, int canvasHeight,
    const FloodFillResult::RawTileMap* selectionMaskTiles = nullptr,
    TilePixelFormat contentFormat = kDefaultTileFormat);

/// Build polygon fill mask without preview pixel snapshots.
FloodFillResult previewFillPolygonMask(
    const std::vector<Vector2>& polygon, int canvasWidth, int canvasHeight);

} // namespace aether

#endif // RUWA_CORE_FILL_FLOODFILL_H
