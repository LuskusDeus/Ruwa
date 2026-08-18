// SPDX-License-Identifier: MPL-2.0

#include "features/effects/GLLayerEffectRenderer.h"

#include "features/effects/EffectCoverageResolver.h"
#include "features/effects/GLLayerEffectRenderRegistry.h"
#include "features/effects/LayerEffectRegistry.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"

#include <algorithm>
#include <utility>

namespace aether {

GLLayerEffectRenderer::GLLayerEffectRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLLayerEffectRenderer::~GLLayerEffectRenderer()
{
    shutdown();
}

Result<void> GLLayerEffectRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_gl->glGenFramebuffers(1, &m_fbo);
    m_gl->glGenVertexArrays(1, &m_emptyVao);
    if (!m_fbo || !m_emptyVao) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed,
            "Failed to create layer effect renderer objects" };
    }

    m_blitProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blitResult
        = m_blitProgram->loadFromFiles(shaderDir + QStringLiteral("/composite.vert.glsl"),
            shaderDir + QStringLiteral("/layer_effect_blit.frag.glsl"));
    if (!blitResult) {
        m_blitProgram.reset();
        shutdown();
        return blitResult;
    }

    const QList<QString> typeIds = GLLayerEffectRenderRegistry::instance().typeIds();
    m_passes.reserve(static_cast<size_t>(typeIds.size()));
    for (const QString& typeId : typeIds) {
        auto pass = GLLayerEffectRenderRegistry::instance().createPass(typeId);
        if (!pass) {
            continue;
        }

        auto result = pass->initialize(m_gl, shaderDir);
        if (!result) {
            shutdown();
            return result;
        }
        m_passes.push_back(std::move(pass));
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLLayerEffectRenderer::shutdown()
{
    clearLiveEditPrefixCache();
    deleteTexture(m_gl, m_scratchTextures[0]);
    deleteTexture(m_gl, m_scratchTextures[1]);
    for (GLuint& texture : m_extraScratchTextures) {
        deleteTexture(m_gl, texture);
    }
    m_extraScratchTextures.clear();
    m_extraScratchCursor = 0;
    for (GLuint& texture : m_extraScratchTexturesF16) {
        deleteTexture(m_gl, texture);
    }
    m_extraScratchTexturesF16.clear();
    m_extraScratchCursorF16 = 0;
    m_scratchWidth = 0;
    m_scratchHeight = 0;

    for (auto& frame : m_padFrames) {
        destroyPadFrame(*frame);
    }
    m_padFrames.clear();
    m_activePadFrame = nullptr;
    for (auto& [size, texture] : m_neighborhoodOutputs) {
        deleteTexture(m_gl, texture);
    }
    m_neighborhoodOutputs.clear();

    for (auto& pool : m_wholeRegionPools) {
        destroyWholeRegionPool(*pool);
    }
    m_wholeRegionPools.clear();
    for (auto& pool : m_groupRegionPools) {
        destroyWholeRegionPool(*pool);
    }
    m_groupRegionPools.clear();
    m_activeWholePool = nullptr;

    m_blitProgram.reset();

    if (m_emptyVao) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    m_passes.clear();
    m_initialized = false;
}

GLuint GLLayerEffectRenderer::applyEffects(const EffectChainRequest& request)
{
    const GLuint sourceTexture = request.sourceTexture;
    if (!m_initialized || !sourceTexture || request.width == 0 || request.height == 0
        || !request.effects
        || !hasRenderableEffects(*request.effects, request.space, request.realtimeOnly)
        || !ensureScratch(request.width, request.height)) {
        return sourceTexture;
    }

    // This chain runs on the shared tile-size scratch, so the padded/whole-region
    // frames are out of scope for it — but only for its own duration. The call
    // can arrive from a neighbourhood assembly callback (a layer below with a
    // plain chain), and clearing the outer frame permanently would send the
    // enclosing evaluation's extra buffers to the wrong (tile-sized) pool.
    PadFrame* const savedPadFrame = m_activePadFrame;
    WholeRegionPool* const savedWholePool = m_activeWholePool;
    m_activePadFrame = nullptr;
    m_activeWholePool = nullptr;
    m_extraScratchCursor = 0;
    m_extraScratchCursorF16 = 0;
    const GLuint effected
        = runEffectChain(sourceTexture, request.width, request.height, m_scratchTextures[0],
            m_scratchTextures[1], *request.effects, request.space, request.realtimeOnly,
            request.backdropTexture, request.finalTargetTexture, request.spaceScale, request.region,
            /*finalRoi=*/QRect(), request.wholeLayerSource, request.liveEditedEffectId,
            request.liveEditSourceVariant);
    m_activePadFrame = savedPadFrame;
    m_activeWholePool = savedWholePool;
    return effected;
}

GLuint GLLayerEffectRenderer::runEffectChain(GLuint sourceTexture, uint32_t width, uint32_t height,
    GLuint scratch0, GLuint scratch1, const QList<ruwa::core::effects::LayerEffectState>& effects,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
    GLuint finalTargetTexture, float spaceScale, ruwa::core::effects::EffectRegionFrame region,
    const QRect& finalRoi, bool wholeLayerSource, const QUuid& liveEditedEffectId,
    quint64 liveEditSourceVariant)
{
    // Per-effect region of interest: `finalRoi` (the rect the caller keeps of
    // the chain output, e.g. the centre-tile crop of the padded neighbourhood)
    // grown by the declared sampling reach of every LATER renderable effect.
    // Anything a pass writes outside its ROI can never influence the kept
    // pixels — the growth uses the same pixelExpansionRadius that already
    // sizes the neighbour padding — so passes may scissor their draws to it.
    std::vector<int> laterReach;
    if (!finalRoi.isEmpty()) {
        laterReach.resize(static_cast<size_t>(effects.size()), 0);
        int reach = 0;
        for (int i = static_cast<int>(effects.size()) - 1; i >= 0; --i) {
            laterReach[static_cast<size_t>(i)] = reach;
            const auto& effect = effects.at(i);
            if (isEffectRenderable(effect, space, realtimeOnly)) {
                reach += ruwa::core::effects::EffectCoverageResolver::effectPixelExpansion(effect);
            }
        }
    }

    const GLuint scratch[2] = { scratch0, scratch1 };
    const auto runRange = [&](GLuint initialTexture, int firstEffect, int endEffect,
                              GLuint rangeFinalTarget, bool applyRoi = true) -> GLuint {
        GLuint activeTexture = initialTexture;
        int scratchIndex = 0;
        for (int i = firstEffect; i < endEffect; ++i) {
            const auto& effect = effects.at(i);
            IGLLayerEffectPass* pass = passFor(effect.typeId);
            if (!pass || !isEffectRenderable(effect, space, realtimeOnly)) {
                continue;
            }

            const bool hasLaterEffect = std::any_of(effects.cbegin() + i + 1,
                effects.cbegin() + endEffect,
                [&](const auto& later) { return isEffectRenderable(later, space, realtimeOnly); });
            GLuint targetTexture = 0;
            if (!hasLaterEffect && rangeFinalTarget && rangeFinalTarget != activeTexture) {
                targetTexture = rangeFinalTarget;
            } else {
                targetTexture = scratch[scratchIndex];
                scratchIndex = 1 - scratchIndex;
            }

            GLLayerEffectRenderContext context;
            context.gl = m_gl;
            context.fbo = m_fbo;
            context.emptyVao = m_emptyVao;
            context.outputWidth = width;
            context.outputHeight = height;
            context.evaluationSpace = space;
            context.spaceScale = spaceScale;
            context.region = region;
            context.wholeLayerSource = wholeLayerSource;
            context.allocateScratchTexture
                = [this](bool highPrecision) { return allocateScratchTexture(highPrecision); };
            if (applyRoi && !finalRoi.isEmpty()) {
                const int grow = laterReach[static_cast<size_t>(i)];
                context.roiX = finalRoi.x() - grow;
                context.roiY = finalRoi.y() - grow;
                context.roiWidth = static_cast<uint32_t>(finalRoi.width() + 2 * grow);
                context.roiHeight = static_cast<uint32_t>(finalRoi.height() + 2 * grow);
            }
            // Keep the original chain input even when `initialTexture` is a
            // cached prefix. Shape-driven passes must continue to see the raw
            // layer alpha, exactly as in an unsplit chain.
            context.source.originalSourceTexture = sourceTexture;
            context.source.layerAlphaTexture = sourceTexture;
            context.backdrop.texture = backdropTexture;

            activeTexture = pass->render(context, effect, activeTexture, targetTexture);
        }
        return activeTexture ? activeTexture : sourceTexture;
    };

    int editIndex = -1;
    if (!liveEditedEffectId.isNull()) {
        for (int i = 0; i < effects.size(); ++i) {
            if (effects.at(i).instanceId == liveEditedEffectId) {
                editIndex = i;
                break;
            }
        }
        if (m_cachedLiveEditEffectId != liveEditedEffectId) {
            clearLiveEditPrefixCache();
            m_cachedLiveEditEffectId = liveEditedEffectId;
        }
    } else if (!m_cachedLiveEditEffectId.isNull()) {
        const bool isFormerEditChain = std::any_of(effects.cbegin(), effects.cend(),
            [&](const auto& effect) { return effect.instanceId == m_cachedLiveEditEffectId; });
        if (isFormerEditChain) {
            clearLiveEditPrefixCache();
        }
    }

    if (editIndex <= 0 || !hasRenderableEffects(effects.mid(0, editIndex), space, realtimeOnly)) {
        return runRange(sourceTexture, 0, effects.size(), finalTargetTexture);
    }

    const QList<ruwa::core::effects::LayerEffectState> prefixEffects = effects.mid(0, editIndex);
    const auto sameRegion = [&](const ruwa::core::effects::EffectRegionFrame& lhs) {
        return lhs.originX == region.originX && lhs.originY == region.originY
            && lhs.documentPxPerTexel == region.documentPxPerTexel && lhs.valid == region.valid
            && lhs.useAffine == region.useAffine && lhs.basisXx == region.basisXx
            && lhs.basisXy == region.basisXy && lhs.basisYx == region.basisYx
            && lhs.basisYy == region.basisYy;
    };

    LiveEditPrefixCacheEntry* cachedPrefix = nullptr;
    for (auto& entry : m_liveEditPrefixCache) {
        if (entry.effectId == liveEditedEffectId && entry.sourceTexture == sourceTexture
            && entry.backdropTexture == backdropTexture && entry.width == width
            && entry.height == height && entry.space == space && entry.realtimeOnly == realtimeOnly
            && entry.wholeLayerSource == wholeLayerSource && entry.spaceScale == spaceScale
            && sameRegion(entry.region) && entry.finalRoi == finalRoi
            && entry.sourceVariant == liveEditSourceVariant
            && entry.prefixEffects == prefixEffects) {
            cachedPrefix = &entry;
            break;
        }
    }

    if (!cachedPrefix) {
        const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        // All supported effect intermediates are at least RGBA8. Avoid doing the
        // prefix work when even the smallest exact copy cannot fit the cache.
        if (pixelCount * 4u > kMaxLiveEditPrefixCacheBytes) {
            return runRange(sourceTexture, 0, effects.size(), finalTargetTexture);
        }

        // Cache the complete prefix region, not the current suffix-dependent
        // ROI. The edited effect's radius may change on every slider tick.
        const GLuint prefixResult = runRange(sourceTexture, 0, editIndex, 0, false);
        GLint internalFormat = 0;
        m_gl->glGetTextureLevelParameteriv(
            prefixResult, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
        TextureParams cacheParams { GL_LINEAR, GL_LINEAR };
        size_t bytesPerPixel = 0;
        switch (internalFormat) {
        case GL_RGBA8:
            cacheParams.internalFormat = GL_RGBA8;
            cacheParams.pixelType = GL_UNSIGNED_BYTE;
            bytesPerPixel = 4;
            break;
        case GL_RGBA16F:
            cacheParams.internalFormat = GL_RGBA16F;
            cacheParams.pixelType = GL_HALF_FLOAT;
            bytesPerPixel = 8;
            break;
        case GL_RGBA32F:
            cacheParams.internalFormat = GL_RGBA32F;
            cacheParams.pixelType = GL_FLOAT;
            bytesPerPixel = 16;
            break;
        default:
            // Do not silently change precision or colour encoding for a plugin
            // texture with an unknown format.
            return runRange(sourceTexture, 0, effects.size(), finalTargetTexture);
        }
        const uint64_t requiredBytes64 = pixelCount * bytesPerPixel;
        if (requiredBytes64 > kMaxLiveEditPrefixCacheBytes) {
            return runRange(sourceTexture, 0, effects.size(), finalTargetTexture);
        }
        const size_t requiredBytes = static_cast<size_t>(requiredBytes64);
        while (!m_liveEditPrefixCache.empty()
            && m_liveEditPrefixCacheBytes + requiredBytes > kMaxLiveEditPrefixCacheBytes) {
            auto victim
                = std::min_element(m_liveEditPrefixCache.begin(), m_liveEditPrefixCache.end(),
                    [](const auto& lhs, const auto& rhs) { return lhs.lastUse < rhs.lastUse; });
            m_liveEditPrefixCacheBytes -= victim->bytes;
            deleteTexture(m_gl, victim->texture);
            m_liveEditPrefixCache.erase(victim);
        }

        const GLuint cacheTexture = createTexture2D(m_gl, width, height, cacheParams);
        if (!cacheTexture) {
            return runRange(sourceTexture, 0, effects.size(), finalTargetTexture);
        }
        m_gl->glCopyImageSubData(prefixResult, GL_TEXTURE_2D, 0, 0, 0, 0, cacheTexture,
            GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
            1);

        LiveEditPrefixCacheEntry entry;
        entry.effectId = liveEditedEffectId;
        entry.sourceTexture = sourceTexture;
        entry.backdropTexture = backdropTexture;
        entry.width = width;
        entry.height = height;
        entry.space = space;
        entry.realtimeOnly = realtimeOnly;
        entry.wholeLayerSource = wholeLayerSource;
        entry.spaceScale = spaceScale;
        entry.region = region;
        entry.finalRoi = finalRoi;
        entry.sourceVariant = liveEditSourceVariant;
        entry.prefixEffects = prefixEffects;
        entry.texture = cacheTexture;
        entry.bytes = requiredBytes;
        entry.lastUse = ++m_liveEditPrefixUseSerial;
        m_liveEditPrefixCacheBytes += requiredBytes;
        m_liveEditPrefixCache.push_back(std::move(entry));
        cachedPrefix = &m_liveEditPrefixCache.back();
    }

    cachedPrefix->lastUse = ++m_liveEditPrefixUseSerial;
    return runRange(cachedPrefix->texture, editIndex, effects.size(), finalTargetTexture);
}

void GLLayerEffectRenderer::clearLiveEditPrefixCache()
{
    for (auto& entry : m_liveEditPrefixCache) {
        deleteTexture(m_gl, entry.texture);
    }
    m_liveEditPrefixCache.clear();
    m_liveEditPrefixCacheBytes = 0;
    m_cachedLiveEditEffectId = QUuid();
}

GLuint GLLayerEffectRenderer::applyEffectsNeighborhood(uint32_t tileSize, int padPixels,
    const std::function<GLuint(int dx, int dy)>& neighborTexture,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
    ruwa::core::effects::EffectRegionFrame region, const QUuid& liveEditedEffectId,
    quint64 liveEditSourceVariant)
{
    return applyEffectsNeighborhoodBlock(tileSize,
        /*blockTiles=*/1u, padPixels, neighborTexture, effects, space, realtimeOnly,
        backdropTexture, region, liveEditedEffectId, liveEditSourceVariant);
}

GLuint GLLayerEffectRenderer::applyEffectsNeighborhoodBlock(uint32_t tileSize, uint32_t blockTiles,
    int padPixels, const std::function<GLuint(int dx, int dy)>& neighborTexture,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
    ruwa::core::effects::EffectRegionFrame region, const QUuid& liveEditedEffectId,
    quint64 liveEditSourceVariant)
{
    if (!m_initialized || tileSize == 0 || blockTiles == 0 || padPixels <= 0 || !m_blitProgram
        || !neighborTexture || !hasRenderableEffects(effects, space, realtimeOnly)) {
        return 0;
    }

    const uint32_t pad = static_cast<uint32_t>(padPixels);
    const uint32_t blockPx = tileSize * blockTiles;
    const uint32_t paddedSize = blockPx + 2u * pad;
    const GLuint outputTexture = ensureNeighborhoodOutput(blockPx);
    // Claimed for the whole evaluation: the stamping loop below hands control to
    // a callback that can start another padded evaluation, and that one must not
    // get this frame.
    PadFrame* const frame = acquirePadFrame(paddedSize);
    if (!frame || !outputTexture) {
        if (frame) {
            releasePadFrame(*frame);
        }
        return 0;
    }
    const GLuint padSourceTexture = frame->source;

    // Number of tile rings the padding reaches into.
    const int ring = static_cast<int>((pad + tileSize - 1u) / tileSize);
    const int tile = static_cast<int>(tileSize);
    const int padI = static_cast<int>(pad);
    const int blockTilesI = static_cast<int>(blockTiles);

    // 1. Assemble the padded source: clear once, then stamp each neighbour tile
    //    at its pixel offset (missing neighbours stay transparent).
    //
    //    The neighbour callback may clobber arbitrary GL state — it can upload a
    //    tile (raster) or run a RE-ENTRANT group composite that returns a
    //    transient buffer reused on the next call. So stamp IMMEDIATELY after
    //    each callback and re-establish the full draw state every time, rather
    //    than resolving all textures up front.
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, padSourceTexture, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(paddedSize), static_cast<GLsizei>(paddedSize));
    m_gl->glDisable(GL_BLEND);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    bool anyStamped = false;
    for (int dy = -ring; dy <= blockTilesI - 1 + ring; ++dy) {
        for (int dx = -ring; dx <= blockTilesI - 1 + ring; ++dx) {
            const GLuint neighborTex = neighborTexture(dx, dy);
            if (!neighborTex) {
                continue;
            }
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            m_gl->glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, padSourceTexture, 0);
            m_gl->glViewport(padI + dx * tile, padI + dy * tile, tile, tile);
            m_gl->glDisable(GL_BLEND);
            m_blitProgram->use();
            m_blitProgram->setUniform("uSource", 0);
            m_blitProgram->setUniform("uTexScale", 1.0f, 1.0f);
            m_blitProgram->setUniform("uTexOffset", 0.0f, 0.0f);
            m_gl->glBindVertexArray(m_emptyVao);
            m_gl->glBindTextureUnit(0, neighborTex);
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            anyStamped = true;
        }
    }
    m_gl->glBindVertexArray(0);
    m_gl->glBindTextureUnit(0, 0);
    if (!anyStamped) {
        releasePadFrame(*frame);
        return 0;
    }

    // 2. Run the whole chain on the padded source. The padded source's texel
    //    (0,0) sits `pad` document pixels up-left of the centre tile's origin, so
    //    shift the region frame accordingly to keep absolute document coords exact.
    ruwa::core::effects::EffectRegionFrame paddedRegion = region;
    if (paddedRegion.valid) {
        paddedRegion.originX -= static_cast<float>(pad) * paddedRegion.documentPxPerTexel;
        paddedRegion.originY -= static_cast<float>(pad) * paddedRegion.documentPxPerTexel;
    }
    PadFrame* const savedPadFrame = m_activePadFrame;
    WholeRegionPool* const savedWholePool = m_activeWholePool;
    m_activePadFrame = frame;
    m_activeWholePool = nullptr;
    frame->extraCursor = 0;
    frame->extraCursorF16 = 0;
    const GLuint effected
        = runEffectChain(padSourceTexture, paddedSize, paddedSize, frame->scratch[0],
            frame->scratch[1], effects, space, realtimeOnly, backdropTexture,
            /*finalTargetTexture=*/0,
            /*spaceScale=*/1.0f, paddedRegion,
            /*finalRoi=*/QRect(padI, padI, static_cast<int>(blockPx), static_cast<int>(blockPx)),
            /*wholeLayerSource=*/false, liveEditedEffectId, liveEditSourceVariant);
    m_activePadFrame = savedPadFrame;
    m_activeWholePool = savedWholePool;

    // 3. Crop the centre block region into the owned output texture.
    const float scale = static_cast<float>(blockPx) / static_cast<float>(paddedSize);
    const float offset = static_cast<float>(pad) / static_cast<float>(paddedSize);
    blitTexture(effected, outputTexture, blockPx, blockPx, 0, 0, static_cast<int>(blockPx),
        static_cast<int>(blockPx), scale, scale, offset, offset);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    releasePadFrame(*frame);
    return outputTexture;
}

