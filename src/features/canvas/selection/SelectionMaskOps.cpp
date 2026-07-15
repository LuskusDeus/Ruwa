// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/selection/SelectionMaskOps.h"

#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileGrid.h"

#include <vector>

namespace aether {

bool isLayerCanvasEditable(const ruwa::core::layers::LayerData* layer)
{
    return layer && layer->visible && !layer->locked;
}

void binarizeSelectionMask(aether::TileGrid& grid)
{
    for (auto& [key, tile] : grid.tiles()) {
        uint8_t* px = tile.pixels();
        constexpr uint32_t pixelCount = aether::TILE_SIZE * aether::TILE_SIZE;
        for (uint32_t i = 0; i < pixelCount; ++i) {
            const uint32_t idx = i * aether::TILE_CHANNELS;
            const uint8_t a = px[idx + 3];
            const uint8_t v = (a > 127) ? 255 : 0;
            px[idx + 0] = px[idx + 1] = px[idx + 2] = px[idx + 3] = v;
        }
    }
}

void clampSelectionMaskToCanvas(aether::TileGrid& grid, uint32_t canvasWidth, uint32_t canvasHeight)
{
    if (grid.empty() || canvasWidth == 0 || canvasHeight == 0)
        return;

    std::vector<aether::TileKey> toRemove;
    toRemove.reserve(grid.tileCount());

    for (auto& [key, tile] : grid.tiles()) {
        int32_t tileX = key.x * static_cast<int32_t>(aether::TILE_SIZE);
        int32_t tileY = key.y * static_cast<int32_t>(aether::TILE_SIZE);
        int32_t tileMaxX = tileX + static_cast<int32_t>(aether::TILE_SIZE);
        int32_t tileMaxY = tileY + static_cast<int32_t>(aether::TILE_SIZE);

        if (tileMaxX <= 0 || tileMaxY <= 0 || tileX >= static_cast<int32_t>(canvasWidth)
            || tileY >= static_cast<int32_t>(canvasHeight)) {
            toRemove.push_back(key);
            continue;
        }

        if (tileX < 0 || tileY < 0 || tileMaxX > static_cast<int32_t>(canvasWidth)
            || tileMaxY > static_cast<int32_t>(canvasHeight)) {
            uint8_t* pixels = tile.pixels();
            for (uint32_t ly = 0; ly < aether::TILE_SIZE; ++ly) {
                int32_t worldY = tileY + static_cast<int32_t>(ly);
                bool yOut = (worldY < 0 || worldY >= static_cast<int32_t>(canvasHeight));
                for (uint32_t lx = 0; lx < aether::TILE_SIZE; ++lx) {
                    int32_t worldX = tileX + static_cast<int32_t>(lx);
                    bool xOut = (worldX < 0 || worldX >= static_cast<int32_t>(canvasWidth));
                    if (xOut || yOut) {
                        uint32_t idx = (ly * aether::TILE_SIZE + lx) * aether::TILE_CHANNELS;
                        pixels[idx + 0] = 0;
                        pixels[idx + 1] = 0;
                        pixels[idx + 2] = 0;
                        pixels[idx + 3] = 0;
                    }
                }
            }
        }

        if (tile.isEmpty()) {
            toRemove.push_back(key);
        }
    }

    for (const auto& key : toRemove) {
        grid.removeTile(key);
    }
}

} // namespace aether
