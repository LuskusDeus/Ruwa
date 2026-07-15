// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   F O R M A T
// ==========================================================================
//
//   Single source of truth for the per-pixel storage format of tile RGBA data.
//
//   Phase 1 scope (this header): the format is a named value that drives
//     - CPU pixel-buffer byte sizing (TileData)
//     - GPU texture internal format & upload pixel type (GLTextureFactory /
//       GLTileRenderer)
//   so that switching the document format becomes a localized change instead
//   of editing scattered GL_RGBA8 / GL_UNSIGNED_BYTE / uint8_t literals.
//
//   The format migration is complete: per-pixel float read/write goes through
//   TilePixelAccess.h (readTilePixelF/writeTilePixelF), and the document format
//   is per-document (chosen at New Project, stored on the document and in the
//   .rwf). The fallback default below is pinned to RGBA8 (see the note at
//   kDefaultTileFormat) — the 8-bit byte helpers in TileData stay valid for it.
//

#ifndef RUWA_CORE_TILES_TILEFORMAT_H
#define RUWA_CORE_TILES_TILEFORMAT_H

#include "TileTypes.h"

#include <cstdint>
#include <cstring>
#include <algorithm>

namespace aether {

// ==========================================================================
//   T I L E   P I X E L   F O R M A T
// ==========================================================================

enum class TilePixelFormat : uint8_t {
    RGBA8 = 0, // 8-bit  unsigned normalized   (1 byte / channel)
    RGBA16F = 1, // 16-bit half float            (2 bytes / channel)
    RGBA32F = 2, // 32-bit float                 (4 bytes / channel)
};

/// Bytes occupied by a single channel in the given format.
constexpr uint32_t tileBytesPerChannel(TilePixelFormat f)
{
    switch (f) {
    case TilePixelFormat::RGBA8:
        return 1;
    case TilePixelFormat::RGBA16F:
        return 2;
    case TilePixelFormat::RGBA32F:
        return 4;
    }
    return 1;
}

/// Bytes occupied by a single RGBA pixel in the given format.
constexpr uint32_t tileBytesPerPixel(TilePixelFormat f)
{
    return tileBytesPerChannel(f) * TILE_CHANNELS;
}

/// Byte size of a full TILE_SIZE x TILE_SIZE buffer in the given format.
constexpr uint32_t tileByteSize(TilePixelFormat f)
{
    return TILE_SIZE * TILE_SIZE * tileBytesPerPixel(f);
}

/// Largest possible tile byte size across every supported format. Used to size
/// shared scratch/zero buffers that must be valid regardless of format.
constexpr uint32_t tileMaxByteSize()
{
    return tileByteSize(TilePixelFormat::RGBA32F);
}

inline const char* tileFormatName(TilePixelFormat f)
{
    switch (f) {
    case TilePixelFormat::RGBA8:
        return "RGBA8";
    case TilePixelFormat::RGBA16F:
        return "RGBA16F";
    case TilePixelFormat::RGBA32F:
        return "RGBA32F";
    }
    return "RGBA8";
}

// ==========================================================================
//   S O L I D - T I L E   F I L L   ( F O R M A T - A W A R E )
// ==========================================================================
//
//   A "solid" tile stores its uniform color as an 8-bit packed premultiplied
//   RGBA value. Materializing it into a real pixel buffer means broadcasting
//   that color across every pixel — but the per-pixel byte layout differs by
//   format, so a naive 32-bit broadcast only works for RGBA8. These helpers do
//   the format-correct broadcast. This header stays Qt-free (no qfloat16), so
//   the half-float packing is a self-contained IEEE-754 float32->float16.
//

/// Qt-free round-to-nearest-even IEEE-754 float32 -> float16 (half) bit
/// conversion. Handles zero/subnormal/normal/overflow/NaN. Solid colors are
/// premultiplied and normalized to [0,1], but the full range is supported.
inline uint16_t floatToHalfBits(float value)
{
    uint32_t x;
    std::memcpy(&x, &value, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    const uint32_t expField = (x >> 23) & 0xFFu;
    const uint32_t mant = x & 0x7FFFFFu;

    if (expField == 0xFFu) { // Inf / NaN
        return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x0200u : 0u));
    }

    const int32_t exp = static_cast<int32_t>(expField) - 127 + 15;
    if (exp >= 0x1F) { // Overflow -> Inf
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exp <= 0) { // Subnormal / zero
        if (exp < -10) {
            return static_cast<uint16_t>(sign);
        }
        uint32_t m = mant | 0x800000u; // restore implicit leading 1
        const int shift = 14 - exp; // shift in [14, 24]
        uint32_t half = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (half & 1u))) {
            ++half; // round to nearest even
        }
        return static_cast<uint16_t>(sign | half);
    }

    uint16_t half = static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
    const uint32_t rem = mant & 0x1FFFu; // dropped low 13 bits
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) {
        ++half; // round to nearest even (may carry into exp)
    }
    return half;
}

/// Fill an entire tile buffer (already sized tileByteSize(fmt)) with a single
/// premultiplied 8-bit color, honoring the destination format's per-pixel
/// encoding. Mirrors readTilePixelF/writeTilePixelF normalization (byte / 255).
inline void fillTileSolid(
    uint8_t* buf, TilePixelFormat fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
    switch (fmt) {
    case TilePixelFormat::RGBA8: {
        const uint32_t packed = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
            | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
        uint32_t* p = reinterpret_cast<uint32_t*>(buf);
        std::fill(p, p + pixelCount, packed);
        break;
    }
    case TilePixelFormat::RGBA16F: {
        constexpr float inv = 1.0f / 255.0f;
        const uint16_t px[4] = { floatToHalfBits(r * inv), floatToHalfBits(g * inv),
            floatToHalfBits(b * inv), floatToHalfBits(a * inv) };
        uint16_t* p = reinterpret_cast<uint16_t*>(buf);
        for (uint32_t i = 0; i < pixelCount; ++i) {
            std::memcpy(p + i * 4, px, sizeof(px));
        }
        break;
    }
    case TilePixelFormat::RGBA32F: {
        constexpr float inv = 1.0f / 255.0f;
        const float px[4] = { r * inv, g * inv, b * inv, a * inv };
        float* p = reinterpret_cast<float*>(buf);
        for (uint32_t i = 0; i < pixelCount; ++i) {
            std::memcpy(p + i * 4, px, sizeof(px));
        }
        break;
    }
    }
}

// ==========================================================================
//   F A L L B A C K   T I L E   F O R M A T
// ==========================================================================
//
//   Per-document tile format is now live: the real document format is chosen at
//   New Project (8/16/32-bit), stored on the document (LayerModel /
//   WorkspaceTab / the .rwf contentTileFormat tag) and stamped onto every
//   content grid, so a document's tiles are self-describing via grid.format().
//
//   This constant is therefore NO LONGER the application-wide format. It is only
//   the FALLBACK default for grids created WITHOUT a document context
//   (scratch/preview/warm-up grids that have no owning document to inherit
//   from). RGBA8 is the correct shipping default here: cheapest, and byte-
//   identical to the pre-refactor path (tileByteSize(RGBA8) == TILE_BYTE_SIZE).
//
//   To smoke-test the higher-precision paths, create a 16- or 32-bit project
//   from New Project (do NOT flip this back to 16F — that only changes the
//   context-less fallback and masks real per-document divergence).
constexpr TilePixelFormat kDefaultTileFormat = TilePixelFormat::RGBA8;

} // namespace aether

#endif // RUWA_CORE_TILES_TILEFORMAT_H
