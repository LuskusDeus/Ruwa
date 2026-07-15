#version 450 core
// SPDX-License-Identifier: MPL-2.0

uniform sampler2D uSourceAtlasTexture;
uniform sampler2D uTargetBaseTexture;
uniform sampler2D uSelectionMaskAtlasTexture;
uniform int uSourceIsScreenTexture;
uniform int uUseSelectionMask;
uniform int uPreserveMaskedSource;
uniform int uTransformMode; // 0 = affine, 1 = free quad (deform mesh uses a separate forward-rasterized pipeline)
uniform vec2 uViewportSize;
uniform vec2 uSourceTextureSize;
uniform vec2 uSourceScreenOffset;
uniform vec2 uTargetBaseTextureSize;
uniform vec2 uTargetBaseScreenOffset;
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
uniform mat3 uInverseTransform;
uniform vec4 uContentBounds;
uniform vec2 uQ0;
uniform vec2 uQ1;
uniform vec2 uQ2;
uniform vec2 uQ3;
// Premultiplied background the source grid carries everywhere it has no tile
// (the grid's default fill). For a hide-all layer mask this is opaque black, so
// the area the moved mask vacates / never covered stays hidden instead of
// reading transparent (= fully revealed). Transparent (0) for normal grids, so
// content transforms are unaffected.
uniform vec4 uSourceBackgroundColor;

in vec2 fragTexCoord;
out vec4 outColor;

float cross2d(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

float roundedRectCoverage(vec2 pixelPos, vec2 rectSize, float radius) {
    if (radius <= 0.0 || rectSize.x <= 0.0 || rectSize.y <= 0.0) {
        return 1.0;
    }

    float clampedRadius = min(radius, 0.5 * min(rectSize.x, rectSize.y));
    vec2 halfSize = rectSize * 0.5;
    vec2 centeredPos = pixelPos - halfSize;
    vec2 q = abs(centeredPos) - (halfSize - vec2(clampedRadius));
    float signedDistance =
        length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - clampedRadius;
    float edgeWidth = max(fwidth(signedDistance), 0.0001);
    return 1.0 - smoothstep(0.0, edgeWidth, signedDistance);
}

float canvasClipCoverage(vec2 pixelPos) {
    if (uClipToCanvas == 0 || uCanvasSize.x <= 0.0 || uCanvasSize.y <= 0.0) {
        return 1.0;
    }

    float edgePadding = 1.0 / max(uCameraZoom, 0.0001);
    if (pixelPos.x < -edgePadding || pixelPos.y < -edgePadding
        || pixelPos.x > uCanvasSize.x + edgePadding
        || pixelPos.y > uCanvasSize.y + edgePadding) {
        return 0.0;
    }

    vec2 clipPos = clamp(pixelPos, vec2(0.0), uCanvasSize);
    return roundedRectCoverage(clipPos, uCanvasSize, uCanvasCornerRadius);
}

vec2 mirrorWorldInCanvas(vec2 worldPos) {
    vec2 mirrored = worldPos;
    vec2 center = uCanvasSize * 0.5;
    if (uFlipH != 0) {
        mirrored.x = 2.0 * center.x - mirrored.x;
    }
    if (uFlipV != 0) {
        mirrored.y = 2.0 * center.y - mirrored.y;
    }
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
            return uSourceBackgroundColor;
        }
        // +0.5 shifts pixel-index screen coords to pixel-center for sampling
        // texel centers; without it bilinear pulls in the half-texel neighbor,
        // displacing the displayed content by 0.5px down-right and leaving a
        // half-pixel gap at the top and left edges of the preview.
        return texture(uSourceAtlasTexture, vec2((sourceScreen.x + 0.5) / sourceSize.x,
                                                 1.0 - (sourceScreen.y + 0.5) / sourceSize.y));
    }

    vec2 atlasPixel = worldPos - uAtlasMinTile * uTileSize;
    if (atlasPixel.x < -0.5 || atlasPixel.y < -0.5
        || atlasPixel.x > uAtlasSize.x + 0.5 || atlasPixel.y > uAtlasSize.y + 0.5) {
        return uSourceBackgroundColor;
    }
    // No +0.5 here: atlasPixel is already a continuous world coordinate (one
    // texel per canvas pixel), so atlasPixel/uAtlasSize samples the true point.
    // This matches the bake path (kTransformFrag sampleAtlas); adding +0.5 here
    // shifts the moved content half a texel and desyncs it from the mask clip,
    // producing the sub-pixel seam at the selection edge that grows with zoom.
    return texture(uSourceAtlasTexture, atlasPixel / uAtlasSize);
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

