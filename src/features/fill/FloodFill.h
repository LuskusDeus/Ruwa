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

/// Selection coverage acts as a per-pixel **alpha ceiling** on every fill
/// result (the same rule the brush commit uses for soft selections, see
/// composite.frag.glsl :: uClipMaskAsAlphaCap). Clamps a premultiplied result
/// to `capAlpha`, scaling RGB proportionally so the unmultiplied color stays
/// stable. `capAlpha == 255` is a no-op.
///
/// Callers pass `fillSelectionAlphaCeiling(coverage, destinationAlpha,
/// fillAlpha)`, not the raw coverage: the ceiling stops a fill from *adding*
/// alpha beyond what the selection covers, it never *reduces* alpha that was
/// already there.
inline void fillApplySelectionAlphaCap(
    uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, uint8_t capAlpha)
{
    if (a <= capAlpha)
        return;
    if (a == 0) {
        r = g = b = 0;
        return;
    }
    const uint32_t num = static_cast<uint32_t>(capAlpha);
    const uint32_t den = static_cast<uint32_t>(a);
    r = static_cast<uint8_t>(static_cast<uint32_t>(r) * num / den);
    g = static_cast<uint8_t>(static_cast<uint32_t>(g) * num / den);
    b = static_cast<uint8_t>(static_cast<uint32_t>(b) * num / den);
    a = capAlpha;
}

/// Strength (0..255) the fill source is applied with at a partially selected
/// pixel: `min(1, coverage / destinationAlpha)`.
///
/// Two different jobs hide behind one coverage value. Where coverage reaches
/// the destination's own alpha, the selection describes the *shape* being
/// filled (a content selection traces the layer's own soft alpha; an empty
/// pixel inside a feathered edge has nothing to protect) — the fill goes down
/// at full strength and the ceiling below trims its alpha, reproducing the
/// silhouette in the new color instead of stacking a second layer of alpha on
/// top of it. Where the destination is denser than the coverage, the selection
/// describes an *edge* across existing content (the antialiased rim of a lasso
/// over an opaque area) — the fill is scaled down so the boundary blends
/// instead of cutting a hard step.
///
/// The ratio matters, not a branch between the two: a selection mask is a
/// quantized copy of the coverage it describes, so `coverage` and
/// `destinationAlpha` cross each other back and forth all along a soft
/// gradient. Switching behaviour on that comparison posterized the fill into
/// visible contour bands, one per quantization step. As a ratio the two cases
/// meet continuously at coverage == destinationAlpha, where both give 255.
///
/// Fully covered pixels always return 255, so hard selections keep plain
/// src-over.
inline uint8_t fillSelectionSourceStrength(uint8_t coverage, uint8_t destinationAlpha)
{
    if (destinationAlpha == 0 || coverage >= destinationAlpha) {
        return 255;
    }
    const uint32_t scaled = (static_cast<uint32_t>(coverage) * 255u
                                + static_cast<uint32_t>(destinationAlpha) / 2u)
        / static_cast<uint32_t>(destinationAlpha);
    return static_cast<uint8_t>(std::min<uint32_t>(scaled, 255u));
}

/// Per-pixel alpha ceiling for a fill:
///
///     ceiling = destinationAlpha + fillAlpha * max(0, coverage - destinationAlpha)
///
/// i.e. the fill may pull a pixel's alpha from where it is towards the
/// selection's coverage, in proportion to its own alpha, and only upwards. A
/// pixel selected at 50% that already holds opaque content keeps its alpha and
/// merely takes the fill color; a pixel selected at 50% and filled with a 50%
/// color lands at 25%, so feathered edges still fade out.
///
/// Two invariants this has to satisfy, both verified in the fill tests:
///   * `coverage == 255` reproduces the plain src-over alpha exactly (integer
///     arithmetic included), so hard selections never enter the clamp at all.
///   * repeated fills converge instead of stacking, which is the bug the
///     ceiling exists for.
inline uint8_t fillSelectionAlphaCeiling(
    uint8_t coverage, uint8_t destinationAlpha, uint8_t fillAlpha)
{
    if (coverage <= destinationAlpha) {
        return destinationAlpha;
    }
    const uint32_t gain = (static_cast<uint32_t>(fillAlpha)
                                  * static_cast<uint32_t>(coverage - destinationAlpha)
                              + 127u)
        / 255u;
    return static_cast<uint8_t>(
        std::min<uint32_t>(static_cast<uint32_t>(destinationAlpha) + gain, 255u));
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

/// Deep-copy the content tiles of a live grid for read-only background processing.
/// The snapshot preserves the grid's native pixel format.
FloodFillResult::RawTileMap snapshotContentTiles(const TileGrid& grid);

/// Build a read-only, contiguous exact-color selection mask using the same
/// region detection as the classic fill tool. The source grid is not modified.
FloodFillResult::RawTileMap buildMagicWandSelectionMask(
    const TileGrid& grid, int seedX, int seedY, int canvasWidth, int canvasHeight);

/// Build a Magic Wand selection from an immutable content snapshot. This overload
/// is suitable for worker threads and never accesses a live TileGrid.
FloodFillResult::RawTileMap buildMagicWandSelectionMask(
    const FloodFillResult::RawTileMap& sourceTiles, int seedX, int seedY, int canvasWidth,
    int canvasHeight, TilePixelFormat contentFormat = kDefaultTileFormat);

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
///
/// If `preserveDestinationAlpha` is true, the fill changes premultiplied color
/// only and keeps each destination pixel's alpha unchanged (alpha-lock
/// semantics). In that mode a selection mask scales the fill coverage.
FloodFillResult fillMaskTiles(TileGrid& grid, const FloodFillResult::RawTileMap& maskTiles,
    uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const TileGrid* selectionMask = nullptr, bool preserveDestinationAlpha = false);

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
