// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   C A N V A S   V E R T E X   S H A D E R
// ==========================================================================
//   Renders a unit quad transformed by MVP matrix

#version 450 core

uniform mat4 uMVP;

out vec2 fragTexCoord;

// Unit quad as two triangles (0,0) to (1,1)
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
    gl_Position = uMVP * vec4(pos, 0.0, 1.0);
    fragTexCoord = pos;
}
