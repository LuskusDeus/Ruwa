// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   C O M P O S I T E   F R A G M E N T
// ==========================================================================
//   Uber-shader for layer compositing via FBO ping-pong.
//   All inputs and outputs are PREMULTIPLIED ALPHA.
//
//   Blend mode IDs match BlendMode enum:
//     0 = Normal, 1 = Multiply, 2 = Screen, 3 = Overlay,
//     4 = SoftLight, 5 = HardLight, 6 = ColorDodge, 7 = ColorBurn,
//     8 = Darken, 9 = Lighten, 10 = Difference, 11 = Exclusion,
//    12 = Dissolve, 13 = LinearBurn, 14 = DarkerColor, 15 = LinearDodge,
//    16 = LighterColor, 17 = VividLight, 18 = LinearLight, 19 = PinLight,
//    20 = HardMix, 21 = Subtract, 22 = Divide, 23 = Hue,
//    24 = Saturation, 25 = Color, 26 = Luminosity
//
//   Internal compositor-only modes:
//   100 = Erase (Destination-Out)

#version 450 core

uniform sampler2D uBaseTexture;   // current composited result (premultiplied)
uniform sampler2D uSrcTexture;    // layer tile being blended (premultiplied)
uniform sampler2D uClipMaskTexture; // optional alpha mask for clipping
uniform sampler2D uClipMaskTexture2; // secondary clip mask (mask-edit preview: committed mask)
uniform sampler2D uProgrammaticBlendBaseTexture; // visible base used by stroke preview blend modes
uniform int       uBlendMode;     // blend mode index
uniform float     uOpacity;       // layer opacity [0..1]
uniform int       uUseClipMask;   // 0 = disabled, 1 = enabled
uniform int       uClipMaskAlphaOnly; // 1 = affect only src alpha
uniform int       uSubtractClipRevealFromSrc; // 1 = remove src under (clip * reveal)
// Layer mask (luminance reveal): the clip texel is a painted grayscale mask where
// reveal = luminance(premultiplied rgb) + (1 - coverage). White paint reveals,
// black paint hides, and tiles with no coverage default to fully revealed.
uniform int       uClipMaskLuminanceReveal;
// Mask-edit live preview: combine the in-progress stroke (uClipMaskTexture) with the
// committed mask (uClipMaskTexture2) exactly as the flattened mask will look, then
// gate src by the resulting reveal. Single pass, both textures premultiplied.
//   reveal = clamp( lum(stroke.rgb)*op + (1 - stroke.a*op) * committedReveal, 0, 1 )
uniform int       uClipMaskEditPreview;
// Replace-mode mask-edit preview (smudge/blur/liquify/wet): the stroke buffer holds
// FINISHED mask tiles and commit does maskTile = mix(committed, stroke, op). Reveal is
// affine, so reveal = mix(committedReveal, strokeReveal, op). On tiles the stroke hasn't
// touched, uClipMaskTexture is fed the committed tile so the mix collapses to committed.
uniform int       uClipMaskEditReplace;
uniform float     uClipMaskEditStrokeOpacity;
uniform int       uPreserveBaseAlpha; // 1 = keep destination alpha
// Soft-selection cap: when 1, the clip mask (selection mask) is treated as a
// per-pixel alpha *ceiling* on the result, not just as gating on src.
//   - if base.a > clipAlpha: pixel is preserved as-is (pre-existing alpha was
//     already above the cap; never reduce).
//   - else after compositing: ao is clamped to clipAlpha and RGB is scaled
//     proportionally to keep the visible color stable under premultiplied storage.
// This prevents alpha accumulation from multiple strokes pushing the final
// alpha above the soft mask's ceiling on non-source layers (where the
// alpha-lock path via uPreserveBaseAlpha doesn't apply).
uniform int       uClipMaskAsAlphaCap;
uniform int       uReplaceBase;   // 1 = src already stores the final tile
// 1 = adjustment-layer replace: src holds the fully effected base, so the mask
// reveal scales the mix factor between base and src instead of darkening src.
uniform int       uReplaceBaseMixReveal;
uniform int       uUseGroupComposite;
uniform sampler2D uGroupPassThroughTexture;
uniform sampler2D uGroupCoverageTexture;
uniform sampler2D uGroupSourceCoverageTexture;
uniform int       uUseProgrammaticBlendBase; // 1 = match commit-time stroke blend flattening
uniform int       uSrcAtop;       // 1 = Porter-Duff src-atop (ao = ab) for clip-group passes
uniform int       uUseRadialReveal;
uniform int       uRadialRevealInvert;
uniform vec2      uRadialRevealOrigin;
uniform float     uRadialRevealRadius;
uniform float     uRadialRevealFeather;
uniform vec2      uTileWorldOrigin;
uniform vec4      uBackdropColor;

