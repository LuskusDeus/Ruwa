// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   A P P L I C A T O R
// ==========================================================================

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMAPPLICATOR_H
#define RUWA_CORE_TRANSFORM_TRANSFORMAPPLICATOR_H

#include "TransformState.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileFormat.h"
#include "shared/tiles/TilePixelAccess.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace aether {

// ==========================================================================
//   T R A N S F O R M   R E S U L T
// ==========================================================================

struct TransformResult {
    // Tiles that existed before but no longer exist after
    std::unordered_set<TileKey, TileKeyHash> removedTiles;

    // Tiles that were created (didn't exist before)
    std::unordered_set<TileKey, TileKeyHash> createdTiles;

    // All affected tile keys (union of touched tiles)
    std::unordered_set<TileKey, TileKeyHash> affectedTiles;

    bool success = false;
};

// ==========================================================================
//   T R A N S F O R M   A P P L I C A T O R
// ==========================================================================

class TransformApplicator {
public:
    /// Apply transform to a TileGrid in-place.
    /// Reads all pixels, resamples with bilinear interpolation,
    /// writes back to a fresh set of tiles.
    static TransformResult apply(
        TileGrid& grid, const TransformState& state, const TileGrid* selectionMask = nullptr)
    {
        TransformResult result;

        if (state.isIdentity()) {
            result.success = true;
            return result;
        }

        if (grid.empty()) {
            result.success = true;
            return result;
        }

        // 1. Record existing tile keys
        std::unordered_set<TileKey, TileKeyHash> oldKeys;
        for (const auto& [key, tile] : grid.tiles()) {
            oldKeys.insert(key);
        }

        // 2. Compute destination AABB (which tiles we need to write to)
        Rect destAABB = state.transformedAABB();
        if (selectionMask && !selectionMask->empty()) {
            Rect srcAABB = TransformState::computeContentBounds(grid);
            float minX = std::min(destAABB.left(), srcAABB.left());
            float minY = std::min(destAABB.top(), srcAABB.top());
            float maxX = std::max(destAABB.right(), srcAABB.right());
            float maxY = std::max(destAABB.bottom(), srcAABB.bottom());
            destAABB = { minX, minY, maxX - minX, maxY - minY };
        }

        // Add margin for sub-pixel coverage
        float margin = 2.0f;
        destAABB.x -= margin;
        destAABB.y -= margin;
        destAABB.width += margin * 2;
        destAABB.height += margin * 2;

        int32_t destMinTX = static_cast<int32_t>(std::floor(destAABB.left() / TILE_SIZE));
        int32_t destMinTY = static_cast<int32_t>(std::floor(destAABB.top() / TILE_SIZE));
        int32_t destMaxTX = static_cast<int32_t>(std::floor(destAABB.right() / TILE_SIZE));
        int32_t destMaxTY = static_cast<int32_t>(std::floor(destAABB.bottom() / TILE_SIZE));

        // CONTENT tiles carry the document format; MASK/coverage tiles are
        // always RGBA8. Thread both so per-pixel access stays format-correct.
        const TilePixelFormat contentFmt = grid.format();
        const TilePixelFormat maskFmt = (selectionMask && !selectionMask->empty())
            ? selectionMask->format()
            : TilePixelFormat::RGBA8;

        // 3. Build a read-only copy of the source pixels
        //    (We need to read from original while writing new data)
        std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> srcPixels;
        for (const auto& [key, tile] : grid.tiles()) {
            auto& buf = srcPixels[key];
            buf.resize(tileByteSize(contentFmt));
            std::memcpy(buf.data(), tile.pixels(), tileByteSize(contentFmt));
        }
        std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> maskPixels;
        if (selectionMask && !selectionMask->empty()) {
            for (const auto& [key, tile] : selectionMask->tiles()) {
                auto& buf = maskPixels[key];
                buf.resize(tileByteSize(maskFmt));
                std::memcpy(buf.data(), tile.pixels(), tileByteSize(maskFmt));
            }
        }
        const bool useNearestContentSample = isPureIntegerTranslation(state);

        // 4. Clear the grid (we'll rebuild it)
        grid.clear();

        // 5. For each destination tile, sample from source
        for (int32_t ty = destMinTY; ty <= destMaxTY; ++ty) {
            for (int32_t tx = destMinTX; tx <= destMaxTX; ++tx) {
                TileKey destKey { tx, ty };
                float tileOriginX = static_cast<float>(tx) * TILE_SIZE;
                float tileOriginY = static_cast<float>(ty) * TILE_SIZE;

                bool hasContent = false;
                // Temporary buffer for this tile (zero = transparent in every format)
                std::vector<uint8_t> destPixels(tileByteSize(contentFmt), 0);

                for (uint32_t ly = 0; ly < TILE_SIZE; ++ly) {
                    for (uint32_t lx = 0; lx < TILE_SIZE; ++lx) {
                        // Destination pixel center in world coords
                        float worldX = tileOriginX + lx + 0.5f;
                        float worldY = tileOriginY + ly + 0.5f;

                        // Inverse transform to find source position
                        Vector2 srcPos;
                        bool hasSrcPos = false;
                        if (state.hasDeformMesh()) {
                            hasSrcPos = state.tryInverseTransformPoint({ worldX, worldY }, srcPos);
                        } else if (state.hasFreeQuad()) {
                            float st[2] = { 0.0f, 0.0f };
                            if (!TransformState::inverseBilinear(
                                    { worldX, worldY }, *state.freeCorners, st)) {
                                hasSrcPos = false;
                            } else {
                                const float l = state.contentBounds.left();
                                const float r = state.contentBounds.right();
                                const float t = state.contentBounds.top();
                                const float b = state.contentBounds.bottom();
                                srcPos = { l + st[0] * (r - l), t + st[1] * (b - t) };
                                hasSrcPos = true;
                            }
                        } else {
                            srcPos = state.inverseTransformPoint({ worldX, worldY });
                            hasSrcPos = true;
                        }

                        // Values below are normalized premultiplied float RGBA
                        // ([0,1] for RGBA8; HDR passes through for float formats).
                        float outR = 0.0f, outG = 0.0f, outB = 0.0f, outA = 0.0f;
                        if (!maskPixels.empty()) {
                            float baseR, baseG, baseB, baseA;
                            sampleContent(srcPixels, contentFmt, worldX - 0.5f, worldY - 0.5f,
                                useNearestContentSample, baseR, baseG, baseB, baseA);
                            float maskDest = sampleMaskBilinear(
                                maskPixels, maskFmt, worldX - 0.5f, worldY - 0.5f);
                            baseR *= (1.0f - maskDest);
                            baseG *= (1.0f - maskDest);
                            baseB *= (1.0f - maskDest);
                            baseA *= (1.0f - maskDest);

                            float selR = 0.0f, selG = 0.0f, selB = 0.0f, selA = 0.0f;
                            if (hasSrcPos) {
                                sampleContent(srcPixels, contentFmt, srcPos.x - 0.5f,
                                    srcPos.y - 0.5f, useNearestContentSample, selR, selG, selB,
                                    selA);
                                float maskSrc = sampleMaskBilinear(
                                    maskPixels, maskFmt, srcPos.x - 0.5f, srcPos.y - 0.5f);
                                selR *= maskSrc;
                                selG *= maskSrc;
                                selB *= maskSrc;
                                selA *= maskSrc;
                            }

                            float invSelA = 1.0f - selA;
                            outR = selR + baseR * invSelA;
                            outG = selG + baseG * invSelA;
                            outB = selB + baseB * invSelA;
                            outA = selA + baseA * invSelA;
                        } else {
                            if (!hasSrcPos) {
                                continue;
                            }
                            sampleContent(srcPixels, contentFmt, srcPos.x - 0.5f, srcPos.y - 0.5f,
                                useNearestContentSample, outR, outG, outB, outA);
                        }

                        // Preserve the original "alpha > 0.5/255" coverage gate.
                        if (outA > (0.5f / 255.0f)) {
                            hasContent = true;
                            const float out[4] = { outR, outG, outB, outA };
                            writeTilePixelF(destPixels.data(), contentFmt, lx, ly, out);
                        }
                    }
                }

                if (hasContent) {
                    TileData& tile = grid.getOrCreateTile(destKey);
                    std::memcpy(tile.pixels(), destPixels.data(), tileByteSize(contentFmt));
                    tile.markDirty();
                    grid.markDirty(destKey);
                    result.affectedTiles.insert(destKey);
                }
            }
        }

        // 6. Determine created/removed tiles
        for (const auto& [key, tile] : grid.tiles()) {
            if (oldKeys.find(key) == oldKeys.end()) {
                result.createdTiles.insert(key);
            }
        }
        for (const auto& key : oldKeys) {
            if (!grid.hasTile(key)) {
                result.removedTiles.insert(key);
            }
            result.affectedTiles.insert(key);
        }

        result.success = true;
        return result;
    }

private:
    static bool isPureIntegerTranslation(const TransformState& state)
    {
        if (state.hasFreeQuad() || state.hasDeformMesh()) {
            return false;
        }

        constexpr float eps = 0.001f;
        return std::abs(state.rotation) < eps && std::abs(state.scale.x - 1.0f) < eps
            && std::abs(state.scale.y - 1.0f) < eps
            && std::abs(state.translation.x - std::round(state.translation.x)) < eps
            && std::abs(state.translation.y - std::round(state.translation.y)) < eps;
    }

