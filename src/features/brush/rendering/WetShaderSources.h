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

// Shared by Wet pickup and apply. RGBA8's low-alpha RGB numerators are a spatial
// dither, so they must be resolved as a neighborhood before any operation raises
// coverage and makes their individual quantization error visible.
inline constexpr std::string_view kWetCanvasSamplingGlsl = R"glsl(
vec4 wetSanitizeCanvasPremultiplied(vec4 color) {
    color = wetFinite(color, vec4(0.0));
    color.a = clamp(color.a, 0.0, 1.0);
    color.rgb = clamp(color.rgb, vec3(0.0), vec3(color.a));
    return color.a > 1.0e-6 ? color : vec4(0.0);
}
vec4 wetResolveRgba8CanvasPremultiplied(
    sampler2D canvasTexture, vec2 uv, vec2 minUv, vec2 maxUv) {
    vec4 center = wetSanitizeCanvasPremultiplied(
        texture(canvasTexture, clamp(uv, minUv, maxUv)));
    if (center.a <= 1.0e-6) return vec4(0.0);

    // At low coverage, premultiplied RGBA8 has too few RGB numerators to
    // describe the straight color. Alpha 1/255 permits only 0 or 1 per channel,
    // so unpremultiplication turns one smooth edge into black/red/blue/magenta
    // pixels. Wet deposit later raises their alpha and exposes that false
    // spectrum. Resolve the straight color from a local premultiplied
    // neighborhood instead: summing RGB and alpha before division reconstructs
    // the color represented spatially by the existing rounding dither. Retain
    // the center pixel's original coverage after that resolve.
    // Below 64 bytes of alpha, a premultiplied RGB numerator carries fewer
    // than six reliable bits of straight color. Do not switch abruptly at one
    // alpha value: that merely turns the quantization error into a contour.
    const float fullyTrustedAlpha = 64.5 / 255.0;
    if (center.a >= fullyTrustedAlpha)
        return center;

    vec4 neighborhood = vec4(0.0);
    vec2 texelSize = 1.0 / vec2(textureSize(canvasTexture, 0));
    // A 7x7 window has enough independent dither samples to recover a stable
    // hue even in a one-byte-alpha band, while remaining local to the edge and
    // avoiding color leakage from unrelated distant shapes. This branch runs
    // only for non-empty pixels below the trusted-alpha boundary.
    for (int y = -3; y <= 3; ++y) {
        for (int x = -3; x <= 3; ++x) {
            vec2 sampleUv = uv + vec2(float(x), float(y)) * texelSize;
            if (any(lessThan(sampleUv, minUv)) || any(greaterThan(sampleUv, maxUv)))
                continue;
            neighborhood += wetSanitizeCanvasPremultiplied(
                texture(canvasTexture, sampleUv));
        }
    }

    // Require at least 24.5 byte-equivalents of accumulated coverage. Below
    // that there are too few spatial samples to distinguish hue from rounding
    // noise at the very edge. A moderately covered isolated detail is already
    // more trustworthy than that, so preserve its center sample instead.
    const float minimumCoverageMass = 24.5 / 255.0;
    const float minimumCenterFallbackAlpha = 8.5 / 255.0;
    if (neighborhood.a < minimumCoverageMass)
        return center.a >= minimumCenterFallbackAlpha ? center : vec4(0.0);

    vec3 neighborhoodStraight = neighborhood.rgb / neighborhood.a;
    vec3 centerStraight = center.rgb / center.a;
    float centerReliability = smoothstep(
        minimumCenterFallbackAlpha, fullyTrustedAlpha, center.a);
    vec3 straightColor = mix(neighborhoodStraight, centerStraight, centerReliability);
    return wetSanitizeCanvasPremultiplied(
        vec4(straightColor * center.a, center.a));
}
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
uniform int uCanvasIsRgba8;
in vec2 fragPixelCoord;
)glsl";

