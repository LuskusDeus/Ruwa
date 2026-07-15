// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   G E O M E T R Y   H E L P E R S
// ==========================================================================

#ifndef RUWA_CORE_GEOMETRY_HELPERS_H
#define RUWA_CORE_GEOMETRY_HELPERS_H

#include "shared/types/Types.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <vector>

namespace aether {

inline bool rectHasArea(const Rect& rect)
{
    return rect.width > 0.0f && rect.height > 0.0f;
}

inline bool nearlyEqualFloat(float a, float b, float epsilon = 0.01f)
{
    return std::abs(a - b) <= epsilon;
}

inline bool nearlyEqualPoint(const Vector2& a, const Vector2& b, float epsilon = 0.01f)
{
    return nearlyEqualFloat(a.x, b.x, epsilon) && nearlyEqualFloat(a.y, b.y, epsilon);
}

inline bool polygonsEquivalent(
    const std::vector<Vector2>& lhs, const std::vector<Vector2>& rhs, float epsilon = 0.01f)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    const float epsilonSq = epsilon * epsilon;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const float dx = lhs[i].x - rhs[i].x;
        const float dy = lhs[i].y - rhs[i].y;
        if ((dx * dx + dy * dy) > epsilonSq) {
            return false;
        }
    }
    return true;
}

inline float lerpScalar(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float vectorLength(const Vector2& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

inline float normalizedDot(const Vector2& a, const Vector2& b)
{
    const float lenA = vectorLength(a);
    const float lenB = vectorLength(b);
    if (lenA <= 0.0001f || lenB <= 0.0001f) {
        return 1.0f;
    }
    return std::clamp((a.x * b.x + a.y * b.y) / (lenA * lenB), -1.0f, 1.0f);
}

inline float pointLineDistance(
    const Vector2& point, const Vector2& lineStart, const Vector2& lineEnd)
{
    const float dx = lineEnd.x - lineStart.x;
    const float dy = lineEnd.y - lineStart.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq <= 0.0001f) {
        return vectorLength({ point.x - lineStart.x, point.y - lineStart.y });
    }

    const float t = std::clamp(
        ((point.x - lineStart.x) * dx + (point.y - lineStart.y) * dy) / lenSq, 0.0f, 1.0f);
    const float projX = lineStart.x + dx * t;
    const float projY = lineStart.y + dy * t;
    return vectorLength({ point.x - projX, point.y - projY });
}

} // namespace aether

#endif // RUWA_CORE_GEOMETRY_HELPERS_H