GLuint GLLayerEffectRenderer::extractNeighborhoodTile(
    GLuint blockTexture, uint32_t blockPx, uint32_t tileSize, uint32_t tileX, uint32_t tileY)
{
    if (!m_initialized || !m_blitProgram || !blockTexture || tileSize == 0 || blockPx < tileSize
        || (tileX + 1u) * tileSize > blockPx || (tileY + 1u) * tileSize > blockPx) {
        return 0;
    }
    const GLuint outputTexture = ensureNeighborhoodOutput(tileSize);
    if (!outputTexture) {
        return 0;
    }

    // 1:1 texel-aligned slice: fragment centres land exactly on source texel
    // centres, so the linear filter degenerates to exact copies.
    const float scale = static_cast<float>(tileSize) / static_cast<float>(blockPx);
    blitTexture(blockTexture, outputTexture, tileSize, tileSize, 0, 0, static_cast<int>(tileSize),
        static_cast<int>(tileSize), scale, scale,
        static_cast<float>(tileX * tileSize) / static_cast<float>(blockPx),
        static_cast<float>(tileY * tileSize) / static_cast<float>(blockPx));
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return outputTexture;
}

GLuint GLLayerEffectRenderer::applyEffectsWholeLayer(uint32_t tileSize, uint32_t tilesW,
    uint32_t tilesH, const std::function<GLuint(int dx, int dy)>& tileTexture,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
    ruwa::core::effects::EffectRegionFrame region, bool useGroupPool,
    const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    if (!hasRenderableEffects(effects, space, realtimeOnly)) {
        return 0;
    }
    const GLuint sourceTexture
        = assembleWholeRegion(tileSize, tilesW, tilesH, tileTexture, useGroupPool);
    if (!sourceTexture) {
        return 0;
    }
    return runWholeRegionChain(sourceTexture, tileSize * tilesW, tileSize * tilesH, effects, space,
        realtimeOnly, backdropTexture, region, useGroupPool, liveEditedEffectId,
        liveEditSourceVariant);
}

