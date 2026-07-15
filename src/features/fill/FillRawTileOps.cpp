// SPDX-License-Identifier: MPL-2.0

#include "features/fill/FillRawTileOps.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace aether {

namespace {

inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

inline uint32_t floorMod(int32_t a, int32_t b)
{
    const int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

} // namespace

void setRawPixel(std::vector<uint8_t>& raw, uint32_t localX, uint32_t localY, uint8_t r, uint8_t g,
    uint8_t b, uint8_t a, TilePixelFormat fmt)
{
    // CONTENT write: store through the format-aware accessor (raw is sized for
    // the target grid's format).
    const float f[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    writeTilePixelF(raw.data(), fmt, localX, localY, f);
}

void writeMaskPixel(aether::FloodFillResult::RawTileMap& maskTiles, const aether::TileKey& key,
    uint32_t localX, uint32_t localY)
{
    // MASK / coverage tiles are always RGBA8.
    std::vector<uint8_t>& maskTile = ensureRawTile(maskTiles, key, TilePixelFormat::RGBA8);
    const uint32_t idx = rawPixelIndex(localX, localY);
    maskTile[idx + 0] = 255;
    maskTile[idx + 1] = 255;
    maskTile[idx + 2] = 255;
    maskTile[idx + 3] = 255;
}

std::vector<uint8_t>& ensureResultTile(const aether::FloodFillResult::RawTileMap& sourceTiles,
    aether::FloodFillResult& result, const aether::TileKey& key, TilePixelFormat fmt)
{
    auto it = result.afterTiles.find(key);
    if (it != result.afterTiles.end()) {
        return it->second;
    }

    auto sourceIt = sourceTiles.find(key);
    if (sourceIt != sourceTiles.end() && sourceIt->second.size() == tileByteSize(fmt)) {
        result.beforeTiles.try_emplace(key, sourceIt->second);
        auto [afterIt, _] = result.afterTiles.emplace(key, sourceIt->second);
        return afterIt->second;
    }

    result.createdTiles.insert(key);
    auto [afterIt, _] = result.afterTiles.emplace(key, std::vector<uint8_t>(tileByteSize(fmt), 0));
    return afterIt->second;
}

bool rawTileIsEmpty(const std::vector<uint8_t>& tile, TilePixelFormat fmt)
{
    // CONTENT tile: "empty" means every pixel's alpha is zero. Tested through the
    // format-aware accessor so this is correct for any tile format.
    if (tile.size() != tileByteSize(fmt)) {
        return true;
    }

    for (uint32_t y = 0; y < aether::TILE_SIZE; ++y) {
        for (uint32_t x = 0; x < aether::TILE_SIZE; ++x) {
            if (!tilePixelAlphaIsZero(tile.data(), fmt, x, y)) {
                return false;
            }
        }
    }
    return true;
}

bool rawTilesEqual(
    const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs, TilePixelFormat fmt)
{
    return lhs.size() == rhs.size() && lhs.size() == tileByteSize(fmt)
        && std::memcmp(lhs.data(), rhs.data(), tileByteSize(fmt)) == 0;
}

bool computeRawMaskPixelBounds(const aether::FloodFillResult::RawTileMap& maskTiles, int canvasW,
    int canvasH, int& outMinX, int& outMinY, int& outWidth, int& outHeight)
{
    const bool clipToCanvas = (canvasW > 0 && canvasH > 0);
    bool hasAny = false;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;

    for (const auto& [key, tile] : maskTiles) {
        if (tile.size() != aether::TILE_BYTE_SIZE) {
            continue;
        }

        const int baseX = key.x * static_cast<int>(aether::TILE_SIZE);
        const int baseY = key.y * static_cast<int>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            const int worldY = baseY + static_cast<int>(localY);
            if (clipToCanvas && (worldY < 0 || worldY >= canvasH)) {
                continue;
            }
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const int worldX = baseX + static_cast<int>(localX);
                if (clipToCanvas && (worldX < 0 || worldX >= canvasW)) {
                    continue;
                }

                const uint32_t idx = rawPixelIndex(localX, localY);
                if (tile[idx + 3] == 0) {
                    continue;
                }

                if (!hasAny) {
                    minX = maxX = worldX;
                    minY = maxY = worldY;
                    hasAny = true;
                } else {
                    minX = std::min(minX, worldX);
                    minY = std::min(minY, worldY);
                    maxX = std::max(maxX, worldX);
                    maxY = std::max(maxY, worldY);
                }
            }
        }
    }

