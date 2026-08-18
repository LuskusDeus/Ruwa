// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   S E L E C T I O N   M A N A G E R
// ==========================================================================

#include "features/selection/LassoSelectionManager.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace aether {

namespace {

inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

inline uint32_t floorMod(int32_t a, int32_t b)
{
    int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

bool pointInPolygon(const Vector2& p, const std::vector<Vector2>& poly)
{
    bool inside = false;
    size_t count = poly.size();
    if (count < 3)
        return false;
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vector2& a = poly[i];
        const Vector2& b = poly[j];
        bool intersect = ((a.y > p.y) != (b.y > p.y))
            && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 0.0000001f) + a.x);
        if (intersect)
            inside = !inside;
    }
    return inside;
}

// ---- Tile coverage bitmaps (edge rebuilding) ----
//
// Edge extraction only ever asks "does this pixel have non-zero selection
// alpha, and do its four neighbours?". Answering that from a bitmap instead of
// from the pixel buffer turns the inner loop into whole-word logic: an interior
// scanline of a solid tile produces no edges at all and costs four word tests
// instead of 256 pixel reads with four neighbour lookups each.

static_assert(TILE_SIZE % 64 == 0, "coverage bitmaps assume a multiple-of-64 tile width");
constexpr uint32_t kRowWords = TILE_SIZE / 64;

/// Coverage of one tile scanline, LSB-first: bit i of word w is pixel
/// (w * 64 + i). Bits outside a covered area (and every bit of an absent
/// neighbour tile) are zero.
struct RowBits {
    uint64_t words[kRowWords] {};

    bool any() const noexcept
    {
        uint64_t merged = 0;
        for (uint32_t w = 0; w < kRowWords; ++w)
            merged |= words[w];
        return merged != 0;
    }
};

/// Coverage bits for one scanline of a tile buffer (null tile = all zero).
RowBits rowCoverage(const uint8_t* px, uint32_t ly)
{
    RowBits bits;
    if (!px)
        return bits;

    const uint8_t* row = px + static_cast<size_t>(ly) * TILE_SIZE * TILE_CHANNELS;
    for (uint32_t w = 0; w < kRowWords; ++w) {
        const uint8_t* chunk = row + static_cast<size_t>(w) * 64 * TILE_CHANNELS;

        // Probe the 64-pixel chunk as whole words first. All-bits-clear means
        // every alpha is zero, all-bits-set means every alpha is 0xFF; both are
        // endian-independent and both are the overwhelmingly common case for a
        // large selection. Only a chunk straddling the selection edge falls
        // through to the per-pixel pass.
        constexpr size_t chunkWords = (64 * TILE_CHANNELS) / sizeof(uint64_t);
        uint64_t andAll = ~uint64_t { 0 };
        uint64_t orAll = 0;
        for (size_t i = 0; i < chunkWords; ++i) {
            uint64_t word = 0;
            std::memcpy(&word, chunk + i * sizeof(uint64_t), sizeof(word));
            andAll &= word;
            orAll |= word;
        }

        if (orAll == 0) {
            bits.words[w] = 0;
        } else if (andAll == ~uint64_t { 0 }) {
            bits.words[w] = ~uint64_t { 0 };
        } else {
            uint64_t acc = 0;
            for (uint32_t i = 0; i < 64; ++i) {
                acc |= static_cast<uint64_t>(chunk[i * TILE_CHANNELS + 3] != 0) << i;
            }
            bits.words[w] = acc;
        }
    }
    return bits;
}

/// Coverage of the pixels one step to the left: bit i becomes bit i-1, with
/// `carryIn` (the neighbour tile's rightmost column) shifted into bit 0.
RowBits shiftedFromLeft(const RowBits& v, bool carryIn)
{
    RowBits r;
    uint64_t carry = carryIn ? 1u : 0u;
    for (uint32_t w = 0; w < kRowWords; ++w) {
        r.words[w] = (v.words[w] << 1) | carry;
        carry = v.words[w] >> 63;
    }
    return r;
}

