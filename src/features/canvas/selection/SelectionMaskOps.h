// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_SELECTION_MASK_OPS_H
#define AETHER_SELECTION_MASK_OPS_H

#include <cstdint>
#include <QList>
#include <QUuid>

namespace aether {
class TileGrid;
}

namespace ruwa::core::layers {
struct LayerData;
}

namespace aether {

bool isLayerCanvasEditable(const ruwa::core::layers::LayerData* layer);
/// Expand selected groups, deduplicate descendants and skip uneditable branches.
/// Includes empty raster layers and layers that need rasterization before editing.
/// editMasks preserves Fill's support for focused masks on non-raster layers.
QList<QUuid> selectionContentEditTargets(
    const QList<ruwa::core::layers::LayerData*>& selectedLayers, bool editMasks = false);
void binarizeSelectionMask(TileGrid& grid);
void clampSelectionMaskToCanvas(TileGrid& grid, uint32_t canvasWidth, uint32_t canvasHeight);

} // namespace aether

#endif // AETHER_SELECTION_MASK_OPS_H
