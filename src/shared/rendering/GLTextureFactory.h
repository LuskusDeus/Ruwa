// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <QOpenGLFunctions_4_5_Core>

#include "shared/tiles/TileFormat.h"

namespace aether {

struct TextureParams {
    GLenum minFilter = GL_NEAREST;
    GLenum magFilter = GL_NEAREST;
    GLenum wrapS = GL_CLAMP_TO_EDGE;
    GLenum wrapT = GL_CLAMP_TO_EDGE;
    GLenum internalFormat = GL_RGBA8;
    GLenum pixelFormat = GL_RGBA;
    GLenum pixelType = GL_UNSIGNED_BYTE;
};

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

inline GLuint createTexture2D(QOpenGLFunctions_4_5_Core* gl, GLsizei width, GLsizei height,
    const TextureParams& p = {}, const void* data = nullptr)
{
    GLuint tex = 0;
    gl->glGenTextures(1, &tex);
    if (!tex)
        return 0;
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, p.minFilter);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, p.magFilter);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, p.wrapS);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, p.wrapT);
    gl->glTexImage2D(
        GL_TEXTURE_2D, 0, p.internalFormat, width, height, 0, p.pixelFormat, p.pixelType, data);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

inline void deleteTexture(QOpenGLFunctions_4_5_Core* gl, GLuint& tex)
{
    if (tex) {
        gl->glDeleteTextures(1, &tex);
        tex = 0;
    }
}

} // namespace aether
