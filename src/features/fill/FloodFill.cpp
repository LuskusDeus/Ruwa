// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   F L O O D   F I L L
// ==========================================================================

#include "features/fill/FloodFill.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <future>
#include <thread>

namespace aether {

namespace {

struct PremultPixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

enum class FillSemanticMode { Exterior = 0, Stroke = 1 };

constexpr int kInteriorSeedTolerance = 6;
constexpr int kStrokeMaterialAlphaDistanceScale = 64;
constexpr uint8_t kComponentHasSource = 0x01;
constexpr uint8_t kComponentTouchesBoundary = 0x02;
// Topology uses 4-connectivity so the fill cannot slip through diagonal
// contacts or narrow necks between nested contours.
constexpr std::array<std::pair<int, int>, 4> kNeighborOffsets { { { 0, -1 }, { -1, 0 }, { 1, 0 },
    { 0, 1 } } };

inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

inline uint32_t floorMod(int32_t a, int32_t b)
{
    int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

inline bool colorsMatch(
    uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1, uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2)
{
    return r1 == r2 && g1 == g2 && b1 == b2 && a1 == a2;
}

inline void writeMaskPixel(
    FloodFillResult& result, const TileKey& key, uint32_t localX, uint32_t localY)
{
    auto& maskTile = result.fillMaskTiles[key];
    if (maskTile.empty()) {
        maskTile.assign(TILE_BYTE_SIZE, 0);
    }

    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    maskTile[idx + 0] = 255;
    maskTile[idx + 1] = 255;
    maskTile[idx + 2] = 255;
    maskTile[idx + 3] = 255;
}

inline void writeMaskPixel(
    FloodFillResult::RawTileMap& maskTiles, const TileKey& key, uint32_t localX, uint32_t localY)
{
    auto& maskTile = maskTiles[key];
    if (maskTile.empty()) {
        maskTile.assign(TILE_BYTE_SIZE, 0);
    }

    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    maskTile[idx + 0] = 255;
    maskTile[idx + 1] = 255;
    maskTile[idx + 2] = 255;
    maskTile[idx + 3] = 255;
}

inline bool maskHasPixel(const FloodFillResult::RawTileMap& maskTiles, int32_t x, int32_t y)
{
    if (x < 0 || y < 0) {
        return false;
    }

    const TileKey key { floorDiv(x, static_cast<int32_t>(TILE_SIZE)),
        floorDiv(y, static_cast<int32_t>(TILE_SIZE)) };
    auto it = maskTiles.find(key);
    if (it == maskTiles.end()) {
        return false;
    }

    const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return it->second[idx + 3] != 0;
}

inline bool maskHasPixel(const std::vector<uint8_t>* maskTile, uint32_t localX, uint32_t localY)
{
    if (!maskTile) {
        return false;
    }
    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return (*maskTile)[idx + 3] != 0;
}

// Selection mask gating used by polygon/lasso fill (and bucket fill via the same
// path). The selection mask defines a per-pixel **alpha cap** for the result.
//
// Rule (matches the brush stroke semantics for soft selection):
//   - If the existing layer pixel's alpha is already above the cap, the fill
//     is a no-op for that pixel — pre-existing content is never reduced.
//   - Otherwise the fill blends normally, and the result's alpha is clamped to
//     the cap. RGB is scaled proportionally so the unmultiplied color stays
//     consistent under premultiplied storage.
//
// `capAlpha == 255` is the unrestricted case (no clipping). `capAlpha == 0`
// means fully outside the selection: the fill cannot raise alpha above 0, so
// pixels with no pre-existing content stay empty.
inline uint8_t selectionAlphaAtLocal(
    const std::vector<uint8_t>* selTile, uint32_t localX, uint32_t localY)
{
    if (!selTile) {
        return 0;
    }
    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return (*selTile)[idx + 3];
}

inline PremultPixel applyFillAlphaCap(const PremultPixel& blended, uint8_t capAlpha)
{
    if (blended.a <= capAlpha) {
        return blended;
    }
    PremultPixel out;
    out.a = capAlpha;
    if (blended.a == 0) {
        out.r = out.g = out.b = 0;
        return out;
    }
    const uint32_t num = static_cast<uint32_t>(capAlpha);
    const uint32_t den = static_cast<uint32_t>(blended.a);
    out.r = static_cast<uint8_t>(static_cast<uint32_t>(blended.r) * num / den);
    out.g = static_cast<uint8_t>(static_cast<uint32_t>(blended.g) * num / den);
    out.b = static_cast<uint8_t>(static_cast<uint32_t>(blended.b) * num / den);
    return out;
}

// Deep-copy selection mask tiles into a RawTileMap with verbatim RGBA bytes.
// Used to feed bucket / polygon fill the alpha-cap mask in raw form for the
// per-pixel selection-clip pass in buildResultFromMaskTiles.
inline FloodFillResult::RawTileMap snapshotSelectionMaskTiles(const TileGrid* selectionMask)
{
    FloodFillResult::RawTileMap tiles;
    if (!selectionMask) {
        return tiles;
    }
    tiles.reserve(selectionMask->tiles().size());
    for (const auto& [key, tile] : selectionMask->tiles()) {
        std::vector<uint8_t> bytes(TILE_BYTE_SIZE);
        std::memcpy(bytes.data(), tile.pixels(), TILE_BYTE_SIZE);
        tiles.emplace(key, std::move(bytes));
    }
    return tiles;
}

inline PremultPixel samplePixel(const TileGrid& grid, int32_t x, int32_t y)
{
    PremultPixel px;
    samplePixelAt(&grid, x, y, px.r, px.g, px.b, px.a);
    return px;
}

// CONTENT tiles use the format of the grid being filled (document layers ->
// kDefaultTileFormat, layer masks -> RGBA8). The format is threaded as `fmt`
// from the entry points; raw byte buffers carry no tag. MASK / coverage tiles
// (fillMaskTiles, interior/soft, selection) are ALWAYS RGBA8 (TILE_BYTE_SIZE).
inline uint8_t quantizeChannelF(float v)
{
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// CONTENT read: format-aware, quantized to 8-bit premultiplied.
inline PremultPixel samplePixel(const std::vector<uint8_t>* rawTile, uint32_t localX,
    uint32_t localY, TilePixelFormat fmt = kDefaultTileFormat)
{
    PremultPixel px;
    if (!rawTile) {
        return px;
    }

    float f[4];
    readTilePixelF(rawTile->data(), fmt, localX, localY, f);
    px.r = quantizeChannelF(f[0]);
    px.g = quantizeChannelF(f[1]);
    px.b = quantizeChannelF(f[2]);
    px.a = quantizeChannelF(f[3]);
    return px;
}

inline uint32_t rawPixelIndex(uint32_t localX, uint32_t localY)
{
    return (localY * TILE_SIZE + localX) * TILE_CHANNELS;
}

inline const std::vector<uint8_t>* rawTileAt(
    const FloodFillResult::RawTileMap& tiles, int32_t x, int32_t y)
{
    if (x < 0 || y < 0) {
        return nullptr;
    }

    const TileKey key { floorDiv(x, static_cast<int32_t>(TILE_SIZE)),
        floorDiv(y, static_cast<int32_t>(TILE_SIZE)) };
    auto it = tiles.find(key);
    return it != tiles.end() ? &it->second : nullptr;
}

// CONTENT read from a raw tile map (document format), quantized to 8-bit.
inline PremultPixel sampleRawPixelAt(const FloodFillResult::RawTileMap& tiles, int32_t x, int32_t y,
    TilePixelFormat fmt = kDefaultTileFormat)
{
    PremultPixel px;
    const std::vector<uint8_t>* tile = rawTileAt(tiles, x, y);
    if (!tile || tile->size() != tileByteSize(fmt)) {
        return px;
    }

    const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
    float f[4];
    readTilePixelF(tile->data(), fmt, localX, localY, f);
    px.r = quantizeChannelF(f[0]);
    px.g = quantizeChannelF(f[1]);
    px.b = quantizeChannelF(f[2]);
    px.a = quantizeChannelF(f[3]);
    return px;
}

inline uint8_t sampleRawAlphaAt(const FloodFillResult::RawTileMap& tiles, int32_t x, int32_t y)
{
    const std::vector<uint8_t>* tile = rawTileAt(tiles, x, y);
    if (!tile || tile->size() != TILE_BYTE_SIZE) {
        return 0;
    }

    const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
    return (*tile)[rawPixelIndex(localX, localY) + 3];
}

// CONTENT tile: empty == every pixel's alpha is zero (format-aware).
inline bool rawTileIsEmpty(
    const std::vector<uint8_t>& tile, TilePixelFormat fmt = kDefaultTileFormat)
{
    if (tile.size() != tileByteSize(fmt)) {
        return true;
    }

    for (uint32_t y = 0; y < TILE_SIZE; ++y) {
        for (uint32_t x = 0; x < TILE_SIZE; ++x) {
            if (!tilePixelAlphaIsZero(tile.data(), fmt, x, y)) {
                return false;
            }
        }
    }
    return true;
}

inline bool rawTilesEqual(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs,
    TilePixelFormat fmt = kDefaultTileFormat)
{
    return lhs.size() == tileByteSize(fmt) && rhs.size() == tileByteSize(fmt)
        && std::memcmp(lhs.data(), rhs.data(), tileByteSize(fmt)) == 0;
}

// CONTENT write: store through the format-aware accessor.
inline void setRawPixel(std::vector<uint8_t>& raw, uint32_t localX, uint32_t localY, uint8_t r,
    uint8_t g, uint8_t b, uint8_t a, TilePixelFormat fmt = kDefaultTileFormat)
{
    const float f[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    writeTilePixelF(raw.data(), fmt, localX, localY, f);
}

inline int pixelDistance(const PremultPixel& lhs, const PremultPixel& rhs)
{
    return std::max(std::max(std::abs(static_cast<int>(lhs.r) - static_cast<int>(rhs.r)),
                        std::abs(static_cast<int>(lhs.g) - static_cast<int>(rhs.g))),
        std::max(std::abs(static_cast<int>(lhs.b) - static_cast<int>(rhs.b)),
            std::abs(static_cast<int>(lhs.a) - static_cast<int>(rhs.a))));
}

inline FillSemanticMode fillModeForSeed(const PremultPixel& seedPixel)
{
    return seedPixel.a == 0 ? FillSemanticMode::Exterior : FillSemanticMode::Stroke;
}

inline uint8_t unpremultiplyChannel(uint8_t channel, uint8_t alpha)
{
    if (alpha == 0) {
        return 0;
    }

    return static_cast<uint8_t>(std::clamp(
        (static_cast<int>(channel) * 255 + static_cast<int>(alpha) / 2) / static_cast<int>(alpha),
        0, 255));
}

inline PremultPixel straightPixel(const PremultPixel& px)
{
    PremultPixel straight = px;
    straight.r = unpremultiplyChannel(px.r, px.a);
    straight.g = unpremultiplyChannel(px.g, px.a);
    straight.b = unpremultiplyChannel(px.b, px.a);
    return straight;
}

inline int materialDistance(const PremultPixel& lhs, const PremultPixel& rhs)
{
    const PremultPixel lhsStraight = straightPixel(lhs);
    const PremultPixel rhsStraight = straightPixel(rhs);
    const int colorDistance = std::max(
        std::max(std::abs(static_cast<int>(lhsStraight.r) - static_cast<int>(rhsStraight.r)),
            std::abs(static_cast<int>(lhsStraight.g) - static_cast<int>(rhsStraight.g))),
        std::abs(static_cast<int>(lhsStraight.b) - static_cast<int>(rhsStraight.b)));
    const int alphaDistance = (std::abs(static_cast<int>(lhs.a) - static_cast<int>(rhs.a))
                                  + kStrokeMaterialAlphaDistanceScale - 1)
        / kStrokeMaterialAlphaDistanceScale;
    return std::max(colorDistance, alphaDistance);
}

inline int fillDistance(const PremultPixel& lhs, const PremultPixel& rhs, FillSemanticMode mode)
{
    return mode == FillSemanticMode::Stroke ? materialDistance(lhs, rhs) : pixelDistance(lhs, rhs);
}

inline PremultPixel compositeOver(
    const PremultPixel& src, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    const int premulR = (static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255;
    const int premulG = (static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255;
    const int premulB = (static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255;
    const int invFillA = 255 - static_cast<int>(fillA);
    PremultPixel out;
    out.r = static_cast<uint8_t>(
        std::clamp(premulR + ((static_cast<int>(src.r) * invFillA + 127) / 255), 0, 255));
    out.g = static_cast<uint8_t>(
        std::clamp(premulG + ((static_cast<int>(src.g) * invFillA + 127) / 255), 0, 255));
    out.b = static_cast<uint8_t>(
        std::clamp(premulB + ((static_cast<int>(src.b) * invFillA + 127) / 255), 0, 255));
    out.a = static_cast<uint8_t>(std::clamp(
        static_cast<int>(fillA) + ((static_cast<int>(src.a) * invFillA + 127) / 255), 0, 255));
    return out;
}

inline PremultPixel compositeOverPreservingAlpha(
    const PremultPixel& dst, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    if (dst.a == 0 || fillA == 0) {
        return dst;
    }

    const uint64_t alphaScale = 255;
    const uint64_t denominator = alphaScale * alphaScale;
    const uint64_t invFillA = alphaScale - static_cast<uint64_t>(fillA);
    auto blendChannel = [&](uint8_t fillChannel, uint8_t dstChannel) {
        const uint64_t numerator
            = static_cast<uint64_t>(fillChannel) * fillA * dst.a
            + static_cast<uint64_t>(dstChannel) * invFillA * alphaScale;
        return static_cast<uint8_t>(
            std::clamp<uint64_t>((numerator + denominator / 2) / denominator, 0, dst.a));
    };

    return { blendChannel(fillR, dst.r), blendChannel(fillG, dst.g),
        blendChannel(fillB, dst.b), dst.a };
}

inline PremultPixel compositeUnder(
    const PremultPixel& src, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    const int premulR = (static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255;
    const int premulG = (static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255;
    const int premulB = (static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255;
    const int invSrcA = 255 - static_cast<int>(src.a);
    PremultPixel out;
    out.r = static_cast<uint8_t>(
        std::clamp(static_cast<int>(src.r) + ((premulR * invSrcA + 127) / 255), 0, 255));
    out.g = static_cast<uint8_t>(
        std::clamp(static_cast<int>(src.g) + ((premulG * invSrcA + 127) / 255), 0, 255));
    out.b = static_cast<uint8_t>(
        std::clamp(static_cast<int>(src.b) + ((premulB * invSrcA + 127) / 255), 0, 255));
    out.a = static_cast<uint8_t>(std::clamp(
        static_cast<int>(src.a) + ((static_cast<int>(fillA) * invSrcA + 127) / 255), 0, 255));
    return out;
}

inline size_t fillComputeWorkerCount(size_t workItemCount)
{
    if (workItemCount == 0) {
        return 0;
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t availableComputeThreads
        = hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : static_cast<size_t>(1);
    return std::min(workItemCount, std::max<size_t>(1, availableComputeThreads));
}

template <typename Fn>
void parallelForFillChunks(size_t workItemCount, size_t minItemsPerWorker, Fn&& fn)
{
    if (workItemCount == 0) {
        return;
    }

    const size_t workerCount = fillComputeWorkerCount(workItemCount);
    if (workerCount <= 1 || workItemCount < workerCount * std::max<size_t>(1, minItemsPerWorker)) {
        fn(0, 0, workItemCount);
        return;
    }

    const size_t chunkSize = (workItemCount + workerCount - 1) / workerCount;
    std::vector<std::future<void>> futures;
    futures.reserve(workerCount - 1);

    for (size_t workerIndex = 1; workerIndex < workerCount; ++workerIndex) {
        const size_t begin = workerIndex * chunkSize;
        const size_t end = std::min(begin + chunkSize, workItemCount);
        if (begin >= end) {
            break;
        }

        futures.push_back(std::async(
            std::launch::async, [begin, end, workerIndex, &fn]() { fn(workerIndex, begin, end); }));
    }

    fn(0, 0, std::min(chunkSize, workItemCount));

    for (auto& future : futures) {
        future.get();
    }
}

inline void mergeMaskTileInto(std::vector<uint8_t>& destination, const std::vector<uint8_t>& source)
{
    if (source.size() != TILE_BYTE_SIZE) {
        return;
    }

    if (destination.empty()) {
        destination = source;
        return;
    }

    if (destination.size() != TILE_BYTE_SIZE) {
        destination.assign(TILE_BYTE_SIZE, 0);
    }

    for (size_t i = 0; i < TILE_BYTE_SIZE; ++i) {
        destination[i] = std::max(destination[i], source[i]);
    }
}

void mergeMaskTileMap(
    FloodFillResult::RawTileMap& destination, FloodFillResult::RawTileMap&& source)
{
    for (auto& [key, tile] : source) {
        if (tile.size() != TILE_BYTE_SIZE) {
            continue;
        }

        auto it = destination.find(key);
        if (it == destination.end()) {
            destination.emplace(key, std::move(tile));
            continue;
        }

        mergeMaskTileInto(it->second, tile);
    }
}

// Rebuild a TileGrid from raw tile bytes. `fmt` is the format of both the source
// bytes and the produced grid: content tiles use kDefaultTileFormat (default),
// selection/mask tiles use RGBA8.
TileGrid rawTileMapToGrid(
    const FloodFillResult::RawTileMap& sourceTiles, TilePixelFormat fmt = kDefaultTileFormat)
{
    TileGrid grid;
    grid.setFormat(fmt);
    grid.tiles().reserve(sourceTiles.size());

    const uint32_t byteSize = tileByteSize(fmt);
    for (const auto& [key, tile] : sourceTiles) {
        if (tile.size() != byteSize) {
            continue;
        }

        TileData& dstTile = grid.getOrCreateTile(key);
        std::memcpy(dstTile.pixels(), tile.data(), byteSize);
        dstTile.clearDirty();
        grid.removeDirty(key);
    }

    return grid;
}

struct TileFillMutation {
    TileKey key;
    std::vector<uint8_t> beforeTile;
    std::vector<uint8_t> afterTile;
    std::vector<uint8_t> fillMaskTile;
    bool hasBefore = false;
    bool hasAfter = false;
    bool created = false;
    bool removed = false;
    int pixelsFilled = 0;
};

// Returns a full-size tile buffer for `key`, or nullptr. `fmt` selects the
// expected buffer size: content tiles (default) vs RGBA8 mask/selection tiles.
inline const std::vector<uint8_t>* rawTileForKey(const FloodFillResult::RawTileMap& tiles,
    const TileKey& key, TilePixelFormat fmt = kDefaultTileFormat)
{
    auto it = tiles.find(key);
    if (it == tiles.end() || it->second.size() != tileByteSize(fmt)) {
        return nullptr;
    }
    return &it->second;
}

inline void applyMaskTileToMutation(TileFillMutation& mutation,
    const std::vector<uint8_t>* maskTile, const std::vector<uint8_t>* selectionTile,
    bool selectionClipping, FillSemanticMode fillMode, uint8_t fillR, uint8_t fillG, uint8_t fillB,
    uint8_t fillA, TilePixelFormat fmt, bool preserveDestinationAlpha)
{
    for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
        for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
            if (!maskHasPixel(maskTile, localX, localY)) {
                continue;
            }

            const PremultPixel originalPx = samplePixel(
                mutation.hasBefore ? &mutation.beforeTile : nullptr, localX, localY, fmt);
            const uint8_t capAlpha
                = selectionClipping ? selectionAlphaAtLocal(selectionTile, localX, localY) : 255;
            if (!preserveDestinationAlpha && selectionClipping && originalPx.a > capAlpha) {
                // Pre-existing alpha already above the soft-mask cap; never reduce.
                continue;
            }

            PremultPixel blendedPx;
            if (preserveDestinationAlpha) {
                const uint8_t effectiveFillA = selectionClipping
                    ? static_cast<uint8_t>(
                          (static_cast<uint32_t>(fillA) * capAlpha + 127u) / 255u)
                    : fillA;
                if (originalPx.a == 0 || effectiveFillA == 0) {
                    continue;
                }
                blendedPx = compositeOverPreservingAlpha(
                    originalPx, fillR, fillG, fillB, effectiveFillA);
            } else {
                blendedPx = (fillMode == FillSemanticMode::Exterior)
                    ? compositeUnder(originalPx, fillR, fillG, fillB, fillA)
                    : compositeOver(originalPx, fillR, fillG, fillB, fillA);
                if (selectionClipping) {
                    blendedPx = applyFillAlphaCap(blendedPx, capAlpha);
                }
            }
            setRawPixel(mutation.afterTile, localX, localY, blendedPx.r, blendedPx.g, blendedPx.b,
                blendedPx.a, fmt);
            ++mutation.pixelsFilled;
        }
    }
}

FloodFillResult buildResultFromMaskTiles(const TileGrid& sourceGrid,
    const FloodFillResult::RawTileMap& interiorMaskTiles,
    const FloodFillResult::RawTileMap& softEdgeMaskTiles, FillSemanticMode fillMode, uint8_t fillR,
    uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const FloodFillResult::RawTileMap* selectionMaskTiles = nullptr,
    bool preserveDestinationAlpha = false)
{
    FloodFillResult result;

    // CONTENT format = the grid being filled (document layer vs RGBA8 mask).
    const TilePixelFormat fmt = sourceGrid.format();
    const uint32_t contentByteSize = tileByteSize(fmt);

    std::unordered_set<TileKey, TileKeyHash> affectedKeySet;
    affectedKeySet.reserve(interiorMaskTiles.size() + softEdgeMaskTiles.size());
    for (const auto& [key, _] : interiorMaskTiles) {
        affectedKeySet.insert(key);
    }
    for (const auto& [key, _] : softEdgeMaskTiles) {
        affectedKeySet.insert(key);
    }

    if (affectedKeySet.empty()) {
        return result;
    }

    std::vector<TileKey> affectedKeys(affectedKeySet.begin(), affectedKeySet.end());
    std::vector<TileFillMutation> mutations(affectedKeys.size());

    parallelForFillChunks(affectedKeys.size(), 4, [&](size_t, size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            TileFillMutation& mutation = mutations[i];
            mutation.key = affectedKeys[i];

            const TileData* sourceTile = sourceGrid.getTile(mutation.key);
            const bool hadSourceTile = sourceTile != nullptr;
            if (hadSourceTile) {
                mutation.beforeTile.resize(contentByteSize);
                std::memcpy(mutation.beforeTile.data(), sourceTile->pixels(), contentByteSize);
                mutation.afterTile = mutation.beforeTile;
                mutation.hasBefore = true;
            } else {
                mutation.afterTile.assign(contentByteSize, 0);
            }

            const std::vector<uint8_t>* interiorTile = nullptr;
            const std::vector<uint8_t>* softTile = nullptr;
            if (auto it = interiorMaskTiles.find(mutation.key); it != interiorMaskTiles.end()) {
                interiorTile = &it->second;
                mutation.fillMaskTile = it->second;
            }
            if (auto it = softEdgeMaskTiles.find(mutation.key); it != softEdgeMaskTiles.end()) {
                softTile = &it->second;
                mergeMaskTileInto(mutation.fillMaskTile, it->second);
            }

            const std::vector<uint8_t>* selTile = nullptr;
            if (selectionMaskTiles) {
                auto it = selectionMaskTiles->find(mutation.key);
                if (it != selectionMaskTiles->end()) {
                    selTile = &it->second;
                }
            }
            const bool selectionClipping = (selectionMaskTiles != nullptr);

            applyMaskTileToMutation(mutation, interiorTile, selTile, selectionClipping, fillMode,
                fillR, fillG, fillB, fillA, fmt, preserveDestinationAlpha);
            applyMaskTileToMutation(mutation, softTile, selTile, selectionClipping, fillMode, fillR,
                fillG, fillB, fillA, fmt, preserveDestinationAlpha);

            if (mutation.fillMaskTile.empty() || mutation.pixelsFilled == 0) {
                mutation.afterTile.clear();
                mutation.beforeTile.clear();
                mutation.hasBefore = false;
                continue;
            }

            if (rawTileIsEmpty(mutation.afterTile, fmt)) {
                mutation.afterTile.clear();
                mutation.hasAfter = false;
                if (mutation.hasBefore) {
                    mutation.removed = true;
                } else {
                    mutation.beforeTile.clear();
                    mutation.hasBefore = false;
                    mutation.fillMaskTile.clear();
                    mutation.pixelsFilled = 0;
                }
                continue;
            }

            if (mutation.hasBefore && rawTilesEqual(mutation.beforeTile, mutation.afterTile, fmt)) {
                mutation.afterTile.clear();
                mutation.beforeTile.clear();
                mutation.fillMaskTile.clear();
                mutation.hasBefore = false;
                mutation.pixelsFilled = 0;
                continue;
            }

            mutation.hasAfter = true;
            mutation.created = !mutation.hasBefore;
        }
    });

    result.beforeTiles.reserve(affectedKeys.size());
    result.afterTiles.reserve(affectedKeys.size());
    result.fillMaskTiles.reserve(affectedKeys.size());
    result.createdTiles.reserve(affectedKeys.size());
    result.removedTiles.reserve(affectedKeys.size());

    for (auto& mutation : mutations) {
        if (mutation.hasBefore) {
            result.beforeTiles.emplace(mutation.key, std::move(mutation.beforeTile));
        }
        if (mutation.hasAfter) {
            result.afterTiles.emplace(mutation.key, std::move(mutation.afterTile));
        }
        if (!mutation.fillMaskTile.empty()) {
            result.fillMaskTiles.emplace(mutation.key, std::move(mutation.fillMaskTile));
        }
        if (mutation.created) {
            result.createdTiles.insert(mutation.key);
        }
        if (mutation.removed) {
            result.removedTiles.insert(mutation.key);
        }
        result.pixelsFilled += mutation.pixelsFilled;
    }

    return result;
}

FloodFillResult buildResultFromMaskTiles(const FloodFillResult::RawTileMap& sourceTiles,
    const FloodFillResult::RawTileMap& interiorMaskTiles,
    const FloodFillResult::RawTileMap& softEdgeMaskTiles, FillSemanticMode fillMode, uint8_t fillR,
    uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const FloodFillResult::RawTileMap* selectionMaskTiles = nullptr,
    TilePixelFormat fmt = kDefaultTileFormat)
{
    FloodFillResult result;

    const uint32_t contentByteSize = tileByteSize(fmt);

    std::unordered_set<TileKey, TileKeyHash> affectedKeySet;
    affectedKeySet.reserve(interiorMaskTiles.size() + softEdgeMaskTiles.size());
    for (const auto& [key, _] : interiorMaskTiles) {
        affectedKeySet.insert(key);
    }
    for (const auto& [key, _] : softEdgeMaskTiles) {
        affectedKeySet.insert(key);
    }

    if (affectedKeySet.empty()) {
        return result;
    }

    std::vector<TileKey> affectedKeys(affectedKeySet.begin(), affectedKeySet.end());
    std::vector<TileFillMutation> mutations(affectedKeys.size());

    parallelForFillChunks(affectedKeys.size(), 4, [&](size_t, size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            TileFillMutation& mutation = mutations[i];
            mutation.key = affectedKeys[i];

            const std::vector<uint8_t>* sourceTile = rawTileForKey(sourceTiles, mutation.key, fmt);
            const bool hadSourceTile = sourceTile != nullptr;
            if (hadSourceTile) {
                mutation.beforeTile = *sourceTile;
                mutation.afterTile = mutation.beforeTile;
                mutation.hasBefore = true;
            } else {
                mutation.afterTile.assign(contentByteSize, 0);
            }

            const std::vector<uint8_t>* interiorTile = nullptr;
            const std::vector<uint8_t>* softTile = nullptr;
            if (auto it = interiorMaskTiles.find(mutation.key); it != interiorMaskTiles.end()) {
                interiorTile = &it->second;
                mutation.fillMaskTile = it->second;
            }
            if (auto it = softEdgeMaskTiles.find(mutation.key); it != softEdgeMaskTiles.end()) {
                softTile = &it->second;
                mergeMaskTileInto(mutation.fillMaskTile, it->second);
            }

            const std::vector<uint8_t>* selTile = nullptr;
            if (selectionMaskTiles) {
                auto it = selectionMaskTiles->find(mutation.key);
                if (it != selectionMaskTiles->end()) {
                    selTile = &it->second;
                }
            }
            const bool selectionClipping = (selectionMaskTiles != nullptr);

            applyMaskTileToMutation(mutation, interiorTile, selTile, selectionClipping, fillMode,
                fillR, fillG, fillB, fillA, fmt, false);
            applyMaskTileToMutation(mutation, softTile, selTile, selectionClipping, fillMode, fillR,
                fillG, fillB, fillA, fmt, false);

            if (mutation.fillMaskTile.empty() || mutation.pixelsFilled == 0) {
                mutation.afterTile.clear();
                mutation.beforeTile.clear();
                mutation.hasBefore = false;
                continue;
            }

            if (rawTileIsEmpty(mutation.afterTile, fmt)) {
                mutation.afterTile.clear();
                mutation.hasAfter = false;
                if (mutation.hasBefore) {
                    mutation.removed = true;
                } else {
                    mutation.beforeTile.clear();
                    mutation.hasBefore = false;
                    mutation.fillMaskTile.clear();
                    mutation.pixelsFilled = 0;
                }
                continue;
            }

            if (mutation.hasBefore && rawTilesEqual(mutation.beforeTile, mutation.afterTile, fmt)) {
                mutation.afterTile.clear();
                mutation.beforeTile.clear();
                mutation.fillMaskTile.clear();
                mutation.hasBefore = false;
                mutation.pixelsFilled = 0;
                continue;
            }

            mutation.hasAfter = true;
            mutation.created = !mutation.hasBefore;
        }
    });

    result.beforeTiles.reserve(affectedKeys.size());
    result.afterTiles.reserve(affectedKeys.size());
    result.fillMaskTiles.reserve(affectedKeys.size());
    result.createdTiles.reserve(affectedKeys.size());
    result.removedTiles.reserve(affectedKeys.size());

    for (auto& mutation : mutations) {
        if (mutation.hasBefore) {
            result.beforeTiles.emplace(mutation.key, std::move(mutation.beforeTile));
        }
        if (mutation.hasAfter) {
            result.afterTiles.emplace(mutation.key, std::move(mutation.afterTile));
        }
        if (!mutation.fillMaskTile.empty()) {
            result.fillMaskTiles.emplace(mutation.key, std::move(mutation.fillMaskTile));
        }
        if (mutation.created) {
            result.createdTiles.insert(mutation.key);
        }
        if (mutation.removed) {
            result.removedTiles.insert(mutation.key);
        }
        result.pixelsFilled += mutation.pixelsFilled;
    }

    return result;
}

void applyFillResultToGrid(TileGrid& grid, const FloodFillResult& result)
{
    std::unordered_set<TileKey, TileKeyHash> affectedKeys;
    affectedKeys.reserve(result.afterTiles.size() + result.removedTiles.size());
    for (const auto& [key, _] : result.afterTiles) {
        affectedKeys.insert(key);
    }
    for (const TileKey& key : result.removedTiles) {
        affectedKeys.insert(key);
    }

    for (const TileKey& key : affectedKeys) {
        if (result.removedTiles.count(key) > 0) {
            grid.removeTile(key);
            continue;
        }

        auto afterIt = result.afterTiles.find(key);
        if (afterIt == result.afterTiles.end()
            || afterIt->second.size() != tileByteSize(grid.format())) {
            continue;
        }

        TileData& tile = grid.getOrCreateTile(key);
        std::memcpy(tile.pixels(), afterIt->second.data(), tileByteSize(grid.format()));
        tile.markDirty();
    }
}

inline size_t pixelIndex(int x, int y, int canvasWidth)
{
    return static_cast<size_t>(y) * static_cast<size_t>(canvasWidth) + static_cast<size_t>(x);
}

class ClassicRawFillRowAccessor {
public:
    ClassicRawFillRowAccessor(const FloodFillResult::RawTileMap& sourceTiles,
        const FloodFillResult::RawTileMap* selectionMaskTiles,
        FloodFillResult::RawTileMap* fillMaskTiles, int y, const PremultPixel& seedPixel,
        TilePixelFormat fmt)
        : m_sourceTiles(sourceTiles)
        , m_selectionMaskTiles(selectionMaskTiles)
        , m_fillMaskTiles(fillMaskTiles)
        , m_tileY(floorDiv(y, static_cast<int32_t>(TILE_SIZE)))
        , m_localY(floorMod(y, static_cast<int32_t>(TILE_SIZE)))
        , m_seedPixel(seedPixel)
        , m_fmt(fmt)
    {
    }

    bool canFill(int x, int canvasWidth)
    {
        if (x < 0 || x >= canvasWidth) {
            return false;
        }

        prepareTile(x);
        const uint32_t idx = rawPixelIndex(localX(x), m_localY);

        if (m_selectionMaskTiles) {
            if (!m_selectionTile || (*m_selectionTile)[idx + 3] == 0) {
                return false;
            }
        }

        if (m_fillMaskTile && (*m_fillMaskTile)[idx + 3] != 0) {
            return false;
        }

        if (!m_sourceTile) {
            return m_seedPixel.r == 0 && m_seedPixel.g == 0 && m_seedPixel.b == 0
                && m_seedPixel.a == 0;
        }

        // CONTENT read (document format), quantized for the exact seed compare.
        float f[4];
        readTilePixelF(m_sourceTile->data(), m_fmt, localX(x), m_localY, f);
        return quantizeChannelF(f[0]) == m_seedPixel.r && quantizeChannelF(f[1]) == m_seedPixel.g
            && quantizeChannelF(f[2]) == m_seedPixel.b && quantizeChannelF(f[3]) == m_seedPixel.a;
    }

    void markSpan(int left, int right)
    {
        int x = left;
        while (x <= right) {
            prepareTile(x);

            const int tileBaseX = m_currentTileX * static_cast<int32_t>(TILE_SIZE);
            const int tileRight = std::min(right, tileBaseX + static_cast<int>(TILE_SIZE) - 1);
            std::vector<uint8_t>& fillMaskTile = ensureWritableMaskTile();
            const uint32_t beginLocalX = localX(x);
            const uint32_t endLocalX = localX(tileRight);
            const uint32_t startIdx = rawPixelIndex(beginLocalX, m_localY);
            const size_t byteCount
                = static_cast<size_t>(endLocalX - beginLocalX + 1) * TILE_CHANNELS;
            std::memset(fillMaskTile.data() + startIdx, 255, byteCount);
            x = tileRight + 1;
        }
    }

private:
    void prepareTile(int x)
    {
        const int32_t tileX = floorDiv(x, static_cast<int32_t>(TILE_SIZE));
        if (m_hasCurrentTile && tileX == m_currentTileX) {
            return;
        }

        m_currentTileX = tileX;
        m_hasCurrentTile = true;

        const TileKey key { m_currentTileX, m_tileY };
        m_sourceTile = rawTileForKey(m_sourceTiles, key, m_fmt);
        if (m_selectionMaskTiles) {
            m_selectionTile = rawTileForKey(*m_selectionMaskTiles, key, TilePixelFormat::RGBA8);
        } else {
            m_selectionTile = nullptr;
        }

        auto fillMaskIt = m_fillMaskTiles->find(key);
        if (fillMaskIt != m_fillMaskTiles->end() && fillMaskIt->second.size() == TILE_BYTE_SIZE) {
            m_fillMaskTile = &fillMaskIt->second;
        } else {
            m_fillMaskTile = nullptr;
        }
    }

    std::vector<uint8_t>& ensureWritableMaskTile()
    {
        if (!m_fillMaskTile) {
            auto& fillMaskTile = (*m_fillMaskTiles)[TileKey { m_currentTileX, m_tileY }];
            if (fillMaskTile.size() != TILE_BYTE_SIZE) {
                fillMaskTile.assign(TILE_BYTE_SIZE, 0);
            }
            m_fillMaskTile = &fillMaskTile;
        }
        return *m_fillMaskTile;
    }

    static uint32_t localX(int x) { return floorMod(x, static_cast<int32_t>(TILE_SIZE)); }

    const FloodFillResult::RawTileMap& m_sourceTiles;
    const FloodFillResult::RawTileMap* m_selectionMaskTiles = nullptr;
    FloodFillResult::RawTileMap* m_fillMaskTiles = nullptr;
    int32_t m_tileY = 0;
    uint32_t m_localY = 0;
    PremultPixel m_seedPixel;
    TilePixelFormat m_fmt = kDefaultTileFormat;
    int32_t m_currentTileX = 0;
    bool m_hasCurrentTile = false;
    const std::vector<uint8_t>* m_sourceTile = nullptr;
    const std::vector<uint8_t>* m_selectionTile = nullptr;
    std::vector<uint8_t>* m_fillMaskTile = nullptr;
};

class ConnectivityDisjointSet {
public:
    explicit ConnectivityDisjointSet(size_t count)
        : m_parent(count, -1)
        , m_rank(count, 0)
        , m_flags(count, 0)
    {
    }

    void activate(int32_t index, uint8_t flags)
    {
        m_parent[static_cast<size_t>(index)] = index;
        m_rank[static_cast<size_t>(index)] = 0;
        m_flags[static_cast<size_t>(index)] = flags;
    }

    bool isActive(int32_t index) const
    {
        return index >= 0 && m_parent[static_cast<size_t>(index)] >= 0;
    }

    int32_t find(int32_t index)
    {
        const size_t idx = static_cast<size_t>(index);
        if (m_parent[idx] == index) {
            return index;
        }
        m_parent[idx] = find(m_parent[idx]);
        return m_parent[idx];
    }

    uint8_t unite(int32_t lhs, int32_t rhs)
    {
        if (!isActive(lhs) || !isActive(rhs)) {
            return 0;
        }

        int32_t lhsRoot = find(lhs);
        int32_t rhsRoot = find(rhs);
        if (lhsRoot == rhsRoot) {
            return m_flags[static_cast<size_t>(lhsRoot)];
        }

        size_t lhsIdx = static_cast<size_t>(lhsRoot);
        size_t rhsIdx = static_cast<size_t>(rhsRoot);
        if (m_rank[lhsIdx] < m_rank[rhsIdx]) {
            std::swap(lhsRoot, rhsRoot);
            std::swap(lhsIdx, rhsIdx);
        }

        m_parent[rhsIdx] = lhsRoot;
        m_flags[lhsIdx] = static_cast<uint8_t>(m_flags[lhsIdx] | m_flags[rhsIdx]);
        if (m_rank[lhsIdx] == m_rank[rhsIdx]) {
            ++m_rank[lhsIdx];
        }
        return m_flags[lhsIdx];
    }

    uint8_t componentFlags(int32_t index)
    {
        if (!isActive(index)) {
            return 0;
        }
        return m_flags[static_cast<size_t>(find(index))];
    }

private:
    std::vector<int32_t> m_parent;
    std::vector<uint8_t> m_rank;
    std::vector<uint8_t> m_flags;
};

} // namespace

FloodFillResult floodFill(TileGrid& grid, int seedX, int seedY, uint8_t fillR, uint8_t fillG,
    uint8_t fillB, uint8_t fillA, const TileGrid* selectionMask, int canvasWidth, int canvasHeight)
{
    FloodFillResult result;

    if (seedX < 0 || seedX >= canvasWidth || seedY < 0 || seedY >= canvasHeight) {
        return result;
    }

    uint8_t seedR = 0;
    uint8_t seedG = 0;
    uint8_t seedB = 0;
    uint8_t seedA = 0;
    if (!samplePixelAt(&grid, seedX, seedY, seedR, seedG, seedB, seedA)) {
        return result;
    }

    if (selectionMask && fillMaskAlphaAt(selectionMask, seedX, seedY) == 0) {
        return result;
    }

    if (colorsMatch(seedR, seedG, seedB, seedA, fillR, fillG, fillB, fillA)) {
        return result;
    }

    const PremultPixel seedPixel { seedR, seedG, seedB, seedA };
    const FillSemanticMode fillMode = fillModeForSeed(seedPixel);
    FloodFillResult::RawTileMap interiorMaskTiles;
    FloodFillResult::RawTileMap softEdgeMaskTiles;

    struct SeedPoint {
        int x = 0;
        int y = 0;
    };

    auto canFillInterior = [&](int x, int y) -> bool {
        if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
            return false;
        }
        if (selectionMask && fillMaskAlphaAt(selectionMask, x, y) == 0) {
            return false;
        }
        if (maskHasPixel(interiorMaskTiles, x, y)) {
            return false;
        }

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
        if (!samplePixelAt(&grid, x, y, r, g, b, a)) {
            return false;
        }
        const PremultPixel px { r, g, b, a };
        if (fillMode == FillSemanticMode::Stroke && px.a == 0) {
            return false;
        }
        if (fillMode == FillSemanticMode::Exterior) {
            return colorsMatch(
                px.r, px.g, px.b, px.a, seedPixel.r, seedPixel.g, seedPixel.b, seedPixel.a);
        }
        return fillDistance(px, seedPixel, fillMode) <= kInteriorSeedTolerance;
    };

    std::vector<SeedPoint> stack;
    stack.push_back({ seedX, seedY });

    while (!stack.empty()) {
        const SeedPoint seed = stack.back();
        stack.pop_back();

        if (!canFillInterior(seed.x, seed.y)) {
            continue;
        }

        int left = seed.x;
        while (left > 0 && canFillInterior(left - 1, seed.y)) {
            --left;
        }

        int right = seed.x;
        while (right + 1 < canvasWidth && canFillInterior(right + 1, seed.y)) {
            ++right;
        }

        bool spanAbove = false;
        bool spanBelow = false;

        for (int x = left; x <= right; ++x) {
            const int32_t tx = floorDiv(x, static_cast<int32_t>(TILE_SIZE));
            const int32_t ty = floorDiv(seed.y, static_cast<int32_t>(TILE_SIZE));
            const TileKey key { tx, ty };
            const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
            const uint32_t localY = floorMod(seed.y, static_cast<int32_t>(TILE_SIZE));

            writeMaskPixel(interiorMaskTiles, key, localX, localY);

            const bool canFillAbove = (seed.y > 0) && canFillInterior(x, seed.y - 1);
            if (canFillAbove && !spanAbove) {
                stack.push_back({ x, seed.y - 1 });
                spanAbove = true;
            } else if (!canFillAbove) {
                spanAbove = false;
            }

            const bool canFillBelow = (seed.y + 1 < canvasHeight) && canFillInterior(x, seed.y + 1);
            if (canFillBelow && !spanBelow) {
                stack.push_back({ x, seed.y + 1 });
                spanBelow = true;
            } else if (!canFillBelow) {
                spanBelow = false;
            }
        }
    }

    if (interiorMaskTiles.empty()) {
        return result;
    }

    const size_t pixelCount = static_cast<size_t>(canvasWidth) * static_cast<size_t>(canvasHeight);
    std::vector<uint8_t> sourceMask(pixelCount, 0);
    std::vector<int32_t> sourcePixels;

    for (const auto& [key, maskTile] : interiorMaskTiles) {
        const uint8_t* maskPixels = maskTile.data();
        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int y = key.y * static_cast<int32_t>(TILE_SIZE) + static_cast<int>(localY);
            if (y < 0 || y >= canvasHeight) {
                continue;
            }

            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const uint32_t maskIdx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
                if (maskPixels[maskIdx + 3] == 0) {
                    continue;
                }

                const int x = key.x * static_cast<int32_t>(TILE_SIZE) + static_cast<int>(localX);
                if (x < 0 || x >= canvasWidth) {
                    continue;
                }

                const size_t idx = pixelIndex(x, y, canvasWidth);
                sourceMask[idx] = 1;
                sourcePixels.push_back(static_cast<int32_t>(idx));
            }
        }
    }

    const bool sourceTouchesCanvasBoundary
        = std::any_of(sourcePixels.begin(), sourcePixels.end(), [&](int32_t idx) {
              const int x = idx % canvasWidth;
              const int y = idx / canvasWidth;
              return x == 0 || y == 0 || x + 1 == canvasWidth || y + 1 == canvasHeight;
          });

    std::vector<uint8_t> allowedMask(pixelCount, 0);
    std::vector<uint8_t> boundaryMask(pixelCount, 0);
    std::vector<uint8_t> preserveBoundaryMask(pixelCount, 0);
    std::vector<uint8_t> distanceMap(pixelCount, 0);
    std::array<std::vector<int32_t>, 256> pixelsByDistance;
    const size_t canvasRowCount = static_cast<size_t>(canvasHeight);
    const size_t precomputeWorkerCount = fillComputeWorkerCount(canvasRowCount);
    std::vector<std::array<std::vector<int32_t>, 256>> localPixelsByDistance(precomputeWorkerCount);

    parallelForFillChunks(
        canvasRowCount, 32, [&](size_t workerIndex, size_t beginRow, size_t endRow) {
            auto& workerBuckets = localPixelsByDistance[workerIndex];
            for (size_t y = beginRow; y < endRow; ++y) {
                for (int x = 0; x < canvasWidth; ++x) {
                    if (selectionMask
                        && fillMaskAlphaAt(selectionMask, x, static_cast<int>(y)) == 0) {
                        continue;
                    }

                    const size_t idx = pixelIndex(x, static_cast<int>(y), canvasWidth);
                    allowedMask[idx] = 1;
                    const uint8_t distance = static_cast<uint8_t>(fillDistance(
                        samplePixel(grid, x, static_cast<int>(y)), seedPixel, fillMode));
                    distanceMap[idx] = distance;
                    workerBuckets[distance].push_back(static_cast<int32_t>(idx));
                }
            }
        });

    for (auto& workerBuckets : localPixelsByDistance) {
        for (size_t distance = 0; distance < pixelsByDistance.size(); ++distance) {
            auto& bucket = workerBuckets[distance];
            if (bucket.empty()) {
                continue;
            }
            auto& mergedBucket = pixelsByDistance[distance];
            mergedBucket.insert(mergedBucket.end(), bucket.begin(), bucket.end());
        }
    }

    parallelForFillChunks(canvasRowCount, 32, [&](size_t, size_t beginRow, size_t endRow) {
        for (size_t y = beginRow; y < endRow; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const size_t idx = pixelIndex(x, static_cast<int>(y), canvasWidth);
                if (!allowedMask[idx]) {
                    continue;
                }

                bool touchesBoundary
                    = (x == 0 || y == 0 || x + 1 == canvasWidth || y + 1 == canvasHeight);
                bool touchesPreserveBoundary = false;
                if (selectionMask) {
                    for (const auto& [ox, oy] : kNeighborOffsets) {
                        const int nx = x + ox;
                        const int ny = static_cast<int>(y) + oy;
                        if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                            touchesBoundary = true;
                            continue;
                        }
                        if (!allowedMask[pixelIndex(nx, ny, canvasWidth)]) {
                            touchesBoundary = true;
                            touchesPreserveBoundary = true;
                        }
                    }
                }

                if (touchesBoundary) {
                    boundaryMask[idx] = 1;
                }
                if (touchesPreserveBoundary) {
                    preserveBoundaryMask[idx] = 1;
                }
            }
        }
    });

    int escapeLevel = kInteriorSeedTolerance + 1;
    if (fillMode == FillSemanticMode::Exterior) {
        ConnectivityDisjointSet connectivity(pixelCount);
        escapeLevel = 256;

        for (int distance = 0; distance < 256 && escapeLevel == 256; ++distance) {
            for (int32_t idx : pixelsByDistance[distance]) {
                uint8_t nodeFlags = 0;
                if (sourceMask[static_cast<size_t>(idx)] != 0) {
                    nodeFlags = static_cast<uint8_t>(nodeFlags | kComponentHasSource);
                }
                if (boundaryMask[static_cast<size_t>(idx)] != 0) {
                    nodeFlags = static_cast<uint8_t>(nodeFlags | kComponentTouchesBoundary);
                }

                connectivity.activate(idx, nodeFlags);

                const int x = idx % canvasWidth;
                const int y = idx / canvasWidth;
                for (const auto& [ox, oy] : kNeighborOffsets) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                        continue;
                    }

                    const int32_t neighborIdx
                        = static_cast<int32_t>(pixelIndex(nx, ny, canvasWidth));
                    if (!allowedMask[static_cast<size_t>(neighborIdx)]
                        || !connectivity.isActive(neighborIdx)) {
                        continue;
                    }

                    connectivity.unite(idx, neighborIdx);
                }

                const uint8_t flags = connectivity.componentFlags(idx);
                if ((flags & (kComponentHasSource | kComponentTouchesBoundary))
                    == (kComponentHasSource | kComponentTouchesBoundary)) {
                    escapeLevel = distance;
                    break;
                }
            }
        }
    }