    if (!hasAny) {
        return false;
    }

    outMinX = minX;
    outMinY = minY;
    outWidth = maxX - minX + 1;
    outHeight = maxY - minY + 1;
    return outWidth > 0 && outHeight > 0;
}

void normalizeFloodFillResultAgainstSource(const aether::FloodFillResult::RawTileMap& sourceTiles,
    aether::FloodFillResult& result, TilePixelFormat fmt)
{
    std::vector<aether::TileKey> keys;
    keys.reserve(result.afterTiles.size());
    for (const auto& [key, _] : result.afterTiles) {
        keys.push_back(key);
    }

    for (const aether::TileKey& key : keys) {
        auto afterIt = result.afterTiles.find(key);
        if (afterIt == result.afterTiles.end()) {
            continue;
        }

        auto sourceIt = sourceTiles.find(key);
        const bool hadSourceTile
            = sourceIt != sourceTiles.end() && sourceIt->second.size() == tileByteSize(fmt);
        if (hadSourceTile) {
            if (rawTileIsEmpty(afterIt->second, fmt)) {
                result.afterTiles.erase(afterIt);
                result.removedTiles.insert(key);
                continue;
            }
            if (rawTilesEqual(afterIt->second, sourceIt->second, fmt)) {
                result.afterTiles.erase(afterIt);
                result.beforeTiles.erase(key);
                result.fillMaskTiles.erase(key);
                result.removedTiles.erase(key);
            }
            continue;
        }

        if (rawTileIsEmpty(afterIt->second, fmt)) {
            result.afterTiles.erase(afterIt);
            result.createdTiles.erase(key);
            result.fillMaskTiles.erase(key);
            continue;
        }

        result.createdTiles.insert(key);
    }

    result.pixelsFilled = 0;
    for (const auto& [_, tile] : result.fillMaskTiles) {
        if (tile.size() != aether::TILE_BYTE_SIZE) {
            continue;
        }
        for (size_t idx = 3; idx < tile.size(); idx += aether::TILE_CHANNELS) {
            if (tile[idx] != 0) {
                ++result.pixelsFilled;
            }
        }
    }
}

aether::FloodFillResult::RawTileMap translateRawMaskTilesToWorld(
    const aether::FloodFillResult::RawTileMap& localMaskTiles, int offsetX, int offsetY)
{
    if ((offsetX == 0 && offsetY == 0) || localMaskTiles.empty()) {
        return localMaskTiles;
    }

    aether::FloodFillResult::RawTileMap worldMaskTiles;
    for (const auto& [localKey, maskTile] : localMaskTiles) {
        if (maskTile.size() != aether::TILE_BYTE_SIZE) {
            continue;
        }

        const int baseLocalX = localKey.x * static_cast<int>(aether::TILE_SIZE);
        const int baseLocalY = localKey.y * static_cast<int>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const uint32_t idx = rawPixelIndex(localX, localY);
                if (maskTile[idx + 3] == 0) {
                    continue;
                }

                const int worldX = baseLocalX + static_cast<int>(localX) + offsetX;
                const int worldY = baseLocalY + static_cast<int>(localY) + offsetY;
                const aether::TileKey worldKey { floorDiv(worldX,
                                                     static_cast<int32_t>(aether::TILE_SIZE)),
                    floorDiv(worldY, static_cast<int32_t>(aether::TILE_SIZE)) };
                const uint32_t worldLocalX
                    = floorMod(worldX, static_cast<int32_t>(aether::TILE_SIZE));
                const uint32_t worldLocalY
                    = floorMod(worldY, static_cast<int32_t>(aether::TILE_SIZE));
                writeMaskPixel(worldMaskTiles, worldKey, worldLocalX, worldLocalY);
            }
        }
    }

    return worldMaskTiles;
}

