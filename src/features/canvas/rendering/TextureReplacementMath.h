// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_TEXTUREREPLACEMENTMATH_H
#define RUWA_FEATURES_CANVAS_RENDERING_TEXTUREREPLACEMENTMATH_H

#include <algorithm>
#include <array>
#include <cstddef>

namespace aether {

using PremultipliedRgba = std::array<float, 4>;

/// CPU reference for target_layer_preview.frag.glsl texture-replacement mode.
/// Inputs and output are premultiplied; coverage selects the already-computed
/// FloodFillResult pixel without applying color/alpha semantics a second time.
inline PremultipliedRgba replacePremultipliedPixel(
    const PremultipliedRgba& base, const PremultipliedRgba& after, float coverage)
{
    const float t = std::clamp(coverage, 0.0f, 1.0f);
    if (t <= 0.0f) {
        return base;
    }
    if (t >= 1.0f) {
        return after;
    }
    PremultipliedRgba result {};
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
        result[channel] = base[channel] + (after[channel] - base[channel]) * t;
    }
    return result;
}

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_TEXTUREREPLACEMENTMATH_H