    std::vector<uint8_t> insideMask(pixelCount, 0);
    std::deque<int32_t> queue;
    for (int32_t idx : sourcePixels) {
        if (insideMask[static_cast<size_t>(idx)] != 0) {
            continue;
        }
        insideMask[static_cast<size_t>(idx)] = 1;
        queue.push_back(idx);
    }

    while (!queue.empty()) {
        const int32_t idx = queue.front();
        queue.pop_front();

        const int x = idx % canvasWidth;
        const int y = idx / canvasWidth;
        const uint8_t currentDistance = distanceMap[static_cast<size_t>(idx)];
        for (const auto& [ox, oy] : kNeighborOffsets) {
            const int nx = x + ox;
            const int ny = y + oy;
            if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                continue;
            }

            const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
            if (!allowedMask[neighborIdx] || insideMask[neighborIdx] != 0) {
                continue;
            }
            const uint8_t neighborDistance = distanceMap[neighborIdx];
            if (static_cast<int>(neighborDistance) >= escapeLevel) {
                continue;
            }
            // Exterior fill may climb a soft edge, but it should not descend
            // into a neighboring basin after crossing that ridge.
            if (fillMode == FillSemanticMode::Exterior && neighborDistance < currentDistance) {
                continue;
            }

            insideMask[neighborIdx] = 1;
            queue.push_back(static_cast<int32_t>(neighborIdx));
        }
    }

    std::vector<uint8_t> boundaryLowMask(pixelCount, 0);
    if (fillMode == FillSemanticMode::Exterior && escapeLevel > 0 && escapeLevel < 256) {
        for (int y = 0; y < canvasHeight; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const size_t idx = pixelIndex(x, y, canvasWidth);
                if (!preserveBoundaryMask[idx] || !allowedMask[idx] || insideMask[idx] != 0) {
                    continue;
                }
                if (static_cast<int>(distanceMap[idx]) >= escapeLevel) {
                    continue;
                }

                boundaryLowMask[idx] = 1;
                queue.push_back(static_cast<int32_t>(idx));
            }
        }

        while (!queue.empty()) {
            const int32_t idx = queue.front();
            queue.pop_front();

            const int x = idx % canvasWidth;
            const int y = idx / canvasWidth;
            for (const auto& [ox, oy] : kNeighborOffsets) {
                const int nx = x + ox;
                const int ny = y + oy;
                if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                    continue;
                }

                const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
                if (!allowedMask[neighborIdx] || insideMask[neighborIdx] != 0
                    || boundaryLowMask[neighborIdx] != 0) {
                    continue;
                }
                if (static_cast<int>(distanceMap[neighborIdx]) >= escapeLevel) {
                    continue;
                }

                boundaryLowMask[neighborIdx] = 1;
                queue.push_back(static_cast<int32_t>(neighborIdx));
            }
        }
    }

    std::vector<int32_t> sourcePlateauDistance(pixelCount, -1);
    std::vector<int32_t> boundaryPlateauDistance(pixelCount, -1);
    auto propagatePlateauDistances
        = [&](const std::vector<int32_t>& seeds, std::vector<int32_t>& distances) {
              std::deque<int32_t> plateauQueue;
              for (int32_t seedIdx : seeds) {
                  if (distances[static_cast<size_t>(seedIdx)] >= 0) {
                      continue;
                  }
                  distances[static_cast<size_t>(seedIdx)] = 0;
                  plateauQueue.push_back(seedIdx);
              }

              while (!plateauQueue.empty()) {
                  const int32_t idx = plateauQueue.front();
                  plateauQueue.pop_front();

                  const int x = idx % canvasWidth;
                  const int y = idx / canvasWidth;
                  const int nextDistance = distances[static_cast<size_t>(idx)] + 1;
                  for (const auto& [ox, oy] : kNeighborOffsets) {
                      const int nx = x + ox;
                      const int ny = y + oy;
                      if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                          continue;
                      }

                      const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
                      if (!allowedMask[neighborIdx]
                          || static_cast<int>(distanceMap[neighborIdx]) != escapeLevel) {
                          continue;
                      }
                      if (distances[neighborIdx] >= 0) {
                          continue;
                      }

                      distances[neighborIdx] = nextDistance;
                      plateauQueue.push_back(static_cast<int32_t>(neighborIdx));
                  }
              }
          };

    if (fillMode == FillSemanticMode::Exterior && escapeLevel < 256) {
        std::vector<int32_t> sourcePlateauSeeds;
        std::vector<int32_t> boundaryPlateauSeeds;

        for (int y = 0; y < canvasHeight; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const size_t idx = pixelIndex(x, y, canvasWidth);
                if (!allowedMask[idx] || static_cast<int>(distanceMap[idx]) != escapeLevel) {
                    continue;
                }

                bool adjacentToInside = false;
                bool adjacentToBoundarySide = preserveBoundaryMask[idx] != 0;
                for (const auto& [ox, oy] : kNeighborOffsets) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                        continue;
                    }

                    const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
                    adjacentToInside = adjacentToInside || (insideMask[neighborIdx] != 0);
                    adjacentToBoundarySide
                        = adjacentToBoundarySide || (boundaryLowMask[neighborIdx] != 0);
                }

                if (adjacentToInside) {
                    sourcePlateauSeeds.push_back(static_cast<int32_t>(idx));
                }
                if (adjacentToBoundarySide) {
                    boundaryPlateauSeeds.push_back(static_cast<int32_t>(idx));
                }
            }
        }

        propagatePlateauDistances(sourcePlateauSeeds, sourcePlateauDistance);
        propagatePlateauDistances(boundaryPlateauSeeds, boundaryPlateauDistance);
    }

    std::vector<uint8_t> finalMask = insideMask;
    parallelForFillChunks(canvasRowCount, 32, [&](size_t, size_t beginRow, size_t endRow) {
        for (size_t y = beginRow; y < endRow; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const size_t idx = pixelIndex(x, static_cast<int>(y), canvasWidth);
                if (!allowedMask[idx] || finalMask[idx] != 0 || escapeLevel >= 256
                    || static_cast<int>(distanceMap[idx]) != escapeLevel) {
                    continue;
                }

                const int32_t sourceDistance = sourcePlateauDistance[idx];
                const int32_t boundaryDistance = boundaryPlateauDistance[idx];
                if ((sourceDistance >= 0)
                    && (boundaryDistance < 0 || sourceDistance <= boundaryDistance)) {
                    finalMask[idx] = 1;
                }
            }
        }
    });

    if (fillMode == FillSemanticMode::Exterior && !selectionMask && sourceTouchesCanvasBoundary) {
        std::array<std::deque<int32_t>, 256> rampFrontier;
        std::vector<int16_t> rampBestLevel(pixelCount, -1);

        auto tryQueueRamp = [&](int x, int y, uint8_t minLevel) {
            if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
                return;
            }

            const size_t idx = pixelIndex(x, y, canvasWidth);
            if (!allowedMask[idx] || finalMask[idx] != 0) {
                return;
            }

            const uint8_t level = distanceMap[idx];
            if (level < minLevel || level <= kInteriorSeedTolerance) {
                return;
            }

            if (rampBestLevel[idx] >= 0 && level >= static_cast<uint8_t>(rampBestLevel[idx])) {
                return;
            }
            rampBestLevel[idx] = static_cast<int16_t>(level);
            rampFrontier[level].push_back(static_cast<int32_t>(idx));
        };

        for (size_t idx = 0; idx < pixelCount; ++idx) {
            if (!finalMask[idx]) {
                continue;
            }

            const int x = static_cast<int>(idx % static_cast<size_t>(canvasWidth));
            const int y = static_cast<int>(idx / static_cast<size_t>(canvasWidth));
            const uint8_t level = distanceMap[idx];
            for (const auto& [ox, oy] : kNeighborOffsets) {
                tryQueueRamp(x + ox, y + oy, level);
            }
        }

        for (int level = 0; level < 256; ++level) {
            auto& frontier = rampFrontier[static_cast<size_t>(level)];
            while (!frontier.empty()) {
                const int32_t idx = frontier.front();
                frontier.pop_front();

                if (rampBestLevel[static_cast<size_t>(idx)] != level
                    || finalMask[static_cast<size_t>(idx)] != 0) {
                    continue;
                }

                const int x = idx % canvasWidth;
                const int y = idx / canvasWidth;
                bool hasHigherAllowedNeighbor = false;
                for (const auto& [ox, oy] : kNeighborOffsets) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                        continue;
                    }

                    const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
                    if (!allowedMask[neighborIdx]) {
                        continue;
                    }
                    if (distanceMap[neighborIdx] > static_cast<uint8_t>(level)) {
                        hasHigherAllowedNeighbor = true;
                        break;
                    }
                }

                const PremultPixel px = samplePixel(grid, x, y);
                const bool touchesCanvasBoundary
                    = x == 0 || y == 0 || x + 1 == canvasWidth || y + 1 == canvasHeight;
                if (!hasHigherAllowedNeighbor && !(touchesCanvasBoundary && px.a < 255)) {
                    continue;
                }

                finalMask[static_cast<size_t>(idx)] = 1;
                for (const auto& [ox, oy] : kNeighborOffsets) {
                    tryQueueRamp(x + ox, y + oy, static_cast<uint8_t>(level));
                }
            }
        }

        std::deque<int32_t> softBoundaryQueue;
        std::vector<uint8_t> queuedSoftBoundary(pixelCount, 0);
        auto tryQueueSoftBoundary = [&](int x, int y) {
            if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
                return;
            }

            const size_t idx = pixelIndex(x, y, canvasWidth);
            if (!allowedMask[idx] || finalMask[idx] != 0 || queuedSoftBoundary[idx] != 0) {
                return;
            }

            const PremultPixel px = samplePixel(grid, x, y);
            if (px.a == 0 || px.a == 255) {
                return;
            }

            queuedSoftBoundary[idx] = 1;
            softBoundaryQueue.push_back(static_cast<int32_t>(idx));
        };
        auto tryQueueCanvasSoftBoundarySeed = [&](int x, int y) {
            if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
                return;
            }

            const size_t idx = pixelIndex(x, y, canvasWidth);
            if (!allowedMask[idx] || finalMask[idx] != 0 || queuedSoftBoundary[idx] != 0) {
                return;
            }

            bool hasBoundaryFinalNeighbor = false;
            if (y == 0 || y + 1 == canvasHeight) {
                if (x > 0) {
                    hasBoundaryFinalNeighbor = hasBoundaryFinalNeighbor
                        || finalMask[pixelIndex(x - 1, y, canvasWidth)] != 0;
                }
                if (x + 1 < canvasWidth) {
                    hasBoundaryFinalNeighbor = hasBoundaryFinalNeighbor
                        || finalMask[pixelIndex(x + 1, y, canvasWidth)] != 0;
                }
            }
            if (x == 0 || x + 1 == canvasWidth) {
                if (y > 0) {
                    hasBoundaryFinalNeighbor = hasBoundaryFinalNeighbor
                        || finalMask[pixelIndex(x, y - 1, canvasWidth)] != 0;
                }
                if (y + 1 < canvasHeight) {
                    hasBoundaryFinalNeighbor = hasBoundaryFinalNeighbor
                        || finalMask[pixelIndex(x, y + 1, canvasWidth)] != 0;
                }
            }
            if (!hasBoundaryFinalNeighbor) {
                return;
            }

            const PremultPixel px = samplePixel(grid, x, y);
            if (px.a == 0 || px.a == 255) {
                return;
            }

            const uint8_t level = distanceMap[idx];
            for (const auto& [ox, oy] : kNeighborOffsets) {
                const int nx = x + ox;
                const int ny = y + oy;
                if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                    continue;
                }

                const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
                if (!allowedMask[neighborIdx]) {
                    continue;
                }
                if (distanceMap[neighborIdx] < level) {
                    return;
                }
            }

            queuedSoftBoundary[idx] = 1;
            softBoundaryQueue.push_back(static_cast<int32_t>(idx));
        };

        for (size_t idx = 0; idx < pixelCount; ++idx) {
            if (!finalMask[idx]) {
                continue;
            }

            const int x = static_cast<int>(idx % static_cast<size_t>(canvasWidth));
            const int y = static_cast<int>(idx / static_cast<size_t>(canvasWidth));
            for (const auto& [ox, oy] : kNeighborOffsets) {
                tryQueueSoftBoundary(x + ox, y + oy);
            }
        }

        for (int x = 0; x < canvasWidth; ++x) {
            tryQueueCanvasSoftBoundarySeed(x, 0);
            if (canvasHeight > 1) {
                tryQueueCanvasSoftBoundarySeed(x, canvasHeight - 1);
            }
        }
        for (int y = 1; y + 1 < canvasHeight; ++y) {
            tryQueueCanvasSoftBoundarySeed(0, y);
            if (canvasWidth > 1) {
                tryQueueCanvasSoftBoundarySeed(canvasWidth - 1, y);
            }
        }

        while (!softBoundaryQueue.empty()) {
            const int32_t idx = softBoundaryQueue.front();
            softBoundaryQueue.pop_front();
            if (finalMask[static_cast<size_t>(idx)] != 0) {
                continue;
            }

            finalMask[static_cast<size_t>(idx)] = 1;
            const int x = idx % canvasWidth;
            const int y = idx / canvasWidth;
            const uint8_t level = distanceMap[static_cast<size_t>(idx)];
            for (const auto& [ox, oy] : kNeighborOffsets) {
                const int nx = x + ox;
                const int ny = y + oy;
                if (nx < 0 || nx >= canvasWidth || ny < 0 || ny >= canvasHeight) {
                    continue;
                }

                const size_t neighborIdx = pixelIndex(nx, ny, canvasWidth);
                if (!allowedMask[neighborIdx] || finalMask[neighborIdx] != 0
                    || queuedSoftBoundary[neighborIdx] != 0) {
                    continue;
                }
                const PremultPixel neighborPx = samplePixel(grid, nx, ny);
                if (neighborPx.a == 0 || neighborPx.a == 255) {
                    continue;
                }
                if (distanceMap[neighborIdx] < level) {
                    continue;
                }

                queuedSoftBoundary[neighborIdx] = 1;
                softBoundaryQueue.push_back(static_cast<int32_t>(neighborIdx));
            }
        }
    }

    FloodFillResult::RawTileMap rebuiltInteriorMaskTiles;
    FloodFillResult::RawTileMap rebuiltSoftEdgeMaskTiles;
    const size_t tileRowCount = static_cast<size_t>(
        (canvasHeight + static_cast<int>(TILE_SIZE) - 1) / static_cast<int>(TILE_SIZE));
    const size_t rebuildWorkerCount = fillComputeWorkerCount(tileRowCount);
    std::vector<FloodFillResult::RawTileMap> localInteriorMaskTiles(rebuildWorkerCount);
    std::vector<FloodFillResult::RawTileMap> localSoftEdgeMaskTiles(rebuildWorkerCount);

    parallelForFillChunks(
        tileRowCount, 1, [&](size_t workerIndex, size_t beginTileRow, size_t endTileRow) {
            auto& localInterior = localInteriorMaskTiles[workerIndex];
            auto& localSoft = localSoftEdgeMaskTiles[workerIndex];

            for (size_t tileRow = beginTileRow; tileRow < endTileRow; ++tileRow) {
                const int yStart = static_cast<int>(tileRow * static_cast<size_t>(TILE_SIZE));
                const int yEnd = std::min(yStart + static_cast<int>(TILE_SIZE), canvasHeight);
                for (int y = yStart; y < yEnd; ++y) {
                    for (int x = 0; x < canvasWidth; ++x) {
                        const size_t idx = pixelIndex(x, y, canvasWidth);
                        if (!allowedMask[idx] || finalMask[idx] == 0) {
                            continue;
                        }

                        const TileKey key { floorDiv(x, static_cast<int32_t>(TILE_SIZE)),
                            floorDiv(y, static_cast<int32_t>(TILE_SIZE)) };
                        const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
                        const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));

                        const PremultPixel originalPx = samplePixel(grid, x, y);
                        if (fillMode == FillSemanticMode::Stroke) {
                            if (originalPx.a == 255) {
                                writeMaskPixel(localInterior, key, localX, localY);
                            } else {
                                writeMaskPixel(localSoft, key, localX, localY);
                            }
                        } else if (static_cast<int>(distanceMap[idx]) <= kInteriorSeedTolerance) {
                            writeMaskPixel(localInterior, key, localX, localY);
                        } else {
                            writeMaskPixel(localSoft, key, localX, localY);
                        }
                    }
                }
            }
        });

    for (size_t workerIndex = 0; workerIndex < rebuildWorkerCount; ++workerIndex) {
        mergeMaskTileMap(rebuiltInteriorMaskTiles, std::move(localInteriorMaskTiles[workerIndex]));
        mergeMaskTileMap(rebuiltSoftEdgeMaskTiles, std::move(localSoftEdgeMaskTiles[workerIndex]));
    }

    interiorMaskTiles = std::move(rebuiltInteriorMaskTiles);
    softEdgeMaskTiles = std::move(rebuiltSoftEdgeMaskTiles);

    // Feed the selection mask into the per-pixel cap pass so multi-fill
    // accumulation under a soft selection cannot push alpha above the cap.
    FloodFillResult::RawTileMap bucketSelectionTiles = snapshotSelectionMaskTiles(selectionMask);
    const FloodFillResult::RawTileMap* bucketSelectionPtr
        = selectionMask ? &bucketSelectionTiles : nullptr;

    result = buildResultFromMaskTiles(grid, interiorMaskTiles, softEdgeMaskTiles, fillMode, fillR,
        fillG, fillB, fillA, bucketSelectionPtr);
    applyFillResultToGrid(grid, result);
    return result;
}

