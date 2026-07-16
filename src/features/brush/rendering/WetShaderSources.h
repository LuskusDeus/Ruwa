// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string_view>

namespace aether::wet_pigment_gpu {

inline constexpr std::string_view kWetPickupOutputsGlsl = R"glsl(
layout(location = 0) out vec4 outPigments0;
layout(location = 1) out vec4 outPigments1;
layout(location = 2) out vec4 outCorrectionAndAlpha;
layout(location = 3) out vec4 outColorMoments;
)glsl";

// Both Wet geometry variants call this single latent update. uUsePen retains
// the optional pen-free latent exchange mode; all contributor weights include
// premultiplied alpha and wetMix4 performs one normalized resolve.
inline constexpr std::string_view kWetPickupUpdateGlsl = R"glsl(
WetLatent wetPickupAtRate(
    WetLatent previous, WetLatent advected, WetLatent canvas, float pickup) {
    pickup = clamp(pickup, 0.0, 1.0);
    if (uUsePen == 0) {
        float alpha = mix(previous.alpha, canvas.alpha, pickup);
        return wetMix4(previous, (1.0 - pickup) * previous.alpha,
            canvas, pickup * canvas.alpha, previous, 0.0, canvas, 0.0, alpha);
    }

    WetLatent pen = wetEncode(clamp(uPenColor, 0.0, 1.0), 1.0);
    float spread = clamp(uSpread, 0.0, 1.0);
    float dilution = clamp(uDilution, 0.0, 1.0);
    float canvasWeight = pickup * (1.0 - spread) * canvas.alpha;
    float penWeight = pickup * spread;
    // Alpha shortfall is coverage, not additional pen pigment. Folding it into
    // penWeight makes a partially covered canvas traverse a new spectral mix at
    // every color<->transparent boundary. With complementary colors that path
    // has a dark midpoint, printed once on entry and once on exit. Resolve only
    // the pigment mass that is actually present; apply the coverage fill after
    // the resolve, matching the established premultiplied-RGBA wet algorithm.
    float coverageFill = max(pickup - canvasWeight - penWeight, 0.0)
        * clamp(uPenFillGate, 0.0, 1.0);
    float previousBase = max(1.0 - penWeight - canvasWeight, 0.0) * (1.0 - dilution);
    float previousWeight = previousBase * (1.0 - clamp(uWetFlow, 0.0, 1.0)) * previous.alpha;
    float advectedWeight = previousBase * clamp(uWetFlow, 0.0, 1.0) * advected.alpha;
    float resolvedAlpha = previousWeight + advectedWeight + canvasWeight + penWeight;
    if (!(resolvedAlpha > 1.0e-8)) return wetZero();
    WetLatent resolved = wetMix4(previous, previousWeight, advected, advectedWeight,
        canvas, canvasWeight, pen, penWeight, resolvedAlpha);
    resolved.alpha = min(resolvedAlpha + coverageFill, 1.0);
    return wetNormalize(resolved);
}
WetLatent wetPickupUpdate(WetLatent previous, WetLatent advected, WetLatent canvas) {
    return wetPickupAtRate(previous, advected, canvas, uCanvasPickup);
}
WetLatent wetInitialPickup(WetLatent canvas) {
    // Initialization is one complete exchange from an empty reservoir. Reusing
    // the steady-state rule is essential at a stroke boundary: its canvas term
    // already contains canvas.alpha. The former boosted-spread initializer then
    // multiplied that term by alpha again, so only partially covered edge pixels
    // entered the reservoir as (1-spread)*alpha^2. The endpoints (transparent and
    // opaque canvas) happened to be correct, hiding the error away from edges.
    return wetPickupAtRate(wetZero(), wetZero(), canvas, 1.0);
}
void wetWritePickup(WetLatent latent) {
    wetWritePlanes(latent, outPigments0, outPigments1,
        outCorrectionAndAlpha, outColorMoments);
}
)glsl";

inline constexpr std::string_view kWetPerDabPickupPreamble = R"glsl(#version 450 core
uniform vec2 uBrushWorldPos;
uniform float uBrushRadius;
uniform float uBrushRoundness;
uniform float uBrushAngleRad;
uniform int uInit;
uniform sampler2D uOriginalTexture;
uniform vec2 uRoiOriginPx;
uniform vec2 uInvRoiSize;
uniform float uReservoirHalf;
uniform vec2 uInvReservoirPhys;
uniform float uCanvasPickup;
uniform float uDilution;
uniform float uSpread;
uniform vec3 uPenColor;
uniform vec2 uAdvectPx;
uniform float uWetFlow;
uniform float uPenFillGate;
uniform int uUsePen;
in vec2 fragPixelCoord;
)glsl";