aether::FloodFillResult::RawTileMap translateRawTilesToWorld(
    const aether::FloodFillResult::RawTileMap& localTiles, int offsetX, int offsetY,
    TilePixelFormat fmt)
{
    if ((offsetX == 0 && offsetY == 0) || localTiles.empty()) {
        return localTiles;
    }

    aether::FloodFillResult::RawTileMap worldTiles;
    for (const auto& [localKey, rawTile] : localTiles) {
        if (rawTile.size() != tileByteSize(fmt)) {
            continue;
        }

        const int baseLocalX = localKey.x * static_cast<int>(aether::TILE_SIZE);
        const int baseLocalY = localKey.y * static_cast<int>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                float f[4];
                readTilePixelF(rawTile.data(), fmt, localX, localY, f);
                const uint8_t r = fillQuantizeChannel(f[0]);
                const uint8_t g = fillQuantizeChannel(f[1]);
                const uint8_t b = fillQuantizeChannel(f[2]);
                const uint8_t a = fillQuantizeChannel(f[3]);
                if (r == 0 && g == 0 && b == 0 && a == 0) {
                    continue;
                }

                const int worldX = baseLocalX + static_cast<int>(localX) + offsetX;
                const int worldY = baseLocalY + static_cast<int>(localY) + offsetY;
                const aether::TileKey worldKey { floorDiv(worldX,
                                                     static_cast<int32_t>(aether::TILE_SIZE)),
                    floorDiv(worldY, static_cast<int32_t>(aether::TILE_SIZE)) };
                const uint32_t worldLocalX
                    = floorMod(worldX, static_cast<int32_t>(aether::TILE_SIZE));
                const uint32_t worldLocalY
                    = floorMod(worldY, static_cast<int32_t>(aether::TILE_SIZE));
                std::vector<uint8_t>& worldTile = ensureRawTile(worldTiles, worldKey, fmt);
                setRawPixel(worldTile, worldLocalX, worldLocalY, r, g, b, a, fmt);
            }
        }
    }

    return worldTiles;
}

void translateFloodFillResultToWorld(const aether::FloodFillResult::RawTileMap& sourceTiles,
    int offsetX, int offsetY, aether::FloodFillResult& result, TilePixelFormat fmt)
{
    if (offsetX == 0 && offsetY == 0) {
        normalizeFloodFillResultAgainstSource(sourceTiles, result, fmt);
        return;
    }

    aether::FloodFillResult worldResult;
    for (const auto& [localKey, maskTile] : result.fillMaskTiles) {
        if (maskTile.size() != aether::TILE_BYTE_SIZE) {
            continue;
        }

        const int baseLocalX = localKey.x * static_cast<int>(aether::TILE_SIZE);
        const int baseLocalY = localKey.y * static_cast<int>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const uint32_t idx = rawPixelIndex(localX, localY);
                if (maskTile[idx + 3] == 0) {
                    continue;
                }

                const int srcX = baseLocalX + static_cast<int>(localX);
                const int srcY = baseLocalY + static_cast<int>(localY);
                const int worldX = srcX + offsetX;
                const int worldY = srcY + offsetY;
                const aether::TileKey worldKey { floorDiv(worldX,
                                                     static_cast<int32_t>(aether::TILE_SIZE)),
                    floorDiv(worldY, static_cast<int32_t>(aether::TILE_SIZE)) };
                const uint32_t worldLocalX
                    = floorMod(worldX, static_cast<int32_t>(aether::TILE_SIZE));
                const uint32_t worldLocalY
                    = floorMod(worldY, static_cast<int32_t>(aether::TILE_SIZE));

                std::vector<uint8_t>& worldAfterTile
                    = ensureResultTile(sourceTiles, worldResult, worldKey, fmt);
                const PremultPixel outPx = sampleRawPixel(result.afterTiles, srcX, srcY, fmt);
                setRawPixel(worldAfterTile, worldLocalX, worldLocalY, outPx.r, outPx.g, outPx.b,
                    outPx.a, fmt);
                writeMaskPixel(worldResult.fillMaskTiles, worldKey, worldLocalX, worldLocalY);
            }
        }
    }

    normalizeFloodFillResultAgainstSource(sourceTiles, worldResult, fmt);
    result = std::move(worldResult);
}

