// SPDX-License-Identifier: MPL-2.0

#version 450 core

uniform sampler2D uSource;
uniform vec2 uSourceUvMin;
uniform vec2 uSourceUvMax;
uniform vec2 uRectSize;
uniform float uCornerRadius;
uniform float uOpacity;

in vec2 fragTexCoord;
out vec4 outColor;

float roundedRectDistance(vec2 pixelPos, vec2 rectSize, float radius) {
    float r = clamp(radius, 0.0, min(rectSize.x, rectSize.y) * 0.5);
    vec2 halfSize = rectSize * 0.5;
    vec2 q = abs(pixelPos - halfSize) - (halfSize - vec2(r));
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 pixelPos = fragTexCoord * uRectSize;
    float distanceToEdge
        = roundedRectDistance(pixelPos, uRectSize, uCornerRadius);
    float aa = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-aa, aa, distanceToEdge);

    vec2 sourceUv = mix(uSourceUvMin, uSourceUvMax, fragTexCoord);
    vec3 blurred = texture(uSource, sourceUv).rgb;

    // Neutral, symmetrical edge darkening adds separation from the canvas
    // without implying a light direction or reintroducing a highlight.
    float innerDistance = max(-distanceToEdge, 0.0);
    float shadeWidth = clamp(min(uRectSize.x, uRectSize.y) * 0.16, 5.0, 10.0);
    float edgeShade = 1.0 - smoothstep(0.0, shadeWidth, innerDistance);
    blurred *= 1.0 - edgeShade * 0.07;

    outColor = vec4(blurred, coverage * clamp(uOpacity, 0.0, 1.0));
}
