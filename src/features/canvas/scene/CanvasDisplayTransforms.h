// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   |   C A N V A S   D I S P L A Y   T R A N S F O R M S
// ==========================================================================
//   Mirror document content in world space around canvas center (display-only).
//   Camera / viewport navigation stays unmirrored.
// ==========================================================================

#ifndef AETHER_SCENE_CANVASDISPLAYTRANSFORMS_H
#define AETHER_SCENE_CANVASDISPLAYTRANSFORMS_H

#include "shared/types/Types.h"

#include <array>
#include <cmath>

namespace aether {

inline std::array<float, 16> multiplyMat4ColMajor(
    const std::array<float, 16>& a, const std::array<float, 16>& b)
{
    std::array<float, 16> r {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + row] * b[col * 4 + k];
            }
            r[col * 4 + row] = s;
        }
    }
    return r;
}

/// World-space reflection about canvas center (cw x ch). Identity when both flips false.
inline std::array<float, 16> canvasContentMirrorMatrix4(float cw, float ch, bool flipH, bool flipV)
{
    if (!flipH && !flipV) {
        return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f };
    }
    const float cx = cw * 0.5f;
    const float cy = ch * 0.5f;
    const float sx = flipH ? -1.0f : 1.0f;
    const float sy = flipV ? -1.0f : 1.0f;
    return { sx, 0.0f, 0.0f, 0.0f, 0.0f, sy, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, cx * (1.0f - sx),
        cy * (1.0f - sy), 0.0f, 1.0f };
}

/// Apply the same mapping as canvasContentMirrorMatrix4 to a 2D world point (self-inverse).
inline Vector2 mirrorWorldInCanvas(Vector2 w, float cw, float ch, bool flipH, bool flipV)
{
    if (!flipH && !flipV) {
        return w;
    }
    const float cx = cw * 0.5f;
    const float cy = ch * 0.5f;
    float x = w.x;
    float y = w.y;
    if (flipH) {
        x = 2.0f * cx - x;
    }
    if (flipV) {
        y = 2.0f * cy - y;
    }
    return { x, y };
}

} // namespace aether

#endif