in vec2 fragTexCoord;
out vec4 outColor;

const vec3 kLumWeights = vec3(0.299, 0.587, 0.114);

// ---- Blend functions (operate on straight-alpha colors) ----

vec3 blendNormal(vec3 Cb, vec3 Cs)     { return Cs; }
vec3 blendMultiply(vec3 Cb, vec3 Cs)   { return Cb * Cs; }
vec3 blendScreen(vec3 Cb, vec3 Cs)     { return Cb + Cs - Cb * Cs; }
vec3 blendDarken(vec3 Cb, vec3 Cs)     { return min(Cb, Cs); }
vec3 blendLighten(vec3 Cb, vec3 Cs)    { return max(Cb, Cs); }
vec3 blendDifference(vec3 Cb, vec3 Cs) { return abs(Cb - Cs); }
vec3 blendExclusion(vec3 Cb, vec3 Cs)  { return Cb + Cs - 2.0 * Cb * Cs; }
vec3 blendLinearBurn(vec3 Cb, vec3 Cs) { return max(vec3(0.0), Cb + Cs - 1.0); }
vec3 blendLinearDodge(vec3 Cb, vec3 Cs) { return min(vec3(1.0), Cb + Cs); }
vec3 blendSubtract(vec3 Cb, vec3 Cs)   { return max(vec3(0.0), Cb - Cs); }

vec3 blendOverlay(vec3 Cb, vec3 Cs) {
    return mix(2.0 * Cb * Cs,
               1.0 - 2.0 * (1.0 - Cb) * (1.0 - Cs),
               step(0.5, Cb));
}

vec3 blendSoftLight(vec3 Cb, vec3 Cs) {
    vec3 d = mix(sqrt(Cb), ((16.0 * Cb - 12.0) * Cb + 4.0) * Cb, step(0.25, Cb));
    return mix(Cb - (1.0 - 2.0 * Cs) * Cb * (1.0 - Cb),
               Cb + (2.0 * Cs - 1.0) * (d - Cb),
               step(0.5, Cs));
}

vec3 blendHardLight(vec3 Cb, vec3 Cs) {
    return mix(2.0 * Cb * Cs,
               1.0 - 2.0 * (1.0 - Cb) * (1.0 - Cs),
               step(0.5, Cs));
}

float colorSaturation(vec3 color) {
    return max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
}

float blendColorDodgeComponent(float Cb, float Cs) {
    if (Cs >= 1.0) {
        return 1.0;
    }
    return min(1.0, Cb / max(1.0 - Cs, 0.00001));
}

float blendColorBurnComponent(float Cb, float Cs) {
    if (Cs <= 0.0) {
        return 0.0;
    }
    return max(0.0, 1.0 - (1.0 - Cb) / max(Cs, 0.00001));
}

vec3 blendColorDodge(vec3 Cb, vec3 Cs) {
    return vec3(
        blendColorDodgeComponent(Cb.r, Cs.r),
        blendColorDodgeComponent(Cb.g, Cs.g),
        blendColorDodgeComponent(Cb.b, Cs.b)
    );
}

vec3 blendColorBurn(vec3 Cb, vec3 Cs) {
    return vec3(
        blendColorBurnComponent(Cb.r, Cs.r),
        blendColorBurnComponent(Cb.g, Cs.g),
        blendColorBurnComponent(Cb.b, Cs.b)
    );
}

float blendLuminance(vec3 color) {
    return dot(color, kLumWeights);
}