GLuint GLLayerEffectRenderer::assembleWholeRegion(uint32_t tileSize, uint32_t tilesW,
    uint32_t tilesH, const std::function<GLuint(int dx, int dy)>& tileTexture, bool useGroupPool)
{
    if (!m_initialized || tileSize == 0 || tilesW == 0 || tilesH == 0 || !m_blitProgram
        || !tileTexture) {
        return 0;
    }

    const uint32_t width = tileSize * tilesW;
    const uint32_t height = tileSize * tilesH;
    // The claim is held past this function: the assembled source stays valid
    // until runWholeRegionChain consumes it (and releases the pool), so nothing
    // in between can hand the same textures to another evaluation.
    WholeRegionPool* const pool = acquireWholeRegionPool(useGroupPool, width, height);
    if (!pool) {
        return 0;
    }
    const GLuint sourceTexture = pool->source;
    const int tile = static_cast<int>(tileSize);

    // 1. Assemble the region: clear once, then stamp each populated tile at its
    //    pixel offset (empty slots stay transparent). The callback may upload a
    //    tile texture, mutating GL binding state, so re-establish the full draw
    //    state before every stamp.
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sourceTexture, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    m_gl->glDisable(GL_BLEND);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    // The callback may run a RE-ENTRANT whole-region evaluation (a group child
    // that is itself a distortion, a nested group, an adjustment recompositing
    // the stack below it). That one takes a pool of its own — this one is
    // claimed — so it can never draw into, free or resize `sourceTexture`, and
    // stamping straight into it after each callback stays correct.
    bool anyStamped = false;
    for (int dy = 0; dy < static_cast<int>(tilesH); ++dy) {
        for (int dx = 0; dx < static_cast<int>(tilesW); ++dx) {
            const GLuint tex = tileTexture(dx, dy);
            if (!tex) {
                continue;
            }
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            m_gl->glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sourceTexture, 0);
            m_gl->glViewport(dx * tile, dy * tile, tile, tile);
            m_gl->glDisable(GL_BLEND);
            m_blitProgram->use();
            m_blitProgram->setUniform("uSource", 0);
            m_blitProgram->setUniform("uTexScale", 1.0f, 1.0f);
            m_blitProgram->setUniform("uTexOffset", 0.0f, 0.0f);
            m_gl->glBindVertexArray(m_emptyVao);
            m_gl->glBindTextureUnit(0, tex);
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            anyStamped = true;
        }
    }
    m_gl->glBindVertexArray(0);
    m_gl->glBindTextureUnit(0, 0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!anyStamped) {
        releaseWholeRegionPool(*pool);
        return 0;
    }
    return sourceTexture;
}

