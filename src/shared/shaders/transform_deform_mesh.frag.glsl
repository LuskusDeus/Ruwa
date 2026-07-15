// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T R A N S F O R M   D E F O R M   M E S H
// ==========================================================================
//   Fragment shader for forward-rasterized B-spline FFD deform.
//   Receives the (u,v) parameter and the destination world-space position
//   from the vertex shader. Samples the source content at the source-space
//   world position stToSrc(u,v), applies optional selection mask and canvas
//   clipping. Output is premultiplied alpha; the host enables premultiplied
//   src-over blending so folded triangles composite naturally.

#version 450 core

in vec2 vUV;
in vec2 vDestWorld;
out vec4 outColor;

uniform sampler2D uSourceAtlasTexture;
uniform sampler2D uSelectionMaskAtlasTexture;
uniform int uSourceIsScreenTexture;
uniform int uUseSelectionMask;
uniform vec2 uViewportSize;
uniform vec2 uSourceTextureSize;
uniform vec2 uSourceScreenOffset;
uniform vec2 uCameraPosition;
uniform float uCameraZoom;
uniform float uCameraRotation;
uniform vec2 uCanvasSize;
uniform float uCanvasCornerRadius;
uniform int uClipToCanvas;
uniform int uFlipH;
uniform int uFlipV;
uniform vec2 uAtlasSize;
uniform vec2 uAtlasMinTile;
uniform vec2 uMaskAtlasSize;
uniform vec2 uMaskAtlasMinTile;
uniform float uTileSize;
uniform vec4 uContentBounds;

vec2 stToSrc(vec2 st) {
    return vec2(
        mix(uContentBounds.x, uContentBounds.z, st.x),
        mix(uContentBounds.y, uContentBounds.w, st.y)
    );
}

float roundedRectCoverage(vec2 pixelPos, vec2 rectSize, float radius) {
    if (radius <= 0.0 || rectSize.x <= 0.0 || rectSize.y <= 0.0) return 1.0;
    float clampedRadius = min(radius, 0.5 * min(rectSize.x, rectSize.y));
    vec2 halfSize = rectSize * 0.5;
    vec2 centeredPos = pixelPos - halfSize;
    vec2 q = abs(centeredPos) - (halfSize - vec2(clampedRadius));
    float signedDistance =
        length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - clampedRadius;
    float edgeWidth = max(fwidth(signedDistance), 0.0001);
    return 1.0 - smoothstep(0.0, edgeWidth, signedDistance);
}

float canvasClipCoverage(vec2 worldPos) {
    if (uClipToCanvas == 0 || uCanvasSize.x <= 0.0 || uCanvasSize.y <= 0.0) return 1.0;
    float edgePadding = 1.0 / max(uCameraZoom, 0.0001);
    if (worldPos.x < -edgePadding || worldPos.y < -edgePadding
        || worldPos.x > uCanvasSize.x + edgePadding
        || worldPos.y > uCanvasSize.y + edgePadding) {
        return 0.0;
    }
    vec2 clipPos = clamp(worldPos, vec2(0.0), uCanvasSize);
    return roundedRectCoverage(clipPos, uCanvasSize, uCanvasCornerRadius);
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

vec4 sampleSourceAtlas(vec2 worldPos) {
    if (uSourceIsScreenTexture != 0) {
        vec2 sourceScreen = screenFromDocumentWorld(worldPos) + uSourceScreenOffset;
        vec2 sourceSize = max(uSourceTextureSize, vec2(1.0));
        if (sourceScreen.x < -0.5 || sourceScreen.y < -0.5
            || sourceScreen.x > sourceSize.x + 0.5
            || sourceScreen.y > sourceSize.y + 0.5) {
            return vec4(0.0);
        }
        return texture(uSourceAtlasTexture, vec2((sourceScreen.x + 0.5) / sourceSize.x,
                                                 1.0 - (sourceScreen.y + 0.5) / sourceSize.y));
    }
    vec2 atlasPixel = worldPos - uAtlasMinTile * uTileSize;
    if (atlasPixel.x < -0.5 || atlasPixel.y < -0.5
        || atlasPixel.x > uAtlasSize.x + 0.5 || atlasPixel.y > uAtlasSize.y + 0.5) {
        return vec4(0.0);
    }
    return texture(uSourceAtlasTexture, (atlasPixel + 0.5) / uAtlasSize);
}

vec4 sampleClampedSource(vec2 worldPos) {
    // (u,v) is always in [0,1] by mesh construction, so srcWorld is always
    // inside contentBounds. The outer bounds check that was here in the
    // per-pixel inverse-mapping path is therefore redundant; sampleSourceAtlas
    // still guards against atlas under-coverage at its own boundary.
    return sampleSourceAtlas(worldPos);
}

float sampleSelectionMask(vec2 worldPos) {
    if (uUseSelectionMask == 0) return 1.0;
    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;
    if (maskPixel.x < 0.0 || maskPixel.y < 0.0
        || maskPixel.x >= uMaskAtlasSize.x || maskPixel.y >= uMaskAtlasSize.y) {
        return 0.0;
    }
    return texture(uSelectionMaskAtlasTexture, maskPixel / uMaskAtlasSize).a;
}

void main() {
    vec2 srcWorld = stToSrc(vUV);
    vec4 c = sampleClampedSource(srcWorld);

    if (uUseSelectionMask != 0) {
        c *= clamp(sampleSelectionMask(srcWorld), 0.0, 1.0);
    }

    float coverage = canvasClipCoverage(vDestWorld);
    outColor = c * coverage;
}
