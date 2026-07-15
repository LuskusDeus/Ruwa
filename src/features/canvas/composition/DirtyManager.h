// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   D I R T Y   M A N A G E R
// ==========================================================================
//
//   Manages dirty state propagation from layer changes to composition cache.
//
//   Dirty sources:
//   - Brush stroke on a layer     -> dirty specific tile keys
//   - Opacity/visibility change   -> dirty ALL tile keys of that layer
//   - Blend mode change           -> dirty ALL tile keys of that layer + layers above
//   - Layer reorder/add/remove    -> dirty union of all affected tile keys
//

#ifndef RUWA_CORE_COMPOSITION_DIRTYMANAGER_H
#define RUWA_CORE_COMPOSITION_DIRTYMANAGER_H

#include "CompositionCache.h"
#include "TilePositionIndex.h"

#include <QUuid>
#include <vector>

namespace aether {

class DirtyManager {
public:
    DirtyManager() = default;

    void setCache(CompositionCache* cache) { m_cache = cache; }
    void setIndex(TilePositionIndex* index) { m_index = index; }

    // --- Dirty propagation methods ---

    // Brush stroke: specific tiles on a specific layer became dirty
    void onTilesDirtied(const QUuid& layerId, const std::vector<TileKey>& keys)
    {
        if (!m_cache)
            return;
        for (const auto& key : keys) {
            m_cache->markDirty(key);
        }
    }

    void onTilesDirtied(const QUuid& layerId, const std::unordered_set<TileKey, TileKeyHash>& keys)
    {
        if (!m_cache)
            return;
        for (const auto& key : keys) {
            m_cache->markDirty(key);
        }
    }

    // Layer property changed (opacity, visibility): dirty all tiles of this layer
    void onLayerPropertyChanged(const QUuid& layerId)
    {
        if (!m_cache || !m_index)
            return;
        auto keys = m_index->tileKeysForLayer(layerId);
        for (const auto& key : keys) {
            m_cache->markDirty(key);
        }
    }

    // Blend mode changed: dirty all positions where this layer or layers above exist
    // (simplified: just dirty all positions this layer touches)
    void onBlendModeChanged(const QUuid& layerId) { onLayerPropertyChanged(layerId); }

    // Layer structure changed (add/remove/reorder): dirty all known positions
    void onStructureChanged()
    {
        if (!m_cache || !m_index)
            return;
        auto allKeys = m_index->allTileKeys();
        for (const auto& key : allKeys) {
            m_cache->markDirty(key);
        }
    }

    // Layer removed: dirty shared positions and drop cache-only positions.
    void onLayerRemoved(const QUuid& layerId)
    {
        if (!m_cache || !m_index)
            return;
        auto keys = m_index->tileKeysForLayer(layerId);
        m_index->removeLayer(layerId);

        for (const auto& key : keys) {
            if (m_index->hasAnyLayerAt(key)) {
                m_cache->markDirty(key);
            } else {
                m_cache->removeTile(key);
            }
        }
    }

private:
    CompositionCache* m_cache = nullptr;
    TilePositionIndex* m_index = nullptr;
};

} // namespace aether

#endif // RUWA_CORE_COMPOSITION_DIRTYMANAGER_H
