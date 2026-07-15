// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/CompositeLayerKeys.h"

#include "features/effects/EffectCoverageResolver.h"
#include "features/canvas/rendering/GLCompositor.h"
#include "features/canvas/rendering/RetainedRenderPayload.h"
#include "features/canvas/scene/CanvasDisplayTransforms.h"
#include "features/canvas/scene/Viewport.h"
#include "shared/tiles/TileGrid.h"

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

void addTransformBoundsTileKeys(
    const TransformState* transform, std::unordered_set<TileKey, TileKeyHash>& outKeys)
{
    if (!transform) {
        return;
    }

    aether::Rect bounds = transform->transformedAABB();
    constexpr float kTransformMargin = 2.0f;
    bounds.x -= kTransformMargin;
    bounds.y -= kTransformMargin;
    bounds.width += kTransformMargin * 2.0f;
    bounds.height += kTransformMargin * 2.0f;
    const int32_t minTX = static_cast<int32_t>(std::floor(bounds.left() / aether::TILE_SIZE));
    const int32_t minTY = static_cast<int32_t>(std::floor(bounds.top() / aether::TILE_SIZE));
    const int32_t maxTX = static_cast<int32_t>(std::floor(bounds.right() / aether::TILE_SIZE));
    const int32_t maxTY = static_cast<int32_t>(std::floor(bounds.bottom() / aether::TILE_SIZE));
    for (int32_t ty = minTY; ty <= maxTY; ++ty) {
        for (int32_t tx = minTX; tx <= maxTX; ++tx) {
            outKeys.insert(aether::TileKey { tx, ty });
        }
    }
}

void insertExpandedLayerKeys(const CompositeLayerInfo& layer,
    std::unordered_set<TileKey, TileKeyHash>& layerKeys,
    std::unordered_set<TileKey, TileKeyHash>& outKeys)
{
    const auto expanded = ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(
        layerKeys, layer.effects);
    outKeys.insert(expanded.begin(), expanded.end());
}

// Collect composite keys for one stack level (bottom-to-top), adding each layer's
// effect-expanded coverage to `outKeys`. Returns the RAW (un-expanded) content
// keys this level contributes, so a caller can feed them into an adjustment's
// expansion. An adjustment layer owns no pixels: it expands the content
// accumulated BELOW it (so a bounds-expanding effect like blur reaches the
// otherwise-empty tiles its bleed touches). Groups are isolated, so an adjustment
// inside a group expands only that group's lower children.
std::unordered_set<TileKey, TileKeyHash> collectLevelKeys(
    const std::vector<CompositeLayerInfo>& layers,
    std::unordered_set<TileKey, TileKeyHash>& outKeys)
{
    std::unordered_set<TileKey, TileKeyHash> belowRawKeys;
    for (const auto& layer : layers) {
        if (!layer.visible) {
            continue;
        }
        if (layer.isAdjustment) {
            // Expand the content below by the adjustment's effects (no own tiles).
            insertExpandedLayerKeys(layer, belowRawKeys, outKeys);
            continue;
        }

        std::unordered_set<TileKey, TileKeyHash> layerKeys;
        if (layer.isGroup) {
            layerKeys = collectLevelKeys(layer.children, outKeys);
            insertExpandedLayerKeys(layer, layerKeys, outKeys);
        } else if (layer.transform) {
            addTransformBoundsTileKeys(layer.transform, layerKeys);
            insertExpandedLayerKeys(layer, layerKeys, outKeys);
        } else {
            if (layer.tileGrid) {
                for (const auto& [key, tile] : layer.tileGrid->tiles()) {
                    Q_UNUSED(tile);
                    layerKeys.insert(key);
                }
            }
            addRetainedPayloadTileKeys(layer.retainedPayload, layerKeys);
            insertExpandedLayerKeys(layer, layerKeys, outKeys);
        }
        belowRawKeys.insert(layerKeys.begin(), layerKeys.end());
    }
    return belowRawKeys;
}

} // namespace

void addRetainedPayloadTileKeys(const aether::RetainedRenderPayload* payload,
    std::unordered_set<aether::TileKey, aether::TileKeyHash>& outKeys)
{
    if (!payload || payload->empty()) {
        return;
    }

    const auto keys = aether::retainedCoverageTileKeys(payload->worldBounds);
    outKeys.insert(keys.begin(), keys.end());
}

void collectVisibleCompositeLayerKeys(const std::vector<aether::CompositeLayerInfo>& layers,
    std::unordered_set<aether::TileKey, aether::TileKeyHash>& outKeys)
{
    collectLevelKeys(layers, outKeys);
}

VisibleTileKeyBounds visibleTileKeyBounds(
    const aether::Viewport& viewport, float canvasWidth, float canvasHeight, bool flipH, bool flipV)
{
    const auto& camera = viewport.camera();
    const aether::Vector2 viewportSize = viewport.size();
    const aether::Vector2 p0
        = aether::mirrorWorldInCanvas(camera.screenToWorld({ 0.0f, 0.0f }, viewportSize),
            canvasWidth, canvasHeight, flipH, flipV);
    const aether::Vector2 p1
        = aether::mirrorWorldInCanvas(camera.screenToWorld({ viewportSize.x, 0.0f }, viewportSize),
            canvasWidth, canvasHeight, flipH, flipV);
    const aether::Vector2 p2
        = aether::mirrorWorldInCanvas(camera.screenToWorld({ 0.0f, viewportSize.y }, viewportSize),
            canvasWidth, canvasHeight, flipH, flipV);
    const aether::Vector2 p3 = aether::mirrorWorldInCanvas(
        camera.screenToWorld({ viewportSize.x, viewportSize.y }, viewportSize), canvasWidth,
        canvasHeight, flipH, flipV);

    const float minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    const float minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    const float maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    const float maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));

    return { aether::worldToTile(minX, minY), aether::worldToTile(maxX, maxY) };
}

bool isTileKeyVisible(const aether::TileKey& key, const VisibleTileKeyBounds& bounds)
{
    return key.x >= bounds.minKey.x && key.x <= bounds.maxKey.x && key.y >= bounds.minKey.y
        && key.y <= bounds.maxKey.y;
}

void collectCompositeLayerKeys(const std::vector<CompositeLayerInfo>& layers,
    std::unordered_set<TileKey, TileKeyHash>& outKeys)
{
    collectLevelKeys(layers, outKeys);
}

void collectCompositeLayerIds(const std::vector<CompositeLayerInfo>& layers, QSet<QUuid>& outIds)
{
    for (const auto& layer : layers) {
        if (!layer.id.isNull()) {
            outIds.insert(layer.id);
        }
        if (layer.isGroup) {
            collectCompositeLayerIds(layer.children, outIds);
        }
    }
}

} // namespace aether