vec3 blendDarkerColor(vec3 Cb, vec3 Cs) {
    return (blendLuminance(Cs) <= blendLuminance(Cb)) ? Cs : Cb;
}

vec3 blendLighterColor(vec3 Cb, vec3 Cs) {
    return (blendLuminance(Cs) >= blendLuminance(Cb)) ? Cs : Cb;
}

vec3 blendDivide(vec3 Cb, vec3 Cs) {
    vec3 result = min(vec3(1.0), Cb / max(Cs, vec3(0.001)));
    return vec3(
        (Cs.r <= 0.0) ? 1.0 : result.r,
        (Cs.g <= 0.0) ? 1.0 : result.g,
        (Cs.b <= 0.0) ? 1.0 : result.b
    );
}

vec3 clipColor(vec3 color) {
    float lum = blendLuminance(color);
    float minComponent = min(color.r, min(color.g, color.b));
    float maxComponent = max(color.r, max(color.g, color.b));

    if (minComponent < 0.0) {
        color = vec3(lum) + ((color - vec3(lum)) * lum / max(lum - minComponent, 0.00001));
    }
    if (maxComponent > 1.0) {
        color = vec3(lum) + ((color - vec3(lum)) * (1.0 - lum) / max(maxComponent - lum, 0.00001));
    }

    return clamp(color, 0.0, 1.0);
}

vec3 setColorLuminance(vec3 color, float lum) {
    return clipColor(color + vec3(lum - blendLuminance(color)));
}

vec3 setColorSaturation(vec3 color, float sat) {
    float components[3] = float[3](color.r, color.g, color.b);
    int minIndex = 0;
    int maxIndex = 0;
    for (int i = 1; i < 3; ++i) {
        if (components[i] < components[minIndex]) {
            minIndex = i;
        }
        if (components[i] > components[maxIndex]) {
            maxIndex = i;
        }
    }

    if (components[maxIndex] <= components[minIndex]) {
        return vec3(0.0);
    }

    int midIndex = 3 - minIndex - maxIndex;
    float result[3];
    result[minIndex] = 0.0;
    result[midIndex] = ((components[midIndex] - components[minIndex]) * sat)
                     / (components[maxIndex] - components[minIndex]);
    result[maxIndex] = sat;
    return clamp(vec3(result[0], result[1], result[2]), 0.0, 1.0);
}

float blendVividLightComponent(float Cb, float Cs) {
    if (Cs < 0.5) {
        return blendColorBurnComponent(Cb, clamp(2.0 * Cs, 0.0, 1.0));
    }
    return blendColorDodgeComponent(Cb, clamp(2.0 * (Cs - 0.5), 0.0, 1.0));
}

vec3 blendVividLight(vec3 Cb, vec3 Cs) {
    return vec3(
        blendVividLightComponent(Cb.r, Cs.r),
        blendVividLightComponent(Cb.g, Cs.g),
        blendVividLightComponent(Cb.b, Cs.b)
    );
}

vec3 blendLinearLight(vec3 Cb, vec3 Cs) {
    return clamp(Cb + 2.0 * Cs - 1.0, 0.0, 1.0);
}

vec3 blendPinLight(vec3 Cb, vec3 Cs) {
    vec3 low = min(Cb, 2.0 * Cs);
    vec3 high = max(Cb, 2.0 * (Cs - 0.5));
    return mix(low, high, step(0.5, Cs));
}

vec3 blendHardMix(vec3 Cb, vec3 Cs) {
    return step(vec3(0.5), blendVividLight(Cb, Cs));
}

vec3 rgbToHsl(vec3 color) {
    float cMax = max(color.r, max(color.g, color.b));
    float cMin = min(color.r, min(color.g, color.b));
    float delta = cMax - cMin;
    float hue = 0.0;
    float saturation = 0.0;
    float lightness = 0.5 * (cMax + cMin);

    if (delta > 0.00001) {
        saturation = (lightness > 0.5)
            ? delta / (2.0 - cMax - cMin)
            : delta / max(cMax + cMin, 0.00001);

        if (cMax == color.r) {
            hue = (color.g - color.b) / delta + ((color.g < color.b) ? 6.0 : 0.0);
        } else if (cMax == color.g) {
            hue = (color.b - color.r) / delta + 2.0;
        } else {
            hue = (color.r - color.g) / delta + 4.0;
        }
        hue /= 6.0;
    }

    return vec3(hue, saturation, lightness);
}

float hueToRgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

vec3 hslToRgb(vec3 hsl) {
    float hue = fract(hsl.x);
    float saturation = clamp(hsl.y, 0.0, 1.0);
    float lightness = clamp(hsl.z, 0.0, 1.0);

    if (saturation <= 0.00001) {
        return vec3(lightness);
    }

    float q = (lightness < 0.5)
        ? lightness * (1.0 + saturation)
        : lightness + saturation - lightness * saturation;
    float p = 2.0 * lightness - q;

    return vec3(
        hueToRgb(p, q, hue + 1.0 / 3.0),
        hueToRgb(p, q, hue),
        hueToRgb(p, q, hue - 1.0 / 3.0)
    );
}

vec3 blendHue(vec3 Cb, vec3 Cs) {
    if (colorSaturation(Cs) <= 0.00001) {
        return Cb;
    }
    return setColorLuminance(setColorSaturation(Cs, colorSaturation(Cb)), blendLuminance(Cb));
}

vec3 blendSaturation(vec3 Cb, vec3 Cs) {
    return setColorLuminance(setColorSaturation(Cb, colorSaturation(Cs)), blendLuminance(Cb));
}

vec3 blendColor(vec3 Cb, vec3 Cs) {
    return setColorLuminance(Cs, blendLuminance(Cb));
}

vec3 blendLuminosity(vec3 Cb, vec3 Cs) {
    return setColorLuminance(Cb, blendLuminance(Cs));
}

vec3 blendByMode(vec3 Cb, vec3 Cs, int mode) {
    switch (mode) {
        case 0:  return blendNormal(Cb, Cs);
        case 1:  return blendMultiply(Cb, Cs);
        case 2:  return blendScreen(Cb, Cs);
        case 3:  return blendOverlay(Cb, Cs);
        case 4:  return blendSoftLight(Cb, Cs);
        case 5:  return blendHardLight(Cb, Cs);
        case 6:  return blendColorDodge(Cb, Cs);
        case 7:  return blendColorBurn(Cb, Cs);
        case 8:  return blendDarken(Cb, Cs);
        case 9:  return blendLighten(Cb, Cs);
        case 10: return blendDifference(Cb, Cs);
        case 11: return blendExclusion(Cb, Cs);
        case 12: return blendNormal(Cb, Cs);
        case 13: return blendLinearBurn(Cb, Cs);
        case 14: return blendDarkerColor(Cb, Cs);
        case 15: return blendLinearDodge(Cb, Cs);
        case 16: return blendLighterColor(Cb, Cs);
        case 17: return blendVividLight(Cb, Cs);
        case 18: return blendLinearLight(Cb, Cs);
        case 19: return blendPinLight(Cb, Cs);
        case 20: return blendHardMix(Cb, Cs);
        case 21: return blendSubtract(Cb, Cs);
        case 22: return blendDivide(Cb, Cs);
        case 23: return blendHue(Cb, Cs);
        case 24: return blendSaturation(Cb, Cs);
        case 25: return blendColor(Cb, Cs);
        case 26: return blendLuminosity(Cb, Cs);
        default: return blendNormal(Cb, Cs);
    }
}

float radialRevealFactor() {
    if (uUseRadialReveal == 0) {
        return 1.0;
    }

    vec2 sourceSize = vec2(textureSize(uSrcTexture, 0));
    vec2 worldPos = uTileWorldOrigin + fragTexCoord * sourceSize;
    float radius = max(uRadialRevealRadius, 0.0001);
    float feather = clamp(uRadialRevealFeather, 0.0001, radius);
    float innerRadius = max(0.0, radius - feather);
    float distanceToOrigin = distance(worldPos, uRadialRevealOrigin);
    float factor = smoothstep(innerRadius, radius, distanceToOrigin);
    factor = 1.0 - factor;
    if (uRadialRevealInvert != 0) {
        factor = 1.0 - factor;
    }
    return clamp(factor, 0.0, 1.0);
}

