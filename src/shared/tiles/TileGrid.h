// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   G R I D
// ==========================================================================

#ifndef RUWA_CORE_TILES_TILEGRID_H
#define RUWA_CORE_TILES_TILEGRID_H

#include "TileTypes.h"
#include "TileData.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <limits>
#include <algorithm>
#include <cmath>

namespace aether {

// ==========================================================================
//   T I L E   G R I D
// ==========================================================================

class TileGrid {
public:
    explicit TileGrid(uint32_t tileSize = TILE_SIZE)
        : m_tileSize(tileSize)
    {
    }
    ~TileGrid() = default;

    TileGrid(const TileGrid&) = delete;
    TileGrid& operator=(const TileGrid&) = delete;
    TileGrid(TileGrid&&) = default;
    TileGrid& operator=(TileGrid&&) = default;

    // ---- Tile access ----

    /// Get existing tile (nullptr if not allocated)
    TileData* getTile(const TileKey& key)
    {
        auto it = m_tiles.find(key);
        return it != m_tiles.end() ? &it->second : nullptr;
    }

    const TileData* getTile(const TileKey& key) const
    {
        auto it = m_tiles.find(key);
        return it != m_tiles.end() ? &it->second : nullptr;
    }

    /// Get or create tile at key.
    /// A freshly created tile inherits the grid's default fill so that "touching"
    /// a position on a non-transparent-background grid (e.g. a hide-all mask)
    /// starts from that background instead of transparent — the untouched pixels
    /// of a partially painted tile then stay at the background reveal. When the
    /// default fill is transparent (every normal grid) the new tile is left empty
    /// exactly as before, so existing behavior is unchanged.
    TileData& getOrCreateTile(const TileKey& key)
    {
        auto [it, inserted] = m_tiles.try_emplace(key);
        if (inserted) {
            // Stamp the grid's format before any pixel storage is allocated so
            // the tile sizes its buffer / GPU texture for this grid's format.
            it->second.setFormat(m_format);
            if (m_defaultFill != 0u) {
                it->second.setSolidPacked(m_defaultFill);
            }
            m_dirtyTiles.insert(key);
            ++m_contentVersion;
        }
        return it->second;
    }

    /// Remove tile (free memory)
    void removeTile(const TileKey& key)
    {
        if (m_tiles.erase(key) > 0) {
            ++m_contentVersion;
        }
        m_dirtyTiles.erase(key);
    }

    /// Check if tile exists
    bool hasTile(const TileKey& key) const { return m_tiles.count(key) > 0; }

    // ---- Dirty tracking ----

    void markDirty(const TileKey& key)
    {
        if (m_tiles.count(key)) {
            m_tiles[key].markDirty();
            m_dirtyTiles.insert(key);
            // markDirty(key) is the documented "the pixels at key changed" signal
            // the raster edit paths (DrawCommand, stroke commit, fill) funnel
            // through; bumping here lets consumers that cache a whole-grid result
            // (the distortion whole-layer materialisation) detect content change
            // across frames without re-materialising every batch.
            ++m_contentVersion;
        }
    }

    void removeDirty(const TileKey& key) { m_dirtyTiles.erase(key); }

    /// Signal that an EXISTING tile's pixels changed out-of-band — i.e. written
    /// straight into the tile's GPU texture (the live brush flatten path) without
    /// touching the CPU buffer or the tile's dirty flag, so markDirty(key) must
    /// NOT be used (it would re-upload the still-stale CPU pixels over the fresh
    /// GPU texture). This bumps only the whole-grid content counter so cross-frame
    /// consumers that cache a whole-grid derivation (the distortion whole-layer
    /// materialisation) detect the change and rebuild. See contentVersion().
    void notePixelsChangedOutOfBand() { ++m_contentVersion; }

    const std::unordered_set<TileKey, TileKeyHash>& dirtyTiles() const { return m_dirtyTiles; }

    void clearDirtySet() { m_dirtyTiles.clear(); }

    // ---- Iteration ----

    using TileMap = std::unordered_map<TileKey, TileData, TileKeyHash>;

    TileMap& tiles() { return m_tiles; }
    const TileMap& tiles() const { return m_tiles; }

    size_t tileCount() const { return m_tiles.size(); }
    bool empty() const { return m_tiles.empty(); }

    // ---- Bounds ----