vec4 sampleTransformedSourceAtlas(vec2 worldPos) {
    vec2 minCenter = uContentBounds.xy + vec2(0.5);
    vec2 maxCenter = uContentBounds.zw - vec2(0.5);
    vec2 safeMin = min(minCenter, maxCenter);
    vec2 safeMax = max(minCenter, maxCenter);
    vec2 outside = max(max(safeMin - worldPos,
                           worldPos - safeMax), vec2(0.0));
    if (outside.x > 0.5 || outside.y > 0.5) {
        return uSourceBackgroundColor;
    }
    return sampleSourceAtlas(clamp(worldPos, safeMin, safeMax));
}

float sampleSelectionMask(vec2 worldPos) {
    if (uUseSelectionMask == 0) {
        return 1.0;
    }
    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;
    if (maskPixel.x < 0.0 || maskPixel.y < 0.0
        || maskPixel.x >= uMaskAtlasSize.x || maskPixel.y >= uMaskAtlasSize.y) {
        return 0.0;
    }
    return texture(uSelectionMaskAtlasTexture, maskPixel / uMaskAtlasSize).a;
}

// Carve mask, dilated by ~1 screen pixel. The base (uTargetBaseTexture) is the
// layer rendered into a screen-space texture and sampled with LINEAR filtering,
// so its content edge is anti-aliased over ~1px. Carving it with the hard
// (NEAREST) selection mask leaves that soft fringe uncarved, which shows as a
// 1px "ghost" frame at the selection's previous position against empty canvas.
// Dilating the carve coverage by one screen pixel removes that fringe too. Only
// used for the base carve; the moved-content clip (m0) must stay un-dilated.
float sampleSelectionMaskCarve(vec2 worldPos) {
    if (uUseSelectionMask == 0) {
        return 1.0;
    }
    float step = 1.0 / max(uCameraZoom, 0.0001);
    float m = sampleSelectionMask(worldPos);
    m = max(m, sampleSelectionMask(worldPos + vec2( step, 0.0)));
    m = max(m, sampleSelectionMask(worldPos + vec2(-step, 0.0)));
    m = max(m, sampleSelectionMask(worldPos + vec2(0.0,  step)));
    m = max(m, sampleSelectionMask(worldPos + vec2(0.0, -step)));
    m = max(m, sampleSelectionMask(worldPos + vec2( step,  step)));
    m = max(m, sampleSelectionMask(worldPos + vec2( step, -step)));
    m = max(m, sampleSelectionMask(worldPos + vec2(-step,  step)));
    m = max(m, sampleSelectionMask(worldPos + vec2(-step, -step)));
    return m;
}

bool trySolveST(vec2 E, vec2 F, vec2 G, vec2 h, float t, float margin, out vec2 st) {
    if (t < -margin || t > 1.0 + margin) return false;
    vec2 denom = E + G * t;
    float s;
    if (abs(denom.x) > abs(denom.y)) {
        if (abs(denom.x) < 1e-8) return false;
        s = (h.x - F.x * t) / denom.x;
    } else {
        if (abs(denom.y) < 1e-8) return false;
        s = (h.y - F.y * t) / denom.y;
    }
    if (s < -margin || s > 1.0 + margin) return false;
    st = clamp(vec2(s, t), 0.0, 1.0);
    return true;
}

