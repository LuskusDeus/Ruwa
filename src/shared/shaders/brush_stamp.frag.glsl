// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   S T A M P   F R A G M E N T
// ==========================================================================
//   Computes brush falloff per-pixel and outputs premultiplied RGBA.
//   Blend mode is selected by renderer (MAX or src-over).

#version 450 core

uniform vec2  uBrushCenter;   // brush center in tile-local pixel coords
uniform float uBrushRadius;   // brush radius in pixels
uniform float uBrushHardness; // hardness [0..1]
uniform float uBrushRoundness; // 0..1 (1 = circle)
uniform float uBrushAngleRad;  // angle in radians
uniform vec3  uBrushColorRGB; // normalized brush color (r/255, g/255, b/255)
uniform float uBrushAlpha;    // brush opacity (a/255) [0..1]
uniform sampler2D uMaskTexture;
uniform sampler2D uTextureTile;
uniform sampler2D uDabShapeTexture;
uniform int   uUseMask;
uniform int   uMaskAffectsAlpha;
uniform int   uUseTexture;
uniform int   uUseDabShapeTexture;
uniform vec2  uDabShapeScale;
uniform float uTextureEdgeBoost;
uniform float uDabSoftEdgeRadiusTexels;
uniform float uInvTileSize;

in vec2 fragPixelCoord;       // tile-local pixel position (from vertex shader)

out vec4 outColor;

float hardnessFalloff(float edgeDistance, float hardness) {
    hardness = clamp(hardness, 0.0, 1.0);
    float softness = max(1.0 - hardness, 0.0);
    if (edgeDistance <= 0.0) {
        return 0.0;
    }
    if (softness <= 0.001) {
        return 1.0;
    }
    return smoothstep(0.0, softness, edgeDistance);
}

vec2 sampleDabShapeSafe(vec2 uv) {
    vec2 clampedUv = clamp(uv, vec2(0.0), vec2(1.0));
    vec2 shape = texture(uDabShapeTexture, clampedUv).rg;
    vec2 outsideUv = max(max(-uv, uv - vec2(1.0)), vec2(0.0)) * 2.0;
    if (outsideUv.x > 0.0 || outsideUv.y > 0.0) {
        shape.r = 0.0;
        shape.g = clamp(shape.g + length(outsideUv), 0.0, 1.0);
    }
    return shape;
}

float customDabSoftEdgeShapePad(float hardness) {
    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);
    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));
    vec2 safeSize = max(texSize - vec2(1.0), vec2(1.0));
    vec2 pad = (2.0 * uDabSoftEdgeRadiusTexels * softness) / safeSize;
    return max(pad.x, pad.y);
}

float sampleCustomDabEdgeFalloff(vec2 uv, float hardness) {
    vec2 shape = sampleDabShapeSafe(uv);
    float baseAlpha = clamp(shape.r, 0.0, 1.0);
    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);
    if (softness <= 0.0001) {
        return baseAlpha;
    }

    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));
    float edgeWidth = (2.0 * uDabSoftEdgeRadiusTexels * softness) / max(max(texSize.x, texSize.y), 1.0);
    if (edgeWidth <= 0.0001) {
        return baseAlpha;
    }

    float halfTexel = 1.0 / max(max(texSize.x, texSize.y), 1.0);
    float edgeDistance = max(shape.g - halfTexel, 0.0);
    float signedDistance = baseAlpha >= 0.5 ? edgeDistance : -edgeDistance;
    float coverage = smoothstep(0.0, 1.0, (signedDistance + edgeWidth) / (2.0 * edgeWidth));
    return clamp(mix(coverage, baseAlpha, clamp(hardness, 0.0, 1.0)), 0.0, 1.0);
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

    vec2 shapeLocal = local / uBrushRadius;
    shapeLocal /= max(uDabShapeScale, vec2(0.0001));
    float edgeDistance = 0.0;
    float edgeFactor = 0.0;

    if (uUseDabShapeTexture != 0) {
        float shapePad = customDabSoftEdgeShapePad(uBrushHardness);
        if (abs(shapeLocal.x) > 1.0 + shapePad || abs(shapeLocal.y) > 1.0 + shapePad) {
            outColor = vec4(0.0);
            return;
        }

        vec2 uv = (shapeLocal + 1.0) * 0.5;
        float baseAlpha = sampleDabShapeSafe(uv).r;
        float falloff = sampleCustomDabEdgeFalloff(uv, uBrushHardness);
        if (falloff <= 0.0) {
            outColor = vec4(0.0);
            return;
        }
        edgeFactor = max(0.0, falloff - baseAlpha);
        float alpha = uBrushAlpha * falloff;
        float maskScale = 1.0;
        if (uUseMask != 0) {
            float maskA = texture(uMaskTexture, fragPixelCoord * uInvTileSize).a;
            if (maskA <= 0.0) {
                outColor = vec4(0.0);
                return;
            }
            maskScale = maskA;
        }
        if (uUseTexture != 0) {
            float textureA = texture(uTextureTile, fragPixelCoord * uInvTileSize).r;
            if (uTextureEdgeBoost > 0.0) {
                float contrast = 1.0 + edgeFactor * uTextureEdgeBoost * 8.0;
                textureA = clamp(0.5 + (textureA - 0.5) * contrast, 0.0, 1.0);
            }
            alpha *= textureA;
            if (alpha <= 0.0) {
                outColor = vec4(0.0);
                return;
            }
        }

        float colorScale = maskScale;
        if (uMaskAffectsAlpha != 0) {
            colorScale = 1.0;
        }
        outColor = vec4(uBrushColorRGB * alpha * colorScale, alpha);
        return;
    } else {
        float t = length(local) / uBrushRadius;
        if (t > 1.0) {
            outColor = vec4(0.0);
            return;
        }

        edgeDistance = max(0.0, 1.0 - t);
        edgeFactor = smoothstep(clamp(uBrushHardness + 0.05, 0.05, 0.95), 1.0, t);
    }

    float falloff = hardnessFalloff(edgeDistance, uBrushHardness);
    float alpha = uBrushAlpha * falloff;
    float maskScale = 1.0;
    if (uUseMask != 0) {
        float maskA = texture(uMaskTexture, fragPixelCoord * uInvTileSize).a;
        if (maskA <= 0.0) {
            outColor = vec4(0.0);
            return;
        }
        maskScale = maskA;
    }
    if (uUseTexture != 0) {
        float textureA = texture(uTextureTile, fragPixelCoord * uInvTileSize).r;
        if (uTextureEdgeBoost > 0.0) {
            float contrast = 1.0 + edgeFactor * uTextureEdgeBoost * 8.0;
            textureA = clamp(0.5 + (textureA - 0.5) * contrast, 0.0, 1.0);
        }
        alpha *= textureA;
        if (alpha <= 0.0) {
            outColor = vec4(0.0);
            return;
        }
    }

    float colorScale = maskScale;
    if (uMaskAffectsAlpha != 0) {
        colorScale = 1.0;
    }
    outColor = vec4(uBrushColorRGB * alpha * colorScale, alpha);
}
