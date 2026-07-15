// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   C A N V A S   F R A G M E N T   S H A D E R
// ==========================================================================
//   Draws checkerboard pattern on canvas area (transparency indicator)

#version 450 core

uniform vec2 uCanvasSize;
uniform float uCornerRadius;
uniform float uCheckerSize;
uniform vec4 uCheckerColor1;
uniform vec4 uCheckerColor2;

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
    // Convert normalized tex coords to pixel coordinates
    vec2 pixelPos = fragTexCoord * uCanvasSize;

    // Checkerboard pattern
    vec2 cell = floor(pixelPos / uCheckerSize);
    float checker = mod(cell.x + cell.y, 2.0);

    outColor = mix(uCheckerColor1, uCheckerColor2, checker);
    outColor.a *= roundedRectCoverage(pixelPos, uCanvasSize, uCornerRadius);
    if (outColor.a <= 0.001) {
        discard;
    }
}
