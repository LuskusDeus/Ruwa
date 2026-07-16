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

inline float softenAlpha(float alpha, float softAlpha, float hardness)
{
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float clampedSoftAlpha = std::clamp(softAlpha, 0.0f, 1.0f);
    const float softness = softnessFromHardness(hardness);
    return std::clamp(
        clampedAlpha + (clampedSoftAlpha - clampedAlpha) * softness, 0.0f, 1.0f);
}

} // namespace aether::dab_shape_falloff

#endif // RUWA_CORE_TILES_DABSHAPEFALLOFF_H
