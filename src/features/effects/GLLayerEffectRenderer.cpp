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

    deleteTexture(m_gl, m_padScratchTextures[0]);
    deleteTexture(m_gl, m_padScratchTextures[1]);
    deleteTexture(m_gl, m_padSourceTexture);
    for (GLuint& texture : m_padExtraTextures) {
        deleteTexture(m_gl, texture);
    }
    m_padExtraTextures.clear();
    m_padExtraCursor = 0;
    for (GLuint& texture : m_padExtraTexturesF16) {
        deleteTexture(m_gl, texture);
    }
    m_padExtraTexturesF16.clear();
    m_padExtraCursorF16 = 0;
    m_padScratchSize = 0;
    for (auto& [size, texture] : m_neighborhoodOutputs) {
        deleteTexture(m_gl, texture);
    }
    m_neighborhoodOutputs.clear();
    m_usingPadScratch = false;

    destroyWholeRegionPool(m_wholePool);
    destroyWholeRegionPool(m_groupPool);
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

    m_usingPadScratch = false;
    m_activeWholePool = nullptr;
    m_extraScratchCursor = 0;
    m_extraScratchCursorF16 = 0;
    return runEffectChain(sourceTexture, request.width, request.height, m_scratchTextures[0],
        m_scratchTextures[1], *request.effects, request.space, request.realtimeOnly,
        request.backdropTexture, request.finalTargetTexture, request.spaceScale, request.region,
        /*finalRoi=*/QRect(), request.wholeLayerSource, request.liveEditedEffectId,
        request.liveEditSourceVariant);
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
    if (!ensurePadScratch(paddedSize) || !outputTexture) {
        return 0;
    }

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
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_padSourceTexture, 0);
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
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_padSourceTexture, 0);
            m_gl->glViewport(padI + dx * tile, padI + dy * tile, tile, tile);
            m_gl->glDisable(GL_BLEND);
            m_blitProgram->use();
            m_blitProgram->setUniform("uSource", 0);
            m_blitProgram->setUniform("uTexScale", 1.0f, 1.0f);
            m_blitProgram->setUniform("uTexOffset", 0.0f, 0.0f);
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindVertexArray(m_emptyVao);
            m_gl->glBindTexture(GL_TEXTURE_2D, neighborTex);
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            anyStamped = true;
        }
    }
    m_gl->glBindVertexArray(0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (!anyStamped) {
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
    m_usingPadScratch = true;
    m_padExtraCursor = 0;
    m_padExtraCursorF16 = 0;
    const GLuint effected
        = runEffectChain(m_padSourceTexture, paddedSize, paddedSize, m_padScratchTextures[0],
            m_padScratchTextures[1], effects, space, realtimeOnly, backdropTexture,
            /*finalTargetTexture=*/0,
            /*spaceScale=*/1.0f, paddedRegion,
            /*finalRoi=*/QRect(padI, padI, static_cast<int>(blockPx), static_cast<int>(blockPx)),
            /*wholeLayerSource=*/false, liveEditedEffectId, liveEditSourceVariant);
    m_usingPadScratch = false;

    // 3. Crop the centre block region into the owned output texture.
    const float scale = static_cast<float>(blockPx) / static_cast<float>(paddedSize);
    const float offset = static_cast<float>(pad) / static_cast<float>(paddedSize);
    blitTexture(effected, outputTexture, blockPx, blockPx, 0, 0, static_cast<int>(blockPx),
        static_cast<int>(blockPx), scale, scale, offset, offset);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    if (!m_initialized || tileSize == 0 || tilesW == 0 || tilesH == 0 || !m_blitProgram
        || !tileTexture || !hasRenderableEffects(effects, space, realtimeOnly)) {
        return 0;
    }

    const uint32_t width = tileSize * tilesW;
    const uint32_t height = tileSize * tilesH;
    WholeRegionPool& pool = useGroupPool ? m_groupPool : m_wholePool;
    if (!ensureWholeRegionPool(pool, width, height)) {
        return 0;
    }
    const GLuint sourceTexture = pool.source;
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
    // that is itself a raster distortion) which draws into the OTHER pool's
    // source — never `sourceTexture` here — so stamping straight into
    // `sourceTexture` immediately after each callback stays correct.
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
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindVertexArray(m_emptyVao);
            m_gl->glBindTexture(GL_TEXTURE_2D, tex);
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            anyStamped = true;
        }
    }
    m_gl->glBindVertexArray(0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (!anyStamped) {
        return 0;
    }

    // 2. Run the whole chain on the materialised region with wholeLayerSource=true
    //    so distortion passes may sample anywhere. No ROI clip: the caller crops
    //    the output tiles it needs via extractWholeLayerTile. The chain has no
    //    tile callbacks, so no re-entrancy happens here — but save/restore the
    //    active pool anyway to stay correct if that ever changes.
    WholeRegionPool* const savedActivePool = m_activeWholePool;
    m_activeWholePool = &pool;
    pool.extraCursor = 0;
    pool.extraCursorF16 = 0;
    const GLuint effected = runEffectChain(sourceTexture, width, height, pool.scratch[0],
        pool.scratch[1], effects, space, realtimeOnly, backdropTexture,
        /*finalTargetTexture=*/0,
        /*spaceScale=*/1.0f, region,
        /*finalRoi=*/QRect(),
        /*wholeLayerSource=*/true, liveEditedEffectId, liveEditSourceVariant);
    m_activeWholePool = savedActivePool;
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sourceTexture);
    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
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

