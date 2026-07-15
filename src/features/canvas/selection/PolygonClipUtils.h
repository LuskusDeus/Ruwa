// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   P O L Y G O N   C L I P   U T I L S
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_SELECTION_POLYGONCLIPUTILS_H
#define RUWA_FEATURES_CANVAS_SELECTION_POLYGONCLIPUTILS_H

#include "shared/types/Types.h"

#include <vector>

namespace aether {

/// Clip polygon against a half-plane (normal.x * x + normal.y * y >= d).
std::vector<Vector2> clipPolygonAxis(
    const std::vector<Vector2>& input, const Vector2& normal, float d);

/// Clip polygon to canvas bounds [0, width] x [0, height].
std::vector<Vector2> clipPolygonToCanvas(
    const std::vector<Vector2>& input, float width, float height);

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_SELECTION_POLYGONCLIPUTILS_H
