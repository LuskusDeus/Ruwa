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
#include <cstddef>
#include <iterator>

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
    // Content moved, so whatever the pyramid holds is worth re-checking once it
    // has caught up. Armed before the seed check: a re-seed does not make the
    // audit unnecessary, it is one of the paths that needs it most.
    m_auditPending = true;
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

void DisplayPyramid::invalidateLevelTile(int level, const TileKey& key)
{
    TileKey ancestor = key;
    for (int l = std::max(level, 1); l <= kMaxLevel; ++l) {
        m_dirty[l].insert(ancestor);
        ancestor = { ancestor.x >> 1, ancestor.y >> 1 };
    }
}

void DisplayPyramid::invalidateAll()
{
    // Every tile the pyramid HOLDS has to be marked here, not just left to the
    // re-seed. seedFrom() derives its dirt from the source grid, so it can only
    // ever mark ancestors of tiles that still EXIST — a level tile whose
    // level-0 footprint has been removed would keep its texture, never be
    // reconsidered, and draw for ever. (Textures are not freed here; clear() is
    // the call that does that.)
    for (int level = 1; level <= kMaxLevel; ++level) {
        for (const auto& entry : m_levels[level]) {
            invalidateLevelTile(level, entry.first);
        }
    }
    m_needsSeed = true;
    m_pendingWork = true;
    m_auditPending = true;
    m_auditCursor = 0;
    m_auditLevel = 1;
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
        m_gl->glDeleteTextures(static_cast<GLsizei>(m_freeTextures.size()), m_freeTextures.data());
        m_freeTextures.clear();
    }
    m_needsSeed = true;
    m_pendingWork = true;
    m_auditPending = false;
    m_auditCursor = 0;
    m_auditLevel = 1;
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
    // The dirty sets are NOT cleared first: invalidateAll() put the tiles the
    // pyramid already holds in there, and those are exactly the ones the source
    // grid can no longer tell us about.
    m_needsSeed = false;
    for (const auto& entry : source.tiles()) {
        // Not invalidate(): its early break assumes a dirty level implies every
        // level above it is dirty too, which the marks left by invalidateAll()
        // do not guarantee.
        invalidateLevelTile(1, ancestorKey(entry.first, 1));
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

    auto anyDirtyUpTo = [this](int top) {
        for (int level = 1; level <= top; ++level) {
            if (!m_dirty[level].empty()) {
                return true;
            }
        }
        return false;
    };

    if (!anyDirtyUpTo(topLevel)) {
        // Settled as far as the feed knows. Spend the frame re-deriving the
        // truth from the cache instead: this is the only thing standing between
        // a missed invalidation and a ghost tile that outlives everything short
        // of painting over it. Reporting the sweep as pending work is what keeps
        // it going across the catch-up frames until it has covered everything.
        if (m_auditPending) {
            auditLevels(source, request, topLevel);
        }
        if (!anyDirtyUpTo(topLevel)) {
            m_pendingWork = m_auditPending;
            return !m_auditPending;
        }
    }

    GLFboViewportBlendGuard guard(m_gl);
    m_gl->glDisable(GL_BLEND);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glViewport(0, 0, kTextureSize, kTextureSize);
    m_downsampleProgram->use();
    m_gl->glBindVertexArray(m_emptyVAO);

    const float focusX
        = request.hasFocusPoint ? request.focusX : 0.5f * (request.worldMinX + request.worldMaxX);
    const float focusY
        = request.hasFocusPoint ? request.focusY : 0.5f * (request.worldMinY + request.worldMaxY);

    bool complete = true;
    uint32_t deferrableBuilds = 0;
    std::vector<TileKey> batch;
    for (auto& level : m_sourceBlocked) {
        level.clear();
    }
    // Climbing past a level that deferred work is safe, and necessary: an absent
    // tile up there has to be built no matter what, and the parent check below
    // keeps whatever it builds from freezing.
    for (int level = 1; level <= topLevel; ++level) {
        if (m_dirty[level].empty()) {
            continue;
        }
        const KeyRange range = rangeForLevel(request, level);
        const KeyRange parentRange = rangeForLevel(request, level - 1);

        batch.clear();
        for (const TileKey& key : m_dirty[level]) {
            if (range.contains(key)) {
                batch.push_back(key);
            }
        }
        if (batch.empty()) {
            continue;
        }

        if (request.deferrableBudget != 0 && batch.size() > 1) {
            // Nearest the focus point first, so a budget that runs out spends
            // what it had on the tiles the user is looking at. Pointless when
            // the whole batch is going to be built anyway.
            const float span = static_cast<float>(levelSpanPixels(level));
            auto distanceToFocus = [span, focusX, focusY](const TileKey& key) {
                const float dx = (static_cast<float>(key.x) + 0.5f) * span - focusX;
                const float dy = (static_cast<float>(key.y) + 0.5f) * span - focusY;
                return dx * dx + dy * dy;
            };
            std::sort(
                batch.begin(), batch.end(), [&distanceToFocus](const TileKey& a, const TileKey& b) {
                    return distanceToFocus(a) < distanceToFocus(b);
                });
        }

        // Level 1 reads composition-cache tiles (256x256, no apron); every
        // level above reads pyramid tiles (258x258, apron 1).
        m_downsampleProgram->setUniform("uParentApron", level == 1 ? 0 : kApron);

        for (const TileKey& key : batch) {
            // An absent tile is not a stale tile: skipping it leaves a hole in
            // the frame, so it is built whatever the budget says. Keep scanning
            // after the budget is gone for exactly that reason — a later key in
            // this batch may be one of them.
            const bool absent = texture(level, key) == 0;

            // Waiting on the compositor, not on this request. The tile keeps
            // its dirt no matter what happens below, and the level above is
            // told so it can inherit the distinction.
            const bool blocked = sampledSourceBlocked(level, key, parentRange, request);
            if (blocked) {
                m_sourceBlocked[level].insert(key);
                if (!absent) {
                    // Rebuilding would only re-derive the same wrong pixels over
                    // the same wrong source. Keep what is on screen and wait for
                    // the recomposite to invalidate this for real.
                    continue;
                }
            }

            if (!absent && request.deferrableBudget != 0
                && deferrableBuilds >= request.deferrableBudget) {
                complete = false;
                continue;
            }

            const bool built = buildTile(source, level, key);
            ++m_lastBuildCount;
            if (!absent) {
                ++deferrableBuilds;
            }

            // Content built over a parent that is itself still dirty is
            // provisional. Rebuilding that parent does NOT re-mark its
            // ancestors, so a tile that dropped its dirt here would hold the
            // provisional pixels until the region next changes.
            if (built && (blocked || sampledParentsDirty(level, key, parentRange))) {
                // Only a deferral is work this request could still finish;
                // blocked tiles must not ask for a catch-up frame.
                if (!blocked) {
                    complete = false;
                }
                continue;
            }
            m_dirty[level].erase(key);
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
    std::array<GLuint, kParentBlockSize> parents {};
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
    // Stamp what this is about to be built FROM, and stamp the tile itself so
    // its own children can notice this rebuild. Sampled here rather than reused
    // from the loop above so the audit and the build read the source the exact
    // same way.
    entry.parentVersions = sampleParentVersions(source, level, key);
    entry.version = ++m_nextTileVersion;

    // Every one of the 258x258 texels is written by the draw, so recycled
    // storage needs no clear.
    m_gl->glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, entry.texture, 0);
    m_gl->glBindTextures(0, 16, parents.data());
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

std::array<uint64_t, DisplayPyramid::kParentBlockSize> DisplayPyramid::sampleParentVersions(
    const TileGrid& source, int level, const TileKey& key) const
{
    std::array<uint64_t, kParentBlockSize> versions {};
    for (int by = -1; by <= 2; ++by) {
        for (int bx = -1; bx <= 2; ++bx) {
            const TileKey parentKey { key.x * 2 + bx, key.y * 2 + by };
            const size_t slot = static_cast<size_t>((bx + 1) + 4 * (by + 1));
            if (level == 1) {
                const TileData* tile = source.getTile(parentKey);
                // hasTexture() mirrors buildTile: a tile with no texture
                // contributes transparency exactly like an absent one, so both
                // stamp 0 and a tile gaining its texture reads as a change.
                if (tile != nullptr && tile->hasTexture()) {
                    versions[slot] = tile->contentVersion();
                }
                continue;
            }
            const LevelMap& parents = m_levels[level - 1];
            auto it = parents.find(parentKey);
            if (it != parents.end() && it->second.texture != 0) {
                versions[slot] = it->second.version;
            }
        }
    }
    return versions;
}

uint32_t DisplayPyramid::auditLevels(
    const TileGrid& source, const UpdateRequest& request, int topLevel)
{
    uint32_t stale = 0;
    size_t examined = 0;

    if (m_auditLevel < 1 || m_auditLevel > topLevel) {
        m_auditLevel = 1;
        m_auditCursor = 0;
    }

    // Bottom-up, same as a rebuild: a stale level-1 tile marked here also marks
    // its whole ancestor chain, so the levels above are consistent by the time
    // the sweep reaches them.
    while (m_auditLevel <= topLevel && examined < kAuditTilesPerPass) {
        const LevelMap& tiles = m_levels[m_auditLevel];
        if (m_auditCursor >= tiles.size()) {
            ++m_auditLevel;
            m_auditCursor = 0;
            continue;
        }

        const KeyRange range = rangeForLevel(request, m_auditLevel);
        auto it = std::next(tiles.begin(), static_cast<std::ptrdiff_t>(m_auditCursor));
        for (; it != tiles.end() && examined < kAuditTilesPerPass; ++it, ++examined) {
            ++m_auditCursor;
            if (!range.contains(it->first)) {
                // Out of range is not audited: nothing in this request would
                // rebuild it anyway, and marking it would make hasPendingWork()
                // spin the catch-up repaint on a tile nobody is looking at.
                continue;
            }
            const auto current = sampleParentVersions(source, m_auditLevel, it->first);
            if (it->second.parentVersions == current) {
                continue;
            }
            invalidateLevelTile(m_auditLevel, it->first);
            ++stale;
        }
    }

    if (m_auditLevel > topLevel) {
        // A full sweep finished. Nothing re-arms it until content changes again,
        // so a canvas nobody is editing audits nothing. The flag is written last
        // on purpose: marks this pass just made are carried by the dirty sets,
        // and the tiles behind them get a fresh stamp when they rebuild.
        m_auditLevel = 1;
        m_auditCursor = 0;
        m_auditPending = false;
    }
    return stale;
}

bool DisplayPyramid::sampledSourceBlocked(int level, const TileKey& key,
    const KeyRange& parentRange, const UpdateRequest& request) const
{
    if (level == 1) {
        const auto* pending = request.pendingSourcePositions;
        if (pending == nullptr || pending->empty()) {
            return false;
        }
        // The whole 4x4 block, for the same reason sampledParentsDirty checks
        // it: the outer ring feeds the apron, and a stale apron is a seam.
        for (int by = -1; by <= 2; ++by) {
            for (int bx = -1; bx <= 2; ++bx) {
                if (pending->count(TileKey { key.x * 2 + bx, key.y * 2 + by }) != 0) {
                    return true;
                }
            }
        }
        return false;
    }

    const KeySet& blockedParents = m_sourceBlocked[level - 1];
    if (blockedParents.empty()) {
        return false;
    }
    // Bounded to the parent level's range for the same reason as
    // sampledParentsDirty: a parent this request never looked at cannot have
    // been recorded as blocked, so anything outside it is simply unknown.
    for (int by = -1; by <= 2; ++by) {
        for (int bx = -1; bx <= 2; ++bx) {
            const TileKey parentKey { key.x * 2 + bx, key.y * 2 + by };
            if (parentRange.contains(parentKey) && blockedParents.count(parentKey) != 0) {
                return true;
            }
        }
    }
    return false;
}

bool DisplayPyramid::sampledParentsDirty(
    int level, const TileKey& key, const KeyRange& parentRange) const
{
    // Level 1's parents are composition-cache tiles, which the pyramid does not
    // track between frames. Two other things cover them instead: whether the
    // cache is out of date is sampledSourceBlocked()'s question, and whether it
    // changed without saying so is auditLevels()'s.
    if (level < 2) {
        return false;
    }
    const KeySet& parentDirty = m_dirty[level - 1];
    if (parentDirty.empty()) {
        return false;
    }
    // The whole 4x4 block buildTile reads, not just the 2x2 core: the outer ring
    // feeds the apron, and a stale apron is a stale seam between two level tiles.
    //
    // Parents OUTSIDE the level's own key range are ignored on purpose. Their
    // dirt is not this request's business — nothing is going to rebuild them
    // before the child is reconsidered, so waiting on one would keep the child
    // dirty for ever, and a permanently pending pyramid spins the catch-up
    // repaint. The ranges nest for the core but not always for the ring, so this
    // is reachable at the boundary of the padded region: one stale apron texel
    // outside the visible area is the price.
    for (int by = -1; by <= 2; ++by) {
        for (int bx = -1; bx <= 2; ++bx) {
            const TileKey parentKey { key.x * 2 + bx, key.y * 2 + by };
            if (parentRange.contains(parentKey) && parentDirty.count(parentKey) != 0) {
                return true;
            }
        }
    }
    return false;
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