FloodFillResult classicFloodFill(TileGrid& grid, int seedX, int seedY, uint8_t fillR, uint8_t fillG,
    uint8_t fillB, uint8_t fillA, const TileGrid* selectionMask, int canvasWidth, int canvasHeight)
{
    FloodFillResult result;

    if (seedX < 0 || seedX >= canvasWidth || seedY < 0 || seedY >= canvasHeight) {
        return result;
    }

    uint8_t seedR = 0;
    uint8_t seedG = 0;
    uint8_t seedB = 0;
    uint8_t seedA = 0;
    if (!samplePixelAt(&grid, seedX, seedY, seedR, seedG, seedB, seedA)) {
        return result;
    }

    if (selectionMask && fillMaskAlphaAt(selectionMask, seedX, seedY) == 0) {
        return result;
    }

    if (colorsMatch(seedR, seedG, seedB, seedA, fillR, fillG, fillB, fillA)) {
        return result;
    }

    FloodFillResult::RawTileMap fillMaskTiles;
    auto canFillPixel = [&](int x, int y) -> bool {
        if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
            return false;
        }
        if (selectionMask && fillMaskAlphaAt(selectionMask, x, y) == 0) {
            return false;
        }
        if (maskHasPixel(fillMaskTiles, x, y)) {
            return false;
        }

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
        if (!samplePixelAt(&grid, x, y, r, g, b, a)) {
            return false;
        }
        return colorsMatch(r, g, b, a, seedR, seedG, seedB, seedA);
    };

    struct SeedPoint {
        int x = 0;
        int y = 0;
    };

    std::vector<SeedPoint> stack;
    stack.push_back({ seedX, seedY });

    std::unordered_set<TileKey, TileKeyHash> affectedKeys;
    affectedKeys.reserve(16);

    while (!stack.empty()) {
        const SeedPoint seed = stack.back();
        stack.pop_back();

        if (!canFillPixel(seed.x, seed.y)) {
            continue;
        }

        int left = seed.x;
        while (left > 0 && canFillPixel(left - 1, seed.y)) {
            --left;
        }

        int right = seed.x;
        while (right + 1 < canvasWidth && canFillPixel(right + 1, seed.y)) {
            ++right;
        }

        bool spanAbove = false;
        bool spanBelow = false;

        for (int x = left; x <= right; ++x) {
            const int32_t tx = floorDiv(x, static_cast<int32_t>(TILE_SIZE));
            const int32_t ty = floorDiv(seed.y, static_cast<int32_t>(TILE_SIZE));
            const TileKey key { tx, ty };
            const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
            const uint32_t localY = floorMod(seed.y, static_cast<int32_t>(TILE_SIZE));

            affectedKeys.insert(key);
            writeMaskPixel(fillMaskTiles, key, localX, localY);

            const bool canFillAbove = seed.y > 0 && canFillPixel(x, seed.y - 1);
            if (canFillAbove && !spanAbove) {
                stack.push_back({ x, seed.y - 1 });
                spanAbove = true;
            } else if (!canFillAbove) {
                spanAbove = false;
            }

            const bool canFillBelow = seed.y + 1 < canvasHeight && canFillPixel(x, seed.y + 1);
            if (canFillBelow && !spanBelow) {
                stack.push_back({ x, seed.y + 1 });
                spanBelow = true;
            } else if (!canFillBelow) {
                spanBelow = false;
            }
        }
    }

    if (affectedKeys.empty()) {
        return result;
    }
    FloodFillResult::RawTileMap classicSelectionTiles = snapshotSelectionMaskTiles(selectionMask);
    const FloodFillResult::RawTileMap* classicSelectionPtr
        = selectionMask ? &classicSelectionTiles : nullptr;
    result = buildResultFromMaskTiles(grid, fillMaskTiles, FloodFillResult::RawTileMap {},
        FillSemanticMode::Stroke, fillR, fillG, fillB, fillA, classicSelectionPtr);
    applyFillResultToGrid(grid, result);
    return result;
}

