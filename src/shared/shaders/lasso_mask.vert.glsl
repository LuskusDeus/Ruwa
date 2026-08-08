#version 450 core
// SPDX-License-Identifier: MPL-2.0

// Screen-space lasso fill mask rasterizer.
//
// Positions arrive in mask pixels, which are viewport pixels with y measured
// DOWN from the top — the space `screenFromDocumentWorld` produces and the one
// target_layer_preview.frag texel-fetches the mask in. Mapping y straight to
// NDC (no flip) puts mask row 0 in framebuffer row 0, so a fragment's
// gl_FragCoord.xy equals its position in that same space and the edge-pad test
// in the fragment stage can compare against the raw segment endpoints.

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aSegment;

uniform vec2 uViewportSize;

flat out vec4 vSegment;

void main()
{
    vSegment = aSegment;
    gl_Position = vec4((aPos / uViewportSize) * 2.0 - 1.0, 0.0, 1.0);
}
