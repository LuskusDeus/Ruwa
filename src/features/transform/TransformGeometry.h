// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMGEOMETRY_H
#define RUWA_CORE_TRANSFORM_TRANSFORMGEOMETRY_H

#include "features/layers/model/LayerData.h"
#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"
#include "features/transform/TransformState.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace aether {

inline TransformState transformStateWithSourceBounds(
    const TransformState& storedState, const Rect& sourceBounds)
{
    TransformState state = storedState;
    if (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f) {
        state.contentBounds = sourceBounds;
        state.pivot = sourceBounds.center();
        state.reset();
        return state;
    }

    state.contentBounds = sourceBounds;
    if (!state.hasFreeQuad() && !state.hasDeformMesh()) {
        const Vector2 oldPivot = state.pivot;
        const Vector2 newPivot = sourceBounds.center();
        const float dpx = newPivot.x - oldPivot.x;
        const float dpy = newPivot.y - oldPivot.y;
        const float sdx = dpx * state.scale.x;
        const float sdy = dpy * state.scale.y;
        const float cosR = std::cos(state.rotation);
        const float sinR = std::sin(state.rotation);
        state.translation.x += (sdx * cosR - sdy * sinR) - dpx;
        state.translation.y += (sdx * sinR + sdy * cosR) - dpy;
        state.pivot = newPivot;
    }
    return state;
}

inline std::optional<Rect> transformBoundsForLayer(const ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return std::nullopt;
    }
    if (layer->isRaster()) {
        const auto* grid = layer->pixelGrid();
        if (!grid || grid->empty()) {
            return std::nullopt;
        }
        return TransformState::computeContentBounds(*grid);
    }
    if (layer->isIsolatedPixelLayer()) {
        const auto* grid = layer->smartContentGrid.get();
        if (!grid || grid->empty()) {
            return std::nullopt;
        }
        const Rect sourceBounds = TransformState::computeContentBounds(*grid);
        if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
            return std::nullopt;
        }
        return transformStateWithSourceBounds(layer->smartTransform, sourceBounds)
            .transformedAABB();
    }
    if (layer->isText() && layer->textData) {
        const Rect sourceBounds = computeTextLayoutSourceBounds(*layer->textData);
        if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
            return std::nullopt;
        }
        return transformStateWithSourceBounds(layer->textData->transform, sourceBounds)
            .transformedAABB();
    }
    return std::nullopt;
}

inline Rect unionTransformBounds(const Rect& a, const Rect& b)
{
    if (a.width <= 0.0f || a.height <= 0.0f) {
        return b;
    }
    if (b.width <= 0.0f || b.height <= 0.0f) {
        return a;
    }
    const float left = std::min(a.left(), b.left());
    const float top = std::min(a.top(), b.top());
    const float right = std::max(a.right(), b.right());
    const float bottom = std::max(a.bottom(), b.bottom());
    return { left, top, right - left, bottom - top };
}

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMGEOMETRY_H