uint hashPixel(uvec2 value) {
    uint hash = value.x * 1597334677u + value.y * 3812015801u + 2246822519u;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;
    return hash;
}

float dissolveAlpha(float combinedAlpha) {
    if (combinedAlpha <= 0.0) {
        return 0.0;
    }
    if (combinedAlpha >= 1.0) {
        return 1.0;
    }

    vec2 sourceSize = vec2(textureSize(uSrcTexture, 0));
    vec2 worldPos = uTileWorldOrigin + fragTexCoord * sourceSize;
    ivec2 pixelPos = ivec2(floor(worldPos));
    float randomValue = float(hashPixel(uvec2(pixelPos)) & 0x00ffffffu) / 16777215.0;
    return (randomValue <= combinedAlpha) ? 1.0 : 0.0;
}

// Apply the soft-selection alpha cap to a final premultiplied color.
// Caller must ensure cap >= 0. No-op when uClipMaskAsAlphaCap is disabled or
// the result alpha is already at/under the cap.
vec4 applySelectionAlphaCap(vec4 c, float cap) {
    if (uClipMaskAsAlphaCap == 0) return c;
    if (c.a <= cap) return c;
    if (c.a <= 0.0) return c;
    float scale = cap / c.a;
    return vec4(clamp(c.rgb * scale, vec3(0.0), vec3(cap)), cap);
}

