// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   D I S P L A Y   P Y R A M I D   T I L E   F R A G M E N T
// ==========================================================================
//
//   Display pass for one quad of the level-L pyramid lattice. Samples the
//   level-L tile and the level-(L+1) tile that contains it and lerps between
//   them by fract(log2(1/zoom)), so continuous zoom-out never pops and never
//   changes filtering quality mid-stroke.
//
//   Zoom is uniform over the frame and rotation is isotropic, so the level is
//   a per-frame scalar: no derivatives, no per-pixel LOD.
//
//   Both taps are PREMULTIPLIED, the lerp is done premultiplied, and the
//   un-premultiply happens once at the end — same contract as tile.frag.glsl,
//   with the dither still the very last quantization before the screen write.
//

#version 450 core

uniform sampler2D uFineTexture;
uniform sampler2D uCoarseTexture;

// fragTexCoord is the tile-local fraction [0,1]. Each tap maps it into its own
// texture with a scale/offset that absorbs the apron and, for the coarse tap,
// the half-tile sub-rect picked by the fine key's parity. The min/max pair is
// the half-texel-inset sampling clamp, already intersected with the canvas clip
// sub-rect on the CPU.
uniform vec2 uFineUVScale;
uniform vec2 uFineUVOffset;
uniform vec2 uFineUVMin;
uniform vec2 uFineUVMax;
uniform vec2 uCoarseUVScale;
uniform vec2 uCoarseUVOffset;
uniform vec2 uCoarseUVMin;
uniform vec2 uCoarseUVMax;
uniform float uLevelBlend;

uniform vec2 uCanvasSize;
uniform vec2 uTileOriginPx; // document pixels
uniform vec2 uTileSpanPx; // document pixels covered by this quad
uniform float uCornerRadius;
uniform int uCompositeRoundedEdgesOverViewportBackground;
uniform vec4 uViewportBackgroundColor;

in vec2 fragTexCoord;

out vec4 outColor;

// Kept in step with tile.frag.glsl — see the long note there for why the
// offset is seeded from gl_FragCoord, why an ordered Bayer matrix is used
// instead of a hash, and why alpha is dithered too.
const float kDitherAmplitude = 1.0;
const float kDitherDeadband = 0.1;

float bayer8(ivec2 c)
{
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

vec4 quantizeTo8Bit(vec4 v, float n)
{
    vec4 s = v * 255.0;
    vec4 nearest = round(s);
    return mix(floor(s + n), nearest, step(abs(s - nearest), vec4(kDitherDeadband))) / 255.0;
}

vec4 ditherForDisplay(vec4 premultiplied)
{
    float n = (bayer8(ivec2(gl_FragCoord.xy)) - 0.5) * kDitherAmplitude + 0.5;
    vec4 q = quantizeTo8Bit(premultiplied, n);
    q.rgb = min(q.rgb, vec3(q.a));
    return q;
}

float roundedRectCoverage(vec2 pixelPos, vec2 rectSize, float radius)
{
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

void main()
{
    vec2 fineUV = clamp(fragTexCoord * uFineUVScale + uFineUVOffset, uFineUVMin, uFineUVMax);
    vec4 premultiplied = texture(uFineTexture, fineUV);

    if (uLevelBlend > 0.0) {
        vec2 coarseUV
            = clamp(fragTexCoord * uCoarseUVScale + uCoarseUVOffset, uCoarseUVMin, uCoarseUVMax);
        premultiplied = mix(premultiplied, texture(uCoarseTexture, coarseUV), uLevelBlend);
    }

    vec4 color = ditherForDisplay(premultiplied);

    // The rounded-canvas clip is evaluated in DOCUMENT pixels, so it is
    // identical at every level — the quad just spans more of them.
    vec2 canvasPixelPos = uTileOriginPx + fragTexCoord * uTileSpanPx;
    float coverage = roundedRectCoverage(canvasPixelPos, uCanvasSize, uCornerRadius);

    // The "replace the pre-drawn background with content composited against the
    // VIEWPORT background" rule only ever meant the rounded corner fringe, and
    // it is gated here on coverage instead of being applied to the whole frame.
    //
    // At level zero the two were interchangeable: an absent composition-cache
    // tile is simply not drawn, so every fragment that reached the shader had
    // alpha 0 or 1 and the branch could never fire on a partial value. The
    // pyramid box-filters ACROSS that boundary, so a texel straddling the edge
    // of the covered tile set arrives with alpha ~0.5 — and blending it against
    // the viewport background painted a dark hairline along every content
    // border. Away from the corners the correct backdrop is the opaque document
    // background already drawn underneath, which is what the blended branch
    // below composites over.
    bool replaceWithViewportBackground
        = uCompositeRoundedEdgesOverViewportBackground != 0 && coverage < 1.0;

    if (color.a > 0.001) {
        if (replaceWithViewportBackground) {
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
