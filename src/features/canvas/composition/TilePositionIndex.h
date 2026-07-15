// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   P O S I T I O N   I N D E X
// ==========================================================================
//
//   For each TileKey, stores which layers have a tile at that position.
//   Enables fast lookup for compositing and cache cleanup.
//

#ifndef RUWA_CORE_COMPOSITION_TILEPOSITIONINDEX_H
#define RUWA_CORE_COMPOSITION_TILEPOSITIONINDEX_H

#include "shared/tiles/TileTypes.h"

#include <QUuid>
#include <QHash>
#include <QSet>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether {

class TilePositionIndex {
public:
    TilePositionIndex() = default;

    // Register that a layer has a tile at the given position
    void addEntry(const TileKey& key, const QUuid& layerId)
    {
        m_positionToLayers[key].insert(layerId);
        m_layerToPositions[layerId].insert(key);
    }

    // Remove a single entry
    void removeEntry(const TileKey& key, const QUuid& layerId)
    {
        auto it = m_positionToLayers.find(key);
        if (it != m_positionToLayers.end()) {
            it->second.remove(layerId);
            if (it->second.empty()) {
                m_positionToLayers.erase(it);
            }
        }
        auto lit = m_layerToPositions.find(layerId);
        if (lit != m_layerToPositions.end()) {
            lit.value().erase(key);
            if (lit.value().empty()) {
                m_layerToPositions.erase(lit);
            }
        }
    }

    // Remove all entries for a layer
    void removeLayer(const QUuid& layerId)
    {
        auto lit = m_layerToPositions.find(layerId);
        if (lit == m_layerToPositions.end())
            return;

        for (const auto& key : lit.value()) {
            auto pit = m_positionToLayers.find(key);
            if (pit != m_positionToLayers.end()) {
                pit->second.remove(layerId);
                if (pit->second.empty()) {
                    m_positionToLayers.erase(pit);
                }
            }
        }
        m_layerToPositions.erase(lit);
    }

    // Update index for a layer from its TileGrid
    void rebuildForLayer(
        const QUuid& layerId, const std::unordered_map<TileKey, class TileData, TileKeyHash>& tiles)
    {
        removeLayer(layerId);
        for (const auto& [key, tile] : tiles) {
            addEntry(key, layerId);
        }
    }

    // Query: which layers have tiles at this position?
    QSet<QUuid> layersAt(const TileKey& key) const
    {
        auto it = m_positionToLayers.find(key);
        if (it == m_positionToLayers.end())
            return {};
        return it->second;
    }

    // Query: which tile positions does this layer occupy?
    std::unordered_set<TileKey, TileKeyHash> tileKeysForLayer(const QUuid& layerId) const
    {
        auto it = m_layerToPositions.find(layerId);
        if (it == m_layerToPositions.end())
            return {};
        return it.value();
    }

    // Get all known tile positions (union across all layers)
    std::vector<TileKey> allTileKeys() const
    {
        std::vector<TileKey> result;
        result.reserve(m_positionToLayers.size());
        for (const auto& [key, layers] : m_positionToLayers) {
            result.push_back(key);
        }
        return result;
    }

    // Check if any layer has a tile at this position
    bool hasAnyLayerAt(const TileKey& key) const { return m_positionToLayers.count(key) > 0; }

    // Check if a specific layer has a tile at this position
    bool hasLayerAt(const TileKey& key, const QUuid& layerId) const
    {
        auto it = m_positionToLayers.find(key);
        if (it == m_positionToLayers.end())
            return false;
        return it->second.contains(layerId);
    }

    void clear()
    {
        m_positionToLayers.clear();
        m_layerToPositions.clear();
    }

private:
    // TileKey -> set of layer IDs that have tiles there
    // Uses std::unordered_map with TileKeyHash (TileKey has no qHash)
    std::unordered_map<TileKey, QSet<QUuid>, TileKeyHash> m_positionToLayers;

    // LayerId -> set of tile positions
    // Uses QHash because QUuid has qHash but NOT std::hash
    QHash<QUuid, std::unordered_set<TileKey, TileKeyHash>> m_layerToPositions;
};

} // namespace aether

#endif // RUWA_CORE_COMPOSITION_TILEPOSITIONINDEX_H
