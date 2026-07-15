// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   P I X E L   A C C E S S
// ==========================================================================
//
//   Format-aware per-pixel access for tile buffers — the CPU-side counterpart
//   to TileFormat.h's GPU mapping.
//
//   The scattered "type-(b)" sites across the codebase interpret tile pixels
//   with hard 8-bit assumptions: stride `* 4`, alpha at `[idx + 3]`, integer
//   premultiplied src-over. To make those sites format-independent they must
//   stop touching raw bytes directly and instead read/write through ONE
//   normalized representation. This header provides it:
//
//     * pixels are exchanged as premultiplied float RGBA (channel order R,G,B,A)
//       in [0,1] for RGBA8; float formats pass HDR values (>1) through verbatim.
//     * RGBA8  -> byte / 255            RGBA16F -> qfloat16 (half)   RGBA32F -> float
//
//   This header (not TileData.h) owns the half-float / Qt dependency so the
//   core tile headers stay Qt-free. Phase 2a introduces the accessor only; the
//   actual migration of the type-(b) sites onto it happens in later slices.
//

#ifndef RUWA_CORE_TILES_TILEPIXELACCESS_H
#define RUWA_CORE_TILES_TILEPIXELACCESS_H

#include "TileTypes.h"
#include "TileFormat.h"
#include "TileData.h"

#include <QtCore/qfloat16.h> // qfloat16 (half) <-> float conversion

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace aether {

// ==========================================================================
//   B Y T E   O F F S E T S
// ==========================================================================

/// Byte offset of pixel (x,y), channel 0, within a tile buffer of `fmt`.
inline size_t tilePixelByteOffset(TilePixelFormat fmt, uint32_t x, uint32_t y)
{
    return (static_cast<size_t>(y) * TILE_SIZE + x) * tileBytesPerPixel(fmt);
}

/// Byte offset of the ALPHA channel of pixel (x,y) (replaces hard `[idx + 3]`).
inline size_t tileAlphaByteOffset(TilePixelFormat fmt, uint32_t x, uint32_t y)
{
    return tilePixelByteOffset(fmt, x, y) + 3u * tileBytesPerChannel(fmt);
}

// ==========================================================================
//   R A W   B U F F E R   A C C E S S
// ==========================================================================

/// Read pixel (x,y) from a raw tile buffer as normalized premultiplied float
/// RGBA. memcpy is used for the float paths to stay alignment/aliasing-safe.
inline void readTilePixelF(
    const uint8_t* buf, TilePixelFormat fmt, uint32_t x, uint32_t y, float out[4])
{
    const size_t off = tilePixelByteOffset(fmt, x, y);
    switch (fmt) {
    case TilePixelFormat::RGBA8: {
        const uint8_t* p = buf + off;
        constexpr float inv = 1.0f / 255.0f;
        out[0] = p[0] * inv;
        out[1] = p[1] * inv;
        out[2] = p[2] * inv;
        out[3] = p[3] * inv;
        break;
    }
    case TilePixelFormat::RGBA16F: {
        qfloat16 h[4];
        std::memcpy(h, buf + off, 4 * sizeof(qfloat16));
        out[0] = static_cast<float>(h[0]);
        out[1] = static_cast<float>(h[1]);
        out[2] = static_cast<float>(h[2]);
        out[3] = static_cast<float>(h[3]);
        break;
    }
    case TilePixelFormat::RGBA32F:
        std::memcpy(out, buf + off, 4 * sizeof(float));
        break;
    }
}

/// Write normalized premultiplied float RGBA into pixel (x,y) of a raw tile
/// buffer. RGBA8 clamps to [0,1] and rounds; float formats store HDR verbatim.
inline void writeTilePixelF(
    uint8_t* buf, TilePixelFormat fmt, uint32_t x, uint32_t y, const float in[4])
{
    const size_t off = tilePixelByteOffset(fmt, x, y);
    switch (fmt) {
    case TilePixelFormat::RGBA8: {
        uint8_t* p = buf + off;
        for (int c = 0; c < 4; ++c) {
            const float v = std::clamp(in[c], 0.0f, 1.0f);
            p[c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
        }
        break;
    }
    case TilePixelFormat::RGBA16F: {
        qfloat16 h[4];
        for (int c = 0; c < 4; ++c)
            h[c] = qfloat16(in[c]);
        std::memcpy(buf + off, h, 4 * sizeof(qfloat16));
        break;
    }
    case TilePixelFormat::RGBA32F:
        std::memcpy(buf + off, in, 4 * sizeof(float));
        break;
    }
}

/// Format-aware "is this pixel's alpha zero" test (replaces hard `[idx+3]==0`).
/// Valid for every format: an all-zero byte pattern is zero in 8-bit, half and
/// float alike.
inline bool tilePixelAlphaIsZero(const uint8_t* buf, TilePixelFormat fmt, uint32_t x, uint32_t y)
{
    const size_t off = tileAlphaByteOffset(fmt, x, y);
    const uint32_t bpc = tileBytesPerChannel(fmt);
    for (uint32_t i = 0; i < bpc; ++i) {
        if (buf[off + i] != 0)
            return false;
    }
    return true;
}

// ==========================================================================
//   T I L E D A T A   C O N V E N I E N C E
// ==========================================================================
//
//   Mirror TileData::getPixel/setPixel semantics (bounds guard, solid-tile
//   handling, dirty marking) but in normalized float and across every format.
//

/// Read pixel (x,y) of a tile as normalized premultiplied float RGBA. Honors
/// solid (uniform-color) tiles and out-of-bounds the same way getPixel() does.
inline void readTilePixelF(const TileData& tile, uint32_t x, uint32_t y, float out[4])
{
    if (x >= TILE_SIZE || y >= TILE_SIZE) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    if (tile.isSolid()) {
        // Solid color is 8-bit packed (Phase 1 invariant); normalize it.
        uint8_t r, g, b, a;
        tile.solidColor(r, g, b, a);
        constexpr float inv = 1.0f / 255.0f;
        out[0] = r * inv;
        out[1] = g * inv;
        out[2] = b * inv;
        out[3] = a * inv;
        return;
    }
    readTilePixelF(tile.pixels(), tile.format(), x, y, out);
}

/// Write normalized premultiplied float RGBA into pixel (x,y), materializing
/// storage and marking dirty (matches setPixel()).
inline void writeTilePixelF(TileData& tile, uint32_t x, uint32_t y, const float in[4])
{
    if (x >= TILE_SIZE || y >= TILE_SIZE)
        return;
    writeTilePixelF(tile.pixels(), tile.format(), x, y, in); // pixels() materializes
    tile.markDirty();
}

} // namespace aether

#endif // RUWA_CORE_TILES_TILEPIXELACCESS_H
