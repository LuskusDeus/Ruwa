// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   G P U   G L A S S   C O M P O S I T E
// ============================================================================
// The frost is prepared by the reduction pyramid. This full-resolution pass
// applies the rounded-rectangle refraction and optional chromatic separation
// before compositing the glass silhouette.
//
// The silhouette uses an exact rounded-rectangle signed-distance field. The
// Coverage uses the exact rounded-rectangle SDF. The optical profile smooths
// its interior axis selection, while a flat-ended transfer curve keeps the
// refraction from exposing the profile's internal foot.
// ============================================================================

#version 450 core

uniform sampler2D uSource;
uniform vec2 uSourceUvMin;
uniform vec2 uSourceUvMax;
uniform vec2 uCompositeSize;
uniform vec2 uRectOffset;
uniform vec2 uRectSize;
uniform float uCornerRadius;
uniform float uOpacity;
uniform vec2 uShadowOffset;
uniform float uShadowFalloff;
uniform float uShadowOpacity;
uniform float uShadowReach;
uniform vec3 uSurfaceTint;
uniform float uSurfaceTintAmount;
/// Width of the curved edge, in target pixels.
uniform float uRefractionDepth;
/// Effective optical thickness, in target pixels.
uniform float uRefractionShift;
/// Largest surface tilt reached at the silhouette, expressed as a sine.
uniform float uMaxTilt;
/// Chromatic separation as a fraction of the refracted displacement.
uniform float uDispersion;
uniform float uSplay;
uniform float uMaxSplayShift;
/// 1 when uSource is the raw sRGB capture rather than the linear frost pyramid.
uniform int uDecodeSrgb;
uniform float uEdgeInset;

in vec2 fragTexCoord;
out vec4 outColor;

vec3 linearFromSrgb(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 srgbFromLinear(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,
        step(vec3(0.0031308), c));
}