int inverseBilinearAll(vec2 P, out vec2 st0, out vec2 st1) {
    vec2 E0 = uQ1 - uQ0;
    vec2 F0 = uQ3 - uQ0;
    float quadScale = max(max(length(E0), length(F0)),
                          max(length(uQ2 - uQ1), length(uQ2 - uQ3)));
    float invScale = 1.0 / max(quadScale, 1e-6);
    vec2 E = E0 * invScale;
    vec2 F = F0 * invScale;
    vec2 G = (uQ0 - uQ1 + uQ2 - uQ3) * invScale;
    vec2 h = (P - uQ0) * invScale;

    float k2 = cross2d(G, F);
    float k1 = cross2d(E, F) + cross2d(h, G);
    float k0 = cross2d(h, E);

    const float margin = 0.002;

    float tCands[2];
    int nCands = 0;

    if (abs(k2) < 1e-8) {
        if (abs(k1) < 1e-8) {
            return 0;
        }
        tCands[0] = -k0 / k1;
        nCands = 1;
    } else {
        float disc = k1 * k1 - 4.0 * k0 * k2;
        if (disc < 0.0) {
            return 0;
        }
        float sq = sqrt(disc);
        float signK1 = (k1 >= 0.0) ? 1.0 : -1.0;
        float qStable = -0.5 * (k1 + signK1 * sq);
        tCands[0] = qStable / k2;
        tCands[1] = (abs(qStable) > 1e-8) ? (k0 / qStable) : tCands[0];
        nCands = 2;
    }

    int count = 0;
    vec2 results[2];
    for (int i = 0; i < 2; ++i) {
        if (i >= nCands) break;
        vec2 cand;
        if (!trySolveST(E, F, G, h, tCands[i], margin, cand)) continue;
        bool dup = false;
        for (int j = 0; j < count; ++j) {
            if (abs(results[j].x - cand.x) < 1e-4 && abs(results[j].y - cand.y) < 1e-4) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        results[count] = cand;
        count++;
    }
    if (count >= 1) st0 = results[0];
    if (count >= 2) st1 = results[1];
    return count;
}


vec2 stToSrc(vec2 st) {
    return vec2(
        mix(uContentBounds.x, uContentBounds.z, st.x),
        mix(uContentBounds.y, uContentBounds.w, st.y)
    );
}

int computeSourceWorldAll(vec2 destWorld, out vec2 srcWorld0, out vec2 srcWorld1) {
    // Deform (mode 2) is no longer handled here — it goes through a separate
    // forward-rasterized pipeline (transform_deform_mesh.* shaders). This
    // shader serves only affine (mode 0) and free-quad (mode 1).
    if (uTransformMode == 1) {
        vec2 st0, st1;
        int n = inverseBilinearAll(destWorld, st0, st1);
        if (n >= 1) srcWorld0 = stToSrc(st0);
        if (n >= 2) srcWorld1 = stToSrc(st1);
        return n;
    }

    vec3 srcWorldH = uInverseTransform * vec3(destWorld, 1.0);
    srcWorld0 = srcWorldH.xy;
    return 1;
}

void main() {
    vec2 destScreen = vec2(
        gl_FragCoord.x - 0.5,
        uViewportSize.y - gl_FragCoord.y - 0.5
    );
    vec2 destWorld = documentWorldFromScreen(destScreen);
    float canvasCoverage = canvasClipCoverage(destWorld);
    if (canvasCoverage <= 0.0) {
        outColor = vec4(0.0);
        return;
    }

    vec2 srcWorld0 = vec2(0.0);
    vec2 srcWorld1 = vec2(0.0);
    int srcCount = computeSourceWorldAll(destWorld, srcWorld0, srcWorld1);

    vec4 c0 = (srcCount >= 1) ? sampleTransformedSourceAtlas(srcWorld0) : vec4(0.0);
    vec4 c1 = (srcCount >= 2) ? sampleTransformedSourceAtlas(srcWorld1) : vec4(0.0);

    if (uUseSelectionMask != 0) {
        float m0 = (srcCount >= 1) ? clamp(sampleSelectionMask(srcWorld0), 0.0, 1.0) : 0.0;
        float m1 = (srcCount >= 2) ? clamp(sampleSelectionMask(srcWorld1), 0.0, 1.0) : 0.0;
        c0 *= m0;
        c1 *= m1;
    }

    // Composite both bilinear branches: front (c0) over back (c1).
    vec4 transformedColor = c0 + c1 * (1.0 - c0.a);

    if (uUseSelectionMask == 0) {
        outColor = transformedColor * canvasCoverage;
        return;
    }

    vec4 baseColor = sampleTargetBase(destScreen);
    if (uPreserveMaskedSource == 0) {
        // Carve the base where the selection content was lifted. The hard
        // (un-dilated) mask is the geometrically correct carve and keeps an
        // in-place flip seamless (carve region == content fill region, so no
        // transparent gap ring). The dilated carve is applied ONLY across the
        // base's own anti-aliased fringe (base.a < 1) — that is the 1px "ghost"
        // of a lifted edge against empty canvas on a move; on solid surrounding
        // pixels (base.a == 1) it must NOT extend, or it eats a 1px ring the
        // moved content does not refill.
        float carveHard = clamp(sampleSelectionMask(destWorld), 0.0, 1.0);
        float carveDilated = clamp(sampleSelectionMaskCarve(destWorld), 0.0, 1.0);
        float carve = max(carveHard, carveDilated * (1.0 - baseColor.a));
        // Carve toward the source grid's background, not transparent: where the
        // selection content was lifted, a hide-all mask must leave its opaque
        // black background (reveal 0 = content stays hidden), matching commit.
        // For normal grids the background is transparent, so this reduces exactly
        // to the old baseColor *= (1.0 - carve).
        baseColor = mix(baseColor, uSourceBackgroundColor, carve);
    }

    outColor = (transformedColor + baseColor * (1.0 - transformedColor.a)) * canvasCoverage;
}