inline constexpr std::string_view kWetPerDabPickupMain = R"glsl(
void main() {
    vec2 local = fragPixelCoord - vec2(uReservoirHalf);
    vec2 canvasUv = (uBrushWorldPos + local - uRoiOriginPx) * uInvRoiSize;
    WetLatent canvas = wetEncodePremultiplied(texture(uOriginalTexture,
        clamp(canvasUv, vec2(0.0), vec2(1.0))));
    if (uInit != 0) {
        wetWritePickup(wetInitialPickup(canvas));
        return;
    }
    vec2 reservoirUv = fragPixelCoord * uInvReservoirPhys;
    WetLatent previous = wetSampleReservoir(reservoirUv);
    if (uUsePen == 0) {
        wetWritePickup(wetPickupUpdate(previous, previous, canvas));
        return;
    }
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 brushLocal = vec2(local.x * c + local.y * s,
        (-local.x * s + local.y * c) / roundness);
    if (length(brushLocal) > uBrushRadius) {
        wetWritePickup(canvas);
        return;
    }
    WetLatent advected = previous;
    vec2 sourcePx = fragPixelCoord + uAdvectPx;
    vec2 sourceLocal = sourcePx - vec2(uReservoirHalf);
    vec2 sourceBrushLocal = vec2(sourceLocal.x * c + sourceLocal.y * s,
        (-sourceLocal.x * s + sourceLocal.y * c) / roundness);
    float limit = 2.0 * uReservoirHalf;
    if (length(sourceBrushLocal) <= uBrushRadius && all(greaterThanEqual(sourcePx, vec2(0.0)))
        && all(lessThan(sourcePx, vec2(limit)))) {
        advected = wetSampleReservoir(sourcePx * uInvReservoirPhys);
    }
    wetWritePickup(wetPickupUpdate(previous, advected, canvas));
}
)glsl";

inline constexpr std::string_view kWetBatchedPickupPreamble = R"glsl(#version 450 core
uniform vec2 uBrushCenter;
uniform float uBrushRadius;
uniform float uBrushRoundness;
uniform float uBrushAngleRad;
uniform int uInit;
uniform sampler2D uOriginalTexture;
uniform vec2 uInvTexSize;
uniform vec2 uMaxValidUv; // exclusive valid edge / physical texture size
uniform float uReservoirHalf;
uniform vec2 uInvReservoirPhys;
uniform float uCanvasPickup;
uniform float uDilution;
uniform float uSpread;
uniform vec3 uPenColor;
uniform vec2 uAdvectPx;
uniform float uWetFlow;
uniform float uPenFillGate;
uniform int uUsePen;
in vec2 fragPixelCoord;
)glsl";

inline constexpr std::string_view kWetBatchedPickupMain = R"glsl(
void main() {
    vec2 local = fragPixelCoord - vec2(uReservoirHalf);
    // Clamp to valid texel centers. The work texture can be larger than this ROI,
    // so sampling at the exclusive edge would blend with an unused texel.
    vec2 halfTexelUv = 0.5 * uInvTexSize;
    vec2 validMaxUv = max(uMaxValidUv - halfTexelUv, halfTexelUv);
    vec2 canvasUv = clamp((uBrushCenter + local) * uInvTexSize, halfTexelUv, validMaxUv);
    WetLatent canvas = wetEncodePremultiplied(texture(uOriginalTexture, canvasUv));
    if (uInit != 0) {
        wetWritePickup(wetInitialPickup(canvas));
        return;
    }
    WetLatent previous = wetSampleReservoir(fragPixelCoord * uInvReservoirPhys);
    if (uUsePen == 0) {
        wetWritePickup(wetPickupUpdate(previous, previous, canvas));
        return;
    }
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 brushLocal = vec2(local.x * c + local.y * s,
        (-local.x * s + local.y * c) / roundness);
    if (length(brushLocal) > uBrushRadius) {
        wetWritePickup(canvas);
        return;
    }
    WetLatent advected = previous;
    vec2 sourcePx = fragPixelCoord + uAdvectPx;
    vec2 sourceLocal = sourcePx - vec2(uReservoirHalf);
    vec2 sourceBrushLocal = vec2(sourceLocal.x * c + sourceLocal.y * s,
        (-sourceLocal.x * s + sourceLocal.y * c) / roundness);
    float limit = 2.0 * uReservoirHalf;
    if (length(sourceBrushLocal) <= uBrushRadius && all(greaterThanEqual(sourcePx, vec2(0.0)))
        && all(lessThan(sourcePx, vec2(limit)))) {
        advected = wetSampleReservoir(sourcePx * uInvReservoirPhys);
    }
    wetWritePickup(wetPickupUpdate(previous, advected, canvas));
}
)glsl";