inline constexpr std::string_view kWetPerDabPickupMain = R"glsl(
void main() {
    vec2 local = fragPixelCoord - vec2(uReservoirHalf);
    vec2 rawCanvasUv = (uBrushWorldPos + local - uRoiOriginPx) * uInvRoiSize;
    vec2 halfTexelUv = 0.5 / vec2(textureSize(uOriginalTexture, 0));
    vec2 minCanvasUv = halfTexelUv;
    vec2 maxCanvasUv = vec2(1.0) - halfTexelUv;
    vec2 canvasUv = clamp(rawCanvasUv, minCanvasUv, maxCanvasUv);
    vec4 canvasColor = texture(uOriginalTexture, canvasUv);
    if (uCanvasIsRgba8 != 0)
        canvasColor = wetResolveRgba8CanvasPremultiplied(
            uOriginalTexture, canvasUv, minCanvasUv, maxCanvasUv);
    WetLatent canvas = wetEncodePremultiplied(canvasColor);
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
uniform int uCanvasIsRgba8;
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
    vec4 canvasColor = texture(uOriginalTexture, canvasUv);
    if (uCanvasIsRgba8 != 0)
        canvasColor = wetResolveRgba8CanvasPremultiplied(
            uOriginalTexture, canvasUv, halfTexelUv, validMaxUv);
    WetLatent canvas = wetEncodePremultiplied(canvasColor);
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
float wetCustomDabCoverage(vec2 uv, out float edgeFactor) {
    vec2 shape = wetSampleDabShapeSafe(uv);
    float baseAlpha = clamp(shape.r, 0.0, 1.0);
    float softAlpha = clamp(shape.g, 0.0, 1.0);
    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);
    float coverage = mix(baseAlpha, softAlpha, softness);
    edgeFactor = max(0.0, coverage - baseAlpha);
    return coverage;
}
float wetBrushCoverage(vec2 local, out float edgeFactor) {
    edgeFactor = 0.0;
    if (uUseDabShapeTexture != 0) {
        vec2 shapeLocal = local / uBrushRadius / max(uDabShapeScale, vec2(0.0001));
        if (abs(shapeLocal.x) > 1.0 || abs(shapeLocal.y) > 1.0)
            return 0.0;
        return wetCustomDabCoverage((shapeLocal + 1.0) * 0.5, edgeFactor);
    }
    float distanceToCenter = length(local) / uBrushRadius;
    if (distanceToCenter > 1.0) return 0.0;
    edgeFactor = smoothstep(clamp(uBrushHardness + 0.05, 0.05, 0.95),
        1.0, distanceToCenter);
    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);
    return softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, 1.0 - distanceToCenter);
}
float wetApplyTextureShaping(float grain) {
    float contrastStrength = 0.5 + uTextureContrast * 2.5;
    float g = clamp(0.5 + (grain - 0.5) * contrastStrength, 0.0, 1.0);
    float depthMix = 1.0 - uTextureDepth * (1.0 - g);
    float blendMix = (1.0 - uTextureBlend) * depthMix
        + uTextureBlend * (depthMix * depthMix);
    return clamp((1.0 - uTextureAmount) + uTextureAmount * blendMix, 0.0, 1.0);
}
float wetTextureFactor(vec2 textureUv, float edgeFactor) {
    if (uUseTexture == 0) return 1.0;
    float factor = wetApplyTextureShaping(texture(uTextureTile, textureUv).r);
    if (uTextureEdgeBoost > 0.0) {
        float contrast = 1.0 + edgeFactor * uTextureEdgeBoost * 8.0;
        factor = clamp(0.5 + (factor - 0.5) * contrast, 0.0, 1.0);
    }
    return factor;
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
uniform sampler2D uTextureTile;
uniform int uUseTexture;
uniform vec2 uInvTextureSize;
uniform float uTextureEdgeBoost;
uniform float uTextureContrast;
uniform float uTextureDepth;
uniform float uTextureBlend;
uniform float uTextureAmount;
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
uniform int uCanvasIsRgba8;
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
    float edgeFactor = 0.0;
    float falloff = wetBrushCoverage(local, edgeFactor);
    if (falloff <= 0.0) discard;
    float textureFactor = wetTextureFactor(fragPixelCoord * uInvTextureSize, edgeFactor);
    if (textureFactor <= 0.0) discard;
    falloff *= textureFactor;
    float maskScale = uUseMask != 0 ? texture(uMaskTexture, fragPixelCoord * uInvTileSize).a : 1.0;
    if (maskScale <= 0.0) discard;
    vec2 worldPixel = uTileOriginPx + fragPixelCoord;
    vec2 rawCanvasUv = (worldPixel - uRoiOriginPx) * uInvRoiSize;
    vec2 halfTexelUv = 0.5 / vec2(textureSize(uOriginalTexture, 0));
    vec2 minCanvasUv = halfTexelUv;
    vec2 maxCanvasUv = vec2(1.0) - halfTexelUv;
    vec2 canvasUv = clamp(rawCanvasUv, minCanvasUv, maxCanvasUv);
    vec4 canvas = wetSanitizePremultiplied(texture(uOriginalTexture, canvasUv));
    if (uCanvasIsRgba8 != 0)
        canvas = wetResolveRgba8CanvasPremultiplied(
            uOriginalTexture, canvasUv, minCanvasUv, maxCanvasUv);
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
uniform sampler2D uTextureTile;
uniform int uUseTexture;
uniform vec2 uInvTextureSize;
uniform float uTextureEdgeBoost;
uniform float uTextureContrast;
uniform float uTextureDepth;
uniform float uTextureBlend;
uniform float uTextureAmount;
uniform vec2 uInvTexSize;
uniform vec2 uMaxValidUv;
uniform float uReservoirHalf;
uniform vec2 uInvReservoirPhys;
uniform float uCoatPerDab;
uniform float uDepositRate;
uniform int uPreserveCanvasAlpha;
uniform int uQuantizeTo8Bit;
uniform int uCanvasIsRgba8;
in vec2 fragPixelCoord;
layout(location = 0) out vec4 outColor;
)glsl";

