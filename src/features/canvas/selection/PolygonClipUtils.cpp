// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   P O L Y G O N   C L I P   U T I L S
// ==========================================================================

#include "PolygonClipUtils.h"

#include <algorithm>
#include <cmath>

namespace aether {

std::vector<Vector2> clipPolygonAxis(
    const std::vector<Vector2>& input, const Vector2& normal, float d)
{
    std::vector<Vector2> output;
    if (input.empty())
        return output;
    auto inside = [&](const Vector2& p) { return (normal.x * p.x + normal.y * p.y) >= d; };
    auto intersect = [&](const Vector2& a, const Vector2& b) {
        Vector2 ab { b.x - a.x, b.y - a.y };
        float denom = normal.x * ab.x + normal.y * ab.y;
        float t = 0.0f;
        if (std::abs(denom) > 0.000001f) {
            t = (d - (normal.x * a.x + normal.y * a.y)) / denom;
        }
        t = std::clamp(t, 0.0f, 1.0f);
        return Vector2 { a.x + ab.x * t, a.y + ab.y * t };
    };

    Vector2 prev = input.back();
    bool prevInside = inside(prev);
    for (const auto& curr : input) {
        bool currInside = inside(curr);
        if (currInside) {
            if (!prevInside) {
                output.push_back(intersect(prev, curr));
            }
            output.push_back(curr);
        } else if (prevInside) {
            output.push_back(intersect(prev, curr));
        }
        prev = curr;
        prevInside = currInside;
    }
    return output;
}

std::vector<Vector2> clipPolygonToCanvas(
    const std::vector<Vector2>& input, float width, float height)
{
    std::vector<Vector2> poly = input;
    poly = clipPolygonAxis(poly, { 1.0f, 0.0f }, 0.0f);
    poly = clipPolygonAxis(poly, { -1.0f, 0.0f }, -width);
    poly = clipPolygonAxis(poly, { 0.0f, 1.0f }, 0.0f);
    poly = clipPolygonAxis(poly, { 0.0f, -1.0f }, -height);
    return poly;
}

} // namespace aether