    static void sampleContent(
        const std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>& srcPixels,
        TilePixelFormat fmt, float x, float y, bool nearest, float& outR, float& outG, float& outB,
        float& outA)
    {
        if (nearest) {
            sampleNearest(srcPixels, fmt, x, y, outR, outG, outB, outA);
            return;
        }
        sampleBilinear(srcPixels, fmt, x, y, outR, outG, outB, outA);
    }

    static void sampleNearest(
        const std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>& srcPixels,
        TilePixelFormat fmt, float x, float y, float& outR, float& outG, float& outB, float& outA)
    {
        fetchPixel(srcPixels, fmt, static_cast<int>(std::round(x)), static_cast<int>(std::round(y)),
            outR, outG, outB, outA);
    }

    /// Bilinear sample from the source tile map.
    /// Coordinates are in world space (continuous). Pixels are returned as
    /// normalized premultiplied RGBA, so we interpolate directly.
    static void sampleBilinear(
        const std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>& srcPixels,
        TilePixelFormat fmt, float x, float y, float& outR, float& outG, float& outB, float& outA)
    {
        int ix = static_cast<int>(std::floor(x));
        int iy = static_cast<int>(std::floor(y));
        float fx = x - ix;
        float fy = y - iy;

        float r00, g00, b00, a00;
        float r10, g10, b10, a10;
        float r01, g01, b01, a01;
        float r11, g11, b11, a11;

        fetchPixel(srcPixels, fmt, ix, iy, r00, g00, b00, a00);
        fetchPixel(srcPixels, fmt, ix + 1, iy, r10, g10, b10, a10);
        fetchPixel(srcPixels, fmt, ix, iy + 1, r01, g01, b01, a01);
        fetchPixel(srcPixels, fmt, ix + 1, iy + 1, r11, g11, b11, a11);

        // Bilinear interpolation (premultiplied alpha is safe to lerp)
        float w00 = (1 - fx) * (1 - fy);
        float w10 = fx * (1 - fy);
        float w01 = (1 - fx) * fy;
        float w11 = fx * fy;

        outR = r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11;
        outG = g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11;
        outB = b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11;
        outA = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
    }

