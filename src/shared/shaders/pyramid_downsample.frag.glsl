// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   D I S P L A Y   P Y R A M I D   D O W N S A M P L E
// ==========================================================================
//
//   Builds one level-L display pyramid tile from the 4x4 block of level-(L-1)
//   tiles that surrounds it. Output is 258x258: a 256x256 core plus a 1-texel
//   apron on every side, so the display pass's bilinear tail at a tile border
//   reads real neighbour data instead of clamping.
//
//   The filter is an exact 2:1 box. Output core texel i covers parent core
//   texels 2i and 2i+1, so the whole 256 core comes from the 2x2 parents alone
//   and the level chain is seam-free by construction. The apron is what pulls
//   in the outer ring of the 4x4 block: output texel 0 covers parent texels -2
//   and -1, output texel 257 covers 512 and 513.
//
//   Parent APRONS ARE NEVER READ. Every parent is addressed in core-texel
//   coordinates [0,256) and the apron offset is added at fetch time, which is
//   what lets level 1 take its parents from the 256x256 composition cache
//   (apron 0) and every higher level from 258x258 pyramid tiles (apron 1)
//   through the same code.
//
//   Tiles are PREMULTIPLIED and stay premultiplied here — un-premultiplying
//   before a box filter would weight transparent texels as if they were opaque.
//

#version 450 core

// The 4x4 parent block, row-major from block (-1,-1) to (2,2) relative to the
// output tile's 2x2 core parents. Absent parents are bound to a shared
// transparent texture, so there is no branch for missing neighbours.
uniform sampler2D uParents[16];

// 0 when the parents are composition-cache tiles (256x256), 1 when they are
// pyramid tiles (258x258).
uniform int uParentApron;

out vec4 outColor;

vec4 fetchParent(int idx, ivec2 c)
{
    // Sampler arrays may only be indexed by a dynamically uniform expression,
    // and the block index varies per fragment — hence the unrolled switch.
    switch (idx) {
    case 0: return texelFetch(uParents[0], c, 0);
    case 1: return texelFetch(uParents[1], c, 0);
    case 2: return texelFetch(uParents[2], c, 0);
    case 3: return texelFetch(uParents[3], c, 0);
    case 4: return texelFetch(uParents[4], c, 0);
    case 5: return texelFetch(uParents[5], c, 0);
    case 6: return texelFetch(uParents[6], c, 0);
    case 7: return texelFetch(uParents[7], c, 0);
    case 8: return texelFetch(uParents[8], c, 0);
    case 9: return texelFetch(uParents[9], c, 0);
    case 10: return texelFetch(uParents[10], c, 0);
    case 11: return texelFetch(uParents[11], c, 0);
    case 12: return texelFetch(uParents[12], c, 0);
    case 13: return texelFetch(uParents[13], c, 0);
    case 14: return texelFetch(uParents[14], c, 0);
    case 15: return texelFetch(uParents[15], c, 0);
    }
    return vec4(0.0);
}

void main()
{
    // Output texel, including the apron: [0,258).
    ivec2 o = ivec2(gl_FragCoord.xy);

    // Parent core texel this output texel's 2x2 box starts at: [-2,512].
    // Even by construction, and the parent tile width is even, so base and
    // base+1 always land in the SAME parent tile — one block index for all
    // four taps.
    ivec2 base = 2 * (o - ivec2(1));

    // floor(base / 256) without relying on a signed right shift. base >= -2,
    // so the +512 bias keeps the division in positive territory.
    ivec2 block = (base + ivec2(512)) / 256 - ivec2(2);
    int idx = (block.x + 1) + 4 * (block.y + 1);

    ivec2 local = base - block * 256 + ivec2(uParentApron);

    vec4 sum = fetchParent(idx, local) + fetchParent(idx, local + ivec2(1, 0))
        + fetchParent(idx, local + ivec2(0, 1)) + fetchParent(idx, local + ivec2(1, 1));

    outColor = sum * 0.25;
}