inline constexpr std::string_view kWetApplyCoverageGlsl = R"glsl(
vec4 wetSanitizePremultiplied(vec4 color) {
    if (color.a <= 1.0e-6 || any(isnan(color)) || any(isinf(color))) return vec4(0.0);
    color.a = clamp(color.a, 0.0, 1.0);
    color.rgb = clamp(color.rgb, vec3(0.0), vec3(color.a));
    return color;
}
vec2 wetSampleDabShapeSafe(vec2 uv) {
    vec2 shape = texture(uDabShapeTexture, clamp(uv, vec2(0.0), vec2(1.0))).rg;
    vec2 outside = max(max(-uv, uv - vec2(1.0)), vec2(0.0)) * 2.0;
    if (outside.x > 0.0 || outside.y > 0.0) {
        shape.r = 0.0;
        shape.g = 0.0;
    }
    return shape;
}
float wetCustomDabCoverage(vec2 uv) {
    vec2 shape = wetSampleDabShapeSafe(uv);
    float baseAlpha = clamp(shape.r, 0.0, 1.0);
    float softAlpha = clamp(shape.g, 0.0, 1.0);
    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);
    return mix(baseAlpha, softAlpha, softness);
}
float wetBrushCoverage(vec2 local) {
    if (uUseDabShapeTexture != 0) {
        vec2 shapeLocal = local / uBrushRadius / max(uDabShapeScale, vec2(0.0001));
        if (abs(shapeLocal.x) > 1.0 || abs(shapeLocal.y) > 1.0)
            return 0.0;
        return wetCustomDabCoverage((shapeLocal + 1.0) * 0.5);
    }
    float distanceToCenter = length(local) / uBrushRadius;
    if (distanceToCenter > 1.0) return 0.0;
    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);
    return softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, 1.0 - distanceToCenter);
}
vec4 wetDeposit(vec4 canvas, vec4 reservoir, float falloff, float maskScale) {
    float intensity = clamp(uBrushAlpha * falloff * maskScale, 0.0, 1.0);
    vec4 deposited;
    if (uCoatPerDab >= 0.0) {
        float weight = clamp(uCoatPerDab * intensity, 0.0, 1.0) * reservoir.a;
        vec3 coatColor = reservoir.rgb / max(reservoir.a, 1.0e-6);
        deposited = vec4(coatColor * weight, weight) + canvas * (1.0 - weight);
    } else {
        float wetIntensity = clamp(uDepositRate * falloff * maskScale, 0.0, 1.0);
        deposited = mix(canvas, reservoir, wetIntensity);
    }
    return wetSanitizePremultiplied(deposited);
}
vec4 wetPreserveCanvasAlpha(vec4 deposited, vec4 canvas) {
    // When alpha preservation is requested, transparent reservoir texels must
    // not erase coverage that already exists on canvas.
    // Restore alpha after dithering and rescale the straight mixed color back
    // to premultiplied form so no dark hidden-RGB fringe is introduced.
    deposited = wetSanitizePremultiplied(deposited);
    if (uPreserveCanvasAlpha != 0 && deposited.a < canvas.a) {
        vec3 straightColor = deposited.a > 1.0e-6
            ? deposited.rgb / deposited.a
            : canvas.rgb / max(canvas.a, 1.0e-6);
        deposited = vec4(min(straightColor * canvas.a, vec3(canvas.a)), canvas.a);
    }
    return wetSanitizePremultiplied(deposited);
}
vec4 wetDitherPremultiplied(vec4 color, vec2 pixel) {
    color = wetSanitizePremultiplied(color);
    if (uQuantizeTo8Bit == 0 || color.a <= 1.0e-6) return color;
    float noise = fract(52.9829189
        * fract(dot(floor(pixel), vec2(0.06711056, 0.00583715))));
    float oldAlpha = color.a;
    float newAlpha = floor(oldAlpha * 255.0 + noise) / 255.0;
    vec3 straightColor = color.rgb / oldAlpha;
    return wetSanitizePremultiplied(
        vec4(min(straightColor * newAlpha, vec3(newAlpha)), newAlpha));
}
)glsl";