FloodFillResult floodFillRawTiles(const FloodFillResult::RawTileMap& sourceTiles, int seedX,
    int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const FloodFillResult::RawTileMap& selectionMaskTiles, int canvasWidth, int canvasHeight,
    TilePixelFormat contentFormat)
{
    TileGrid sourceGrid = rawTileMapToGrid(sourceTiles, contentFormat);
    TileGrid selectionGrid;
    const TileGrid* selectionMask = nullptr;
    if (!selectionMaskTiles.empty()) {
        selectionGrid = rawTileMapToGrid(selectionMaskTiles, TilePixelFormat::RGBA8);
        selectionMask = &selectionGrid;
    }

    return floodFill(sourceGrid, seedX, seedY, fillR, fillG, fillB, fillA, selectionMask,
        canvasWidth, canvasHeight);
}
FloodFillResult classicFloodFillRawTiles(const FloodFillResult::RawTileMap& sourceTiles, int seedX,
    int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const FloodFillResult::RawTileMap& selectionMaskTiles, int canvasWidth, int canvasHeight,
    TilePixelFormat contentFormat)
{
    // The classic worker path already operates on raw snapshots; keep it that
    // way so large fills do not pay for a full RawTileMap -> TileGrid rebuild.
    FloodFillResult result;

    if (seedX < 0 || seedX >= canvasWidth || seedY < 0 || seedY >= canvasHeight) {
        return result;
    }

    const PremultPixel seedPixel = sampleRawPixelAt(sourceTiles, seedX, seedY, contentFormat);
    if (!selectionMaskTiles.empty() && sampleRawAlphaAt(selectionMaskTiles, seedX, seedY) == 0) {
        return result;
    }

    if (colorsMatch(
            seedPixel.r, seedPixel.g, seedPixel.b, seedPixel.a, fillR, fillG, fillB, fillA)) {
        return result;
    }

    struct SeedPoint {
        int x = 0;
        int y = 0;
    };

    FloodFillResult::RawTileMap fillMaskTiles;
    std::vector<SeedPoint> stack;
    stack.push_back({ seedX, seedY });

    while (!stack.empty()) {
        const SeedPoint seed = stack.back();
        stack.pop_back();

        ClassicRawFillRowAccessor rowAccessor(sourceTiles,
            selectionMaskTiles.empty() ? nullptr : &selectionMaskTiles, &fillMaskTiles, seed.y,
            seedPixel, contentFormat);
        if (!rowAccessor.canFill(seed.x, canvasWidth)) {
            continue;
        }

        int left = seed.x;
        while (left > 0 && rowAccessor.canFill(left - 1, canvasWidth)) {
            --left;
        }

        int right = seed.x;
        while (right + 1 < canvasWidth && rowAccessor.canFill(right + 1, canvasWidth)) {
            ++right;
        }

        rowAccessor.markSpan(left, right);

        if (seed.y > 0) {
            ClassicRawFillRowAccessor aboveAccessor(sourceTiles,
                selectionMaskTiles.empty() ? nullptr : &selectionMaskTiles, &fillMaskTiles,
                seed.y - 1, seedPixel, contentFormat);
            bool spanAbove = false;
            for (int x = left; x <= right; ++x) {
                const bool canFillAbove = aboveAccessor.canFill(x, canvasWidth);
                if (canFillAbove && !spanAbove) {
                    stack.push_back({ x, seed.y - 1 });
                    spanAbove = true;
                } else if (!canFillAbove) {
                    spanAbove = false;
                }
            }
        }

        if (seed.y + 1 < canvasHeight) {
            ClassicRawFillRowAccessor belowAccessor(sourceTiles,
                selectionMaskTiles.empty() ? nullptr : &selectionMaskTiles, &fillMaskTiles,
                seed.y + 1, seedPixel, contentFormat);
            bool spanBelow = false;
            for (int x = left; x <= right; ++x) {
                const bool canFillBelow = belowAccessor.canFill(x, canvasWidth);
                if (canFillBelow && !spanBelow) {
                    stack.push_back({ x, seed.y + 1 });
                    spanBelow = true;
                } else if (!canFillBelow) {
                    spanBelow = false;
                }
            }
        }
    }

    if (fillMaskTiles.empty()) {
        return result;
    }

    return buildResultFromMaskTiles(sourceTiles, fillMaskTiles, FloodFillResult::RawTileMap {},
        FillSemanticMode::Stroke, fillR, fillG, fillB, fillA,
        selectionMaskTiles.empty() ? nullptr : &selectionMaskTiles, contentFormat);
}

