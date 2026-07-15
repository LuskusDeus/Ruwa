// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   C O M P O S I T E   V E R T E X
// ==========================================================================
//   Fullscreen quad for FBO composition passes

#version 450 core

out vec2 fragTexCoord;

// Fullscreen quad covering [0,1] UV space -> [-1,1] NDC
vec2 positions[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);

void main() {
    vec2 pos = positions[gl_VertexID];
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord = pos;
}
