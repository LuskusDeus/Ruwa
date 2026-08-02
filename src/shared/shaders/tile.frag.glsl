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

// Dither for the 8-bit screen write. This is the LAST quantization in the
// chain, and the only one that can be fixed from here: everything the stroke,
// flatten and composite stages dither is sampled through GL_LINEAR (and a mip
// level when zoomed out), which low-passes a ±0.5 LSB pattern back into the
// mean before this shader ever sees it. So the offset is seeded from
// gl_FragCoord — screen pixels, not document pixels — to sit exactly at the
// frequency of the grid being quantized.
//
// Applied to the PREMULTIPLIED sample, before the un-premultiply below. Both
// output branches multiply the straight colour back by alpha (branch one
// through `* coverage`, branch two through the SRC_ALPHA blend), so a 1 LSB
// offset here is 1 LSB on the pixel that actually reaches the framebuffer.
// Rounding-offset form, so a fully black or fully saturated channel is left
// bit-exact and only the ramp between them is touched.
//
// The offset comes from an ordered 8x8 Bayer matrix rather than the hash the
// other stages use. A hash of the form fract(k * fract(dot(p, c))) is a shallow
// diagonal plane broken up by a second fract: it carries real low-frequency
// energy, and against a radial alpha ramp that beats into curved moire with
// visible foci — structure that reads as banding rather than hiding it. The
// Bayer matrix has no energy below its 8px period, so it cannot beat with
// anything the ramp does.
//
// DIAGNOSTIC: raise kDitherAmplitude to ~32.0 to make the dither unmistakably
// visible. At 1.0 the expression is identical to floor(v * 255 + n).
const float kDitherAmplitude = 1.0;

float bayer8(ivec2 c) {
    int x = c.x & 7;
    int y = c.y & 7;
    int xc = x ^ y;
    int v = ((xc >> 2) & 1)
          | (((y >> 2) & 1) << 1)
          | (((xc >> 1) & 1) << 2)
          | (((y >> 1) & 1) << 3)
          | ((xc & 1) << 4)
          | ((y & 1) << 5);
    return (float(v) + 0.5) / 64.0;
}

// ALPHA IS DITHERED TOO, and that is the whole point. Paint on a transparent
// layer carries its gradient in alpha, not in colour: the straight rgb below is
// very nearly the flat brush colour, and what the eye reads as the ramp is the
// blend weight — `color.a * coverage` for the blended branch, `clippedAlpha`
// against the viewport background for the rounded-edges branch. Dithering rgb
// alone therefore does nothing to the visible gradient no matter how large the
// amplitude, which is exactly why banding stayed legible underneath a ±16 LSB
// test pattern. One shared offset for all four channels keeps hue stable.

// A value that is ALREADY on the 1/255 grid must round, not dither — at 1:1
// zoom every sample here comes straight from an 8-bit composition tile and must
// survive bit-exact, and a value left a hundredth of an LSB short by 16F/32F
// storage upstream would otherwise be reproduced as a sparse off-by-one speckle
// instead of being rounded away. The deadband is wider than any half-float
// residue (0.062 LSB at worst) and narrower than anything that could band.
// Duplicated in composite.frag.glsl, brush_stamp.frag.glsl and the two embedded
// shaders in GLBrushRenderer.cpp; keep the copies in step.
const float kDitherDeadband = 0.1;

vec4 quantizeTo8Bit(vec4 v, float n) {
    vec4 s = v * 255.0;
    vec4 nearest = round(s);
    return mix(floor(s + n), nearest, step(abs(s - nearest), vec4(kDitherDeadband))) / 255.0;
}

vec4 ditherForDisplay(vec4 premultiplied) {
    float n = (bayer8(ivec2(gl_FragCoord.xy)) - 0.5) * kDitherAmplitude + 0.5;
    vec4 q = quantizeTo8Bit(premultiplied, n);
    q.rgb = min(q.rgb, vec3(q.a));
    return q;
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

void main() {
    vec2 uv = clamp(fragTexCoord, uSampleMinUV, uSampleMaxUV);
    vec4 color = ditherForDisplay(texture(uTileTexture, uv));
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