GLuint GLLayerEffectRenderer::runWholeRegionChain(GLuint sourceTexture, uint32_t width,
    uint32_t height, const QList<ruwa::core::effects::LayerEffectState>& effects,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
    ruwa::core::effects::EffectRegionFrame region, bool useGroupPool,
    const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    // This call ends the span assembleWholeRegion opened: when `sourceTexture` is
    // a pool's own source, that pool is the one to run on AND the one to release
    // — on every exit path, or a failed chain would strand it forever. A
    // caller-owned source (the baked-source path) has no claim to inherit and
    // just needs scratch of the right size.
    WholeRegionPool* pool = claimedPoolForSource(sourceTexture);
    if (!m_initialized || !sourceTexture || width == 0 || height == 0
        || !hasRenderableEffects(effects, space, realtimeOnly)) {
        if (pool) {
            releaseWholeRegionPool(*pool);
        }
        return 0;
    }
    if (pool && (pool->width != width || pool->height != height)) {
        // The assembly sized the pool; a caller asking for other dimensions here
        // would read the source with the wrong scale.
        releaseWholeRegionPool(*pool);
        return 0;
    }
    if (!pool) {
        pool = acquireWholeRegionPool(useGroupPool, width, height);
        if (!pool) {
            return 0;
        }
    }

    // Run the whole chain on the materialised region with wholeLayerSource=true
    // so distortion passes may sample anywhere. No ROI clip: the caller crops
    // the output tiles it needs via extractWholeLayerTile. The chain has no
    // tile callbacks, so no re-entrancy happens here — but save/restore the
    // active pool anyway to stay correct if that ever changes.
    WholeRegionPool* const savedActivePool = m_activeWholePool;
    PadFrame* const savedPadFrame = m_activePadFrame;
    m_activeWholePool = pool;
    m_activePadFrame = nullptr;
    pool->extraCursor = 0;
    pool->extraCursorF16 = 0;
    const GLuint effected = runEffectChain(sourceTexture, width, height, pool->scratch[0],
        pool->scratch[1], effects, space, realtimeOnly, backdropTexture,
        /*finalTargetTexture=*/0,
        /*spaceScale=*/1.0f, region,
        /*finalRoi=*/QRect(),
        /*wholeLayerSource=*/true, liveEditedEffectId, liveEditSourceVariant);
    m_activeWholePool = savedActivePool;
    m_activePadFrame = savedPadFrame;
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    releaseWholeRegionPool(*pool);
    return effected;
}

