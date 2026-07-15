// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_COMPOSITE_LAYER_KEYS_H
#define AETHER_COMPOSITE_LAYER_KEYS_H

#include "shared/tiles/TileTypes.h"

#include <QSet>
#include <QUuid>

#include <unordered_set>
#include <vector>

namespace aether {

class Viewport;
struct CompositeLayerInfo;
struct RetainedRenderPayload;

struct VisibleTileKeyBounds {
    TileKey minKey {};
    TileKey maxKey {};
};

void addRetainedPayloadTileKeys(
    const RetainedRenderPayload* payload, std::unordered_set<TileKey, TileKeyHash>& outKeys);
void collectVisibleCompositeLayerKeys(const std::vector<CompositeLayerInfo>& layers,
    std::unordered_set<TileKey, TileKeyHash>& outKeys);
VisibleTileKeyBounds visibleTileKeyBounds(
    const Viewport& viewport, float canvasWidth, float canvasHeight, bool flipH, bool flipV);
bool isTileKeyVisible(const TileKey& key, const VisibleTileKeyBounds& bounds);
void collectCompositeLayerKeys(const std::vector<CompositeLayerInfo>& layers,
    std::unordered_set<TileKey, TileKeyHash>& outKeys);
void collectCompositeLayerIds(const std::vector<CompositeLayerInfo>& layers, QSet<QUuid>& outIds);

} // namespace aether

#endif // AETHER_COMPOSITE_LAYER_KEYS_H
