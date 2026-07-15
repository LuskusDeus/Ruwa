// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T I L E   F R A G M E N T   S H A D E R
// ==========================================================================
//   Samples the tile RGBA8 texture (premultiplied alpha).
//   Un-premultiplies before output for correct display with standard blending.
//   (Variant A: un-premultiply + glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA))

#version 450 core

uniform sampler2D uTileTexture;
uniform vec2 uSampleMinUV;
uniform vec2 uSampleMaxUV;
uniform float uInvTileSize;
uniform vec2 uCanvasSize;
uniform vec2 uTileOriginPx;
uniform float uCornerRadius;
uniform int uCompositeRoundedEdgesOverViewportBackground;
uniform vec4 uViewportBackgroundColor;

in vec2 fragTexCoord;

out vec4 outColor;

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

void main() {
    vec2 uv = clamp(fragTexCoord, uSampleMinUV, uSampleMaxUV);
    vec4 color = texture(uTileTexture, uv);
    float tileSize = 1.0 / max(uInvTileSize, 0.000001);
    vec2 canvasPixelPos = uTileOriginPx + fragTexCoord * tileSize;
    float coverage = roundedRectCoverage(canvasPixelPos, uCanvasSize, uCornerRadius);

    // Un-premultiply: convert from premultiplied to straight alpha
    if (color.a > 0.001) {
        if (uCompositeRoundedEdgesOverViewportBackground != 0) {
            float clippedAlpha = color.a * coverage;
            vec3 background = uViewportBackgroundColor.rgb * uViewportBackgroundColor.a;
            vec3 rgb = color.rgb * coverage + background * (1.0 - clippedAlpha);
            outColor = vec4(rgb, 1.0);
        } else {
            outColor = vec4(color.rgb / color.a, color.a * coverage);
        }
    } else {
        outColor = vec4(0.0);
    }

    if (outColor.a <= 0.001) {
        discard;
    }
}
