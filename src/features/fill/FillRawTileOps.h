// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_FILL_RAW_TILE_OPS_H
#define AETHER_FILL_RAW_TILE_OPS_H

#include "features/fill/FloodFill.h"
#include "shared/tiles/TileFormat.h"
#include "shared/tiles/TilePixelAccess.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace aether {

// Channel index of pixel (x,y). Valid only for RGBA8 (1 byte / channel) buffers,
// i.e. MASK / coverage tiles. CONTENT tiles use the format-aware accessor below.
inline uint32_t rawPixelIndex(uint32_t localX, uint32_t localY)
{
    return (localY * aether::TILE_SIZE + localX) * aether::TILE_CHANNELS;
}

// Quantize a normalized [0,1] float channel to 8-bit (round-trips RGBA8 exactly).
inline uint8_t fillQuantizeChannel(float v)
{
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct PremultPixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

inline int progressivePixelDistance(const PremultPixel& lhs, const PremultPixel& rhs)
{
    return std::max(std::max(std::abs(static_cast<int>(lhs.r) - static_cast<int>(rhs.r)),
                        std::abs(static_cast<int>(lhs.g) - static_cast<int>(rhs.g))),
        std::max(std::abs(static_cast<int>(lhs.b) - static_cast<int>(rhs.b)),
            std::abs(static_cast<int>(lhs.a) - static_cast<int>(rhs.a))));
}

template <typename RawTileMap>
const std::vector<uint8_t>* rawTileAt(const RawTileMap& tiles, int x, int y)
{
    const aether::TileKey key { x / static_cast<int>(aether::TILE_SIZE),
        y / static_cast<int>(aether::TILE_SIZE) };
    auto it = tiles.find(key);
    return it != tiles.end() ? &it->second : nullptr;
}

// Content-tile sampler: reads through the format-aware accessor and quantizes to
// 8-bit premultiplied for the fill algorithm. `fmt` defaults to the document
// (content) format; pass a different format only for non-content buffers.
template <typename RawTileMap>
PremultPixel sampleRawPixel(
    const RawTileMap& tiles, int x, int y, TilePixelFormat fmt = kDefaultTileFormat)
{
    PremultPixel px;
    if (x < 0 || y < 0) {
        return px;
    }

    const std::vector<uint8_t>* tile = rawTileAt(tiles, x, y);
    if (!tile || tile->size() != tileByteSize(fmt)) {
        return px;
    }

    const uint32_t localX = static_cast<uint32_t>(x % static_cast<int>(aether::TILE_SIZE));
    const uint32_t localY = static_cast<uint32_t>(y % static_cast<int>(aether::TILE_SIZE));
    float f[4];
    readTilePixelF(tile->data(), fmt, localX, localY, f);
    px.r = fillQuantizeChannel(f[0]);
    px.g = fillQuantizeChannel(f[1]);
    px.b = fillQuantizeChannel(f[2]);
    px.a = fillQuantizeChannel(f[3]);
    return px;
}

// Alpha sampler valid for both CONTENT (default) and RGBA8 MASK buffers — pass
// `TilePixelFormat::RGBA8` for mask / selection / coverage tiles.
template <typename RawTileMap>
uint8_t sampleRawAlpha(
    const RawTileMap& tiles, int x, int y, TilePixelFormat fmt = kDefaultTileFormat)
{
    if (x < 0 || y < 0) {
        return 0;
    }

    const std::vector<uint8_t>* tile = rawTileAt(tiles, x, y);
    if (!tile || tile->size() != tileByteSize(fmt)) {
        return 0;
    }

    const uint32_t localX = static_cast<uint32_t>(x % static_cast<int>(aether::TILE_SIZE));
    const uint32_t localY = static_cast<uint32_t>(y % static_cast<int>(aether::TILE_SIZE));
    float f[4];
    readTilePixelF(tile->data(), fmt, localX, localY, f);
    return fillQuantizeChannel(f[3]);
}

// Allocates a tile buffer sized for `fmt`. Defaults to the content format; pass
// `TilePixelFormat::RGBA8` for mask / coverage tiles.
template <typename RawTileMap>
std::vector<uint8_t>& ensureRawTile(
    RawTileMap& tiles, const aether::TileKey& key, TilePixelFormat fmt = kDefaultTileFormat)
{
    auto& raw = tiles[key];
    if (raw.empty()) {
        raw.resize(tileByteSize(fmt), 0);
    }
    return raw;
}

// `fmt` is the CONTENT (source/result) tile format — the format of the grid the
// fill targets (document layer vs RGBA8 mask). MASK / coverage tiles in these
// signatures stay RGBA8 regardless.
void setRawPixel(std::vector<uint8_t>& raw, uint32_t localX, uint32_t localY, uint8_t r, uint8_t g,
    uint8_t b, uint8_t a, TilePixelFormat fmt = kDefaultTileFormat);
void writeMaskPixel(
    FloodFillResult::RawTileMap& maskTiles, const TileKey& key, uint32_t localX, uint32_t localY);
std::vector<uint8_t>& ensureResultTile(const FloodFillResult::RawTileMap& sourceTiles,
    FloodFillResult& result, const TileKey& key, TilePixelFormat fmt = kDefaultTileFormat);
bool rawTileIsEmpty(const std::vector<uint8_t>& tile, TilePixelFormat fmt = kDefaultTileFormat);
bool rawTilesEqual(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs,
    TilePixelFormat fmt = kDefaultTileFormat);
bool computeRawMaskPixelBounds(const FloodFillResult::RawTileMap& maskTiles, int canvasW,
    int canvasH, int& outMinX, int& outMinY, int& outWidth, int& outHeight);
void normalizeFloodFillResultAgainstSource(const FloodFillResult::RawTileMap& sourceTiles,
    FloodFillResult& result, TilePixelFormat fmt = kDefaultTileFormat);
FloodFillResult::RawTileMap translateRawMaskTilesToWorld(
    const FloodFillResult::RawTileMap& localMaskTiles, int offsetX, int offsetY);
FloodFillResult::RawTileMap translateRawTilesToWorld(const FloodFillResult::RawTileMap& localTiles,
    int offsetX, int offsetY, TilePixelFormat fmt = kDefaultTileFormat);
void translateFloodFillResultToWorld(const FloodFillResult::RawTileMap& sourceTiles, int offsetX,
    int offsetY, FloodFillResult& result, TilePixelFormat fmt = kDefaultTileFormat);
void clipFloodFillResultToSelectionMask(const FloodFillResult::RawTileMap& sourceTiles,
    const FloodFillResult::RawTileMap& selectionMaskTiles, FloodFillResult& result,
    TilePixelFormat fmt = kDefaultTileFormat);

} // namespace aether

#endif // AETHER_FILL_RAW_TILE_OPS_H