/// Coverage of the pixels one step to the right: bit i becomes bit i+1, with
/// `carryIn` (the neighbour tile's leftmost column) shifted into the top bit.
RowBits shiftedFromRight(const RowBits& v, bool carryIn)
{
    RowBits r;
    uint64_t carry = carryIn ? (uint64_t { 1 } << 63) : 0u;
    for (uint32_t w = kRowWords; w-- > 0;) {
        r.words[w] = (v.words[w] >> 1) | carry;
        carry = (v.words[w] & 1u) << 63;
    }
    return r;
}

/// Invoke `emit(lx)` for every set bit of `a & ~b`, in ascending order.
template <typename Fn> void forEachUncovered(const RowBits& a, const RowBits& b, Fn&& emit)
{
    for (uint32_t w = 0; w < kRowWords; ++w) {
        uint64_t bits = a.words[w] & ~b.words[w];
        while (bits != 0) {
            const uint32_t bit = static_cast<uint32_t>(std::countr_zero(bits));
            bits &= bits - 1;
            emit(w * 64 + bit);
        }
    }
}

/// True when pixel (lx, ly) of a tile buffer has non-zero alpha. Used only for
/// the single-column carries at a tile's left / right seam.
bool pixelCovered(const uint8_t* px, uint32_t lx, uint32_t ly)
{
    return px && px[(static_cast<size_t>(ly) * TILE_SIZE + lx) * TILE_CHANNELS + 3] != 0;
}

/// True when every pixel of an RGBA8 tile buffer equals the first one.
/// Compares two pixels at a time and bails on the first mismatch, so the
/// non-uniform case (a tile straddling the selection edge) costs almost nothing
/// while the uniform case costs one streaming pass over the tile.
bool uniformTilePixel(const uint8_t* px)
{
    static_assert(TILE_BYTE_SIZE % sizeof(uint64_t) == 0);
    static_assert(
        TILE_CHANNELS * 2 == sizeof(uint64_t), "the probe pattern holds exactly 2 pixels");
    uint8_t pair[sizeof(uint64_t)];
    std::memcpy(pair, px, TILE_CHANNELS);
    std::memcpy(pair + TILE_CHANNELS, px, TILE_CHANNELS);
    uint64_t pattern = 0;
    std::memcpy(&pattern, pair, sizeof(pattern));

    constexpr size_t wordCount = TILE_BYTE_SIZE / sizeof(uint64_t);
    for (size_t i = 0; i < wordCount; ++i) {
        uint64_t word = 0;
        std::memcpy(&word, px + i * sizeof(uint64_t), sizeof(word));
        if (word != pattern) {
            return false;
        }
    }
    return true;
}

uint8_t maskAlphaAt(
    const TileGrid& grid, int32_t x, int32_t y, uint32_t canvasWidth, uint32_t canvasHeight)
{
    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    if (clipToCanvas) {
        if (x < 0 || y < 0)
            return 0;
        if (x >= static_cast<int32_t>(canvasWidth) || y >= static_cast<int32_t>(canvasHeight))
            return 0;
    }

    int32_t tx = floorDiv(x, static_cast<int32_t>(TILE_SIZE));
    int32_t ty = floorDiv(y, static_cast<int32_t>(TILE_SIZE));
    uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
    uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
    const TileData* tile = grid.getTile(TileKey { tx, ty });
    if (!tile)
        return 0;

    uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return tile->pixels()[idx + 3];
}
} // namespace

void LassoSelectionManager::clear()
{
    m_regions.clear();
    m_mask.clear();
    clearEdges();
    setMaskHasSoftAlpha(false);
    invalidateMaskSnapshotCache();
}

