// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_SELECTION_MASK_OPS_H
#define AETHER_SELECTION_MASK_OPS_H

#include <cstdint>

namespace aether {
class TileGrid;
}

namespace ruwa::core::layers {
struct LayerData;
}

namespace aether {

bool isLayerCanvasEditable(const ruwa::core::layers::LayerData* layer);
void binarizeSelectionMask(TileGrid& grid);
void clampSelectionMaskToCanvas(TileGrid& grid, uint32_t canvasWidth, uint32_t canvasHeight);

} // namespace aether

#endif // AETHER_SELECTION_MASK_OPS_H
