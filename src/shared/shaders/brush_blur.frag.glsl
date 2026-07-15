// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   B L U R   F R A G M E N T
// ==========================================================================
//   Isotropic Gaussian blur with cross-tile sampling.
//   Outputs the FINAL pixel value (mix of original and blurred) so the
//   stroke buffer can replace the layer tile content at flatten time.

#version 450 core

uniform vec2  uBrushCenter;     // brush center in tile-local pixel coords
uniform float uBrushRadius;     // brush radius in pixels
uniform float uBrushHardness;   // hardness [0..1]
uniform float uBrushRoundness;  // 0..1 (1 = circle)
uniform float uBrushAngleRad;   // angle in radians
uniform float uBrushAlpha;      // brush opacity (a/255) [0..1]

uniform sampler2D uLayerTexture; // current layer tile (premultiplied RGBA)
uniform sampler2D uNeighborL;    // left neighbor tile
uniform sampler2D uNeighborR;    // right neighbor tile
uniform sampler2D uNeighborT;    // top neighbor tile
uniform sampler2D uNeighborB;    // bottom neighbor tile
uniform sampler2D uNeighborTL;   // top-left neighbor tile
uniform sampler2D uNeighborTR;   // top-right neighbor tile
uniform sampler2D uNeighborBL;   // bottom-left neighbor tile
uniform sampler2D uNeighborBR;   // bottom-right neighbor tile

uniform int   uHasNeighborL;
uniform int   uHasNeighborR;
uniform int   uHasNeighborT;
uniform int   uHasNeighborB;
uniform int   uHasNeighborTL;
uniform int   uHasNeighborTR;
uniform int   uHasNeighborBL;
uniform int   uHasNeighborBR;

uniform float uBlurRadius;      // max blur kernel radius in pixels
uniform float uInvTileSize;     // 1.0 / TILE_SIZE
uniform float uTileSize;        // TILE_SIZE as float
uniform vec2  uTileOriginPx;    // tile origin in document pixel coords

in vec2 fragPixelCoord;
out vec4 outColor;

vec2 pixelCoordToUv(vec2 pixelCoord) {
    return pixelCoord * uInvTileSize;
}

float stableDither(vec2 pixelCoord) {
    vec2 p = floor(uTileOriginPx + pixelCoord);
    float n = fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
    return n - 0.5;
}

// Sample a pixel from the tile neighborhood.
// pixelCoord is in tile-local space: [0, TILE_SIZE) for the current tile.
vec4 sampleNeighborhood(vec2 pixelCoord) {
    float ts = uTileSize;
    vec2 uv;

    // Current tile — the common fast path
    if (pixelCoord.x >= 0.0 && pixelCoord.x < ts &&
        pixelCoord.y >= 0.0 && pixelCoord.y < ts) {
        return texture(uLayerTexture, pixelCoordToUv(pixelCoord));
    }

    // Cardinal neighbors
    if (pixelCoord.x < 0.0 && pixelCoord.y >= 0.0 && pixelCoord.y < ts) {
        if (uHasNeighborL == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x + ts, pixelCoord.y));
        return texture(uNeighborL, uv);
    }
    if (pixelCoord.x >= ts && pixelCoord.y >= 0.0 && pixelCoord.y < ts) {
        if (uHasNeighborR == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x - ts, pixelCoord.y));
        return texture(uNeighborR, uv);
    }
    if (pixelCoord.y < 0.0 && pixelCoord.x >= 0.0 && pixelCoord.x < ts) {
        if (uHasNeighborT == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x, pixelCoord.y + ts));
        return texture(uNeighborT, uv);
    }
    if (pixelCoord.y >= ts && pixelCoord.x >= 0.0 && pixelCoord.x < ts) {
        if (uHasNeighborB == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x, pixelCoord.y - ts));
        return texture(uNeighborB, uv);
    }

    // Diagonal corners — return transparent (negligible visual impact)
    if (pixelCoord.x < 0.0 && pixelCoord.y < 0.0) {
        if (uHasNeighborTL == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x + ts, pixelCoord.y + ts));
        return texture(uNeighborTL, uv);
    }
    if (pixelCoord.x >= ts && pixelCoord.y < 0.0) {
        if (uHasNeighborTR == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x - ts, pixelCoord.y + ts));
        return texture(uNeighborTR, uv);
    }
    if (pixelCoord.x < 0.0 && pixelCoord.y >= ts) {
        if (uHasNeighborBL == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x + ts, pixelCoord.y - ts));
        return texture(uNeighborBL, uv);
    }
    if (pixelCoord.x >= ts && pixelCoord.y >= ts) {
        if (uHasNeighborBR == 0) return vec4(0.0);
        uv = pixelCoordToUv(vec2(pixelCoord.x - ts, pixelCoord.y - ts));
        return texture(uNeighborBR, uv);
    }

    return vec4(0.0);
}