GLuint GLLayerEffectRenderer::extractWholeLayerTile(GLuint regionTexture, uint32_t regionTilesW,
    uint32_t regionTilesH, uint32_t tileSize, uint32_t tileX, uint32_t tileY)
{
    if (!m_initialized || !m_blitProgram || !regionTexture || tileSize == 0 || regionTilesW == 0
        || regionTilesH == 0 || tileX >= regionTilesW || tileY >= regionTilesH) {
        return 0;
    }
    const GLuint outputTexture = ensureNeighborhoodOutput(tileSize);
    if (!outputTexture) {
        return 0;
    }
    const uint32_t regionW = regionTilesW * tileSize;
    const uint32_t regionH = regionTilesH * tileSize;

    // 1:1 texel-aligned slice, same reasoning as extractNeighborhoodTile.
    const float scaleX = static_cast<float>(tileSize) / static_cast<float>(regionW);
    const float scaleY = static_cast<float>(tileSize) / static_cast<float>(regionH);
    blitTexture(regionTexture, outputTexture, tileSize, tileSize, 0, 0, static_cast<int>(tileSize),
        static_cast<int>(tileSize), scaleX, scaleY,
        static_cast<float>(tileX * tileSize) / static_cast<float>(regionW),
        static_cast<float>(tileY * tileSize) / static_cast<float>(regionH));
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return outputTexture;
}