float bayerDither() {
    const float m[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    int x = int(mod(gl_FragCoord.x, 4.0));
    int y = int(mod(gl_FragCoord.y, 4.0));
    return (m[y * 4 + x] + 0.5) / 16.0 - 0.5;
}

float roundedRectDistance(vec2 pixelPos, vec2 rectSize, float radius) {
    float r = clamp(radius, 0.0, min(rectSize.x, rectSize.y) * 0.5);
    vec2 halfSize = rectSize * 0.5;
    vec2 q = abs(pixelPos - halfSize) - (halfSize - vec2(r));
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

/// Continuation of the rounded-rect distance inside the corner core. The exact
/// SDF uses max(q.x, q.y) there, whose gradient jumps across its medial axis.
/// Blending the axis choice across the complete optical depth keeps the normal
/// continuous enough for the bevel without changing the exact outer coverage.
float roundedRectProfileDistance(
    vec2 pixelPos, vec2 rectSize, float radius, float blendWidth) {
    float r = clamp(radius, 0.0, min(rectSize.x, rectSize.y) * 0.5);
    vec2 halfSize = rectSize * 0.5;
    vec2 q = abs(pixelPos - halfSize) - (halfSize - vec2(r));

    float maximumWidth = max(blendWidth, 1.0);
    float xWeight = smoothstep(-maximumWidth, maximumWidth, q.x - q.y);
    float profileAxis = mix(q.y, q.x, xWeight);
    return length(max(q, vec2(0.0))) + min(profileAxis, 0.0) - r;
}

vec2 roundedRectProfileNormal(
    vec2 pixelPos, vec2 rectSize, float radius, float blendWidth) {
    const vec2 dx = vec2(1.0, 0.0);
    const vec2 dy = vec2(0.0, 1.0);
    vec2 gradient = vec2(
        roundedRectProfileDistance(pixelPos + dx, rectSize, radius, blendWidth)
            - roundedRectProfileDistance(pixelPos - dx, rectSize, radius, blendWidth),
        roundedRectProfileDistance(pixelPos + dy, rectSize, radius, blendWidth)
            - roundedRectProfileDistance(pixelPos - dy, rectSize, radius, blendWidth));
    return gradient / max(length(gradient), 1e-5);
}

/// Reaches @p maximum with zero slope instead of hitting a hard min() cap.
/// The cubic Hermite curve preserves unit slope at the flat inner foot, so it
/// does not double-flatten the refraction profile there.
float smoothTiltProfile(float coordinate, float maximum) {
    float x = clamp(coordinate, 0.0, 1.0);
    float m = clamp(maximum, 0.0, 0.98);
    if (m < 0.5) {
        float smoothCoordinate = x * x * (3.0 - 2.0 * x);
        return m * smoothCoordinate;
    }
    return x + (3.0 * m - 2.0) * x * x
        + (1.0 - 2.0 * m) * x * x * x;
}

float snellDisplacement(float sinIncident) {
    const float kGlassIor = 1.5;
    float cosIncident = sqrt(max(1.0 - sinIncident * sinIncident, 1e-4));
    float sinRefracted = sinIncident / kGlassIor;
    float cosRefracted = sqrt(max(1.0 - sinRefracted * sinRefracted, 1e-4));
    float sinDeviation = sinIncident * cosRefracted - cosIncident * sinRefracted;
    float cosDeviation = cosIncident * cosRefracted + sinIncident * sinRefracted;
    return sinDeviation / max(cosDeviation, 0.2);
}

/// Reflect once at the capture boundary. This avoids stretching a single edge
/// texel when a panel touches the canvas edge and there is no captured scene on
/// the far side.
vec2 foldIntoRange(vec2 uv) {
    vec2 folded = abs(uv);
    return 1.0 - abs(1.0 - folded);
}

vec3 fetchLinear(vec2 uv) {
    vec3 sampled = textureLod(uSource, foldIntoRange(uv), 0.0).rgb;
    return uDecodeSrgb != 0 ? linearFromSrgb(sampled) : sampled;
}

vec3 sampleRefracted(vec2 sourceUv, vec2 offset, float dispersion) {
    vec2 spread = offset * clamp(dispersion, 0.0, 1.0);
    if (max(abs(spread.x), abs(spread.y)) < 1e-6) {
        return fetchLinear(sourceUv + offset);
    }

    // One image per colour channel is enough for chromatic separation. The old
    // splay gather averaged six spatially separated copies of the entire
    // backdrop, which exposed each tap as a nested copy of the widget outline.
    vec3 middle = fetchLinear(sourceUv + offset);
    return vec3(fetchLinear(sourceUv + offset - spread).r, middle.g,
        fetchLinear(sourceUv + offset + spread).b);
}

void main() {
    vec2 pixelPos = fragTexCoord * uCompositeSize - uRectOffset;
    float distanceToEdge
        = roundedRectDistance(pixelPos, uRectSize, uCornerRadius) + uEdgeInset;
    float aa = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-aa, aa, distanceToEdge);

    // Analytic large-radius shadow: no blur texture, cache invalidation or
    // extra pass. Exponential falloff drops quickly immediately outside the
    // panel but keeps a softer long tail than the previous Gaussian profile.
    // Begin the compact-support fade only after the tail falls below one 8-bit
    // alpha step, so even a white backdrop cannot reveal the viewport boundary.
    float shadowFalloff = max(uShadowFalloff, 1.0);
    float shadowReach = max(uShadowReach, 1.0);
    float shadowFadeStart = max(shadowReach - 1.5, 0.0);
    float shadowDistance = roundedRectDistance(
        pixelPos - uShadowOffset, uRectSize, uCornerRadius);
    float outsideShadowDistance = max(shadowDistance, 0.0);
    float normalizedShadowDistance = outsideShadowDistance / shadowFalloff;
    float shadowAlpha = exp(-normalizedShadowDistance)
        * (1.0 - smoothstep(
              shadowFadeStart, shadowReach, normalizedShadowDistance))
        * clamp(uShadowOpacity, 0.0, 1.0) * clamp(uOpacity, 0.0, 1.0);

    float glassAlpha = coverage * clamp(uOpacity, 0.0, 1.0);
    if (glassAlpha <= 1e-4) {
        outColor = vec4(0.0, 0.0, 0.0, shadowAlpha);
        return;
    }

    float halfShortSide = min(uRectSize.x, uRectSize.y) * 0.5;
    float maxDepth = max(halfShortSide - 1.0, 1.0);
    float depth = clamp(uRefractionDepth, 1.0, maxDepth);

    // Coverage stays exact while refraction uses the smoothed interior profile.
    float profileDistanceToEdge
        = roundedRectProfileDistance(pixelPos, uRectSize, uCornerRadius, depth)
        + uEdgeInset;
    vec2 normal
        = roundedRectProfileNormal(pixelPos, uRectSize, uCornerRadius, depth);

    // `bevel` is one at the silhouette and zero at the inner foot. Smoothstep
    // makes both ends flat, so displacement joins the untouched interior
    // without exposing the profile's inner foot.
    float edgeCoordinate = clamp(1.0 + profileDistanceToEdge / depth, 0.0, 1.0);
    float bevel = edgeCoordinate * edgeCoordinate * (3.0 - 2.0 * edgeCoordinate);
    float surfaceTilt = smoothTiltProfile(bevel, uMaxTilt);
    float bend = snellDisplacement(surfaceTilt) * max(uRefractionShift, 0.0);

    // Figma's 100-splay reference turns a straight split beneath the panel into
    // a circular cap. This is the corresponding sagitta: one at the outer edge,
    // zero at the inner foot, with the circle's tangent meeting the silhouette.
    // It is a single coordinate displacement, not a multi-tap gather.
    float splayProfile
        = 1.0 - sqrt(max(1.0 - edgeCoordinate * edgeCoordinate, 0.0));
    float splayShift = min(depth * splayProfile * max(uSplay, 0.0),
        max(uMaxSplayShift, 0.0));

    vec2 uvSpan = uSourceUvMax - uSourceUvMin;
    vec2 sourceUv = uSourceUvMin + (pixelPos / uRectSize) * uvSpan;
    vec2 pixelToUv = uvSpan / max(uRectSize, vec2(1.0));

    vec2 halfSize = max(uRectSize * 0.5, vec2(1.0));
    vec2 positionFromCentre = pixelPos - halfSize;

    // Splay is a screen-space fan, distinct from the base normal refraction.
    // The texture lookup moves towards the plate centre, so the sampled image
    // appears to spread away from it. Keeping the vector circular in pixels
    // (rather than normalizing each axis by the rectangular bounds) makes its
    // vertical component strongest at the middle of a long edge and
    // progressively weaker towards the ends. A horizontal background edge is
    // therefore bowed instead of being translated as one visible straight line.
    // The optical profile above still confines the fan to the rounded edge depth.
    float distanceFromCentre = length(positionFromCentre);
    vec2 splayDirection = distanceFromCentre > 1e-5
        ? -positionFromCentre / distanceFromCentre
        : vec2(0.0);
    vec2 offset = (-normal * bend + splayDirection * splayShift) * pixelToUv;
    vec3 glass = bend < 1e-4 ? fetchLinear(sourceUv)
                             : sampleRefracted(sourceUv, offset, uDispersion);

    glass = mix(
        glass, linearFromSrgb(uSurfaceTint), clamp(uSurfaceTintAmount, 0.0, 1.0));
    vec3 encoded = srgbFromLinear(glass) + bayerDither() / 255.0;

    // Compose the black shadow behind the glass in-source, then hand one
    // straight-alpha colour to the renderer's existing SRC_ALPHA blend.
    float visibleShadowAlpha = shadowAlpha * (1.0 - glassAlpha);
    float combinedAlpha = glassAlpha + visibleShadowAlpha;
    vec3 combinedPremultiplied = encoded * glassAlpha;
    outColor = vec4(combinedPremultiplied / max(combinedAlpha, 1e-5), combinedAlpha);
}
