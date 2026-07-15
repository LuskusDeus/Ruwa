// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B A C K D R O P   C O P Y   ( D O W N S A M P L E )
// ==========================================================================
//   Plain passthrough sample. Downsampling is achieved by rendering into a
//   smaller FBO with GL_LINEAR filtering (box-averaging halving step).
// ==========================================================================

#version 450 core

uniform sampler2D uSource;

in vec2 fragTexCoord;
out vec4 outColor;

void main() {
    outColor = texture(uSource, fragTexCoord);
}