std::shared_ptr<const MaskTileSnapshot> LassoSelectionManager::snapshotMask() const
{
    if (m_cachedMaskSnapshot) {
        return m_cachedMaskSnapshot;
    }
    auto snapshot = std::make_shared<MaskTileSnapshot>();
    snapshot->reserve(m_mask.tiles().size());
    for (const auto& [key, tile] : m_mask.tiles()) {
        if (tile.isSolid()) {
            uint8_t r = 0, g = 0, b = 0, a = 0;
            tile.solidColor(r, g, b, a);
            snapshot->emplace(key, makeUniformMaskTile(r, g, b, a));
            continue;
        }
        const uint8_t* px = tile.pixels();
        // A large selection is mostly solid interior tiles; storing those as a
        // single RGBA value keeps the snapshot (and the undo entry holding it)
        // small and skips the copy entirely. The probe bails on the first
        // mismatch, so a partially covered tile costs almost nothing.
        if (uniformTilePixel(px)) {
            snapshot->emplace(key, makeUniformMaskTile(px[0], px[1], px[2], px[3]));
            continue;
        }
        // Range-construct rather than size-then-memcpy: the sized constructor
        // would zero all TILE_BYTE_SIZE bytes before the copy overwrote them.
        snapshot->emplace(key, std::vector<uint8_t>(px, px + TILE_BYTE_SIZE));
    }
    m_cachedMaskSnapshot = std::move(snapshot);
    return m_cachedMaskSnapshot;
}

void LassoSelectionManager::applyMaskSnapshot(std::shared_ptr<const MaskTileSnapshot> maskTiles,
    std::vector<LassoRegion> regions, bool softAlpha, uint32_t canvasWidth, uint32_t canvasHeight)
{
    m_regions = std::move(regions);
    {
        MaskMutationScope scope(*this);
        // Soft-alpha state is set explicitly below. The snapshot cache is
        // invalidated by the scope, then replaced with maskTiles after it ends.
        scope.disableSoftAlphaInvalidation();
        TileGrid& mask = scope.grid();
        mask.clear();
        if (maskTiles) {
            for (const auto& [key, bytes] : *maskTiles) {
                const MaskTileView incoming = viewMaskTile(bytes);
                if (!incoming.valid)
                    continue;
                TileData& tile = mask.getOrCreateTile(key);
                // Expand even a uniform entry into real pixels: the live mask
                // grid is read through const pixels() all over the paint / fill
                // / transform paths, and that returns zeros for a solid tile.
                // The compact encoding is a storage format for the snapshot,
                // not a representation the live grid supports.
                incoming.expandInto(tile.pixels());
                tile.markDirty();
            }
        }
    }
    setMaskHasSoftAlpha(softAlpha);
    // The live mask now exactly equals *maskTiles, so we can adopt it as the
    // cached snapshot — no copy needed for any subsequent capture until the
    // next mutation.
    m_cachedMaskSnapshot
        = maskTiles ? std::move(maskTiles) : std::make_shared<const MaskTileSnapshot>();

    if (m_mask.empty()) {
        clearEdges();
    } else {
        rebuildEdges(canvasWidth, canvasHeight);
    }
}