    /// Compute bounding box of all existing tiles in world coordinates
    /// Returns false if grid is empty
    bool computeBounds(float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) const
    {
        if (m_tiles.empty())
            return false;

        int32_t minTX = std::numeric_limits<int32_t>::max();
        int32_t minTY = std::numeric_limits<int32_t>::max();
        int32_t maxTX = std::numeric_limits<int32_t>::min();
        int32_t maxTY = std::numeric_limits<int32_t>::min();

        for (const auto& [key, tile] : m_tiles) {
            minTX = std::min(minTX, key.x);
            minTY = std::min(minTY, key.y);
            maxTX = std::max(maxTX, key.x);
            maxTY = std::max(maxTY, key.y);
        }

        const float ts = static_cast<float>(m_tileSize);
        outMinX = static_cast<float>(minTX) * ts;
        outMinY = static_cast<float>(minTY) * ts;
        outMaxX = static_cast<float>(maxTX + 1) * ts;
        outMaxY = static_cast<float>(maxTY + 1) * ts;
        return true;
    }

    // ---- Visible tiles ----

    /// Collect keys of tiles visible in the given world-space AABB
    std::vector<TileKey> visibleTiles(
        float worldMinX, float worldMinY, float worldMaxX, float worldMaxY) const
    {
        const float ts = static_cast<float>(m_tileSize);
        int32_t tMinX = static_cast<int32_t>(std::floor(worldMinX / ts));
        int32_t tMinY = static_cast<int32_t>(std::floor(worldMinY / ts));
        int32_t tMaxX = static_cast<int32_t>(std::floor(worldMaxX / ts));
        int32_t tMaxY = static_cast<int32_t>(std::floor(worldMaxY / ts));

        std::vector<TileKey> result;
        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                TileKey key { tx, ty };
                if (m_tiles.count(key)) {
                    result.push_back(key);
                }
            }
        }
        return result;
    }

    // ---- Cleanup ----

    /// Remove all empty (fully transparent) tiles
    void pruneEmpty()
    {
        for (auto it = m_tiles.begin(); it != m_tiles.end();) {
            if (it->second.isEmpty()) {
                m_dirtyTiles.erase(it->first);
                it = m_tiles.erase(it);
                ++m_contentVersion;
            } else {
                ++it;
            }
        }
    }

    void clear()
    {
        if (!m_tiles.empty()) {
            ++m_contentVersion;
        }
        m_tiles.clear();
        m_dirtyTiles.clear();
    }

    uint32_t tileSize() const { return m_tileSize; }

    /// Monotonic counter bumped whenever the grid's tile set or a tile's pixels
    /// change through the standard mutation entry points (getOrCreateTile,
    /// removeTile, markDirty, clear, pruneEmpty). Consumers that cache a
    /// whole-grid derivation across frames pair it with tileCount() to detect
    /// staleness cheaply. NOT bumped by out-of-band writes that touch
    /// TileData::pixels() without calling markDirty(key) — the raster edit paths
    /// all do, but a new bespoke writer must call markDirty to stay visible here.
    uint64_t contentVersion() const { return m_contentVersion; }

    // ---- Pixel format ----
    //
    //   The per-pixel storage format stamped onto tiles created by this grid.
    //   Defaults to the document-wide kDefaultTileFormat. setFormat() affects
    //   only tiles created afterward (it does not convert existing tiles).

    TilePixelFormat format() const { return m_format; }
    void setFormat(TilePixelFormat fmt) { m_format = fmt; }

    // ---- Default fill (implicit value of every absent tile) ----
    //
    //   Packed premultiplied RGBA (r | g<<8 | b<<16 | a<<24) representing the
    //   uniform value of every tile position not present in m_tiles. The default
    //   (0 = fully transparent) preserves the historical "absent = nothing"
    //   behavior for pixel grids. Layer masks set this to encode an infinite
    //   uniform background (e.g. opaque black = hide-all) without allocating
    //   any tiles for it. Consumers that understand it (the compositor, mask
    //   thumbnail) use it; everything else is unaffected because the default
    //   keeps the legacy transparent behavior.

    void setDefaultFill(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        m_defaultFill = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
            | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }
    void setDefaultFillPacked(uint32_t packed) { m_defaultFill = packed; }
    uint32_t defaultFillPacked() const { return m_defaultFill; }
    bool hasDefaultFill() const { return m_defaultFill != 0u; }
    void defaultFill(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const
    {
        r = static_cast<uint8_t>(m_defaultFill & 0xFFu);
        g = static_cast<uint8_t>((m_defaultFill >> 8) & 0xFFu);
        b = static_cast<uint8_t>((m_defaultFill >> 16) & 0xFFu);
        a = static_cast<uint8_t>((m_defaultFill >> 24) & 0xFFu);
    }

private:
    uint32_t m_tileSize;
    TileMap m_tiles;
    std::unordered_set<TileKey, TileKeyHash> m_dirtyTiles;
    uint32_t m_defaultFill = 0; // Packed premult RGBA; 0 = transparent (legacy)
    TilePixelFormat m_format = kDefaultTileFormat; // Format stamped on new tiles
    uint64_t m_contentVersion = 0; // Bumped on any tile-set / pixel mutation
};

} // namespace aether

#endif // RUWA_CORE_TILES_TILEGRID_H