inline constexpr std::string_view kWetPerDabApplyPreamble = R"glsl(#version 450 core
uniform vec2 uBrushCenter;
uniform float uBrushRadius;
uniform float uBrushHardness;
uniform float uBrushRoundness;
uniform float uBrushAngleRad;
uniform float uBrushAlpha;
uniform sampler2D uOriginalTexture;
uniform sampler2D uMaskTexture;
uniform int uUseMask;
uniform sampler2D uDabShapeTexture;
uniform int uUseDabShapeTexture;
uniform vec2 uDabShapeScale;
uniform vec2 uTileOriginPx;
uniform vec2 uInvTileSize;
uniform vec2 uRoiOriginPx;
uniform vec2 uInvRoiSize;
uniform float uReservoirHalf;
uniform vec2 uInvReservoirPhys;
uniform float uCoatPerDab;
uniform float uDepositRate;
uniform int uPreserveCanvasAlpha;
uniform int uQuantizeTo8Bit;
in vec2 fragPixelCoord;
layout(location = 0) out vec4 outColor;
)glsl";

inline constexpr std::string_view kWetPerDabApplyMain = R"glsl(
void main() {
    vec2 delta = fragPixelCoord - uBrushCenter;
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 local = vec2(delta.x * c + delta.y * s, (-delta.x * s + delta.y * c) / roundness);
    float falloff = wetBrushCoverage(local);
    if (falloff <= 0.0) discard;
    float maskScale = uUseMask != 0 ? texture(uMaskTexture, fragPixelCoord * uInvTileSize).a : 1.0;
    if (maskScale <= 0.0) discard;
    vec2 worldPixel = uTileOriginPx + fragPixelCoord;
    vec4 canvas = wetSanitizePremultiplied(texture(uOriginalTexture,
        (worldPixel - uRoiOriginPx) * uInvRoiSize));
    WetLatent latent = wetSampleReservoir((delta + vec2(uReservoirHalf)) * uInvReservoirPhys);
    vec4 reservoir = wetDecodePremultiplied(latent);
    outColor = wetPreserveCanvasAlpha(wetDitherPremultiplied(
        wetDeposit(canvas, reservoir, falloff, maskScale), worldPixel), canvas);
}
)glsl";

inline constexpr std::string_view kWetBatchedApplyPreamble = R"glsl(#version 450 core
uniform vec2 uBrushCenter;
uniform float uBrushRadius;
uniform float uBrushHardness;
uniform float uBrushRoundness;
uniform float uBrushAngleRad;
uniform float uBrushAlpha;
uniform sampler2D uOriginalTexture;
uniform sampler2D uMaskTexture;
uniform int uUseMask;
uniform vec2 uInvMaskSize;
uniform sampler2D uDabShapeTexture;
uniform int uUseDabShapeTexture;
uniform vec2 uDabShapeScale;
uniform vec2 uInvTexSize;
uniform float uReservoirHalf;
uniform vec2 uInvReservoirPhys;
uniform float uCoatPerDab;
uniform float uDepositRate;
uniform int uPreserveCanvasAlpha;
uniform int uQuantizeTo8Bit;
in vec2 fragPixelCoord;
layout(location = 0) out vec4 outColor;
)glsl";

inline constexpr std::string_view kWetBatchedApplyMain = R"glsl(
void main() {
    vec4 canvas = wetSanitizePremultiplied(texture(uOriginalTexture, fragPixelCoord * uInvTexSize));
    vec2 delta = fragPixelCoord - uBrushCenter;
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 local = vec2(delta.x * c + delta.y * s, (-delta.x * s + delta.y * c) / roundness);
    float falloff = wetBrushCoverage(local);
    if (falloff <= 0.0) { outColor = canvas; return; }
    float maskScale = uUseMask != 0 ? texture(uMaskTexture, fragPixelCoord * uInvMaskSize).a : 1.0;
    if (maskScale <= 0.0) { outColor = canvas; return; }
    WetLatent latent = wetSampleReservoir((delta + vec2(uReservoirHalf)) * uInvReservoirPhys);
    vec4 reservoir = wetDecodePremultiplied(latent);
    outColor = wetPreserveCanvasAlpha(wetDitherPremultiplied(
        wetDeposit(canvas, reservoir, falloff, maskScale), fragPixelCoord), canvas);
}
)glsl";

} // namespace aether::wet_pigment_gpu
