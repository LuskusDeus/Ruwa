// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   S T A M P   V E R T E X
// ==========================================================================
//   Renders a quad covering the brush footprint within a single tile.
//   Tile-local pixel coordinates [0..256] are mapped to NDC [-1..1].

#version 450 core

uniform vec2 uQuadMin;   // tile-local pixel coords (top-left of brush bbox)
uniform vec2 uQuadMax;   // tile-local pixel coords (bottom-right of brush bbox)

out vec2 fragPixelCoord; // interpolated tile-local pixel position

vec2 positions[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

void main() {
    vec2 t = positions[gl_VertexID];
    vec2 pixel = mix(uQuadMin, uQuadMax, t);

    // Map [0, 256] pixel coords to [-1, 1] NDC
    gl_Position = vec4(pixel / 128.0 - 1.0, 0.0, 1.0);

    // Pass pixel coords for distance calculation in fragment shader
    fragPixelCoord = pixel;
}
