// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T R A N S F O R M   D E F O R M   B A S E
// ==========================================================================
//   Base pass for the forward-rasterized deform mask path.
//
//   When a selection mask is active during deform, the legacy per-pixel
//   shader composited the transformed source over the target-base content
//   (baseColor * (1 - maskDest) when !preserveMaskedSource). The forward
//   path replaces that single fragment with TWO passes:
//
//     Pass A (this shader): full-screen quad, writes the base content with
//                           the mask carved out, modulated by canvas clip.
//     Pass B (mesh shader): rasterizes the deformed mesh with src-over
//                           premultiplied blending, multiplying the
//                           transformed source by the selection mask.
//
//   The composited result matches the legacy shader at coverage=1 and is
//   within sub-pixel tolerance at the canvas rounded-corner edge.

#version 450 core

uniform sampler2D uTargetBaseTexture;
uniform sampler2D uSelectionMaskAtlasTexture;
uniform int uPreserveMaskedSource;
uniform vec2 uViewportSize;
uniform vec2 uTargetBaseTextureSize;
uniform vec2 uTargetBaseScreenOffset;
uniform vec2 uMaskAtlasSize;
uniform vec2 uMaskAtlasMinTile;
uniform float uTileSize;
uniform vec2 uCameraPosition;
uniform float uCameraZoom;
uniform float uCameraRotation;
uniform vec2 uCanvasSize;
uniform float uCanvasCornerRadius;
uniform int uClipToCanvas;
uniform int uFlipH;
uniform int uFlipV;

in vec2 fragTexCoord;
out vec4 outColor;

vec2 mirrorWorldInCanvas(vec2 worldPos) {
    vec2 mirrored = worldPos;
    vec2 center = uCanvasSize * 0.5;
    if (uFlipH != 0) mirrored.x = 2.0 * center.x - mirrored.x;
    if (uFlipV != 0) mirrored.y = 2.0 * center.y - mirrored.y;
    return mirrored;
}

vec2 documentWorldFromScreen(vec2 screenPixel) {
    vec2 screenOffset = screenPixel - uViewportSize * 0.5;
    vec2 scaledOffset = screenOffset / max(uCameraZoom, 0.0001);
    float c = cos(uCameraRotation);
    float s = sin(uCameraRotation);
    vec2 worldOffset = vec2(
        scaledOffset.x * c + scaledOffset.y * s,
        -scaledOffset.x * s + scaledOffset.y * c
    );
    return mirrorWorldInCanvas(uCameraPosition + worldOffset);
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

vec4 sampleTargetBase(vec2 destScreen) {
    vec2 baseScreen = destScreen + uTargetBaseScreenOffset;
    vec2 baseSize = max(uTargetBaseTextureSize, vec2(1.0));
    if (baseScreen.x < -0.5 || baseScreen.y < -0.5
        || baseScreen.x > baseSize.x + 0.5
        || baseScreen.y > baseSize.y + 0.5) {
        return vec4(0.0);
    }
    return texture(uTargetBaseTexture, vec2((baseScreen.x + 0.5) / baseSize.x,
                                            1.0 - (baseScreen.y + 0.5) / baseSize.y));
}

float sampleSelectionMask(vec2 worldPos) {
    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;
    if (maskPixel.x < 0.0 || maskPixel.y < 0.0
        || maskPixel.x >= uMaskAtlasSize.x || maskPixel.y >= uMaskAtlasSize.y) {
        return 0.0;
    }
    return texture(uSelectionMaskAtlasTexture, maskPixel / uMaskAtlasSize).a;
}

void main() {
    vec2 destScreen = vec2(
        gl_FragCoord.x - 0.5,
        uViewportSize.y - gl_FragCoord.y - 0.5
    );
    vec2 destWorld = documentWorldFromScreen(destScreen);
    float coverage = canvasClipCoverage(destWorld);
    if (coverage <= 0.0) {
        outColor = vec4(0.0);
        return;
    }

    vec4 baseColor = sampleTargetBase(destScreen);
    if (uPreserveMaskedSource == 0) {
        float maskDest = clamp(sampleSelectionMask(destWorld), 0.0, 1.0);
        baseColor *= (1.0 - maskDest);
    }
    outColor = baseColor * coverage;
}
