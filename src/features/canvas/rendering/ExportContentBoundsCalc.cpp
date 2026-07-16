// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/ExportContentBoundsCalc.h"

#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileGrid.h"

#include <algorithm>

namespace aether {

bool computeTileGridContentBounds(
    const aether::TileGrid* grid, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY)
{
    if (!grid || grid->empty()) {
        return false;
    }

    bool hasAny = false;
    for (const auto& [key, tile] : grid->tiles()) {
        if (tile.isEmpty()) {
            continue;
        }

        const int tileMinX = key.x * static_cast<int>(aether::TILE_SIZE);
        const int tileMinY = key.y * static_cast<int>(aether::TILE_SIZE);
        const int tileMaxX = tileMinX + static_cast<int>(aether::TILE_SIZE) - 1;
        const int tileMaxY = tileMinY + static_cast<int>(aether::TILE_SIZE) - 1;
        if (!hasAny) {
            outMinX = tileMinX;
            outMinY = tileMinY;
            outMaxX = tileMaxX;
            outMaxY = tileMaxY;
            hasAny = true;
        } else {
            outMinX = std::min(outMinX, tileMinX);
            outMinY = std::min(outMinY, tileMinY);
            outMaxX = std::max(outMaxX, tileMaxX);
            outMaxY = std::max(outMaxY, tileMaxY);
        }
    }

    return hasAny;
}

bool computeExportLayerBoundsRecursive(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const aether::LayerCompositingBuilder* compositingBuilder, bool parentVisible, int& outMinX,
    int& outMinY, int& outMaxX, int& outMaxY)
{
    bool hasAny = false;
    auto includeBounds = [&](int minX, int minY, int maxX, int maxY) {
        if (!hasAny) {
            outMinX = minX;
            outMinY = minY;
            outMaxX = maxX;
            outMaxY = maxY;
            hasAny = true;
            return;
        }
        outMinX = std::min(outMinX, minX);
        outMinY = std::min(outMinY, minY);
        outMaxX = std::max(outMaxX, maxX);
        outMaxY = std::max(outMaxY, maxY);
    };

    for (const auto& layerPtr : layers) {
        const auto* layer = layerPtr.get();
        if (!layer) {
            continue;
        }

        const bool visible
            = parentVisible && layer->visible && layer->opacity > 0.0 && !layer->isExportExcluded();

        if (visible) {
            const aether::TileGrid* grid
                = compositingBuilder ? compositingBuilder->compositingGridForLayer(layer) : nullptr;

            int minX = 0;
            int minY = 0;
            int maxX = 0;
            int maxY = 0;
            if (computeTileGridContentBounds(grid, minX, minY, maxX, maxY)) {
                includeBounds(minX, minY, maxX, maxY);
            }
        }

        if (!layer->children.isEmpty()
            && computeExportLayerBoundsRecursive(
                layer->children, compositingBuilder, visible, outMinX, outMinY, outMaxX, outMaxY)) {
            hasAny = true;
        }
    }

    return hasAny;
}

bool computeNavigatorLayerBoundsRecursive(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const aether::LayerCompositingBuilder* compositingBuilder, bool parentVisible, int& outMinX,
    int& outMinY, int& outMaxX, int& outMaxY)
{
    bool hasAny = false;
    auto includeBounds = [&](int minX, int minY, int maxX, int maxY) {
        if (!hasAny) {
            outMinX = minX;
            outMinY = minY;
            outMaxX = maxX;
            outMaxY = maxY;
            hasAny = true;
            return;
        }
        outMinX = std::min(outMinX, minX);
        outMinY = std::min(outMinY, minY);
        outMaxX = std::max(outMaxX, maxX);
        outMaxY = std::max(outMaxY, maxY);
    };

    for (const auto& layerPtr : layers) {
        const auto* layer = layerPtr.get();
        if (!layer) {
            continue;
        }

        const bool visible = parentVisible && layer->visible && layer->opacity > 0.0;

        if (visible && !layer->isBackground()) {
            const aether::TileGrid* grid
                = compositingBuilder ? compositingBuilder->compositingGridForLayer(layer) : nullptr;

            int minX = 0;
            int minY = 0;
            int maxX = 0;
            int maxY = 0;
            if (computeTileGridContentBounds(grid, minX, minY, maxX, maxY)) {
                includeBounds(minX, minY, maxX, maxY);
            }
        }

        if (!layer->children.isEmpty()
            && computeNavigatorLayerBoundsRecursive(
                layer->children, compositingBuilder, visible, outMinX, outMinY, outMaxX, outMaxY)) {
            hasAny = true;
        }
    }

    return hasAny;
}

} // namespace aether
