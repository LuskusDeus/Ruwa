// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_EXPORT_CONTENT_BOUNDS_CALC_H
#define AETHER_EXPORT_CONTENT_BOUNDS_CALC_H

#include <QList>

#include <memory>

namespace aether {

class LayerCompositingBuilder;
class TileGrid;

} // namespace aether

namespace ruwa::core::layers {
struct LayerData;
}

namespace aether {

bool computeTileGridContentBounds(
    const TileGrid* grid, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);
bool computeExportLayerBoundsRecursive(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const LayerCompositingBuilder* compositingBuilder, bool parentVisible, int& outMinX,
    int& outMinY, int& outMaxX, int& outMaxY);
bool computeComposerLayerBoundsRecursive(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const LayerCompositingBuilder* compositingBuilder, bool parentVisible, int& outMinX,
    int& outMinY, int& outMaxX, int& outMaxY);

} // namespace aether

#endif // AETHER_EXPORT_CONTENT_BOUNDS_CALC_H