void GLLayerEffectRenderer::blitTexture(GLuint sourceTexture, GLuint targetTexture,
    uint32_t targetWidth, uint32_t targetHeight, int viewportX, int viewportY, int viewportW,
    int viewportH, float scaleX, float scaleY, float offsetX, float offsetY)
{
    Q_UNUSED(targetWidth);
    Q_UNUSED(targetHeight);
    if (!m_blitProgram || !sourceTexture || !targetTexture) {
        return;
    }
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    m_gl->glViewport(viewportX, viewportY, viewportW, viewportH);
    m_gl->glDisable(GL_BLEND);
    m_blitProgram->use();
    m_blitProgram->setUniform("uSource", 0);
    m_blitProgram->setUniform("uTexScale", scaleX, scaleY);
    m_blitProgram->setUniform("uTexOffset", offsetX, offsetY);
    m_gl->glBindTextureUnit(0, sourceTexture);
    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
    m_gl->glBindTextureUnit(0, 0);
}

bool GLLayerEffectRenderer::hasRenderableEffects(
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly) const
{
    return std::any_of(effects.cbegin(), effects.cend(),
        [&](const auto& effect) { return isEffectRenderable(effect, space, realtimeOnly); });
}

bool GLLayerEffectRenderer::ensureScratch(uint32_t width, uint32_t height)
{
    if (m_scratchTextures[0] && m_scratchTextures[1] && m_scratchWidth == width
        && m_scratchHeight == height) {
        return true;
    }

    deleteTexture(m_gl, m_scratchTextures[0]);
    deleteTexture(m_gl, m_scratchTextures[1]);
    for (GLuint& texture : m_extraScratchTextures) {
        deleteTexture(m_gl, texture);
    }
    m_extraScratchTextures.clear();
    m_extraScratchCursor = 0;
    for (GLuint& texture : m_extraScratchTexturesF16) {
        deleteTexture(m_gl, texture);
    }
    m_extraScratchTexturesF16.clear();
    m_extraScratchCursorF16 = 0;
    m_scratchWidth = width;
    m_scratchHeight = height;

    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    m_scratchTextures[0] = createTexture2D(m_gl, width, height, linear);
    m_scratchTextures[1] = createTexture2D(m_gl, width, height, linear);
    return m_scratchTextures[0] && m_scratchTextures[1];
}