namespace {

struct PolygonMaskBuildResult {
    FloodFillResult::RawTileMap maskTiles;
    int pixelsFilled = 0;
};

void fillMaskSpan(std::vector<uint8_t>& tile, uint32_t localY, uint32_t localX0, uint32_t localX1)
{
    if (tile.size() != TILE_BYTE_SIZE) {
        tile.assign(TILE_BYTE_SIZE, 0);
    }

    const uint32_t begin = rawPixelIndex(localX0, localY);
    const size_t byteCount = static_cast<size_t>(localX1 - localX0 + 1) * TILE_CHANNELS;
    std::memset(tile.data() + begin, 255, byteCount);
}

PolygonMaskBuildResult buildPolygonFillMaskTiles(
    const std::vector<Vector2>& polygon, int canvasWidth, int canvasHeight)
{
    PolygonMaskBuildResult result;
    if (polygon.size() < 3 || canvasWidth <= 0 || canvasHeight <= 0) {
        return result;
    }

    float minX = polygon[0].x;
    float minY = polygon[0].y;
    float maxX = polygon[0].x;
    float maxY = polygon[0].y;
    for (const auto& p : polygon) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }

    int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(minX)));
    int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(minY)));
    int32_t x1 = std::min(canvasWidth - 1, static_cast<int32_t>(std::ceil(maxX)));
    int32_t y1 = std::min(canvasHeight - 1, static_cast<int32_t>(std::ceil(maxY)));

    if (x1 < x0 || y1 < y0)
        return result;

    constexpr int32_t ts = static_cast<int32_t>(TILE_SIZE);
    const int32_t tileMinX = floorDiv(x0, ts);
    const int32_t tileMaxX = floorDiv(x1, ts);
    const int32_t tileMinY = floorDiv(y0, ts);
    const int32_t tileMaxY = floorDiv(y1, ts);
    const size_t bboxTileCapacity = static_cast<size_t>(tileMaxX - tileMinX + 1)
        * static_cast<size_t>(tileMaxY - tileMinY + 1);

    const size_t rowCount = static_cast<size_t>(y1 - y0 + 1);
    if (rowCount == 0) {
        return result;
    }

    const size_t workerCount = std::max<size_t>(1, fillComputeWorkerCount(rowCount));
    struct WorkerMaskTiles {
        FloodFillResult::RawTileMap maskTiles;
        int pixelsFilled = 0;
    };

    std::vector<WorkerMaskTiles> workers(workerCount);
    parallelForFillChunks(rowCount, 32, [&](size_t workerIndex, size_t begin, size_t end) {
        WorkerMaskTiles& worker = workers[workerIndex];
        const size_t reserveHint = bboxTileCapacity / workerCount + 1;
        worker.maskTiles.reserve(std::max<size_t>(worker.maskTiles.size(), reserveHint));

        std::vector<float> intersections;
        intersections.reserve(polygon.size());

        for (size_t row = begin; row < end; ++row) {
            const int32_t y = y0 + static_cast<int32_t>(row);
            const float scanY = static_cast<float>(y) + 0.5f;
            intersections.clear();

            for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
                const Vector2& a = polygon[j];
                const Vector2& b = polygon[i];
                if ((a.y <= scanY) == (b.y <= scanY)) {
                    continue;
                }

                const float t = (scanY - a.y) / (b.y - a.y + 0.0000001f);
                intersections.push_back(a.x + t * (b.x - a.x));
            }

            if (intersections.size() < 2) {
                continue;
            }

            std::sort(intersections.begin(), intersections.end());
            if ((intersections.size() & 1U) != 0U) {
                intersections.pop_back();
            }

            const int32_t ty = floorDiv(y, ts);
            const uint32_t localY = floorMod(y, ts);
            for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
                const int32_t spanX0
                    = std::max(static_cast<int32_t>(std::ceil(intersections[k] - 0.5f)), x0);
                const int32_t spanX1
                    = std::min(static_cast<int32_t>(std::floor(intersections[k + 1] - 0.5f)), x1);
                if (spanX1 < spanX0) {
                    continue;
                }

                const int32_t tileStartX = floorDiv(spanX0, ts);
                const int32_t tileEndX = floorDiv(spanX1, ts);
                for (int32_t tx = tileStartX; tx <= tileEndX; ++tx) {
                    const int32_t tileWorldX0 = tx * ts;
                    const int32_t segmentX0 = std::max(spanX0, tileWorldX0);
                    const int32_t segmentX1 = std::min(spanX1, tileWorldX0 + ts - 1);
                    const uint32_t localX0 = floorMod(segmentX0, ts);
                    const uint32_t localX1 = floorMod(segmentX1, ts);
                    fillMaskSpan(worker.maskTiles[TileKey { tx, ty }], localY, localX0, localX1);
                    worker.pixelsFilled += segmentX1 - segmentX0 + 1;
                }
            }
        }
    });

    result.maskTiles.reserve(bboxTileCapacity);
    for (WorkerMaskTiles& worker : workers) {
        result.pixelsFilled += worker.pixelsFilled;
        mergeMaskTileMap(result.maskTiles, std::move(worker.maskTiles));
    }

    return result;
}

} // namespace

