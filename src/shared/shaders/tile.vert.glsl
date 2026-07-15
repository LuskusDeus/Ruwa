// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T I L E   V E R T E X   S H A D E R
// ==========================================================================
//   Renders a unit quad [0,1]x[0,1] transformed by MVP to tile world position

#version 450 core

uniform mat4 uMVP;
uniform vec2 uQuadMinPx; // tile-local pixel coordinates [0..TILE_SIZE]
uniform vec2 uQuadMaxPx; // tile-local pixel coordinates [0..TILE_SIZE]
uniform float uInvTileSize;

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
    vec2 pixel = mix(uQuadMinPx, uQuadMaxPx, pos);
    vec2 local = pixel * uInvTileSize;
    gl_Position = uMVP * vec4(local, 0.0, 1.0);
    fragTexCoord = local;
}