void LassoSelectionManager::applyRasterSelectionMask(const MaskTileSnapshot& maskTiles,
    LassoSelectionMode mode, uint32_t canvasWidth, uint32_t canvasHeight)
{
    const bool replaceSelection = mode == LassoSelectionMode::Replace;
    if (mode == LassoSelectionMode::Replace) {
        m_regions.clear();
        m_mask.clear();
        clearEdges();
        setMaskHasSoftAlpha(false);
    } else if (mode == LassoSelectionMode::Subtract && m_mask.empty()) {
        return;
    }

    if (maskTiles.empty()) {
        invalidateMaskSnapshotCache();
        return;
    }

    invalidateMaskSnapshotCache();
    if (replaceSelection) {
        // The destination is empty after clear(), so combining pixel-by-pixel is
        // unnecessary. Write each ready-made mask tile in one block.
        for (const auto& [key, bytes] : maskTiles) {
            const MaskTileView incoming = viewMaskTile(bytes);
            if (!incoming.valid) {
                continue;
            }

            TileData& tile = m_mask.getOrCreateTile(key);
            incoming.expandInto(tile.pixels());
            tile.markDirty();
        }
    } else if (mode == LassoSelectionMode::Add) {
        for (const auto& [key, bytes] : maskTiles) {
            const MaskTileView incoming = viewMaskTile(bytes);
            if (!incoming.valid) {
                continue;
            }

            TileData* existingTile = m_mask.getTile(key);
            if (!existingTile) {
                TileData& tile = m_mask.getOrCreateTile(key);
                incoming.expandInto(tile.pixels());
                tile.markDirty();
                continue;
            }

            TileData& tile = *existingTile;
            uint8_t* destination = tile.pixels();
            bool changed = false;
            for (uint32_t idx = 0; idx < TILE_BYTE_SIZE; idx += TILE_CHANNELS) {
                const uint8_t next = std::max(destination[idx + 3], incoming.alphaAt(idx));
                if (next != destination[idx + 3]) {
                    destination[idx + 0] = next;
                    destination[idx + 1] = next;
                    destination[idx + 2] = next;
                    destination[idx + 3] = next;
                    changed = true;
                }
            }
            if (changed) {
                tile.markDirty();
            }
        }
    } else {
        std::vector<TileKey> emptyTiles;
        for (auto& [key, tile] : m_mask.tiles()) {
            const auto incomingIt = maskTiles.find(key);
            if (incomingIt == maskTiles.end()) {
                continue;
            }
            const MaskTileView incoming = viewMaskTile(incomingIt->second);
            if (!incoming.valid) {
                continue;
            }

            uint8_t* destination = tile.pixels();
            bool changed = false;
            for (uint32_t idx = 0; idx < TILE_BYTE_SIZE; idx += TILE_CHANNELS) {
                const uint8_t current = destination[idx + 3];
                const uint8_t amount = incoming.alphaAt(idx);
                const uint8_t next = amount >= current ? 0 : static_cast<uint8_t>(current - amount);
                if (next != current) {
                    destination[idx + 0] = next;
                    destination[idx + 1] = next;
                    destination[idx + 2] = next;
                    destination[idx + 3] = next;
                    changed = true;
                }
            }
            if (changed) {
                tile.markDirty();
            }
            if (tile.isEmpty()) {
                emptyTiles.push_back(key);
            }
        }
        for (const TileKey& key : emptyTiles) {
            m_mask.removeTile(key);
        }
    }

    if (m_mask.empty()) {
        m_regions.clear();
        clearEdges();
        setMaskHasSoftAlpha(false);
        return;
    }

    // Raster selections cannot be faithfully represented as polygons. Keep one
    // full-document marker so the existing hasSelection()/undo plumbing remains
    // authoritative while pixels and edges come from the mask itself.
    m_regions.clear();
    m_regions.push_back(
        { { Vector2(0.0f, 0.0f), Vector2(static_cast<float>(canvasWidth), 0.0f),
              Vector2(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight)),
              Vector2(0.0f, static_cast<float>(canvasHeight)) },
            LassoSelectionMode::Add });
    markMaskSoftAlphaUnknown();
    rebuildEdges(canvasWidth, canvasHeight);
}

void LassoSelectionManager::invalidateMaskSnapshotCache() const noexcept
{
    m_cachedMaskSnapshot.reset();
}

void LassoSelectionManager::addRegion(const std::vector<Vector2>& polygon, LassoSelectionMode mode)
{
    if (polygon.size() < 3)
        return;
    if (mode == LassoSelectionMode::Replace) {
        m_regions.clear();
        clearEdges();
        mode = LassoSelectionMode::Add;
    }
    m_regions.push_back({ polygon, mode });
}

void LassoSelectionManager::rebuildEdgesFromMask(uint32_t canvasWidth, uint32_t canvasHeight)
{
    rebuildEdges(canvasWidth, canvasHeight);
}

bool LassoSelectionManager::maskHasSoftAlpha() const
{
    if (m_maskSoftAlphaKnown) {
        return m_maskHasSoftAlpha;
    }

    bool hasSoftAlpha = false;
    constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
    for (const auto& [key, tile] : m_mask.tiles()) {
        (void) key;
        const uint8_t* px = tile.pixels();
        for (uint32_t i = 0; i < pixelCount; ++i) {
            const uint8_t alpha = px[i * TILE_CHANNELS + 3];
            if (alpha > 0 && alpha < 255) {
                hasSoftAlpha = true;
                break;
            }
        }
        if (hasSoftAlpha) {
            break;
        }
    }

    setMaskHasSoftAlpha(hasSoftAlpha);
    return hasSoftAlpha;
}

void LassoSelectionManager::setMaskHasSoftAlpha(bool hasSoftAlpha) const
{
    m_maskSoftAlphaKnown = true;
    m_maskHasSoftAlpha = hasSoftAlpha;
}