FloodFillResult fillPolygon(TileGrid& grid, const std::vector<Vector2>& polygon, uint8_t fillR,
    uint8_t fillG, uint8_t fillB, uint8_t fillA, int canvasWidth, int canvasHeight,
    const TileGrid* selectionMask)
{
    PolygonMaskBuildResult mask = buildPolygonFillMaskTiles(polygon, canvasWidth, canvasHeight);
    if (mask.maskTiles.empty()) {
        return {};
    }

    FloodFillResult::RawTileMap selectionTiles = snapshotSelectionMaskTiles(selectionMask);
    const FloodFillResult::RawTileMap* selectionPtr = selectionMask ? &selectionTiles : nullptr;

    FloodFillResult result
        = buildResultFromMaskTiles(grid, mask.maskTiles, FloodFillResult::RawTileMap {},
            FillSemanticMode::Stroke, fillR, fillG, fillB, fillA, selectionPtr);
    if (result.pixelsFilled > 0) {
        applyFillResultToGrid(grid, result);
    }
    return result;
}

FloodFillResult fillMaskTiles(TileGrid& grid, const FloodFillResult::RawTileMap& maskTiles,
    uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA, const TileGrid* selectionMask,
    bool preserveDestinationAlpha)
{
    if (maskTiles.empty()) {
        return {};
    }

    FloodFillResult::RawTileMap selectionTiles = snapshotSelectionMaskTiles(selectionMask);
    const FloodFillResult::RawTileMap* selectionPtr = selectionMask ? &selectionTiles : nullptr;

    FloodFillResult result
        = buildResultFromMaskTiles(grid, maskTiles, FloodFillResult::RawTileMap {},
            FillSemanticMode::Stroke, fillR, fillG, fillB, fillA, selectionPtr,
            preserveDestinationAlpha);
    if (result.pixelsFilled > 0) {
        applyFillResultToGrid(grid, result);
    }
    return result;
}

