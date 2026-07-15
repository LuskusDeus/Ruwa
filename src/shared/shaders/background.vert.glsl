// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B A C K G R O U N D   V E R T E X
// ==========================================================================
//   Fullscreen triangle for solid background fill

#version 450 core

// Fullscreen triangle (covers entire screen with 3 vertices)
vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