    /// Fetch a single pixel from the tile map at integer coordinates, returned
    /// as normalized premultiplied float RGBA via the format-aware accessor.
    static void fetchPixel(
        const std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>& srcPixels,
        TilePixelFormat fmt, int worldX, int worldY, float& r, float& g, float& b, float& a)
    {
        TileKey key = worldToTile(static_cast<float>(worldX), static_cast<float>(worldY));
        auto it = srcPixels.find(key);
        if (it == srcPixels.end()) {
            r = g = b = a = 0.0f;
            return;
        }

        float tileOriginX = static_cast<float>(key.x) * TILE_SIZE;
        float tileOriginY = static_cast<float>(key.y) * TILE_SIZE;
        uint32_t lx = static_cast<uint32_t>(worldX - static_cast<int>(tileOriginX));
        uint32_t ly = static_cast<uint32_t>(worldY - static_cast<int>(tileOriginY));

        if (lx >= TILE_SIZE || ly >= TILE_SIZE) {
            r = g = b = a = 0.0f;
            return;
        }

        float px[4];
        readTilePixelF(it->second.data(), fmt, lx, ly, px);
        r = px[0];
        g = px[1];
        b = px[2];
        a = px[3];
    }

    static float sampleMaskBilinear(
        const std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>& maskPixels,
        TilePixelFormat fmt, float x, float y)
    {
        float r, g, b, a;
        sampleBilinear(maskPixels, fmt, x, y, r, g, b, a);
        return a;
    }
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMAPPLICATOR_H
