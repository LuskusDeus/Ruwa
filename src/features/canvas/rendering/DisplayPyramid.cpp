// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   D I S P L A Y   P Y R A M I D
// ==========================================================================

#include "features/canvas/rendering/DisplayPyramid.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLStateGuard.h"
#include "shared/rendering/GLTextureFactory.h"

#include <QByteArray>

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

/// Retained 258x258 storage ceiling. A screenful of level tiles is a few dozen;
/// this absorbs the churn of a zoom sweep without holding a large reservation.
constexpr size_t kMaxFreeTextures = 64;

TextureParams pyramidTextureParams(TilePixelFormat format)
{
    // Sampled bilinearly at display and by texelFetch when it feeds the next
    // level up, so LINEAR both ways with no mip chain of its own — the pyramid
    // IS the chain.
    TextureParams params;
    params.minFilter = GL_LINEAR;
    params.magFilter = GL_LINEAR;
    params.wrapS = GL_CLAMP_TO_EDGE;
    params.wrapT = GL_CLAMP_TO_EDGE;
    params.internalFormat = tileGLInternalFormat(format);
    params.pixelFormat = GL_RGBA;
    params.pixelType = tileGLPixelType(format);
    params.levels = 1;
    return params;
}

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

DisplayPyramid::DisplayPyramid(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

DisplayPyramid::~DisplayPyramid()
{
    shutdown();
}

Result<void> DisplayPyramid::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_downsampleProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto loaded = m_downsampleProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/pyramid_downsample.frag.glsl");
    if (!loaded) {
        m_downsampleProgram.reset();
        return loaded;
    }

    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        m_downsampleProgram.reset();
        return { ErrorCode::PipelineCreationFailed, "Failed to create display pyramid VAO" };
    }

    m_gl->glCreateFramebuffers(1, &m_fbo);
    if (m_fbo == 0) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
        m_downsampleProgram.reset();
        return { ErrorCode::PipelineCreationFailed, "Failed to create display pyramid FBO" };
    }
    m_gl->glNamedFramebufferDrawBuffer(m_fbo, GL_COLOR_ATTACHMENT0);

    // The sampler unit assignment never changes: block index i is always unit i.
    m_downsampleProgram->use();
    for (int i = 0; i < 16; ++i) {
        const QByteArray name = QByteArray("uParents[") + QByteArray::number(i) + "]";
        m_downsampleProgram->setUniform(name.constData(), i);
    }

    m_initialized = true;

    auto transparent = ensureTransparentTexture();
    if (!transparent) {
        shutdown();
        return transparent;
    }

    return Result<void>::ok();
}