GLLayerEffectRenderer::PadFrame* GLLayerEffectRenderer::acquirePadFrame(uint32_t paddedSize)
{
    if (paddedSize == 0) {
        return nullptr;
    }

    // Prefer a free frame that already has the right size; only when none does
    // is a free one resized (which frees its textures — safe, because a claimed
    // frame is never a candidate). Frames are heap-allocated so that appending
    // one from a nested acquire cannot move the frame its caller is holding.
    PadFrame* reusable = nullptr;
    for (auto& framePtr : m_padFrames) {
        PadFrame& frame = *framePtr;
        if (frame.claimed) {
            continue;
        }
        if (frame.size == paddedSize && frame.source && frame.scratch[0] && frame.scratch[1]) {
            frame.claimed = true;
            return &frame;
        }
        if (!reusable) {
            reusable = &frame;
        }
    }

    if (!reusable) {
        m_padFrames.push_back(std::make_unique<PadFrame>());
        reusable = m_padFrames.back().get();
    }

    destroyPadFrame(*reusable);
    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    reusable->size = paddedSize;
    reusable->source = createTexture2D(m_gl, paddedSize, paddedSize, linear);
    reusable->scratch[0] = createTexture2D(m_gl, paddedSize, paddedSize, linear);
    reusable->scratch[1] = createTexture2D(m_gl, paddedSize, paddedSize, linear);
    if (!reusable->source || !reusable->scratch[0] || !reusable->scratch[1]) {
        destroyPadFrame(*reusable);
        return nullptr;
    }
    reusable->claimed = true;
    return reusable;
}

void GLLayerEffectRenderer::destroyPadFrame(PadFrame& frame)
{
    deleteTexture(m_gl, frame.source);
    deleteTexture(m_gl, frame.scratch[0]);
    deleteTexture(m_gl, frame.scratch[1]);
    frame.source = 0;
    frame.scratch[0] = 0;
    frame.scratch[1] = 0;
    for (GLuint& texture : frame.extra) {
        deleteTexture(m_gl, texture);
    }
    frame.extra.clear();
    frame.extraCursor = 0;
    for (GLuint& texture : frame.extraF16) {
        deleteTexture(m_gl, texture);
    }
    frame.extraF16.clear();
    frame.extraCursorF16 = 0;
    frame.size = 0;
}

GLLayerEffectRenderer::WholeRegionPool* GLLayerEffectRenderer::acquireWholeRegionPool(
    bool groupFamily, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return nullptr;
    }

    // Same rule as acquirePadFrame: a claimed pool is off limits, so nothing a
    // nested evaluation does can free or resize the source an outer assembly is
    // still stamping into.
    auto& family = groupFamily ? m_groupRegionPools : m_wholeRegionPools;
    WholeRegionPool* reusable = nullptr;
    for (auto& poolPtr : family) {
        WholeRegionPool& pool = *poolPtr;
        if (pool.claimed) {
            continue;
        }
        if (pool.width == width && pool.height == height && pool.source && pool.scratch[0]
            && pool.scratch[1]) {
            pool.claimed = true;
            return &pool;
        }
        if (!reusable) {
            reusable = &pool;
        }
    }

    if (!reusable) {
        family.push_back(std::make_unique<WholeRegionPool>());
        reusable = family.back().get();
    }

    destroyWholeRegionPool(*reusable);
    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    reusable->width = width;
    reusable->height = height;
    reusable->source = createTexture2D(m_gl, width, height, linear);
    reusable->scratch[0] = createTexture2D(m_gl, width, height, linear);
    reusable->scratch[1] = createTexture2D(m_gl, width, height, linear);
    if (!reusable->source || !reusable->scratch[0] || !reusable->scratch[1]) {
        destroyWholeRegionPool(*reusable);
        return nullptr;
    }
    reusable->claimed = true;
    return reusable;
}