inline constexpr std::string_view kWetBatchedApplyMain = R"glsl(
void main() {
    vec2 halfTexelUv = 0.5 * uInvTexSize;
    vec2 validMaxUv = max(uMaxValidUv - halfTexelUv, halfTexelUv);
    vec2 canvasUv = clamp(fragPixelCoord * uInvTexSize, halfTexelUv, validMaxUv);
    vec4 originalCanvas = wetSanitizePremultiplied(texture(uOriginalTexture, canvasUv));
    vec2 delta = fragPixelCoord - uBrushCenter;
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 local = vec2(delta.x * c + delta.y * s, (-delta.x * s + delta.y * c) / roundness);
    float edgeFactor = 0.0;
    float falloff = wetBrushCoverage(local, edgeFactor);
    if (falloff <= 0.0) { outColor = originalCanvas; return; }
    float textureFactor = wetTextureFactor(fragPixelCoord * uInvTextureSize, edgeFactor);
    if (textureFactor <= 0.0) { outColor = originalCanvas; return; }
    falloff *= textureFactor;
    float maskScale = uUseMask != 0 ? texture(uMaskTexture, fragPixelCoord * uInvMaskSize).a : 1.0;
    if (maskScale <= 0.0) { outColor = originalCanvas; return; }
    vec4 canvas = originalCanvas;
    if (uCanvasIsRgba8 != 0)
        canvas = wetResolveRgba8CanvasPremultiplied(
            uOriginalTexture, canvasUv, halfTexelUv, validMaxUv);
    WetLatent latent = wetSampleReservoir((delta + vec2(uReservoirHalf)) * uInvReservoirPhys);
    vec4 reservoir = wetDecodePremultiplied(latent);
    outColor = wetPreserveCanvasAlpha(wetDitherPremultiplied(
        wetDeposit(canvas, reservoir, falloff, maskScale), fragPixelCoord), canvas);
}
)glsl";

} // namespace aether::wet_pigment_gpu
