// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T R A N S F O R M   D E F O R M   M E S H
// ==========================================================================
//   Vertex shader for forward-rasterized B-spline FFD deform.
//   Each vertex carries a (u,v) parameter in [0,1]. The shader evaluates
//   the cubic B-spline surface S(u,v) using uControlPoints, transforms the
//   resulting world-space position through the camera, and emits clip-space.
//   The fragment shader samples the source by the same (u,v).
//
//   This is the forward equivalent of the per-pixel Newton-based inverse
//   mapping in transform_viewport_preview.frag.glsl. It is robust under
//   folds and extreme deformations because the rasterizer trivially handles
//   self-overlap; there is no Newton, no inverse, no boundary polygon.

#version 450 core

layout(location = 0) in vec2 aUV;
out vec2 vUV;
out vec2 vDestWorld;

uniform vec2 uViewportSize;
uniform vec2 uCameraPosition;
uniform float uCameraZoom;
uniform float uCameraRotation;
uniform vec2 uCanvasSize;
uniform int uFlipH;
uniform int uFlipV;
uniform int uLatticeRows;
uniform int uLatticeCols;
uniform vec2 uControlPoints[64];

float knotVal(int i, int n) {
    if (i <= 3) return 0.0;
    if (i >= n) return 1.0;
    return float(i - 3) / float(n - 3);
}

int findSpan(float u, int n) {
    if (u >= 1.0) return n - 1;
    if (u <= 0.0) return 3;
    int span = 3 + int(u * float(n - 3));
    span = clamp(span, 3, n - 1);
    for (int fix = 0; fix < 2; ++fix) {
        if (span < n - 1 && knotVal(span + 1, n) <= u) span++;
        if (span > 3 && knotVal(span, n) > u) span--;
    }
    return span;
}

vec4 basisFunctions(float u, int span, int n) {
    float N[4];
    N[0] = 1.0;
    for (int j = 1; j <= 3; ++j) {
        float saved = 0.0;
        for (int r = 0; r < j; ++r) {
            float lft = u - knotVal(span + 1 - j + r, n);
            float rgt = knotVal(span + j - r, n) - u;
            float d = lft + rgt;
            float tmp = (d > 1e-10) ? N[r] / d : 0.0;
            N[r] = saved + rgt * tmp;
            saved = lft * tmp;
        }
        N[j] = saved;
    }
    return vec4(N[0], N[1], N[2], N[3]);
}

vec2 evalSurface(float u, float v) {
    int spanU = findSpan(u, uLatticeCols);
    int spanV = findSpan(v, uLatticeRows);
    vec4 bU = basisFunctions(u, spanU, uLatticeCols);
    vec4 bV = basisFunctions(v, spanV, uLatticeRows);
    float Nu[4] = float[](bU.x, bU.y, bU.z, bU.w);
    float Nv[4] = float[](bV.x, bV.y, bV.z, bV.w);
    vec2 S = vec2(0.0);
    for (int jv = 0; jv < 4; ++jv) {
        int row = spanV - 3 + jv;
        for (int iu = 0; iu < 4; ++iu) {
            int col = spanU - 3 + iu;
            S += Nu[iu] * Nv[jv] * uControlPoints[row * uLatticeCols + col];
        }
    }
    return S;
}

vec2 mirrorWorldInCanvas(vec2 worldPos) {
    vec2 mirrored = worldPos;
    vec2 center = uCanvasSize * 0.5;
    if (uFlipH != 0) mirrored.x = 2.0 * center.x - mirrored.x;
    if (uFlipV != 0) mirrored.y = 2.0 * center.y - mirrored.y;
    return mirrored;
}

vec2 screenFromDocumentWorld(vec2 worldPos) {
    vec2 unmirrored = mirrorWorldInCanvas(worldPos);
    vec2 worldOffset = unmirrored - uCameraPosition;
    float c = cos(uCameraRotation);
    float s = sin(uCameraRotation);
    vec2 scaledOffset = vec2(
        worldOffset.x * c - worldOffset.y * s,
        worldOffset.x * s + worldOffset.y * c
    );
    return uViewportSize * 0.5 + scaledOffset * max(uCameraZoom, 0.0001);
}

void main() {
    vec2 S = evalSurface(aUV.x, aUV.y);
    vDestWorld = S;
    vUV = aUV;

    vec2 screenPx = screenFromDocumentWorld(S);
    // screenPx.y is measured top-down. Convert to NDC where y is bottom-up.
    vec2 ndc = vec2(
        (screenPx.x / max(uViewportSize.x, 1.0)) * 2.0 - 1.0,
        1.0 - (screenPx.y / max(uViewportSize.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
