// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C O M P O S I T I O N   C A C H E
// ==========================================================================
//
//   Stores the final composited result as a TileGrid.
//   This is the ONLY source the renderer reads from.
//

#ifndef RUWA_CORE_COMPOSITION_COMPOSITIONCACHE_H
#define RUWA_CORE_COMPOSITION_COMPOSITIONCACHE_H

#include "shared/tiles/TileGrid.h"

namespace aether {

class CompositionCache {
public:
    explicit CompositionCache(uint32_t tileSize = TILE_SIZE)
        : m_grid(tileSize)
    {
    }

    // The composited tile grid — renderer draws from this
    TileGrid& grid() { return m_grid; }
    const TileGrid& grid() const { return m_grid; }

    // Mark a tile position as needing recomposite
    void markDirty(const TileKey& key) { m_dirtyPositions.insert(key); }

    // Mark ALL existing cache tiles dirty (e.g. full recomposite)
    void markAllDirty()
    {
        for (const auto& [key, tile] : m_grid.tiles()) {
            m_dirtyPositions.insert(key);
        }
    }

    // Mark a set of tile keys dirty
    void markDirty(const std::unordered_set<TileKey, TileKeyHash>& keys)
    {
        for (const auto& k : keys) {
            m_dirtyPositions.insert(k);
        }
    }

    void markDirty(const std::vector<TileKey>& keys)
    {
        for (const auto& k : keys) {
            m_dirtyPositions.insert(k);
        }
    }

    // Dirty positions that need recomposite
    const std::unordered_set<TileKey, TileKeyHash>& dirtyPositions() const
    {
        return m_dirtyPositions;
    }

    bool hasDirtyPositions() const { return !m_dirtyPositions.empty(); }

    void clearDirtyPosition(const TileKey& key) { m_dirtyPositions.erase(key); }

    void clearAllDirty() { m_dirtyPositions.clear(); }

    // Remove cache tile (when no layers have tiles at this position)
    void removeTile(const TileKey& key)
    {
        m_grid.removeTile(key);
        m_dirtyPositions.erase(key);
    }

    void clear()
    {
        m_grid.clear();
        m_dirtyPositions.clear();
    }

private:
    TileGrid m_grid;
    std::unordered_set<TileKey, TileKeyHash> m_dirtyPositions;
};

} // namespace aether

#endif // RUWA_CORE_COMPOSITION_COMPOSITIONCACHE_H
