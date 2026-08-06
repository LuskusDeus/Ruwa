// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <QOpenGLFunctions_4_5_Core>

#include "shared/tiles/TileFormat.h"

#include <algorithm>

namespace aether {

struct TextureParams {
    GLenum minFilter = GL_NEAREST;
    GLenum magFilter = GL_NEAREST;
    GLenum wrapS = GL_CLAMP_TO_EDGE;
    GLenum wrapT = GL_CLAMP_TO_EDGE;
    GLenum internalFormat = GL_RGBA8;
    GLenum pixelFormat = GL_RGBA;
    GLenum pixelType = GL_UNSIGNED_BYTE;
    /// Mip levels to allocate. 1 = no chain (the default everywhere except the
    /// blur brush's ROI scratch, which stores its blur pyramid in the mips).
    GLsizei levels = 1;
};

/// Full mip chain depth for a texture of this size, capped at `maxLevels`.
inline GLsizei mipLevelsFor(GLsizei width, GLsizei height, GLsizei maxLevels = 16)
{
    GLsizei minDim = std::min(width, height);
    GLsizei levels = 1;
    while (minDim > 1 && levels < maxLevels) {
        minDim >>= 1;
        ++levels;
    }
    return levels;
}

// ---- Tile pixel format -> GL enums ----
//
//   The single mapping from the abstract TilePixelFormat to the GL texture
//   internal format and the matching upload pixel type. Used by the document
//   tile texture path (GLTileRenderer) so the GPU format follows the tile.

inline GLenum tileGLInternalFormat(TilePixelFormat f)
{
    switch (f) {
    case TilePixelFormat::RGBA8:
        return GL_RGBA8;
    case TilePixelFormat::RGBA16F:
        return GL_RGBA16F;
    case TilePixelFormat::RGBA32F:
        return GL_RGBA32F;
    }
    return GL_RGBA8;
}

inline GLenum tileGLPixelType(TilePixelFormat f)
{
    switch (f) {
    case TilePixelFormat::RGBA8:
        return GL_UNSIGNED_BYTE;
    case TilePixelFormat::RGBA16F:
        return GL_HALF_FLOAT;
    case TilePixelFormat::RGBA32F:
        return GL_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}

/// Build TextureParams for a document tile texture of the given format,
/// keeping the tile sampling filters (linear min / nearest mag).
inline TextureParams tileTextureParams(
    TilePixelFormat f, GLenum minFilter = GL_LINEAR, GLenum magFilter = GL_NEAREST)
{
    TextureParams p;
    p.minFilter = minFilter;
    p.magFilter = magFilter;
    p.internalFormat = tileGLInternalFormat(f);
    p.pixelFormat = GL_RGBA;
    p.pixelType = tileGLPixelType(f);
    return p;
}

/// Create a 2D texture with immutable storage.
///
/// glTextureStorage2D fixes the size and format for the texture's lifetime,
/// which lets the driver validate completeness once at creation instead of
/// re-checking on every use — worth having when a document holds thousands of
/// tile textures. It also makes the texture usable as an image-load/store
/// binding, which requires immutable format.
///
/// The whole call is direct-state-access, so it neither binds the texture nor
/// disturbs the currently bound one.
///
/// IMPORTANT: the storage cannot be re-specified afterwards — a later
/// glTexImage2D on this texture fails with GL_INVALID_OPERATION and silently
/// leaves the old contents. To change size or format use recreateTexture2D().
/// Uploading pixels into the existing storage (glTextureSubImage2D /
/// glTexSubImage2D), clearing it (glClearTexImage) and copying into it
/// (glCopyImageSubData) all remain valid.
///
/// `p.internalFormat` must be a SIZED format (GL_RGBA8, GL_RGBA16F, ...);
/// unsized formats such as GL_RGBA are not accepted by glTextureStorage2D.
inline GLuint createTexture2D(QOpenGLFunctions_4_5_Core* gl, GLsizei width, GLsizei height,
    const TextureParams& p = {}, const void* data = nullptr)
{
    GLuint tex = 0;
    gl->glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    if (!tex)
        return 0;
    gl->glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, p.minFilter);
    gl->glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, p.magFilter);
    gl->glTextureParameteri(tex, GL_TEXTURE_WRAP_S, p.wrapS);
    gl->glTextureParameteri(tex, GL_TEXTURE_WRAP_T, p.wrapT);
    gl->glTextureStorage2D(tex, std::max<GLsizei>(p.levels, 1), p.internalFormat, width, height);
    if (data != nullptr) {
        gl->glTextureSubImage2D(tex, 0, 0, 0, width, height, p.pixelFormat, p.pixelType, data);
    }
    return tex;
}

inline void deleteTexture(QOpenGLFunctions_4_5_Core* gl, GLuint& tex)
{
    if (tex) {
        gl->glDeleteTextures(1, &tex);
        tex = 0;
    }
}

/// Replace `tex` with a new texture of the given size/format, in place.
///
/// Immutable storage cannot be resized or re-formatted, so what used to be a
/// re-specifying glTexImage2D on the same object becomes delete + create. The
/// name changes, so callers must not cache the old id (GL may even hand the
/// same number back for the new object).
inline void recreateTexture2D(QOpenGLFunctions_4_5_Core* gl, GLuint& tex, GLsizei width,
    GLsizei height, const TextureParams& p = {}, const void* data = nullptr)
{
    deleteTexture(gl, tex);
    tex = createTexture2D(gl, width, height, p, data);
}

} // namespace aether
