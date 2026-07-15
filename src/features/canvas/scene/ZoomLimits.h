// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   Z O O M   L I M I T S
// ==========================================================================

#ifndef RUWA_CORE_CANVAS_ZOOMLIMITS_H
#define RUWA_CORE_CANVAS_ZOOMLIMITS_H

#include <algorithm>
#include <utility>

namespace ruwa::core::canvas {

/**
 * @brief Compute zoom limits for canvas viewport.
 *
 * Zoom limits are tied to brush size: minZoom ensures the largest brush
 * is visible at a "convenient" scale; maxZoom caps zoom-in.
 *
 * @param viewportWidth  Viewport width in pixels
 * @param viewportHeight Viewport height in pixels
 * @param maxBrushRadius Maximum brush radius (from canvas size, e.g. maxBrushRadiusFromCanvas)
 * @return {minZoom, maxZoom} pair
 */
inline std::pair<float, float> computeZoomLimits(
    int viewportWidth, int viewportHeight, float maxBrushRadius)
{
    const float viewportMin
        = static_cast<float>(std::max(1, std::min(viewportWidth, viewportHeight)));
    constexpr float kMaxZoom = 58.0f;
    const float minZoom = viewportMin / (10.0f * maxBrushRadius);
    return { minZoom, kMaxZoom };
}

} // namespace ruwa::core::canvas

#endif // RUWA_CORE_CANVAS_ZOOMLIMITS_H