bool GLLayerEffectRenderer::ensurePadScratch(uint32_t paddedSize)
{
    if (m_padScratchTextures[0] && m_padScratchTextures[1] && m_padSourceTexture
        && m_padScratchSize == paddedSize) {
        return true;
    }

    deleteTexture(m_gl, m_padScratchTextures[0]);
    deleteTexture(m_gl, m_padScratchTextures[1]);
    deleteTexture(m_gl, m_padSourceTexture);
    for (GLuint& texture : m_padExtraTextures) {
        deleteTexture(m_gl, texture);
    }
    m_padExtraTextures.clear();
    m_padExtraCursor = 0;
    for (GLuint& texture : m_padExtraTexturesF16) {
        deleteTexture(m_gl, texture);
    }
    m_padExtraTexturesF16.clear();
    m_padExtraCursorF16 = 0;
    m_padScratchSize = paddedSize;

    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    m_padScratchTextures[0] = createTexture2D(m_gl, paddedSize, paddedSize, linear);
    m_padScratchTextures[1] = createTexture2D(m_gl, paddedSize, paddedSize, linear);
    m_padSourceTexture = createTexture2D(m_gl, paddedSize, paddedSize, linear);
    return m_padScratchTextures[0] && m_padScratchTextures[1] && m_padSourceTexture;
}

bool GLLayerEffectRenderer::ensureWholeRegionPool(
    WholeRegionPool& pool, uint32_t width, uint32_t height)
{
    if (pool.source && pool.scratch[0] && pool.scratch[1] && pool.width == width
        && pool.height == height) {
        return true;
    }

    destroyWholeRegionPool(pool);
    pool.width = width;
    pool.height = height;

    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    pool.source = createTexture2D(m_gl, width, height, linear);
    pool.scratch[0] = createTexture2D(m_gl, width, height, linear);
    pool.scratch[1] = createTexture2D(m_gl, width, height, linear);
    return pool.source && pool.scratch[0] && pool.scratch[1];
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

    if (m_usingPadScratch) {
        std::vector<GLuint>& pool = highPrecision ? m_padExtraTexturesF16 : m_padExtraTextures;
        uint32_t& cursor = highPrecision ? m_padExtraCursorF16 : m_padExtraCursor;
        const size_t index = static_cast<size_t>(cursor++);
        if (index >= pool.size()) {
            pool.push_back(createTexture2D(m_gl, m_padScratchSize, m_padScratchSize, params));
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
