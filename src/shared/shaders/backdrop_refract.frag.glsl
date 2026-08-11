// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B A C K D R O P   E D G E   R E F R A C T I O N
// ==========================================================================
//   Bends the captured scene through the glass bevel of one overlay panel,
//   before it is blurred. Runs over the whole capture, so the frost that the
//   blur passes build afterwards is the frost of the *refracted* image, which
//   is the physical order: the scattering happens in the glass, past the
//   surface that did the bending.
// ==========================================================================

#version 450 core

uniform sampler2D uSource;
uniform vec2 uSourceUvMin;
uniform vec2 uSourceUvMax;
uniform vec2 uRectSize;
uniform float uCornerRadius;
uniform float uRefractionWidth;
uniform float uRefractionShift;

in vec2 fragTexCoord;
out vec4 outColor;

float roundedRectDistance(vec2 pixelPos, vec2 rectSize, float radius) {
    float r = clamp(radius, 0.0, min(rectSize.x, rectSize.y) * 0.5);
    vec2 halfSize = rectSize * 0.5;
    vec2 q = abs(pixelPos - halfSize) - (halfSize - vec2(r));
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

/// Outward unit gradient of roundedRectDistance, analytically: on the straight
/// sides it is axis aligned, inside a corner it points away from the corner arc
/// centre. Cheaper and far steadier than dFdx/dFdy of the distance itself.
vec2 roundedRectNormal(vec2 pixelPos, vec2 rectSize, float radius) {
    float r = clamp(radius, 0.0, min(rectSize.x, rectSize.y) * 0.5);
    vec2 halfSize = rectSize * 0.5;
    vec2 centered = pixelPos - halfSize;
    vec2 q = abs(centered) - (halfSize - vec2(r));
    vec2 gradient;
    if (max(q.x, q.y) > 0.0) {
        gradient = normalize(max(q, vec2(0.0)) + vec2(1e-6));
    } else {
        gradient = q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    }
    return gradient * vec2(centered.x >= 0.0 ? 1.0 : -1.0, centered.y >= 0.0 ? 1.0 : -1.0);
}

/// Lateral displacement of a ray entering a quarter-round glass bevel, where
/// `rim` is 0 at the inner foot of the bevel and 1 at the outer edge. The
/// surface tilt equals asin(rim), so Snell against kGlassIor gives the
/// deviation directly; it stays near zero over most of the band and climbs
/// steeply in the last pixels, which is what reads as a glass edge.
float bevelRefraction(float rim) {
    const float kGlassIor = 1.5;
    float sinIncident = rim;
    float cosIncident = sqrt(max(1.0 - sinIncident * sinIncident, 1e-4));
    float sinRefracted = sinIncident / kGlassIor;
    float cosRefracted = sqrt(max(1.0 - sinRefracted * sinRefracted, 1e-4));
    float sinDeviation = sinIncident * cosRefracted - cosIncident * sinRefracted;
    float cosDeviation = cosIncident * cosRefracted + sinIncident * sinRefracted;
    return sinDeviation / max(cosDeviation, 0.2);
}

/// Reflects a coordinate back into [0,1] instead of clamping it. A panel parked
/// against the edge of the canvas has no captured scene on that side, and a
/// clamp there smears one row of texels along the whole rim - which reads as
/// the bevel bending the same way on both sides instead of mirroring. Folding
/// the coordinate keeps plausible structure and, more importantly, keeps the
/// direction of the bend legible. Valid for one fold, which is all the capture
/// padding ever allows.
vec2 foldIntoRange(vec2 uv) {
    vec2 folded = abs(uv);
    return 1.0 - abs(1.0 - folded);
}

void main() {
    vec2 uvSpan = uSourceUvMax - uSourceUvMin;
    // Widget-local pixels. Outside the panel this runs negative or past the
    // rect size, which the distance field handles on its own.
    vec2 pixelPos = (fragTexCoord - uSourceUvMin) / uvSpan * uRectSize;

    float distanceToEdge = roundedRectDistance(pixelPos, uRectSize, uCornerRadius);
    float bevelWidth = max(min(uRefractionWidth, min(uRectSize.x, uRectSize.y) * 0.5), 1.0);

    // The band ramps down on both sides of the silhouette: inwards over the
    // bevel, outwards over a short fade. Stopping it dead at the edge would
    // leave a step for the blur to smear back over the rim.
    float outerFade = max(bevelWidth * 0.35, 2.0);
    float rim = distanceToEdge <= 0.0
        ? clamp(1.0 + distanceToEdge / bevelWidth, 0.0, 1.0)
        : clamp(1.0 - distanceToEdge / outerFade, 0.0, 1.0);
    float shape = rim * rim * (3.0 - 2.0 * rim);

    vec2 normal = roundedRectNormal(pixelPos, uRectSize, uCornerRadius);
    float bend = bevelRefraction(shape) * uRefractionShift;
    // uvSpan / uRectSize is one widget pixel in capture UV.
    vec2 offset = normal * bend * (uvSpan / uRectSize);

    // Thick glass bends the short wavelengths slightly further.
    const float kDispersion = 0.055;
    vec2 spread = offset * kDispersion;
    outColor.r = texture(uSource, foldIntoRange(fragTexCoord + offset - spread)).r;
    vec4 mid = texture(uSource, foldIntoRange(fragTexCoord + offset));
    outColor.g = mid.g;
    outColor.b = texture(uSource, foldIntoRange(fragTexCoord + offset + spread)).b;
    outColor.a = mid.a;
}
