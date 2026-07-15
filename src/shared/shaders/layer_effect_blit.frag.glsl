// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   L A Y E R   E F F E C T   B L I T
// ==========================================================================
//   Affine-sampled passthrough used by the document-tile effect renderer to
//   (a) stamp neighbour tiles into the padded source (scale 1, offset 0) and
//   (b) crop the centre tile back out of the padded result (scale/offset map
//   the centre region to the full output). Premultiplied colour is preserved.
// ==========================================================================

#version 450 core

uniform sampler2D uSource;
uniform vec2 uTexScale;
uniform vec2 uTexOffset;

in vec2 fragTexCoord;
out vec4 outColor;

void main() {
    outColor = texture(uSource, fragTexCoord * uTexScale + uTexOffset);
}
