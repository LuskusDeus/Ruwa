// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B A C K D R O P   D U A L - F I L T E R   U P
// ==========================================================================
//   The expanding half of the dual-filter blur: an eight-tap tent around the
//   source texel, written into a double-size target. Each level up widens the
//   support again, so the chain down-down-up-up ends far smoother than the
//   texel count suggests - and, unlike a plain bilinear expand, it leaves no
//   trace of the pyramid grid.
//
//   Linear light, like the rest of the chain.
// ==========================================================================

#version 450 core

uniform sampler2D uSource;
/// Half a texel of the SOURCE level (the smaller one), in its own UV space.
uniform vec2 uHalfPixel;
uniform float uOffset;

in vec2 fragTexCoord;
out vec4 outColor;

void main() {
    vec2 offset = uHalfPixel * uOffset;

    vec4 sum = texture(uSource, fragTexCoord + vec2(-offset.x * 2.0, 0.0));
    sum += texture(uSource, fragTexCoord + vec2(-offset.x, offset.y)) * 2.0;
    sum += texture(uSource, fragTexCoord + vec2(0.0, offset.y * 2.0));
    sum += texture(uSource, fragTexCoord + vec2(offset.x, offset.y)) * 2.0;
    sum += texture(uSource, fragTexCoord + vec2(offset.x * 2.0, 0.0));
    sum += texture(uSource, fragTexCoord + vec2(offset.x, -offset.y)) * 2.0;
    sum += texture(uSource, fragTexCoord + vec2(0.0, -offset.y * 2.0));
    sum += texture(uSource, fragTexCoord + vec2(-offset.x, -offset.y)) * 2.0;

    outColor = sum / 12.0;
}