void DisplayPyramid::shutdown()
{
    if (m_gl != nullptr) {
        clear();

        if (m_transparentTexture != 0) {
            m_gl->glDeleteTextures(1, &m_transparentTexture);
            m_transparentTexture = 0;
        }
        if (m_fbo != 0) {
            m_gl->glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
        if (m_emptyVAO != 0) {
            m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
            m_emptyVAO = 0;
        }
    }

    m_downsampleProgram.reset();
    m_initialized = false;
}

Result<void> DisplayPyramid::ensureTransparentTexture()
{
    if (m_transparentTexture != 0) {
        return Result<void>::ok();
    }

    const TextureParams params = pyramidTextureParams(m_format);
    m_transparentTexture = createTexture2D(m_gl, kTextureSize, kTextureSize, params);
    if (m_transparentTexture == 0) {
        return { ErrorCode::PipelineCreationFailed,
            "Failed to create display pyramid transparent texture" };
    }
    m_gl->glClearTexImage(m_transparentTexture, 0, params.pixelFormat, params.pixelType, nullptr);
    return Result<void>::ok();
}

// ==========================================================================
//   L E V E L   M A T H
// ==========================================================================

float DisplayPyramid::continuousLevelForZoom(float zoom)
{
    const float safeZoom = std::max(zoom, 1.0e-6f);
    return std::log2(1.0f / safeZoom);
}

// ==========================================================================
//   I N V A L I D A T I O N
// ==========================================================================

void DisplayPyramid::invalidate(const TileKey& level0Key)
{
    if (m_needsSeed) {
        // The next update() re-derives everything from the source grid anyway.
        return;
    }
    for (int level = 1; level <= kMaxLevel; ++level) {
        // Ancestors are marked as a chain, so finding one already dirty means
        // every level above it was marked by the same call that dirtied it.
        if (!m_dirty[level].insert(ancestorKey(level0Key, level)).second) {
            break;
        }
    }
}

void DisplayPyramid::invalidateAll()
{
    for (auto& level : m_dirty) {
        level.clear();
    }
    m_needsSeed = true;
    m_pendingWork = true;
}

void DisplayPyramid::clear()
{
    for (auto& level : m_levels) {
        for (auto& entry : level) {
            if (entry.second.texture != 0) {
                m_gl->glDeleteTextures(1, &entry.second.texture);
            }
        }
        level.clear();
    }
    for (auto& level : m_dirty) {
        level.clear();
    }
    if (!m_freeTextures.empty()) {
        m_gl->glDeleteTextures(
            static_cast<GLsizei>(m_freeTextures.size()), m_freeTextures.data());
        m_freeTextures.clear();
    }
    m_needsSeed = true;
    m_pendingWork = true;
}

void DisplayPyramid::adoptFormat(TilePixelFormat format)
{
    if (format == m_format) {
        return;
    }
    // Pyramid storage must match the cache's per-document format, not the
    // build-wide default: a 16F document downsampled through RGBA8 would band.
    clear();
    m_format = format;
    if (m_transparentTexture != 0) {
        m_gl->glDeleteTextures(1, &m_transparentTexture);
        m_transparentTexture = 0;
    }
    ensureTransparentTexture();
}

void DisplayPyramid::seedFrom(const TileGrid& source)
{
    for (auto& level : m_dirty) {
        level.clear();
    }
    m_needsSeed = false;
    for (const auto& entry : source.tiles()) {
        invalidate(entry.first);
    }
}

// ==========================================================================
//   B U I L D
// ==========================================================================

DisplayPyramid::KeyRange DisplayPyramid::rangeForLevel(
    const UpdateRequest& request, int level) const
{
    const float span = static_cast<float>(levelSpanPixels(level));
    KeyRange range;
    // One tile of slack on every side: building a level-L tile reads the 4x4
    // block of level-(L-1) tiles, which reaches one tile past the 2x2 core.
    // Applying the pad at every level makes the descending ranges nest.
    range.minX = static_cast<int32_t>(std::floor(request.worldMinX / span)) - 1;
    range.minY = static_cast<int32_t>(std::floor(request.worldMinY / span)) - 1;
    range.maxX = static_cast<int32_t>(std::floor(request.worldMaxX / span)) + 1;
    range.maxY = static_cast<int32_t>(std::floor(request.worldMaxY / span)) + 1;
    return range;
}

bool DisplayPyramid::update(const TileGrid& source, const UpdateRequest& request)
{
    m_lastBuildCount = 0;
    if (!m_initialized || m_transparentTexture == 0) {
        return true;
    }

    adoptFormat(source.format());
    if (m_needsSeed) {
        seedFrom(source);
    }

    // The display lerps level L with L+1, so both must be current.
    const int topLevel = std::clamp(request.topLevel + 1, 1, kMaxLevel);

    bool anyDirty = false;
    for (int level = 1; level <= topLevel; ++level) {
        if (!m_dirty[level].empty()) {
            anyDirty = true;
            break;
        }
    }
    if (!anyDirty) {
        m_pendingWork = false;
        return true;
    }

    GLFboViewportBlendGuard guard(m_gl);
    m_gl->glDisable(GL_BLEND);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glViewport(0, 0, kTextureSize, kTextureSize);
    m_downsampleProgram->use();
    m_gl->glBindVertexArray(m_emptyVAO);

    bool complete = true;
    std::vector<TileKey> batch;
    for (int level = 1; level <= topLevel && complete; ++level) {
        if (m_dirty[level].empty()) {
            continue;
        }
        const KeyRange range = rangeForLevel(request, level);

        batch.clear();
        for (const TileKey& key : m_dirty[level]) {
            if (range.contains(key)) {
                batch.push_back(key);
            }
        }
        if (batch.empty()) {
            continue;
        }

        // Level 1 reads composition-cache tiles (256x256, no apron); every
        // level above reads pyramid tiles (258x258, apron 1).
        m_downsampleProgram->setUniform("uParentApron", level == 1 ? 0 : kApron);

        for (const TileKey& key : batch) {
            if (request.budget != 0 && m_lastBuildCount >= request.budget) {
                complete = false;
                break;
            }
            buildTile(source, level, key);
            m_dirty[level].erase(key);
            ++m_lastBuildCount;
        }
    }

    m_gl->glBindVertexArray(0);
    // Composition-cache tiles were bound here as level-1 parents, and the
    // compositor renders straight INTO those same texture objects (it swaps a
    // ping-pong texture into the cache TileData). Leaving them bound would let
    // the next frame's composite draw into a texture still attached to a
    // sampler unit.
    m_gl->glBindTextures(0, 16, nullptr);

    m_pendingWork = !complete;
    return complete;
}

bool DisplayPyramid::buildTile(const TileGrid& source, int level, const TileKey& key)
{
    // The 4x4 block of parents, row-major from (-1,-1) to (2,2). The 2x2 core
    // supplies the whole 256x256 output core; the outer ring only ever feeds
    // the apron.
    std::array<GLuint, 16> parents {};
    bool coreHasContent = false;
    for (int by = -1; by <= 2; ++by) {
        for (int bx = -1; bx <= 2; ++bx) {
            const TileKey parentKey { key.x * 2 + bx, key.y * 2 + by };
            GLuint parentTexture = 0;
            if (level == 1) {
                const TileData* tile = source.getTile(parentKey);
                if (tile != nullptr && tile->hasTexture()) {
                    parentTexture = tile->textureId();
                }
            } else {
                parentTexture = texture(level - 1, parentKey);
            }
            if (parentTexture != 0 && bx >= 0 && bx <= 1 && by >= 0 && by <= 1) {
                coreHasContent = true;
            }
            parents[static_cast<size_t>((bx + 1) + 4 * (by + 1))]
                = (parentTexture != 0) ? parentTexture : m_transparentTexture;
        }
    }

    if (!coreHasContent) {
        // Nothing under this tile any more (the level-0 tiles were removed, or
        // never existed). Dropping it is what keeps the pyramid sparse — the
        // level above then sees a transparent parent, which is correct.
        releaseTile(level, key);
        return false;
    }

    LevelTile& entry = m_levels[level][key];
    if (entry.texture == 0) {
        entry.texture = acquireTexture();
        if (entry.texture == 0) {
            m_levels[level].erase(key);
            return false;
        }
    }

    // Every one of the 258x258 texels is written by the draw, so recycled
    // storage needs no clear.
    m_gl->glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, entry.texture, 0);
    m_gl->glBindTextures(0, 16, parents.data());
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

void DisplayPyramid::releaseTile(int level, const TileKey& key)
{
    auto it = m_levels[level].find(key);
    if (it == m_levels[level].end()) {
        return;
    }
    recycleTexture(it->second.texture);
    m_levels[level].erase(it);
}

GLuint DisplayPyramid::acquireTexture()
{
    if (!m_freeTextures.empty()) {
        const GLuint texture = m_freeTextures.back();
        m_freeTextures.pop_back();
        return texture;
    }
    return createTexture2D(m_gl, kTextureSize, kTextureSize, pyramidTextureParams(m_format));
}

void DisplayPyramid::recycleTexture(GLuint texture)
{
    if (texture == 0) {
        return;
    }
    if (m_freeTextures.size() >= kMaxFreeTextures) {
        m_gl->glDeleteTextures(1, &texture);
        return;
    }
    m_freeTextures.push_back(texture);
}

// ==========================================================================
//   Q U E R I E S
// ==========================================================================

GLuint DisplayPyramid::texture(int level, const TileKey& key) const
{
    if (level < 1 || level > kMaxLevel) {
        return 0;
    }
    const LevelMap& map = m_levels[level];
    auto it = map.find(key);
    return it != map.end() ? it->second.texture : 0;
}

size_t DisplayPyramid::tileCount() const
{
    size_t total = 0;
    for (const auto& level : m_levels) {
        total += level.size();
    }
    return total;
}

size_t DisplayPyramid::approximateBytes() const
{
    size_t bytesPerTexel = 4;
    switch (m_format) {
    case TilePixelFormat::RGBA8:
        bytesPerTexel = 4;
        break;
    case TilePixelFormat::RGBA16F:
        bytesPerTexel = 8;
        break;
    case TilePixelFormat::RGBA32F:
        bytesPerTexel = 16;
        break;
    }
    const size_t texels = static_cast<size_t>(kTextureSize) * static_cast<size_t>(kTextureSize);
    return (tileCount() + m_freeTextures.size()) * texels * bytesPerTexel;
}

} // namespace aether
