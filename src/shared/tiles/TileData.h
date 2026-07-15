// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   D A T A
// ==========================================================================

#ifndef RUWA_CORE_TILES_TILEDATA_H
#define RUWA_CORE_TILES_TILEDATA_H

#include "TileTypes.h"
#include "TileFormat.h"

#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace aether {

// ==========================================================================
//   O R P H A N E D   T E X T U R E   C O L L E C T O R
// ==========================================================================
//
//   Thread-safe collector for GPU texture IDs that could not be deleted
//   at TileData destruction time (no GL context, wrong thread, etc.).
//   Call flushOrphanedTextures() once per frame from the GL thread.
//

class OrphanedTextureCollector {
public:
    static OrphanedTextureCollector& instance()
    {
        static OrphanedTextureCollector s_instance;
        return s_instance;
    }

    void add(uint32_t textureId)
    {
        if (textureId == 0)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_orphaned.push_back(textureId);
    }

    std::vector<uint32_t> takeAll()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> result;
        result.swap(m_orphaned);
        return result;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_orphaned.empty();
    }

private:
    OrphanedTextureCollector() = default;
    mutable std::mutex m_mutex;
    std::vector<uint32_t> m_orphaned;
};

// ==========================================================================
//   T I L E   D A T A
// ==========================================================================
//
//   IMPORTANT: Pixel data is stored in PREMULTIPLIED ALPHA format.
//   Each pixel (R,G,B,A) satisfies: R <= A, G <= A, B <= A.
//   The RGB channels are already multiplied by alpha:
//       stored_r = actual_r * (a / 255)
//       stored_g = actual_g * (a / 255)
//       stored_b = actual_b * (a / 255)
//
//   This is required for correct GPU blending and blend mode compositing.
//   Use premultiplied src-over: dst = src + dst * (1 - src_a)
//

class TileData {
public:
    TileData() = default;

    ~TileData()
    {
        if (m_textureId != 0) {
            OrphanedTextureCollector::instance().add(m_textureId);
            m_textureId = 0;
        }
    }

    TileData(const TileData&) = delete;
    TileData& operator=(const TileData&) = delete;

    TileData(TileData&& other) noexcept
        : m_pixels(std::move(other.m_pixels))
        , m_textureId(other.m_textureId)
        , m_format(other.m_format)
        , m_dirty(other.m_dirty)
        , m_solid(other.m_solid)
        , m_solidColor(other.m_solidColor)
    {
        other.m_textureId = 0;
        other.m_dirty = false;
        other.m_solid = false;
        other.m_solidColor = 0;
    }

    TileData& operator=(TileData&& other) noexcept
    {
        if (this != &other) {
            m_pixels = std::move(other.m_pixels);
            m_textureId = other.m_textureId;
            m_format = other.m_format;
            m_dirty = other.m_dirty;
            m_solid = other.m_solid;
            m_solidColor = other.m_solidColor;
            other.m_textureId = 0;
            other.m_dirty = false;
            other.m_solid = false;
            other.m_solidColor = 0;
        }
        return *this;
    }

    // ---- Pixel format ----
    //
    //   The per-pixel storage format of this tile. Drives CPU buffer sizing and
    //   GPU texture format. Defaults to the document-wide kDefaultTileFormat.
    //   setFormat() must be called before any pixel storage is allocated (it
    //   only stamps metadata; it does not convert existing pixels) — Phase 1
    //   stamps it on freshly created tiles. Per-pixel access below is 8-bit and
    //   only valid for RGBA8 (Phase 2 widens it).

    TilePixelFormat format() const { return m_format; }
    void setFormat(TilePixelFormat fmt) { m_format = fmt; }

    // ---- Solid (uniform-color) tile state ----
    //
    //   A tile may represent a single uniform premultiplied RGBA color across
    //   all TILE_SIZE x TILE_SIZE pixels WITHOUT allocating the full pixel
    //   buffer. The color is packed as bytes r | g<<8 | b<<16 | a<<24.
    //
    //   Any per-pixel write (setPixel / non-const pixels()) materializes the
    //   tile: it allocates the buffer, fills it with the solid color, and clears
    //   the solid flag. Hot read-only consumers (the compositor) check isSolid()
    //   and use the packed color directly instead of forcing materialization.
    //
    //   This is used by layer masks so an "infinite" uniform background (e.g. a
    //   hide-all mask) costs no per-tile memory until the user paints into it.