void LassoSelectionManager::markMaskSoftAlphaUnknown() const
{
    m_maskSoftAlphaKnown = false;
}

void LassoSelectionManager::setRegionsOnly(const std::vector<LassoRegion>& regions)
{
    m_regions = regions;
}

void LassoSelectionManager::applyState(
    const std::vector<LassoRegion>& regions, uint32_t canvasWidth, uint32_t canvasHeight)
{
    clear();

    for (const LassoRegion& r : regions) {
        if (r.polygon.size() < 3)
            continue;
        LassoSelectionMode mode = r.mode;
        if (mode == LassoSelectionMode::Replace) {
            mode = LassoSelectionMode::Add;
        }
        if (mode == LassoSelectionMode::Subtract && m_regions.empty())
            continue;
        applySelection(r.polygon, mode, canvasWidth, canvasHeight, 255);
    }
}

void LassoSelectionManager::applySelection(const std::vector<Vector2>& polygon,
    LassoSelectionMode mode, uint32_t canvasWidth, uint32_t canvasHeight, uint8_t strength)
{
    if (polygon.size() < 3)
        return;

    // Mask pixel data will change below; any cached undo snapshot is now stale.
    invalidateMaskSnapshotCache();

    const bool canPreserveSoftAlphaState = (mode != LassoSelectionMode::Replace);
    const bool previousSoftAlpha = canPreserveSoftAlphaState ? maskHasSoftAlpha() : false;

    if (mode == LassoSelectionMode::Replace) {
        m_regions.clear();
        m_mask.clear();
        clearEdges();
        setMaskHasSoftAlpha(false);
        mode = LassoSelectionMode::Add;
    }

    if (mode == LassoSelectionMode::Subtract && m_regions.empty()) {
        return;
    }

    m_regions.push_back({ polygon, mode });

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

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    int32_t x0 = static_cast<int32_t>(std::floor(minX));
    int32_t y0 = static_cast<int32_t>(std::floor(minY));
    int32_t x1 = static_cast<int32_t>(std::ceil(maxX));
    int32_t y1 = static_cast<int32_t>(std::ceil(maxY));
    if (clipToCanvas) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(static_cast<int32_t>(canvasWidth) - 1, x1);
        y1 = std::min(static_cast<int32_t>(canvasHeight) - 1, y1);
    }

    if (x1 < x0 || y1 < y0)
        return;

    constexpr int32_t TS = static_cast<int32_t>(TILE_SIZE);
    const size_t count = polygon.size();

    // Tile resolution is hoisted out of the pixel loop. One band of TILE_SIZE
    // scanlines maps to a single row of tiles, so each tile is looked up once
    // per band instead of once per pixel — the difference between ~36M hash
    // lookups and ~24 for a full-canvas 6000x6000 selection.
    const int32_t bandMinTX = floorDiv(x0, TS);
    const size_t bandWidth = static_cast<size_t>(floorDiv(x1, TS) - bandMinTX + 1);
    std::vector<TileData*> bandTiles(bandWidth, nullptr);
    std::vector<uint8_t> bandProbed(bandWidth, 0);
    int32_t bandTY = 0;
    bool bandValid = false;

    // Fills one horizontal run [xa, xb] on scanline y, one tile-clipped segment
    // at a time. Each segment is contiguous in the tile buffer, so the common
    // opaque-add case collapses to a memset.
    auto fillRun = [&](int32_t y, int32_t xa, int32_t xb) {
        const int32_t ty = floorDiv(y, TS);
        if (!bandValid || ty != bandTY) {
            bandTY = ty;
            bandValid = true;
            std::fill(bandTiles.begin(), bandTiles.end(), nullptr);
            std::fill(bandProbed.begin(), bandProbed.end(), uint8_t { 0 });
        }
        const uint32_t localY = floorMod(y, TS);

        for (int32_t x = xa; x <= xb;) {
            const int32_t tx = floorDiv(x, TS);
            const int32_t tileBaseX = tx * TS;
            const int32_t segmentEnd = std::min(xb, tileBaseX + TS - 1);
            const size_t slot = static_cast<size_t>(tx - bandMinTX);

            if (!bandProbed[slot]) {
                bandProbed[slot] = 1;
                // Subtracting from a tile that does not exist is a no-op, so
                // unlike Add it must not allocate one.
                bandTiles[slot] = (mode == LassoSelectionMode::Subtract)
                    ? m_mask.getTile(TileKey { tx, ty })
                    : &m_mask.getOrCreateTile(TileKey { tx, ty });
            }

            TileData* tile = bandTiles[slot];
            if (tile) {
                const uint32_t localX = static_cast<uint32_t>(x - tileBaseX);
                const size_t length = static_cast<size_t>(segmentEnd - x + 1);
                uint8_t* run = tile->pixels() + (localY * TILE_SIZE + localX) * TILE_CHANNELS;

                if (mode == LassoSelectionMode::Add && strength == 255) {
                    std::memset(run, 255, length * TILE_CHANNELS);
                } else if (mode == LassoSelectionMode::Add) {
                    for (size_t i = 0; i < length; ++i) {
                        uint8_t* px = run + i * TILE_CHANNELS;
                        const uint8_t next = std::max(px[3], strength);
                        px[0] = px[1] = px[2] = px[3] = next;
                    }
                } else {
                    for (size_t i = 0; i < length; ++i) {
                        uint8_t* px = run + i * TILE_CHANNELS;
                        const uint8_t next
                            = (strength >= px[3]) ? 0 : static_cast<uint8_t>(px[3] - strength);
                        px[0] = px[1] = px[2] = px[3] = next;
                    }
                }
                tile->markDirty();
            }

            x = segmentEnd + 1;
        }
    };

    std::vector<float> intersections;
    intersections.reserve(count);
    for (int32_t y = y0; y <= y1; ++y) {
        float scanY = static_cast<float>(y) + 0.5f;
        intersections.clear();

        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            const Vector2& a = polygon[j];
            const Vector2& b = polygon[i];
            if ((a.y <= scanY) == (b.y <= scanY))
                continue;
            float t = (scanY - a.y) / (b.y - a.y);
            float ix = a.x + t * (b.x - a.x);
            intersections.push_back(ix);
        }

        if (intersections.size() < 2)
            continue;
        std::sort(intersections.begin(), intersections.end());

        if (intersections.size() % 2 != 0) {
            intersections.pop_back();
        }

        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            int32_t xa = std::max(static_cast<int32_t>(std::ceil(intersections[k] - 0.5f)), x0);
            int32_t xb
                = std::min(static_cast<int32_t>(std::floor(intersections[k + 1] - 0.5f)), x1);
            if (xb < xa)
                continue;

            fillRun(y, xa, xb);
        }
    }

    rebuildEdges(canvasWidth, canvasHeight);
    setMaskHasSoftAlpha(previousSoftAlpha || (strength > 0 && strength < 255));
}

