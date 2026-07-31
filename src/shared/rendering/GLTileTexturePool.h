// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "shared/rendering/GLTextureFactory.h"
#include "shared/tiles/TileTypes.h"

#include <QOpenGLFunctions_4_5_Core>

#include <unordered_map>
#include <utility>
#include <vector>

namespace aether {

// ==========================================================================
//   G L   T I L E   T E X T U R E   P O O L
// ==========================================================================
//
//   Free-list of TILE_SIZE x TILE_SIZE textures, bucketed by sized internal
//   format and immutable mip-level count.
//
//   Allocating a tile texture is expensive: glCreateTextures +
//   glTextureStorage2D for one 256x256 tile measured ~190 us against ~8 us to
//   recomposite the same tile (see the note in
//   OpenGLCanvasWidget::updateBoardCompositionCache). Strokes create tiles as
//   they travel and the composition cache drops and remakes them constantly,
//   so that allocation cost lands straight in the paint loop. Recycling turns
//   it into a hash lookup plus a GPU-side clear.
//
//   The pool never trusts the caller about what a released texture is: it asks
//   GL for the object's real size and internal format. That matters because
//   tile textures do not all come from ensureTileTexture —
//   GLCompositor::compositeTile swaps one of its own RGBA8 ping-pong textures
//   into a cache TileData, so a TileData::format() is not a reliable
//   description of the texture it currently owns.
//
//   Textures are GL objects, so a pool instance belongs to exactly one
//   context: it lives in GLTileRenderer and is emptied by its shutdown().
//

class GLTileTexturePool {
private:
    struct PoolKey {
        GLenum internalFormat = 0;
        GLsizei levels = 0;

        bool operator==(const PoolKey& other) const
        {
            return internalFormat == other.internalFormat && levels == other.levels;
        }
    };