void clipFloodFillResultToSelectionMask(const aether::FloodFillResult::RawTileMap& sourceTiles,
    const aether::FloodFillResult::RawTileMap& selectionMaskTiles, aether::FloodFillResult& result,
    TilePixelFormat fmt)
{
    if (selectionMaskTiles.empty() || result.fillMaskTiles.empty()) {
        return;
    }

    aether::FloodFillResult::RawTileMap clippedBeforeTiles;
    aether::FloodFillResult::RawTileMap clippedAfterTiles;
    aether::FloodFillResult::RawTileMap clippedMaskTiles;
    std::unordered_set<aether::TileKey, aether::TileKeyHash> clippedCreatedTiles;
    std::unordered_set<aether::TileKey, aether::TileKeyHash> clippedRemovedTiles;
    int clippedPixelsFilled = 0;

    for (const auto& [key, maskTile] : result.fillMaskTiles) {
        // fillMaskTiles are RGBA8 coverage tiles.
        if (maskTile.size() != aether::TILE_BYTE_SIZE) {
            continue;
        }

        auto sourceIt = sourceTiles.find(key);
        const bool hadSourceTile
            = sourceIt != sourceTiles.end() && sourceIt->second.size() == tileByteSize(fmt);
        const std::vector<uint8_t>* sourceTile = hadSourceTile ? &sourceIt->second : nullptr;

        std::vector<uint8_t> clippedAfterTile(
            hadSourceTile ? *sourceTile : std::vector<uint8_t>(tileByteSize(fmt), 0));
        bool tileHasSelectedChanges = false;
        aether::FloodFillResult::RawTileMap tileMaskTiles;
        int tilePixelsFilled = 0;

        // Selection mask is RGBA8.
        const std::vector<uint8_t>* selectionTile = nullptr;
        if (auto selectionIt = selectionMaskTiles.find(key); selectionIt != selectionMaskTiles.end()
            && selectionIt->second.size() == aether::TILE_BYTE_SIZE) {
            selectionTile = &selectionIt->second;
        }

        // afterTiles are CONTENT (document format).
        const std::vector<uint8_t>* filledAfterTile = nullptr;
        if (auto afterIt = result.afterTiles.find(key);
            afterIt != result.afterTiles.end() && afterIt->second.size() == tileByteSize(fmt)) {
            filledAfterTile = &afterIt->second;
        }

        const bool tileRemovedByFill = result.removedTiles.count(key) > 0;

        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const uint32_t idx = rawPixelIndex(localX, localY);
                if (maskTile[idx + 3] == 0) {
                    continue;
                }
                if (!selectionTile || (*selectionTile)[idx + 3] == 0) {
                    continue;
                }

                tileHasSelectedChanges = true;
                if (tileRemovedByFill) {
                    setRawPixel(clippedAfterTile, localX, localY, 0, 0, 0, 0, fmt);
                } else if (filledAfterTile) {
                    float f[4];
                    readTilePixelF(filledAfterTile->data(), fmt, localX, localY, f);
                    setRawPixel(clippedAfterTile, localX, localY, fillQuantizeChannel(f[0]),
                        fillQuantizeChannel(f[1]), fillQuantizeChannel(f[2]),
                        fillQuantizeChannel(f[3]), fmt);
                }

                writeMaskPixel(tileMaskTiles, key, localX, localY);
                ++tilePixelsFilled;
            }
        }

        if (!tileHasSelectedChanges) {
            continue;
        }

        if (hadSourceTile) {
            if (rawTileIsEmpty(clippedAfterTile, fmt)) {
                clippedBeforeTiles.emplace(key, *sourceTile);
                clippedRemovedTiles.insert(key);
                clippedMaskTiles.merge(tileMaskTiles);
                clippedPixelsFilled += tilePixelsFilled;
                continue;
            }

            if (!rawTilesEqual(clippedAfterTile, *sourceTile, fmt)) {
                clippedBeforeTiles.emplace(key, *sourceTile);
                clippedAfterTiles.emplace(key, std::move(clippedAfterTile));
                clippedMaskTiles.merge(tileMaskTiles);
                clippedPixelsFilled += tilePixelsFilled;
            }
            continue;
        }

        if (!rawTileIsEmpty(clippedAfterTile, fmt)) {
            clippedCreatedTiles.insert(key);
            clippedAfterTiles.emplace(key, std::move(clippedAfterTile));
            clippedMaskTiles.merge(tileMaskTiles);
            clippedPixelsFilled += tilePixelsFilled;
        }
    }

    result.beforeTiles = std::move(clippedBeforeTiles);
    result.afterTiles = std::move(clippedAfterTiles);
    result.fillMaskTiles = std::move(clippedMaskTiles);
    result.createdTiles = std::move(clippedCreatedTiles);
    result.removedTiles = std::move(clippedRemovedTiles);
    result.pixelsFilled = clippedPixelsFilled;
}

} // namespace aether