void main() {
    // Sample the original pixel at this fragment position
    vec4 original = texture(uLayerTexture, pixelCoordToUv(fragPixelCoord));

    // Brush ellipse distance test
    vec2 delta = fragPixelCoord - uBrushCenter;
    float c = cos(uBrushAngleRad);
    float s = sin(uBrushAngleRad);
    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));
    vec2 local = vec2(
        delta.x * c + delta.y * s,
        (-delta.x * s + delta.y * c) / roundness
    );
    float t = length(local) / uBrushRadius;

    if (t > 1.0) {
        // Outside brush — keep original pixel unchanged
        outColor = original;
        return;
    }

    // Brush falloff (matches brush stamp hardness curve)
    float edgeDistance = max(0.0, 1.0 - t);
    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);
    float falloff = softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, edgeDistance);
    if (falloff <= 0.0) {
        outColor = original;
        return;
    }

    // Effective blur parameters modulated by brush falloff
    float effRadius = uBlurRadius * falloff;
    float intensity = uBrushAlpha * falloff;

    // Gaussian sigma — half the effective radius gives good coverage
    float sigma = max(effRadius * 0.5, 0.5);
    float invTwoSigma2 = 1.0 / (2.0 * sigma * sigma);

    // Use a discrete 1px Gaussian kernel with pair sampling.
    // This removes the visible "banding staircase" on transparent edges
    // that appears when a single click uses only a coarse sparse kernel.
    const int kMaxHalf = 24;
    const int kMaxPairs = 25;
    int supportHalf = int(ceil(min(effRadius * 1.22474487139, float(kMaxHalf))));
    supportHalf = max(supportHalf, 1);

    float axisOffsets[kMaxPairs];
    float axisWeights[kMaxPairs];
    int sampleCount = 0;

    axisOffsets[sampleCount] = 0.0;
    axisWeights[sampleCount] = 1.0;
    ++sampleCount;

    for (int tap = 1; tap <= supportHalf && sampleCount + 1 < kMaxPairs; tap += 2) {
        float pairOffset = float(tap);
        float pairWeight = exp(-(pairOffset * pairOffset) * invTwoSigma2);

        if (tap < supportHalf) {
            float nextOffset = float(tap + 1);
            float nextWeight = exp(-(nextOffset * nextOffset) * invTwoSigma2);
            pairWeight += nextWeight;
            pairOffset += nextWeight / pairWeight;
        }

        axisOffsets[sampleCount] = -pairOffset;
        axisWeights[sampleCount] = pairWeight;
        ++sampleCount;

        axisOffsets[sampleCount] = pairOffset;
        axisWeights[sampleCount] = pairWeight;
        ++sampleCount;
    }

    // Blur premultiplied RGBA directly, including alpha.
    // Keeping the blurred alpha allows the stroke to spread outward into
    // transparent space instead of collapsing inward on the existing edge.
    vec4 blurred = vec4(0.0);
    float totalWeight = 0.0;

    for (int iy = 0; iy < kMaxPairs; ++iy) {
        if (iy >= sampleCount) continue;
        float offsetY = axisOffsets[iy];
        float weightY = axisWeights[iy];
        for (int ix = 0; ix < kMaxPairs; ++ix) {
            if (ix >= sampleCount) continue;
            float offsetX = axisOffsets[ix];
            vec2 offset = vec2(offsetX, offsetY);
            float w = axisWeights[ix] * weightY;
            vec4 texel = sampleNeighborhood(fragPixelCoord + offset);

            blurred += texel * w;
            totalWeight += w;
        }
    }

    if (totalWeight > 0.0) {
        blurred /= totalWeight;
    } else {
        outColor = original;
        return;
    }

    // Output the FINAL pixel: lerp between original and blurred.
    // This is a replacement value, not an overlay.
    outColor = mix(original, blurred, intensity);

    // Single-click blur can show visible banding on smooth alpha ramps because
    // the stroke buffer is RGBA8. Apply a tiny stable dither in document space
    // before quantization so the gradient reads as continuous instead of stepped.
    if (outColor.a > 0.0 && outColor.a < 1.0) {
        float dither = stableDither(fragPixelCoord) / 255.0;
        float oldAlpha = outColor.a;
        float newAlpha = clamp(oldAlpha + dither, 0.0, 1.0);
        if (oldAlpha > 1e-6) {
            vec3 straightColor = outColor.rgb / oldAlpha;
            outColor = vec4(straightColor * newAlpha, newAlpha);
        } else {
            outColor.a = newAlpha;
        }
    }
}