void LassoSelectionManager::rebuildEdges(uint32_t canvasWidth, uint32_t canvasHeight)
{
    m_edges.clear();
    ++m_edgesRevision;
    if (m_mask.empty())
        return;
    constexpr uint32_t TS = TILE_SIZE;
    (void) canvasWidth;
    (void) canvasHeight;

    // ---- Step 1: Collect raw unit-length edges from tile coverage bitmaps ----
    // HEdge: horizontal from (a, b) to (a+1, b)
    // VEdge: vertical   from (a, b) to (a, b+1)
    // A pixel contributes an edge on each side whose neighbour is uncovered, so
    // every edge falls out of `coverage & ~neighbourCoverage`.
    struct RawEdge {
        int32_t a, b;
    };
    std::vector<RawEdge> hRaw;
    std::vector<RawEdge> vRaw;

    std::vector<RowBits> rows(TS);

    for (const auto& [key, tile] : m_mask.tiles()) {
        const int32_t baseX = key.x * static_cast<int32_t>(TS);
        const int32_t baseY = key.y * static_cast<int32_t>(TS);
        const uint8_t* px = tile.pixels();

        // Cache neighbor tile pointers (avoids a hash lookup per boundary pixel)
        const TileData* tL = m_mask.getTile({ key.x - 1, key.y });
        const TileData* tR = m_mask.getTile({ key.x + 1, key.y });
        const TileData* tU = m_mask.getTile({ key.x, key.y - 1 });
        const TileData* tD = m_mask.getTile({ key.x, key.y + 1 });
        const uint8_t* pxL = tL ? tL->pixels() : nullptr;
        const uint8_t* pxR = tR ? tR->pixels() : nullptr;

        for (uint32_t ly = 0; ly < TS; ++ly) {
            rows[ly] = rowCoverage(px, ly);
        }
        // Coverage just outside the tile's top and bottom seams.
        const RowBits above = rowCoverage(tU ? tU->pixels() : nullptr, TS - 1);
        const RowBits below = rowCoverage(tD ? tD->pixels() : nullptr, 0);

        for (uint32_t ly = 0; ly < TS; ++ly) {
            const RowBits& cur = rows[ly];
            if (!cur.any())
                continue;

            const int32_t y = baseY + static_cast<int32_t>(ly);
            const RowBits& up = (ly > 0) ? rows[ly - 1] : above;
            const RowBits& down = (ly + 1 < TS) ? rows[ly + 1] : below;

            // Left / right neighbours, carrying in the adjacent tile's seam column.
            const RowBits left = shiftedFromLeft(cur, pixelCovered(pxL, TS - 1, ly));
            const RowBits right = shiftedFromRight(cur, pixelCovered(pxR, 0, ly));

            forEachUncovered(cur, up,
                [&](uint32_t lx) { hRaw.push_back({ baseX + static_cast<int32_t>(lx), y }); });
            forEachUncovered(cur, down,
                [&](uint32_t lx) { hRaw.push_back({ baseX + static_cast<int32_t>(lx), y + 1 }); });
            forEachUncovered(cur, left,
                [&](uint32_t lx) { vRaw.push_back({ baseX + static_cast<int32_t>(lx), y }); });
            forEachUncovered(cur, right,
                [&](uint32_t lx) { vRaw.push_back({ baseX + static_cast<int32_t>(lx) + 1, y }); });
        }
    }

    // ---- Step 2: Sort, deduplicate, and merge collinear edges ----

    auto dedup = [](std::vector<RawEdge>& v) {
        size_t out = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0 && v[i].a == v[i - 1].a && v[i].b == v[i - 1].b)
                continue;
            v[out++] = v[i];
        }
        v.resize(out);
    };

    // Horizontal edges: sort by (y, x), deduplicate, merge consecutive x at same y
    std::sort(hRaw.begin(), hRaw.end(),
        [](const RawEdge& a, const RawEdge& b) { return a.b < b.b || (a.b == b.b && a.a < b.a); });
    dedup(hRaw);

    m_edges.reserve(hRaw.size() / 4 + vRaw.size() / 4);

    for (size_t i = 0; i < hRaw.size();) {
        const int32_t row = hRaw[i].b;
        int32_t startX = hRaw[i].a;
        int32_t endX = startX + 1;
        size_t j = i + 1;
        while (j < hRaw.size() && hRaw[j].b == row && hRaw[j].a == endX) {
            ++endX;
            ++j;
        }
        m_edges.push_back({ { static_cast<float>(startX), static_cast<float>(row) },
            { static_cast<float>(endX), static_cast<float>(row) } });
        i = j;
    }

    // Vertical edges: sort by (x, y), deduplicate, merge consecutive y at same x
    std::sort(vRaw.begin(), vRaw.end(),
        [](const RawEdge& a, const RawEdge& b) { return a.a < b.a || (a.a == b.a && a.b < b.b); });
    dedup(vRaw);

    for (size_t i = 0; i < vRaw.size();) {
        const int32_t col = vRaw[i].a;
        int32_t startY = vRaw[i].b;
        int32_t endY = startY + 1;
        size_t j = i + 1;
        while (j < vRaw.size() && vRaw[j].a == col && vRaw[j].b == endY) {
            ++endY;
            ++j;
        }
        m_edges.push_back({ { static_cast<float>(col), static_cast<float>(startY) },
            { static_cast<float>(col), static_cast<float>(endY) } });
        i = j;
    }
}

} // namespace aether
