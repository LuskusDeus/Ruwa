// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B A C K D R O P   B L U R   ( S E P A R A B L E   G A U S S )
// ==========================================================================
//   9-tap separable Gaussian. Run twice (horizontal, then vertical) over the
//   downsampled scene to produce the frosted backdrop. uTexelStep encodes the
//   axis direction * (1 / target-size) * spread.
// ==========================================================================

#version 450 core

uniform sampler2D uSource;
uniform vec2 uTexelStep;
uniform int uDither;  // 1 on the final pass: ordered dither before 8-bit write

in vec2 fragTexCoord;
out vec4 outColor;

// 4x4 Bayer ordered-dither matrix, normalized to [-0.5, 0.5).
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

void main() {
    const float w0 = 0.2270270270;
    const float w1 = 0.1945945946;
    const float w2 = 0.1216216216;
    const float w3 = 0.0540540541;
    const float w4 = 0.0162162162;

    vec4 sum = texture(uSource, fragTexCoord) * w0;
    sum += texture(uSource, fragTexCoord + uTexelStep * 1.0) * w1;
    sum += texture(uSource, fragTexCoord - uTexelStep * 1.0) * w1;
    sum += texture(uSource, fragTexCoord + uTexelStep * 2.0) * w2;
    sum += texture(uSource, fragTexCoord - uTexelStep * 2.0) * w2;
    sum += texture(uSource, fragTexCoord + uTexelStep * 3.0) * w3;
    sum += texture(uSource, fragTexCoord - uTexelStep * 3.0) * w3;
    sum += texture(uSource, fragTexCoord + uTexelStep * 4.0) * w4;
    sum += texture(uSource, fragTexCoord - uTexelStep * 4.0) * w4;

    if (uDither != 0) {
        sum.rgb += bayerDither() / 255.0;
    }

    outColor = sum;
}
