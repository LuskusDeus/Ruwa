#version 450 core
// SPDX-License-Identifier: MPL-2.0

uniform vec2 uCanvasSize;
uniform vec2 uTileOrigin;  // pixel position of tile (key.x * 256, key.y * 256)
uniform float uTileSize;

out vec2 fragTexCoord;

vec2 positions[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

void main() {
    vec2 t = positions[gl_VertexID];
    vec2 pixel = uTileOrigin + t * uTileSize;
    vec2 ndc = 2.0 * pixel / uCanvasSize - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragTexCoord = t;
}