FloodFillResult previewFillPolygon(const TileGrid& grid, const std::vector<Vector2>& polygon,
    uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA, int canvasWidth, int canvasHeight,
    const TileGrid* selectionMask)
{
    PolygonMaskBuildResult mask = buildPolygonFillMaskTiles(polygon, canvasWidth, canvasHeight);
    if (mask.maskTiles.empty()) {
        return {};
    }

    FloodFillResult::RawTileMap selectionTiles = snapshotSelectionMaskTiles(selectionMask);
    const FloodFillResult::RawTileMap* selectionPtr = selectionMask ? &selectionTiles : nullptr;

    return buildResultFromMaskTiles(grid, mask.maskTiles, FloodFillResult::RawTileMap {},
        FillSemanticMode::Stroke, fillR, fillG, fillB, fillA, selectionPtr);
}

FloodFillResult previewFillPolygonRawTiles(const FloodFillResult::RawTileMap& sourceTiles,
    const std::vector<Vector2>& polygon, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    int canvasWidth, int canvasHeight, const FloodFillResult::RawTileMap* selectionMaskTiles,
    TilePixelFormat contentFormat)
{
    PolygonMaskBuildResult mask = buildPolygonFillMaskTiles(polygon, canvasWidth, canvasHeight);
    if (mask.maskTiles.empty()) {
        return {};
    }

    return buildResultFromMaskTiles(sourceTiles, mask.maskTiles, FloodFillResult::RawTileMap {},
        FillSemanticMode::Stroke, fillR, fillG, fillB, fillA, selectionMaskTiles, contentFormat);
}

FloodFillResult previewFillPolygonMask(
    const std::vector<Vector2>& polygon, int canvasWidth, int canvasHeight)
{
    PolygonMaskBuildResult mask = buildPolygonFillMaskTiles(polygon, canvasWidth, canvasHeight);
    FloodFillResult result;
    result.fillMaskTiles = std::move(mask.maskTiles);
    result.pixelsFilled = mask.pixelsFilled;
    return result;
}

} // namespace aether
