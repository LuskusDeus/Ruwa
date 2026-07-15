// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B A C K G R O U N D   F R A G M E N T
// ==========================================================================
//   Solid color fill for the area outside the canvas

#version 450 core

uniform vec4 uColor;

out vec4 outColor;

void main() {
    outColor = uColor;
}
