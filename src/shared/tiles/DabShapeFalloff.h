// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   D A B   S H A P E   F A L L O F F
// ==========================================================================

#ifndef RUWA_CORE_TILES_DABSHAPEFALLOFF_H
#define RUWA_CORE_TILES_DABSHAPEFALLOFF_H

#include <algorithm>

namespace aether::dab_shape_falloff {

inline constexpr float kSoftEdgeRadiusTexels = 48.0f;

inline float softnessFromHardness(float hardness)
{
    return std::max(1.0f - std::clamp(hardness, 0.0f, 1.0f), 0.0f);
}

inline float softEdgeRadiusTexels(float hardness)
{
    return kSoftEdgeRadiusTexels * softnessFromHardness(hardness);
}

inline float softEdgeWidthNorm(int width, int height, float hardness)
{
    const float radiusTexels = softEdgeRadiusTexels(hardness);
    if (radiusTexels <= 0.0001f || width <= 0 || height <= 0) {
        return 0.0f;
    }

    const float maxSize = static_cast<float>(std::max(std::max(width, height), 1));
    return (2.0f * radiusTexels) / maxSize;
}

inline float shapePad(int width, int height, float hardness)
{
    const float radiusTexels = softEdgeRadiusTexels(hardness);
    if (radiusTexels <= 0.0001f || width <= 0 || height <= 0) {
        return 0.0f;
    }

    const float safeWidth = static_cast<float>(std::max(width - 1, 1));
    const float safeHeight = static_cast<float>(std::max(height - 1, 1));
    const float padX = (2.0f * radiusTexels) / safeWidth;
    const float padY = (2.0f * radiusTexels) / safeHeight;
    return std::max(padX, padY);
}

inline float smoothstep01(float x)
{
    const float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float edgeFeatherAlpha(
    float alpha, float outsideDistanceNorm, int width, int height, float hardness)
{
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float edgeWidth = softEdgeWidthNorm(width, height, hardness);
    if (edgeWidth <= 0.0001f) {
        return clampedAlpha;
    }

    const float maxSize = static_cast<float>(std::max(std::max(width, height), 1));
    const float halfTexel = 1.0f / maxSize;
    const float edgeDistance = std::max(outsideDistanceNorm - halfTexel, 0.0f);
    const bool inside = clampedAlpha >= 0.5f;
    const float signedDistance = inside ? edgeDistance : -edgeDistance;
    const float coverage = smoothstep01((signedDistance + edgeWidth) / (2.0f * edgeWidth));
    const float sourceWeight = std::clamp(hardness, 0.0f, 1.0f);
    return std::clamp(coverage + (clampedAlpha - coverage) * sourceWeight, 0.0f, 1.0f);
}

} // namespace aether::dab_shape_falloff

#endif // RUWA_CORE_TILES_DABSHAPEFALLOFF_H