GLLayerEffectRenderer::WholeRegionPool* GLLayerEffectRenderer::claimedPoolForSource(
    GLuint sourceTexture)
{
    if (!sourceTexture) {
        return nullptr;
    }
    for (auto* family : { &m_wholeRegionPools, &m_groupRegionPools }) {
        for (auto& poolPtr : *family) {
            if (poolPtr->claimed && poolPtr->source == sourceTexture) {
                return poolPtr.get();
            }
        }
    }
    return nullptr;
}

void GLLayerEffectRenderer::destroyWholeRegionPool(WholeRegionPool& pool)
{
    deleteTexture(m_gl, pool.source);
    deleteTexture(m_gl, pool.scratch[0]);
    deleteTexture(m_gl, pool.scratch[1]);
    pool.source = 0;
    pool.scratch[0] = 0;
    pool.scratch[1] = 0;
    for (GLuint& texture : pool.extra) {
        deleteTexture(m_gl, texture);
    }
    pool.extra.clear();
    pool.extraCursor = 0;
    for (GLuint& texture : pool.extraF16) {
        deleteTexture(m_gl, texture);
    }
    pool.extraF16.clear();
    pool.extraCursorF16 = 0;
    pool.width = 0;
    pool.height = 0;
}

GLuint GLLayerEffectRenderer::ensureNeighborhoodOutput(uint32_t sizePx)
{
    auto it = m_neighborhoodOutputs.find(sizePx);
    if (it != m_neighborhoodOutputs.end() && it->second) {
        return it->second;
    }
    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    const GLuint texture = createTexture2D(m_gl, sizePx, sizePx, linear);
    if (texture) {
        m_neighborhoodOutputs[sizePx] = texture;
    }
    return texture;
}

GLuint GLLayerEffectRenderer::allocateScratchTexture(bool highPrecision)
{
    TextureParams params { GL_LINEAR, GL_LINEAR };
    if (highPrecision) {
        params.internalFormat = GL_RGBA16F;
        params.pixelType = GL_HALF_FLOAT;
    }

    if (m_activeWholePool) {
        WholeRegionPool& active = *m_activeWholePool;
        std::vector<GLuint>& extra = highPrecision ? active.extraF16 : active.extra;
        uint32_t& cursor = highPrecision ? active.extraCursorF16 : active.extraCursor;
        const size_t index = static_cast<size_t>(cursor++);
        if (index >= extra.size()) {
            extra.push_back(createTexture2D(m_gl, active.width, active.height, params));
        }
        return extra.at(index);
    }

    if (m_activePadFrame) {
        PadFrame& frame = *m_activePadFrame;
        std::vector<GLuint>& pool = highPrecision ? frame.extraF16 : frame.extra;
        uint32_t& cursor = highPrecision ? frame.extraCursorF16 : frame.extraCursor;
        const size_t index = static_cast<size_t>(cursor++);
        if (index >= pool.size()) {
            pool.push_back(createTexture2D(m_gl, frame.size, frame.size, params));
        }
        return pool.at(index);
    }

    std::vector<GLuint>& pool = highPrecision ? m_extraScratchTexturesF16 : m_extraScratchTextures;
    uint32_t& cursor = highPrecision ? m_extraScratchCursorF16 : m_extraScratchCursor;
    const size_t index = static_cast<size_t>(cursor++);
    if (index >= pool.size()) {
        pool.push_back(createTexture2D(m_gl, m_scratchWidth, m_scratchHeight, params));
    }

    return pool.at(index);
}

IGLLayerEffectPass* GLLayerEffectRenderer::passFor(const QString& typeId) const
{
    const auto it = std::find_if(m_passes.cbegin(), m_passes.cend(),
        [&](const auto& pass) { return pass && pass->typeId() == typeId; });
    return it == m_passes.cend() ? nullptr : it->get();
}

bool GLLayerEffectRenderer::isEffectRenderable(const ruwa::core::effects::LayerEffectState& effect,
    ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly) const
{
    if (!effect.enabled || (realtimeOnly && !effect.realtimePreviewEnabled)) {
        return false;
    }

    const auto* descriptor
        = ruwa::core::effects::LayerEffectRegistry::instance().descriptor(effect.typeId);
    if (!descriptor) {
        return false;
    }
    if (space == ruwa::core::effects::EffectEvaluationSpace::DocumentTile
        && !descriptor->capabilities.supportsDocumentTile) {
        return false;
    }
    if (space == ruwa::core::effects::EffectEvaluationSpace::ViewportScreen
        && !descriptor->capabilities.supportsViewportScreen) {
        return false;
    }

    return passFor(effect.typeId) != nullptr;
}

} // namespace aether