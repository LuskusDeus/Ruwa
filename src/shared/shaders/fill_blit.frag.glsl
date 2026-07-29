#version 450 core
// SPDX-License-Identifier: MPL-2.0

uniform sampler2D uTileTexture;
uniform int uUseCanvasClip;
uniform vec2 uViewportSize;
uniform vec2 uCameraPosition;
uniform float uCameraZoom;
uniform float uCameraRotation;
uniform vec2 uCanvasSize;
uniform float uCanvasCornerRadius;

uniform sampler2D uLassoMask;
uniform int uUseLassoMask;
uniform ivec2 uLassoMaskOrigin;
uniform ivec2 uLassoMaskSize;

// When 1, output the source as a coverage-weighted REPLACE instead of a src-over.
// alpha is set to the coverage (not the source's own alpha) so that, with
// GL_ONE / GL_ONE_MINUS_SRC_ALPHA, the result is dst = src*cov + scene*(1-cov):
// a true lerp toward the source. Without this, a source with alpha < 1 (e.g. a
// layer at < 100% opacity) lets the already-composited scene bleed through and
// double-composites in the covered region. (The destination alpha is irrelevant
// here — the default framebuffer's alpha is force-cleared to 1 afterwards.)
uniform int uReplaceWithCoverage;
uniform int uUseRadialReveal;
uniform vec2 uRadialRevealOrigin;
uniform float uRadialRevealRadius;
uniform float uRadialRevealFeather;
uniform int uContentFlipH;
uniform int uContentFlipV;
uniform int uUseCheckerBackdrop;
uniform int uCheckerDocumentSpace;
uniform vec4 uCheckerColor1;
uniform vec4 uCheckerColor2;
uniform vec4 uCheckerViewportColor;
uniform float uCheckerSize;

in vec2 fragTexCoord;

out vec4 outColor;

float lassoMaskCoverage() {
    if (uUseLassoMask == 0 || uLassoMaskSize.x <= 0 || uLassoMaskSize.y <= 0) {
        return 1.0;
    }
    vec2 screenPixel = vec2(gl_FragCoord.x - 0.5,
                            uViewportSize.y - gl_FragCoord.y - 0.5);
    vec2 maskPixel = screenPixel - vec2(uLassoMaskOrigin);
    if (maskPixel.x < 0.0 || maskPixel.y < 0.0
        || maskPixel.x >= float(uLassoMaskSize.x)
        || maskPixel.y >= float(uLassoMaskSize.y)) {
        return 0.0;
    }
    return texelFetch(uLassoMask, ivec2(maskPixel), 0).r;
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

vec2 documentWorldFromScreen(vec2 screenPixel) {
    vec2 screenOffset = screenPixel - uViewportSize * 0.5;
    vec2 scaledOffset = screenOffset / max(uCameraZoom, 0.0001);
    float c = cos(uCameraRotation);
    float s = sin(uCameraRotation);
    vec2 worldOffset = vec2(
        scaledOffset.x * c + scaledOffset.y * s,
        -scaledOffset.x * s + scaledOffset.y * c
    );
    vec2 documentPos = uCameraPosition + worldOffset;
    if (uCanvasSize.x > 0.0 && uContentFlipH != 0) {
        documentPos.x = uCanvasSize.x - documentPos.x;
    }
    if (uCanvasSize.y > 0.0 && uContentFlipV != 0) {
        documentPos.y = uCanvasSize.y - documentPos.y;
    }
    return documentPos;
}

float radialRevealCoverage() {
    if (uUseRadialReveal == 0) {
        return 1.0;
    }

    vec2 screenPixel = vec2(gl_FragCoord.x, uViewportSize.y - gl_FragCoord.y);
    vec2 documentPos = documentWorldFromScreen(screenPixel);
    float radius = max(uRadialRevealRadius, 0.0001);
    float feather = clamp(uRadialRevealFeather, 0.0001, radius);
    float innerRadius = max(0.0, radius - feather);
    return 1.0 - smoothstep(
        innerRadius, radius, distance(documentPos, uRadialRevealOrigin));
}

vec4 checkerBackdrop(vec2 screenPixel) {
    vec2 checkerPixel = uCheckerDocumentSpace != 0
        ? documentWorldFromScreen(screenPixel)
        : screenPixel;
    vec2 cell = floor(checkerPixel / max(uCheckerSize, 0.0001));
    float checker = mod(cell.x + cell.y, 2.0);
    vec4 checkerStraight = mix(uCheckerColor1, uCheckerColor2, checker);
    vec4 viewportPremul = vec4(
        uCheckerViewportColor.rgb * uCheckerViewportColor.a, uCheckerViewportColor.a);
    vec4 checkerPremul = vec4(
        checkerStraight.rgb * checkerStraight.a, checkerStraight.a);
    return checkerPremul + viewportPremul * (1.0 - checkerPremul.a);
}

float canvasClipCoverage() {
    if (uUseCanvasClip == 0 || uCanvasSize.x <= 0.0 || uCanvasSize.y <= 0.0) {
        return 1.0;
    }

    // Use pixel CENTER worldPos to match tile rendering's quad-coverage rule:
    // a pixel is drawn iff its center falls inside the canvas. The previous
    // pixel-index (corner) convention is half a pixel too tight and at certain
    // zooms/camera positions rejects pixels whose centers are inside the canvas
    // but whose corners are outside, producing a 1-pixel-wide gap around the
    // preview fill that the committed layer does not have.
    vec2 screenPixel = vec2(
        gl_FragCoord.x,
        uViewportSize.y - gl_FragCoord.y
    );
    vec2 worldPos = documentWorldFromScreen(screenPixel);
    if (worldPos.x < 0.0 || worldPos.y < 0.0
        || worldPos.x > uCanvasSize.x
        || worldPos.y > uCanvasSize.y) {
        return 0.0;
    }

    vec2 clipPos = clamp(worldPos, vec2(0.0), uCanvasSize);
    return roundedRectCoverage(clipPos, uCanvasSize, uCanvasCornerRadius);
}

void main() {
    float coverage = canvasClipCoverage() * lassoMaskCoverage() * radialRevealCoverage();
    vec4 src = texture(uTileTexture, fragTexCoord);
    if (uUseCheckerBackdrop != 0) {
        vec2 screenPixel = vec2(gl_FragCoord.x, uViewportSize.y - gl_FragCoord.y);
        vec4 backdrop = checkerBackdrop(screenPixel);
        src = src + backdrop * (1.0 - src.a);
    }
    if (uReplaceWithCoverage != 0) {
        // Premultiplied src.rgb scaled by coverage, alpha = coverage → replace.
        outColor = vec4(src.rgb * coverage, coverage);
    } else {
        outColor = src * coverage;
    }
}
