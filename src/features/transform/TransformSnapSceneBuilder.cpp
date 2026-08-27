// SPDX-License-Identifier: MPL-2.0

#include "features/transform/TransformSnapSceneBuilder.h"

#include "features/layers/model/LayerModel.h"
#include "features/transform/TransformGeometry.h"
#include "features/transform/TransformTargetSet.h"

#include <optional>
#include <utility>

namespace aether {

SnapScene buildTransformSnapScene(const ruwa::core::layers::LayerModel* layerModel,
    const Vector2& canvasSize, bool finiteCanvas, bool includeLayers,
    const SnapLayerExclusionPredicate& excludeLayer)
{
    SnapScene scene;
    scene.canvasSize = canvasSize;
    scene.finiteCanvas = finiteCanvas;
    if (!layerModel || !includeLayers) {
        return scene;
    }

    const auto effectivelyVisible = [](const ruwa::core::layers::LayerData* layer) {
        for (auto* current = layer; current; current = current->parent) {
            if (!current->visible || current->opacity <= 0.0 || current->isBackground()) {
                return false;
            }
        }
        return layer != nullptr;
    };

    std::function<std::optional<Rect>(const ruwa::core::layers::LayerData*)> visibleBounds;
    visibleBounds = [&](const ruwa::core::layers::LayerData* layer) -> std::optional<Rect> {
        if (!effectivelyVisible(layer) || (excludeLayer && excludeLayer(layer))) {
            return std::nullopt;
        }
        Rect bounds {};
        if (const auto ownBounds = transformBoundsForLayer(layer)) {
            bounds = *ownBounds;
        }
        for (const auto& child : layer->children) {
            if (const auto childBounds = visibleBounds(child.get())) {
                bounds = unionTransformBounds(bounds, *childBounds);
            }
        }
        return bounds.width > 0.0f && bounds.height > 0.0f ? std::optional<Rect>(bounds)
                                                           : std::nullopt;
    };

    const QList<ruwa::core::layers::LayerData*> layers = layerModel->allLayersFlattened();
    scene.targets.reserve(static_cast<size_t>(layers.size()));
    for (const auto* layer : layers) {
        if (!effectivelyVisible(layer) || (excludeLayer && excludeLayer(layer))
            || (!transformIsVisualTarget(layer) && !layer->isGroup())) {
            continue;
        }
        const auto bounds
            = layer->isGroup() ? visibleBounds(layer) : transformBoundsForLayer(layer);
        if (!bounds) {
            continue;
        }
        SnapTarget target;
        target.bounds = *bounds;
        target.id = layer->id;
        target.parentId = layer->parent ? layer->parent->id : QUuid {};
        target.type = layer->isGroup()      ? SnapTargetType::Group
            : layer->isText()               ? SnapTargetType::Text
            : layer->isIsolatedPixelLayer() ? SnapTargetType::IsolatedPixel
                                            : SnapTargetType::Raster;
        scene.targets.push_back(std::move(target));
    }
    return scene;
}

} // namespace aether
