// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMSNAPSCENEBUILDER_H
#define RUWA_CORE_TRANSFORM_TRANSFORMSNAPSCENEBUILDER_H

#include "features/transform/TransformSnapTypes.h"

#include <functional>

namespace ruwa::core::layers {
class LayerModel;
struct LayerData;
} // namespace ruwa::core::layers

namespace aether {

using SnapLayerExclusionPredicate
    = std::function<bool(const ruwa::core::layers::LayerData*)>;

SnapScene buildTransformSnapScene(const ruwa::core::layers::LayerModel* layerModel,
    const Vector2& canvasSize, bool finiteCanvas, bool includeLayers,
    const SnapLayerExclusionPredicate& excludeLayer = {});

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMSNAPSCENEBUILDER_H
