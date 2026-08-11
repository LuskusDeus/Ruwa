// SPDX-License-Identifier: MPL-2.0

#version 450 core

/// Frosted body: the refracted capture, blurred. The bend is already in it, so
/// this pass only shades the bevel - it must not reach for a crisper level to
/// make the rim legible. That reads as an unblurred low-resolution patch, not
/// as glass, because a crisper level is exactly a lower-resolution one here.
uniform sampler2D uSource;
uniform vec2 uSourceUvMin;
uniform vec2 uSourceUvMax;
uniform vec2 uRectSize;
uniform float uCornerRadius;
uniform float uOpacity;
uniform vec3 uSurfaceTint;
uniform float uSurfaceTintAmount;
uniform float uRefractionWidth;
uniform float uEdgeInset;
uniform float uBevelShade;

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
    // Offsetting the distance field shrinks the silhouette concentrically, so
    // the glass ends underneath the widget's own border stroke instead of
    // alongside it. Without this the coverage ramp, being centred on the
    // geometric edge, reaches a pixel past it, and the corners - where the two
    // rasterisers approximate the same arc differently - leak blurred content
    // out beyond the stroke. The inset is sized so coverage is already zero by
    // the outer edge of the rect at any device pixel ratio.
    float distanceToEdge
        = roundedRectDistance(pixelPos, uRectSize, uCornerRadius) + uEdgeInset;
    float aa = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-aa, aa, distanceToEdge);

    vec2 sourceUv = mix(uSourceUvMin, uSourceUvMax, fragTexCoord);
    vec3 glass = texture(uSource, sourceUv).rgb;

    // The bend already happened, in backdrop_refract; this is only the bevel's
    // own shading, on a profile that is flat at both ends so the band never
    // draws an edge of its own.
    float bevelWidth = max(min(uRefractionWidth, min(uRectSize.x, uRectSize.y) * 0.5), 1.0);
    float rim = clamp(1.0 + distanceToEdge / bevelWidth, 0.0, 1.0);
    float shape = rim * rim * (3.0 - 2.0 * rim);

    // Tint towards the theme surface instead of multiplying towards black. A
    // multiply can only ever darken, so dark artwork under the panel crushes
    // and anything drawn on the glass loses its contrast with it; pulling
    // towards a fixed colour gives the frost a floor as well as a ceiling, and
    // lands it on the theme's own value whatever is behind the canvas.
    glass = mix(glass, uSurfaceTint, clamp(uSurfaceTintAmount, 0.0, 1.0));

    // Neutral, symmetrical edge darkening adds separation from the canvas
    // without implying a light direction or reintroducing a highlight. Cubed so
    // it sits in the outer part of the bevel instead of shading the whole band.
    // Currently off (uBevelShade is 0): on trial together with the widget-side
    // inner shadow, since the refracting bevel may already separate enough.
    glass *= 1.0 - shape * shape * shape * clamp(uBevelShade, 0.0, 1.0);

    outColor = vec4(glass, coverage * clamp(uOpacity, 0.0, 1.0));
}
