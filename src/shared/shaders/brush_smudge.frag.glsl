// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   S M U D G E   F R A G M E N T
// ==========================================================================
//
//   Reference / documentation copy of the apply pass. The runtime shader
//   is the inline `kSmudgeApplyFrag` string in GLBrushRenderer.cpp; this
//   file is kept in sync as a readable reference and is NOT loaded.
//
//   Carry-buffer (reservoir) smudge — each dab does two passes:
//
//     1. Pickup (kSmudgePickupFrag) — for every reservoir pixel, sample
//        the canvas under the brush at (brushWorldPos + (frag - half))
//        and blend it into the previous reservoir by (pickupRate*falloff).
//        On the first dab of a stroke uInit=1 forces a full canvas-to-
//        reservoir copy ignoring falloff, so the brush starts fully
//        loaded. Without this initialization low-intensity strokes
//        converge to a fixed point after just a few dabs because the
//        mix(a, a, t) operator quickly saturates inside the brush disk.
//
//     2. Apply  (this shader) — for every canvas pixel under the brush,
//        mix the reservoir back onto the canvas by (strength*falloff).
//        Reservoir pixel at offset (dx, dy) from the brush center is
//        always at the same brush-local position regardless of brush
//        motion, so when the brush moves the loaded pigment moves with
//        it — that's what produces the smear across uniform regions.
//
//   The reservoir is an axis-aligned RGBA16F texture sized to enclose the
//   maximum brush footprint for the stroke. It supports the full set of
//   brush parameters: custom dab images, hardness, roundness, angle, x/y
//   scale, soft-edge texel padding — every parameter flows through
//   brushCoverage() identically in both passes.

#version 450 core

uniform vec2  uBrushCenter;        // brush center in tile-local pixel coords
uniform float uBrushRadius;
uniform float uBrushHardness;
uniform float uBrushRoundness;
uniform float uBrushAngleRad;
uniform float uBrushAlpha;         // strength

uniform sampler2D uOriginalTexture;   // canvas ROI snapshot (premultiplied)
uniform sampler2D uReservoirTexture;  // brush-loaded paint, axis-aligned
uniform sampler2D uMaskTexture;       // selection mask tile alpha
uniform int   uUseMask;

uniform vec2  uTileOriginPx;          // tile origin in document pixel coords
uniform vec2  uInvTileSize;
uniform vec2  uRoiOriginPx;
uniform vec2  uInvRoiSize;

uniform float uReservoirHalf;         // 0.5 * logical reservoir side
uniform vec2  uInvReservoirPhys;      // 1 / physical reservoir size

in vec2 fragPixelCoord;
out vec4 outColor;

vec4 sanitizePremultiplied(vec4 color) {
    if (color.a <= 1e-6) {
        return vec4(0.0);
    }
    color.rgb = min(color.rgb, vec3(color.a));
    return color;
}

void main() {
    vec2 delta = fragPixelCoord - uBrushCenter;
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 local = vec2(
        delta.x * c + delta.y * s,
        (-delta.x * s + delta.y * c) / roundness
    );

    // brushCoverage() picks circular fallback or the custom dab shape
    // texture path, applying hardness/softness, x/y scale and soft-edge
    // padding identically to the brush stamp shader. Omitted here for
    // brevity — see kSmudgeApplyFrag in GLBrushRenderer.cpp.
    float falloff = /* brushCoverage(local) */ 1.0;
    if (falloff <= 0.0) {
        discard;
    }
    float maskScale = 1.0;
    if (uUseMask != 0) {
        maskScale = texture(uMaskTexture, fragPixelCoord * uInvTileSize).a;
        if (maskScale <= 0.0) {
            discard;
        }
    }

    vec2 worldPixelCoord = uTileOriginPx + fragPixelCoord;
    vec2 originalUv = (worldPixelCoord - uRoiOriginPx) * uInvRoiSize;
    vec4 canvas = sanitizePremultiplied(texture(uOriginalTexture, originalUv));

    vec2 reservoirPx = delta + vec2(uReservoirHalf);
    vec2 reservoirUv = reservoirPx * uInvReservoirPhys;
    vec4 reservoir = sanitizePremultiplied(texture(uReservoirTexture, reservoirUv));

    float strength = clamp(uBrushAlpha, 0.0, 1.0);
    float intensity = clamp(strength * falloff * maskScale, 0.0, 1.0);
    outColor = sanitizePremultiplied(mix(canvas, reservoir, intensity));
    if (outColor.a <= 1e-6) {
        outColor = vec4(0.0);
    }
}