    struct PoolKeyHash {
        size_t operator()(const PoolKey& key) const
        {
            return (static_cast<size_t>(key.internalFormat) << 8) ^ static_cast<size_t>(key.levels);
        }
    };

public:
    /// Take a texture matching `params`' internal format and mip-level count,
    /// or 0 when the pool has none. The result is zero-filled and carries
    /// `params`' sampler state, so callers cannot tell it from a freshly
    /// created texture.
    GLuint acquire(QOpenGLFunctions_4_5_Core* gl, const TextureParams& params)
    {
        const PoolKey key { params.internalFormat, std::max<GLsizei>(params.levels, 1) };
        auto it = m_buckets.find(key);
        if (it == m_buckets.end() || it->second.empty()) {
            return 0;
        }

        const GLuint texture = it->second.back();
        it->second.pop_back();
        const size_t tileBytes = pooledBytesPerTexture(key.internalFormat, key.levels);
        m_pooledBytes -= (tileBytes <= m_pooledBytes) ? tileBytes : m_pooledBytes;

        // Sampler state lives on the texture object and the previous owner may
        // have wanted different filters or wrapping.
        gl->glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, params.minFilter);
        gl->glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, params.magFilter);
        gl->glTextureParameteri(texture, GL_TEXTURE_WRAP_S, params.wrapS);
        gl->glTextureParameteri(texture, GL_TEXTURE_WRAP_T, params.wrapT);
        // Fresh immutable storage reads back as zero on every driver we target
        // and call sites depend on that (a stroke tile is only cleared when it
        // is NEW; a composite tile is fully overdrawn but empty regions must
        // stay empty). A recycled texture still holds the previous tile, so
        // wipe it. A 256 KB GPU-side clear is two orders of magnitude cheaper
        // than the allocation it replaces.
        gl->glClearTexImage(texture, 0, params.pixelFormat, params.pixelType, nullptr);
        return texture;
    }

    /// Make everything released since the last call available to acquire().
    /// Call once per frame, before the frame's passes run.
    ///
    /// The one-frame delay exists because glDeleteTextures implicitly unbinds
    /// the texture from every binding point, and handing it back does not. A
    /// texture released mid-frame could otherwise be re-acquired as a render
    /// target while a sampler unit from earlier in that same frame still points
    /// at it. Every pass binds the units it samples, so a frame boundary is
    /// enough separation.
    void promotePending()
    {
        for (const auto& entry : m_pending) {
            m_buckets[entry.first].push_back(entry.second);
        }
        m_pending.clear();
    }

    /// Hand a texture back. Returns false when the caller must delete it
    /// itself — wrong size, a format the pool does not recycle, a stale name,
    /// or the budget is full. Accepted textures become available to acquire()
    /// at the next promotePending().
    bool release(QOpenGLFunctions_4_5_Core* gl, GLuint texture)
    {
        if (texture == 0) {
            return false;
        }
        // Orphaned ids can outlive their context (TileData destructors run on
        // worker threads and during teardown). Asking about a dead name would
        // raise GL_INVALID_OPERATION on every query below, so check first.
        if (gl->glIsTexture(texture) == GL_FALSE) {
            return false;
        }

        GLint width = 0;
        GLint height = 0;
        GLint internalFormat = 0;
        GLint levels = 0;
        gl->glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_WIDTH, &width);
        gl->glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_HEIGHT, &height);
        gl->glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
        gl->glGetTextureParameteriv(texture, GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
        if (width != static_cast<GLint>(TILE_SIZE) || height != static_cast<GLint>(TILE_SIZE)) {
            return false;
        }

        const GLenum format = static_cast<GLenum>(internalFormat);
        const size_t tileBytes = pooledBytesPerTexture(format, levels);
        if (tileBytes == 0 || m_pooledBytes + tileBytes > kMaxPooledBytes) {
            return false;
        }

        m_pending.emplace_back(PoolKey { format, levels }, texture);
        m_pooledBytes += tileBytes;
        return true;
    }

    void clear(QOpenGLFunctions_4_5_Core* gl)
    {
        for (auto& bucket : m_buckets) {
            std::vector<GLuint>& textures = bucket.second;
            if (!textures.empty()) {
                gl->glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
            }
        }
        for (const auto& entry : m_pending) {
            GLuint id = entry.second;
            gl->glDeleteTextures(1, &id);
        }
        m_buckets.clear();
        m_pending.clear();
        m_pooledBytes = 0;
    }

    size_t pooledBytes() const { return m_pooledBytes; }

private:
    /// VRAM one pooled tile costs, or 0 for formats that are not recycled —
    /// only the formats tile textures are actually created with are accepted,
    /// so an unexpected object can never be handed to a caller expecting tile
    /// storage.
    static size_t pooledBytesPerTexture(GLenum internalFormat, GLsizei levels)
    {
        size_t bytesPerTexel = 0;
        switch (internalFormat) {
        case GL_RGBA8:
            bytesPerTexel = 4u;
            break;
        case GL_RGBA16F:
            bytesPerTexel = 8u;
            break;
        case GL_RGBA32F:
            bytesPerTexel = 16u;
            break;
        default:
            return 0u;
        }

        if (levels <= 0) {
            return 0u;
        }

        size_t texels = 0;
        size_t width = TILE_SIZE;
        size_t height = TILE_SIZE;
        for (GLsizei level = 0; level < levels; ++level) {
            texels += width * height;
            width = std::max<size_t>(width >> 1, 1u);
            height = std::max<size_t>(height >> 1, 1u);
        }
        return texels * bytesPerTexel;
    }

    // Ceiling on retained VRAM: enough to absorb a screenful of tiles churning
    // between frames without letting an idle document sit on a large
    // reservation. With full mip chains, 64 MB is about 192 RGBA8 display
    // tiles, or 48 at RGBA32F.
    static constexpr size_t kMaxPooledBytes = 64u * 1024u * 1024u;

    std::unordered_map<PoolKey, std::vector<GLuint>, PoolKeyHash> m_buckets;
    // Released this frame, available from the next promotePending(). Counted
    // against the budget immediately so a burst cannot overshoot it.
    std::vector<std::pair<PoolKey, GLuint>> m_pending;
    size_t m_pooledBytes = 0;
};

} // namespace aether