    static uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
            | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }

    static void unpackColor(uint32_t packed, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a)
    {
        r = static_cast<uint8_t>(packed & 0xFFu);
        g = static_cast<uint8_t>((packed >> 8) & 0xFFu);
        b = static_cast<uint8_t>((packed >> 16) & 0xFFu);
        a = static_cast<uint8_t>((packed >> 24) & 0xFFu);
    }

    /// Mark this tile as a uniform color, dropping any pixel buffer/texture.
    void setSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        m_solid = true;
        m_solidColor = packColor(r, g, b, a);
        m_pixels.clear();
        m_pixels.shrink_to_fit();
        if (m_textureId != 0) {
            OrphanedTextureCollector::instance().add(m_textureId);
            m_textureId = 0;
        }
        m_dirty = true;
    }

    void setSolidPacked(uint32_t packed)
    {
        uint8_t r, g, b, a;
        unpackColor(packed, r, g, b, a);
        setSolid(r, g, b, a);
    }

    bool isSolid() const { return m_solid && m_pixels.empty(); }
    uint32_t solidColorPacked() const { return m_solidColor; }
    void solidColor(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const
    {
        unpackColor(m_solidColor, r, g, b, a);
    }

    // ---- CPU pixel access ----

    uint8_t* pixels()
    {
        materializeSolid();
        ensurePixelStorage();
        return m_pixels.data();
    }

    const uint8_t* pixels() const
    {
        return m_pixels.empty() ? zeroPixels().data() : m_pixels.data();
    }

    void setPixel(uint32_t localX, uint32_t localY, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (localX >= TILE_SIZE || localY >= TILE_SIZE)
            return;
        materializeSolid();
        ensurePixelStorage();
        uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
        m_pixels[idx + 0] = r;
        m_pixels[idx + 1] = g;
        m_pixels[idx + 2] = b;
        m_pixels[idx + 3] = a;
        m_dirty = true;
    }

    void getPixel(
        uint32_t localX, uint32_t localY, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const
    {
        if (localX >= TILE_SIZE || localY >= TILE_SIZE) {
            r = g = b = a = 0;
            return;
        }
        if (m_pixels.empty()) {
            if (m_solid) {
                unpackColor(m_solidColor, r, g, b, a);
            } else {
                r = g = b = a = 0;
            }
            return;
        }
        uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
        r = m_pixels[idx + 0];
        g = m_pixels[idx + 1];
        b = m_pixels[idx + 2];
        a = m_pixels[idx + 3];
    }

    void clear()
    {
        m_solid = false;
        m_solidColor = 0;
        if (!m_pixels.empty()) {
            std::memset(m_pixels.data(), 0, m_pixels.size());
        }
        m_dirty = true;
    }

    /// Check if tile has any non-transparent pixel (for sparse cleanup).
    /// Scans the raw buffer as 32-bit words: an all-zero byte pattern is the
    /// fully-transparent value in every supported format (0.0 half/float is
    /// all-zero bits), so this is correct regardless of m_format.
    bool isEmpty() const
    {
        if (m_pixels.empty()) {
            // A solid tile is "empty" only when its color is fully transparent.
            return !m_solid || (m_solidColor >> 24) == 0u;
        }
        const uint32_t* data32 = reinterpret_cast<const uint32_t*>(m_pixels.data());
        const uint32_t count = static_cast<uint32_t>(m_pixels.size() / sizeof(uint32_t));
        for (uint32_t i = 0; i < count; ++i) {
            if (data32[i] != 0)
                return false;
        }
        return true;
    }

    // ---- GPU texture ----

    uint32_t textureId() const { return m_textureId; }
    void setTextureId(uint32_t id) { m_textureId = id; }
    bool hasTexture() const { return m_textureId != 0; }

    // ---- Dirty tracking ----

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

private:
    /// Shared all-zero buffer returned by const pixels() for an unallocated
    /// tile. Sized to the largest supported format so a consumer reading
    /// tileByteSize(format) bytes for any format stays in bounds.
    static const std::array<uint8_t, tileMaxByteSize()>& zeroPixels()
    {
        static const std::array<uint8_t, tileMaxByteSize()> s_zeroPixels {};
        return s_zeroPixels;
    }

    void ensurePixelStorage()
    {
        if (m_pixels.empty()) {
            m_pixels.resize(tileByteSize(m_format), 0);
        }
    }

    /// Promote a solid (uniform-color) tile to a real pixel buffer filled with
    /// the solid color, then drop the solid flag. No-op for non-solid tiles.
    void materializeSolid()
    {
        if (!m_solid)
            return;
        m_solid = false;
        if (m_pixels.empty()) {
            uint8_t r, g, b, a;
            unpackColor(m_solidColor, r, g, b, a);
            m_pixels.resize(tileByteSize(m_format));
            // Broadcast the 8-bit packed color across the buffer in the tile's
            // actual format (RGBA8 / RGBA16F / RGBA32F). The solid color itself
            // stays 8-bit; only the materialized pixels adopt the wider format.
            fillTileSolid(m_pixels.data(), m_format, r, g, b, a);
        }
        m_dirty = true;
    }

    std::vector<uint8_t> m_pixels;
    uint32_t m_textureId = 0; // OpenGL texture name (managed externally)
    TilePixelFormat m_format = kDefaultTileFormat; // Per-pixel storage format
    bool m_dirty = true; // Needs GPU upload
    bool m_solid = false; // Uniform-color tile with no allocated buffer
    uint32_t m_solidColor = 0; // Packed premultiplied RGBA (r|g<<8|b<<16|a<<24)
};

} // namespace aether

#endif // RUWA_CORE_TILES_TILEDATA_H
