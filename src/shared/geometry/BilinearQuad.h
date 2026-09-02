// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_GEOMETRY_BILINEARQUAD_H
#define RUWA_SHARED_GEOMETRY_BILINEARQUAD_H

#include "shared/types/Types.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace aether::geometry {

/// Find (s,t) in [0,1]^2 such that P is the bilinear interpolation of q.
/// Shared by Free Corners and every renderer that consumes its quad mapping.
inline bool inverseBilinearPoint(
    const Vector2& point, const std::array<Vector2, 4>& quad, float st[2])
{
    const float ex = quad[1].x - quad[0].x;
    const float ey = quad[1].y - quad[0].y;
    const float fx = quad[3].x - quad[0].x;
    const float fy = quad[3].y - quad[0].y;
    const float gx = quad[0].x - quad[1].x + quad[2].x - quad[3].x;
    const float gy = quad[0].y - quad[1].y + quad[2].y - quad[3].y;
    const float hx = point.x - quad[0].x;
    const float hy = point.y - quad[0].y;
    const float k2 = gx * fy - gy * fx;
    const float k1 = ex * fy - ey * fx + hx * gy - hy * gx;
    const float k0 = hx * ey - hy * ex;

    auto tryComputeS = [&](float t, float& s) {
        const float denominatorX = ex + gx * t;
        const float denominatorY = ey + gy * t;
        if (std::abs(denominatorX) > std::abs(denominatorY)) {
            if (std::abs(denominatorX) < 1e-10f) {
                return false;
            }
            s = (hx - fx * t) / denominatorX;
        } else {
            if (std::abs(denominatorY) < 1e-10f) {
                return false;
            }
            s = (hy - fy * t) / denominatorY;
        }
        return true;
    };

    constexpr float kMargin = 0.002f;
    float discriminant = k1 * k1 - 4.0f * k0 * k2;
    if (discriminant < 0.0f) {
        return false;
    }
    discriminant = std::sqrt(discriminant);
    const float signedRoot = k1 >= 0.0f ? discriminant : -discriminant;
    const float stableQ = -0.5f * (k1 + signedRoot);
    float tCandidates[2];
    int candidateCount = 0;
    if (std::abs(k2) > 1e-10f) {
        tCandidates[candidateCount++] = stableQ / k2;
    }
    if (std::abs(stableQ) > 1e-10f) {
        tCandidates[candidateCount++] = k0 / stableQ;
    }
    if (candidateCount == 0) {
        if (std::abs(k1) < 1e-10f) {
            return false;
        }
        tCandidates[candidateCount++] = -k0 / k1;
    }

    for (int candidate = 0; candidate < candidateCount; ++candidate) {
        const float t = tCandidates[candidate];
        if (t < -kMargin || t > 1.0f + kMargin) {
            continue;
        }
        float s = 0.0f;
        if (!tryComputeS(t, s) || s < -kMargin || s > 1.0f + kMargin) {
            continue;
        }
        st[0] = std::clamp(s, 0.0f, 1.0f);
        st[1] = std::clamp(t, 0.0f, 1.0f);
        return true;
    }
    return false;
}

} // namespace aether::geometry

#endif // RUWA_SHARED_GEOMETRY_BILINEARQUAD_H
