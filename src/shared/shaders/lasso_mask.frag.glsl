#version 450 core
// SPDX-License-Identifier: MPL-2.0

// Two jobs, selected by uCapsule / colour writes from the host:
//
//   uCapsule == 0  plain geometry (parity fan, canvas gate, resolve rect).
//   uCapsule == 1  edge-pad capsule. The host draws an oriented box that is
//                  slightly larger than the capsule and this test carves the
//                  exact shape: distance from the pixel CENTRE to the segment,
//                  which is bit-for-bit the rule the CPU commit path uses in
//                  OpenGLCanvasWidget::buildLassoFillScreenMask.
//
// During accumulation colour writes are masked off and only the stencil moves;
// during resolve the stencil is read-only and this writes uWriteValue.

flat in vec4 vSegment;

uniform int uCapsule;
uniform float uEdgePadSq;
uniform float uWriteValue;

out vec4 outColor;

void main()
{
    if (uCapsule != 0) {
        vec2 p = gl_FragCoord.xy;
        vec2 a = vSegment.xy;
        vec2 b = vSegment.zw;
        vec2 ab = b - a;
        float denom = max(dot(ab, ab), 0.0000001);
        float t = clamp(dot(p - a, ab) / denom, 0.0, 1.0);
        vec2 d = p - (a + ab * t);
        if (dot(d, d) > uEdgePadSq) {
            discard;
        }
    }

    outColor = vec4(uWriteValue, 0.0, 0.0, 1.0);
}
