// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B A C K D R O P   D U A L - F I L T E R   D O W N
// ==========================================================================
//   One reduction step of the dual-filter ("dual Kawase") blur: five bilinear
//   taps - centre plus the four diagonals of the source texel - written into a
//   half-size target. Chained with backdrop_upsample it approximates a very
//   wide Gaussian at a fraction of the taps a separable kernel of the same
//   radius would need, because most of the reach comes from the pyramid rather
//   than from the kernel.
//
//   Works in LINEAR light. The first step of the chain reads the raw capture
//   and decodes it (uDecodeSrgb); every step after that is already linear, and
//   backdrop_composite re-encodes at the very end. Averaging encoded sRGB
//   darkens the result and eats the highlights, which is what made the old
//   frost look grey rather than luminous - so the decode has to happen per tap,
//   before the weighting, not once on the result.
// ==========================================================================

#version 450 core

uniform sampler2D uSource;
/// Half a texel of the SOURCE level, in its own UV space.
uniform vec2 uHalfPixel;
/// Tap spacing in half-texels. Widens the blur; past ~2 the five-tap pattern
/// starts to show as a cross.
uniform float uOffset;
/// 1 on the first reduction only, where the source is the sRGB capture.
uniform int uDecodeSrgb;

in vec2 fragTexCoord;
out vec4 outColor;

vec3 linearFromSrgb(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 tap(vec2 uv) {
    vec3 c = texture(uSource, uv).rgb;
    return uDecodeSrgb != 0 ? linearFromSrgb(c) : c;
}

void main() {
    vec2 offset = uHalfPixel * uOffset;

    vec3 sum = tap(fragTexCoord) * 4.0;
    sum += tap(fragTexCoord - offset);
    sum += tap(fragTexCoord + offset);
    sum += tap(fragTexCoord + vec2(offset.x, -offset.y));
    sum += tap(fragTexCoord - vec2(offset.x, -offset.y));

    outColor = vec4(sum / 8.0, 1.0);
}