void main() {
    vec4 base = texture(uBaseTexture, fragTexCoord);
    vec4 src  = texture(uSrcTexture,  fragTexCoord);
    vec4 srcRaw = src;            // src before any mask reveal multiply
    float maskReveal = 1.0;       // scalar reveal for the adjustment mix path
    float clipAlpha = 1.0;
    vec4 clipTexel = vec4(0.0, 0.0, 0.0, 1.0);
    if (uUseClipMask != 0) {
        clipTexel = texture(uClipMaskTexture, fragTexCoord);
        clipAlpha = clipTexel.a;
    }

    // Soft-selection cap, early exit: pre-existing layer alpha already exceeds
    // the mask ceiling — preserve the pixel verbatim, no stroke contribution
    // is allowed to alter it. Implements the user-facing rule "if originally
    // there were already pixels above the cap, don't reduce them".
    if (uClipMaskAsAlphaCap != 0 && base.a > clipAlpha) {
        outColor = base;
        return;
    }

    float revealFactor = radialRevealFactor();

    if (uClipMaskEditPreview != 0) {
        // Live preview of (stroke over committed) mask, computed exactly per pixel.
        vec4 strokeTexel = clipTexel; // uClipMaskTexture bound to the stroke buffer
        vec4 committedTexel = texture(uClipMaskTexture2, fragTexCoord);
        float op = clamp(uClipMaskEditStrokeOpacity, 0.0, 1.0) * revealFactor;
        float strokeLum = dot(strokeTexel.rgb, kLumWeights) * op;
        float strokeCov = strokeTexel.a * op;
        float committedReveal =
            clamp(dot(committedTexel.rgb, kLumWeights) + (1.0 - committedTexel.a), 0.0, 1.0);
        float reveal = clamp(strokeLum + (1.0 - strokeCov) * committedReveal, 0.0, 1.0);
        maskReveal = reveal;
        src *= reveal;
    } else if (uClipMaskEditReplace != 0) {
        // Replace-mode preview: reveal = mix(committedReveal, strokeReveal, op).
        // uClipMaskTexture is the stroke buffer (or the committed tile where the
        // stroke has no tile, making the mix collapse to the committed reveal).
        vec4 strokeTexel = clipTexel;
        vec4 committedTexel = texture(uClipMaskTexture2, fragTexCoord);
        float op = clamp(uClipMaskEditStrokeOpacity, 0.0, 1.0) * revealFactor;
        float strokeReveal =
            clamp(dot(strokeTexel.rgb, kLumWeights) + (1.0 - strokeTexel.a), 0.0, 1.0);
        float committedReveal =
            clamp(dot(committedTexel.rgb, kLumWeights) + (1.0 - committedTexel.a), 0.0, 1.0);
        float reveal = mix(committedReveal, strokeReveal, op);
        maskReveal = reveal;
        src *= reveal;
    } else if (uClipMaskLuminanceReveal != 0) {
        // Layer mask: white paint reveals, black paint hides, no coverage reveals.
        float reveal = clamp(dot(clipTexel.rgb, kLumWeights) + (1.0 - clipTexel.a), 0.0, 1.0);
        maskReveal = reveal;
        src *= reveal;
    } else if (uSubtractClipRevealFromSrc != 0) {
        maskReveal = 1.0 - clipAlpha * revealFactor;
        src *= maskReveal;
    } else {
        if (uClipMaskAlphaOnly != 0) {
            src.a *= clipAlpha;
        } else {
            src *= clipAlpha;
        }
        src *= revealFactor;
        // For an adjustment, clip-to-below / radial reveal scales the mix factor.
        maskReveal = clipAlpha * revealFactor;
    }

    float opacity = clamp(uOpacity, 0.0, 1.0);

    if (uUseGroupComposite != 0) {
        vec4 passThrough = texture(uGroupPassThroughTexture, fragTexCoord);
        float sourceCoverage = clamp(
            texture(uGroupSourceCoverageTexture, fragTexCoord).a, 0.0, 1.0);
        float coverage = clamp(texture(uGroupCoverageTexture, fragTexCoord).a, 0.0, 1.0);
        float visibleCoverage = coverage * maskReveal;

        // A spatial effect moves coverage. Replace the union of the original
        // and effected footprints so the undistorted group cannot remain below
        // the effected result as a ghost copy.
        float effectInfluence = max(sourceCoverage, coverage);
        vec4 groupVisual = mix(passThrough, srcRaw, effectInfluence);
        groupVisual = mix(base, groupVisual, maskReveal);

        if (uBlendMode == 12) {
            float dissolve = dissolveAlpha(visibleCoverage * opacity);
            outColor = applySelectionAlphaCap(mix(base, groupVisual, dissolve), clipAlpha);
            return;
        }

        vec4 backdrop = vec4(uBackdropColor.rgb * uBackdropColor.a, uBackdropColor.a);
        vec4 visibleBase = base + backdrop * (1.0 - base.a);
        vec4 visibleGroup = groupVisual + backdrop * (1.0 - groupVisual.a);
        vec3 Cb = (visibleBase.a > 0.0) ? visibleBase.rgb / visibleBase.a : vec3(0.0);
        vec3 Cs = (visibleGroup.a > 0.0) ? visibleGroup.rgb / visibleGroup.a : vec3(0.0);
        vec3 modeColor = blendByMode(Cb, Cs, uBlendMode);
        vec4 modeVisual = vec4(modeColor * groupVisual.a, groupVisual.a);

        vec4 compositedGroup = (uBlendMode == 0)
            ? groupVisual
            : mix(groupVisual, modeVisual, visibleCoverage);
        outColor = applySelectionAlphaCap(mix(base, compositedGroup, opacity), clipAlpha);
        return;
    }

    if (uReplaceBase != 0) {
        if (uReplaceBaseMixReveal != 0) {
            // Adjustment layer: srcRaw is the fully effected base. Reveal scales
            // the mix between original base and effected result so reveal<1
            // preserves base instead of darkening it toward zero.
            outColor = applySelectionAlphaCap(
                mix(base, srcRaw, opacity * maskReveal), clipAlpha);
            return;
        }
        outColor = applySelectionAlphaCap(mix(base, src, opacity), clipAlpha);
        return;
    }

    float ab = base.a;
    float as_raw = src.a;
    float combinedAlpha = clamp(as_raw * opacity, 0.0, 1.0);
    float as = (uBlendMode == 12) ? dissolveAlpha(combinedAlpha) : combinedAlpha;
    vec3 Cs = (as_raw > 0.0) ? src.rgb / as_raw : vec3(0.0);

    if (uBlendMode == 100) {
        outColor = vec4(base.rgb * (1.0 - as), ab * (1.0 - as));
        return;
    }

    if (uUseProgrammaticBlendBase != 0) {
        vec4 blendBase = texture(uProgrammaticBlendBaseTexture, fragTexCoord);
        vec4 backdrop = vec4(uBackdropColor.rgb * uBackdropColor.a, uBackdropColor.a);
        vec4 visibleBlendBase = blendBase + backdrop * (1.0 - blendBase.a);
        float visibleBlendBaseAlpha = visibleBlendBase.a;
        vec3 Cb = (visibleBlendBaseAlpha > 0.0)
            ? visibleBlendBase.rgb / visibleBlendBaseAlpha
            : vec3(0.0);
        vec3 B = blendByMode(Cb, Cs, uBlendMode);
        vec3 strokeColor = mix(Cs, B, visibleBlendBaseAlpha);

        if (uPreserveBaseAlpha != 0) {
            vec3 CbLocked = (ab > 0.0) ? base.rgb / ab : vec3(0.0);
            vec3 CoLocked = ab * (as * strokeColor + (1.0 - as) * CbLocked);
            outColor = vec4(clamp(CoLocked, vec3(0.0), vec3(ab)), ab);
            return;
        }

        vec3 Co = as * strokeColor + (1.0 - as) * base.rgb;
        float ao = as + ab * (1.0 - as);
        outColor = applySelectionAlphaCap(vec4(clamp(Co, vec3(0.0), vec3(ao)), ao), clipAlpha);
        return;
    }

    if (uPreserveBaseAlpha != 0) {
        vec3 CbLocked = (ab > 0.0) ? base.rgb / ab : vec3(0.0);
        vec3 BLocked = blendByMode(CbLocked, Cs, uBlendMode);
        vec3 CoLocked = (uBlendMode == 0 || uBlendMode == 12)
            ? Cs * as + base.rgb * (1.0 - as)
            : as * ab * BLocked + (1.0 - as) * base.rgb;
        outColor = vec4(CoLocked, ab);
        return;
    }

    vec4 backdrop = vec4(uBackdropColor.rgb * uBackdropColor.a, uBackdropColor.a);
    vec4 visibleBase = base + backdrop * (1.0 - ab);
    float visibleBaseAlpha = visibleBase.a;

    vec3 Cb = (visibleBaseAlpha > 0.0) ? visibleBase.rgb / visibleBaseAlpha : vec3(0.0);

    vec3 B = blendByMode(Cb, Cs, uBlendMode);

    if (uSrcAtop != 0) {
        vec3 visibleCo = as * visibleBaseAlpha * B + (1.0 - as) * visibleBase.rgb;
        float visibleAo = visibleBaseAlpha;

        if (backdrop.a >= 0.99999) {
            outColor = applySelectionAlphaCap(vec4(visibleCo, visibleAo), clipAlpha);
            return;
        }

        float outAlpha = (backdrop.a > 0.0)
            ? clamp((visibleAo - backdrop.a) / max(1.0 - backdrop.a, 0.00001), 0.0, 1.0)
            : visibleAo;
        vec3 outRgb = visibleCo - backdrop.rgb * (1.0 - outAlpha);
        outColor = applySelectionAlphaCap(
            vec4(clamp(outRgb, vec3(0.0), vec3(outAlpha)), outAlpha), clipAlpha);
        return;
    }

    vec3 visibleCo = as * (1.0 - visibleBaseAlpha) * Cs
                   + as * visibleBaseAlpha * B
                   + (1.0 - as) * visibleBase.rgb;
    float visibleAo = as + visibleBaseAlpha * (1.0 - as);

    if (backdrop.a >= 0.99999) {
        outColor = applySelectionAlphaCap(vec4(visibleCo, visibleAo), clipAlpha);
        return;
    }

    float ao = (backdrop.a > 0.0)
        ? clamp((visibleAo - backdrop.a) / max(1.0 - backdrop.a, 0.00001), 0.0, 1.0)
        : visibleAo;
    vec3 Co = visibleCo - backdrop.rgb * (1.0 - ao);
    outColor = applySelectionAlphaCap(vec4(clamp(Co, vec3(0.0), vec3(ao)), ao), clipAlpha);
}
