// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <QOpenGLFunctions_4_5_Core>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace aether {

// ==========================================================================
//   G L   P I X E L   P A C K   B U F F E R
// ==========================================================================
//
//   Immutable, persistently mapped GL_PIXEL_PACK_BUFFER for asynchronous
//   texture -> CPU readback. Shared by the tile-grid readbacks in the brush,
//   transform and selection renderers, which previously each carried their own
//   copy of the same glBufferData / glMapBuffer pair.
//
//   Storage is allocated once with glNamedBufferStorage and mapped once with
//   glMapNamedBufferRange; the client pointer stays valid until the buffer is
//   grown or destroyed. Consuming a finished readback is then a plain memcpy
//   out of that pointer — no glMapBuffer/glUnmapBuffer round trip per readback
//   and no glBufferData reallocation on every size change.
//
//   The mapping is deliberately NOT coherent. Coherent readback memory is
//   uncached on several drivers, which makes the (large, multi-megabyte) memcpy
//   out of it markedly slower. Non-coherent instead requires the producer to
//   publish its writes to the client mapping, which endPacking() does via
//   glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT). That barrier MUST be
//   issued before the fence guarding the readback, otherwise the fence can
//   signal while the mapping still reads back stale bytes.
//
//   Usage:
//       if (!pbo.reserve(totalBytes)) return nullptr;   // allocation failed
//       pbo.beginPacking();
//       for each tile: pbo.packTextureLevel(tex, w, h, GL_RGBA, type, offset);
//       pbo.endPacking();
//       GLsync fence = glFenceSync(...);
//       ... later, once the fence has signalled ...
//       memcpy(dst, pbo.data() + offset, bytes);
//
class GLPixelPackBuffer {
public:
    explicit GLPixelPackBuffer(QOpenGLFunctions_4_5_Core* gl)
        : m_gl(gl)
    {
    }

    ~GLPixelPackBuffer() { destroy(); }

    GLPixelPackBuffer(const GLPixelPackBuffer&) = delete;
    GLPixelPackBuffer& operator=(const GLPixelPackBuffer&) = delete;

    /// Guarantee at least `bytes` of mapped capacity. Immutable storage cannot
    /// be resized, so growing recreates and remaps the buffer; the capacity is
    /// rounded up to kGrowthGranularity so strokes of similar size reuse one
    /// allocation instead of reallocating per readback. Never shrinks (the
    /// previous glBufferData path did not either).
    ///
    /// Returns false when allocation or mapping failed — the caller must then
    /// skip the readback entirely rather than packing into a dead buffer.
    bool reserve(size_t bytes)
    {
        if (bytes == 0) {
            return isValid();
        }
        if (isValid() && m_capacity >= bytes) {
            return true;
        }

        constexpr size_t kMaxSize = static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max());
        if (bytes > kMaxSize - kGrowthGranularity) {
            return false;
        }
        const size_t rounded
            = ((bytes + kGrowthGranularity - 1u) / kGrowthGranularity) * kGrowthGranularity;

        destroy();

        m_gl->glCreateBuffers(1, &m_buffer);
        if (m_buffer == 0) {
            return false;
        }

        // CLIENT_STORAGE_BIT: this is a readback staging buffer, so the driver
        // should keep the backing store in host memory rather than VRAM.
        m_gl->glNamedBufferStorage(m_buffer, static_cast<GLsizeiptr>(rounded), nullptr,
            GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_CLIENT_STORAGE_BIT);

        m_mapped = static_cast<uint8_t*>(m_gl->glMapNamedBufferRange(m_buffer, 0,
            static_cast<GLsizeiptr>(rounded), GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT));
        if (m_mapped == nullptr) {
            // Storage allocation failed (out of memory): the name exists but has
            // no usable store, so drop it and report failure.
            m_gl->glDeleteBuffers(1, &m_buffer);
            m_buffer = 0;
            return false;
        }

        m_capacity = rounded;
        return true;
    }

    /// Release the mapping and the buffer. Safe to call repeatedly; must be
    /// called while the GL context is still current (renderer shutdown()).
    void destroy()
    {
        if (m_buffer != 0) {
            if (m_mapped != nullptr) {
                m_gl->glUnmapNamedBuffer(m_buffer);
            }
            m_gl->glDeleteBuffers(1, &m_buffer);
        }
        m_buffer = 0;
        m_mapped = nullptr;
        m_capacity = 0;
    }

    /// Bind as the active pack buffer for a run of packTextureLevel() calls.
    void beginPacking() { m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_buffer); }

    /// Pack the whole of texture level 0 into the buffer at `byteOffset`.
    ///
    /// glGetTextureSubImage reads the texture object directly, so — unlike the
    /// glFramebufferTexture2D + glReadPixels pair this replaces — no FBO is
    /// bound, no attachment is rebound per tile, and the driver does not
    /// re-validate framebuffer completeness once per tile.
    void packTextureLevel(GLuint texture, GLsizei width, GLsizei height, GLenum format, GLenum type,
        size_t byteOffset)
    {
        if (texture == 0 || m_mapped == nullptr || byteOffset >= m_capacity) {
            return;
        }
        // bufSize is a GLsizei; clamp so an oversized capacity cannot wrap it.
        const GLsizei bufSize = static_cast<GLsizei>(std::min<size_t>(
            m_capacity - byteOffset, static_cast<size_t>(std::numeric_limits<GLsizei>::max())));
        m_gl->glGetTextureSubImage(texture, 0, 0, 0, 0, width, height, 1, format, type, bufSize,
            reinterpret_cast<void*>(byteOffset));
    }

    /// Publish the packed bytes to the persistent client mapping and unbind.
    /// Must run before the fence that guards this readback (see class note).
    void endPacking()
    {
        m_gl->glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    bool isValid() const { return m_buffer != 0 && m_mapped != nullptr; }
    size_t capacity() const { return m_capacity; }

    /// Byte view of the mapped storage; null when unallocated. The contents are
    /// only meaningful for a readback whose fence has already signalled.
    const uint8_t* data() const { return m_mapped; }

private:
    // Growth quantum. Large enough that same-sized strokes never reallocate
    // (one RGBA8 tile is 256 KB, so this covers 16 tiles), small enough that
    // the never-shrinking capacity does not strand much pinned host memory.
    static constexpr size_t kGrowthGranularity = 4u * 1024u * 1024u;

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_buffer = 0;
    uint8_t* m_mapped = nullptr;
    size_t m_capacity = 0;
};

} // namespace aether
