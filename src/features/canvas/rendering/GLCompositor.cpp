// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   C O M P O S I T O R
// ==========================================================================

#include "features/canvas/rendering/GLCompositor.h"
#include "features/canvas/rendering/CompositeLayerKeys.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLStateGuard.h"
#include "shared/rendering/GLTextureFactory.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/canvas/rendering/GLRetainedRenderer.h"
#include "features/transform/GLTransformRenderer.h"
#include "features/effects/GLLayerEffectRenderer.h"
#include "features/effects/EffectCoverageResolver.h"
#include "features/effects/LayerEffectRegistry.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstring>
#include <functional>
#include <unordered_set>
#include <QElapsedTimer>
namespace aether {

namespace {
// Convert a tile's packed premultiplied RGBA (r|g<<8|b<<16|a<<24) into the
// float Color the compositor binds as a 1x1 clip texture.
Color colorFromPackedPremul(uint32_t packed)
{
    const float inv = 1.0f / 255.0f;
    Color c;
    c.r = static_cast<float>(packed & 0xFFu) * inv;
    c.g = static_cast<float>((packed >> 8) & 0xFFu) * inv;
    c.b = static_cast<float>((packed >> 16) & 0xFFu) * inv;
    c.a = static_cast<float>((packed >> 24) & 0xFFu) * inv;
    return c;
}

uint64_t liveEditCacheVariant(uint64_t generation, uint64_t sourceRevision, uint64_t variant = 0)
{
    uint64_t value = generation;
    value ^= sourceRevision + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
    value ^= variant + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
    return value;
}
} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLCompositor::GLCompositor(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLCompositor::~GLCompositor()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> GLCompositor::initialize(const QString& shaderDir)
{
    if (m_initialized)
        return Result<void>::ok();

    // Load composite shader
    m_compositeProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_compositeProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/composite.frag.glsl");
    if (!result) {
        return result;
    }

    // Create FBO
    m_gl->glGenFramebuffers(1, &m_fbo);
    if (m_fbo == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create compositor FBO" };
    }

    // Create empty VAO
    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create compositor VAO" };
    }

    // Create ping-pong textures
    ensurePingPongTextures();

    m_retainedRenderer = std::make_unique<GLRetainedRenderer>(m_gl);
    auto retainedResult = m_retainedRenderer->initialize();
    if (!retainedResult) {
        shutdown();
        return retainedResult;
    }

    m_effectRenderer = std::make_unique<GLLayerEffectRenderer>(m_gl);
    auto effectResult = m_effectRenderer->initialize(shaderDir);
    if (!effectResult) {
        shutdown();
        return effectResult;
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLCompositor::shutdown()
{
    destroyEffectBlockCache();
    m_effectRenderer.reset();
    m_retainedRenderer.reset();
    m_compositeProgram.reset();

    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    for (int i = 0; i < 2; ++i) {
        deleteTexture(m_gl, m_pingPongTex[i]);
    }
    deleteTexture(m_gl, m_groupTempTex[0]);
    deleteTexture(m_gl, m_groupTempTex[1]);
    deleteTexture(m_gl, m_neighborhoodCenterTex);
    deleteTexture(m_gl, m_clipGroupTempTex[0]);
    deleteTexture(m_gl, m_clipGroupTempTex[1]);
    deleteTexture(m_gl, m_wholeGroupTempTex[0]);
    deleteTexture(m_gl, m_wholeGroupTempTex[1]);
    deleteTexture(m_gl, m_adjustmentTempTex[0]);
    deleteTexture(m_gl, m_adjustmentTempTex[1]);
    deleteTexture(m_gl, m_programmaticBlendBaseTex);
    for (const auto& frame : m_groupCompositeFrames) {
        deleteTexture(m_gl, frame->ping[0]);
        deleteTexture(m_gl, frame->ping[1]);
        deleteTexture(m_gl, frame->passThrough);
        deleteTexture(m_gl, frame->effected);
        deleteTexture(m_gl, frame->sourceCoverage);
        deleteTexture(m_gl, frame->coverage);
    }
    m_groupCompositeFrames.clear();
    m_groupCompositeDepth = 0;
    deleteTexture(m_gl, m_solidColorTex);
    deleteTexture(m_gl, m_solidClipTex);
    deleteTexture(m_gl, m_solidClipTex2);
    deleteTexture(m_gl, m_transparentTex);

    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }

    m_initialized = false;
}

// ==========================================================================
//   P I N G - P O N G   T E X T U R E S
// ==========================================================================

void GLCompositor::ensurePingPongTextures()
{
    // Use GL_LINEAR minFilter to match cache tile textures created by
    // GLTileRenderer::ensureTileTexture().  This allows compositeTile()
    // to swap ping-pong and cache textures without a filter mismatch.
    static constexpr TextureParams kTileParams { GL_LINEAR, GL_NEAREST };
    auto ensureTex = [this](GLuint& tex) {
        if (!tex)
            tex = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, kTileParams);
    };

    for (int i = 0; i < 2; ++i)
        ensureTex(m_pingPongTex[i]);
    ensureTex(m_groupTempTex[0]);
    ensureTex(m_groupTempTex[1]);
    ensureTex(m_neighborhoodCenterTex);
    ensureTex(m_clipGroupTempTex[0]);
    ensureTex(m_clipGroupTempTex[1]);
    ensureTex(m_wholeGroupTempTex[0]);
    ensureTex(m_wholeGroupTempTex[1]);
    ensureTex(m_adjustmentTempTex[0]);
    ensureTex(m_adjustmentTempTex[1]);
    ensureTex(m_programmaticBlendBaseTex);
}

void GLCompositor::swapPingPong()
{
    m_currentPing = 1 - m_currentPing;
}

GLCompositor::GroupCompositeFrame& GLCompositor::ensureGroupCompositeFrame(size_t depth)
{
    if (m_groupCompositeFrames.size() <= depth) {
        m_groupCompositeFrames.resize(depth + 1);
    }
    if (!m_groupCompositeFrames[depth]) {
        m_groupCompositeFrames[depth] = std::make_unique<GroupCompositeFrame>();
    }

    static constexpr TextureParams kTileParams { GL_LINEAR, GL_NEAREST };
    GroupCompositeFrame& frame = *m_groupCompositeFrames[depth];
    auto ensureTex = [this](GLuint& texture) {
        if (!texture) {
            texture = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, kTileParams);
        }
    };
    ensureTex(frame.ping[0]);
    ensureTex(frame.ping[1]);
    ensureTex(frame.passThrough);
    ensureTex(frame.effected);
    ensureTex(frame.sourceCoverage);
    ensureTex(frame.coverage);
    return frame;
}

// ==========================================================================
//   C O M P O S I T I N G
// ==========================================================================

void GLCompositor::compositeTile(const TileKey& key, const std::vector<CompositeLayerInfo>& layers,
    CompositionCache& cache, GLTileRenderer* tileRenderer, const Color& backdropColor)
{
    if (!m_initialized)
        return;

    QElapsedTimer dbgTileTimer;
    dbgTileTimer.start();

    // NOTE: the FBO/viewport state guard is intentionally NOT created here.
    // Its constructor issues glGetIntegerv (GL_FRAMEBUFFER_BINDING + GL_VIEWPORT),
    // which forces a CPU<->driver sync. Doing that per tile cost ~17 us/tile and
    // dominated frame time when many tiles were composited (e.g. during a
    // transform over a large layer). The guard is now created ONCE per batch in
    // compositeDirtyKeys(); compositeTile only sets the tile-sized viewport.
    const qint64 dbgGuardUs = 0;

    // Set viewport for tile-sized FBO
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glDisable(GL_BLEND);

    // Reset ping-pong state
    m_currentPing = 0;

    QElapsedTimer dbgClearTimer;
    dbgClearTimer.start();
    // Clear base texture to transparent
    clearTexture(m_pingPongTex[0]);
    const qint64 dbgClearUs = dbgClearTimer.nsecsElapsed() / 1000;

    QElapsedTimer dbgStackTimer;
    dbgStackTimer.start();
    // Composite layers bottom to top
    m_groupCompositeDepth = 0;
    m_activeRootLayers = &layers;
    GLuint resultTex = compositeLayerStack(key, layers, tileRenderer, 1.0f, false, backdropColor);
    m_activeRootLayers = nullptr;
    m_groupCompositeDepth = 0;
    const qint64 dbgStackUs = dbgStackTimer.nsecsElapsed() / 1000;

    QElapsedTimer dbgSwapTimer;
    dbgSwapTimer.start();
    // Transfer result to cache tile via texture swap (zero GPU copy).
    TileData& cacheTile = cache.grid().getOrCreateTile(key);
    if (!cacheTile.hasTexture()) {
        tileRenderer->ensureTileTexture(cacheTile);
    }

    const int resultSlot = (resultTex == m_pingPongTex[0]) ? 0 : 1;
    const GLuint oldCacheTex = cacheTile.textureId();
    cacheTile.setTextureId(resultTex);
    m_pingPongTex[resultSlot] = oldCacheTex;

    cacheTile.clearDirty();
    cache.grid().removeDirty(key);
    const qint64 dbgSwapUs = dbgSwapTimer.nsecsElapsed() / 1000;

    // Accumulate per-tile stats for batch reporting
    m_dbgTotalGuardUs += dbgGuardUs;
    m_dbgTotalClearUs += dbgClearUs;
    m_dbgTotalStackUs += dbgStackUs;
    m_dbgTotalSwapUs += dbgSwapUs;
    m_dbgTotalTileUs += dbgTileTimer.nsecsElapsed() / 1000;
    ++m_dbgTileCount;
}

GLuint GLCompositor::compositeLayerStack(const TileKey& key,
    const std::vector<CompositeLayerInfo>& layers, GLTileRenderer* tileRenderer,
    float parentOpacity, bool useSrcAtop, const Color& backdropColor,
    GLuint strokeBlendOuterBaseTex, int strokeBlendLayerMode, float strokeBlendLayerOpacity,
    const Color& strokeBlendBackdropColor)
{
    const GLuint transparentTex = transparentTexture();
    GLuint activeClipBaseTex = transparentTex;

    size_t idx = 0;
    while (idx < layers.size()) {
        const auto& layer = layers[idx];
        if (!layer.visible) {
            ++idx;
            continue;
        }

        float effectiveOpacity = layer.opacity * parentOpacity;
        if (effectiveOpacity <= 0.0f) {
            ++idx;
            continue;
        }

        // ── Clip-group detection ──────────────────────────────────────────
        // When this layer is the clip base (not itself clipped) and the next
        // layer is clippedToBelow, we handle the entire clip group in an
        // isolated buffer using src-atop for the clipped layers.  This
        // prevents double-transparency on semi-transparent clip-base edges.
        //
        // We skip this when useSrcAtop==true because in that context every
        // layer in the sub-stack is already clipped and consumed as a unit.
        const bool passThroughGroupClipBase = layer.isGroup && !layer.forceIsolation;
        const bool isClipBase = !useSrcAtop && !passThroughGroupClipBase && !layer.clippedToBelow
            && (idx + 1 < layers.size()) && layers[idx + 1].clippedToBelow;

        if (isClipBase) {
            // Collect the full clip group: base at idx, clipped at idx+1..clipEnd-1
            size_t clipEnd = idx + 1;
            while (clipEnd < layers.size() && layers[clipEnd].clippedToBelow)
                ++clipEnd;

            // Save main ping-pong state
            const int savedPing = m_currentPing;
            const GLuint savedTex0 = m_pingPongTex[0];
            const GLuint savedTex1 = m_pingPongTex[1];

            // Use the dedicated clip-group temp textures for isolation.
            // (Separate from m_groupTempTex so that an isGroup layer inside
            // the clip base can still use m_groupTempTex without conflict.)
            m_pingPongTex[0] = m_clipGroupTempTex[0];
            m_pingPongTex[1] = m_clipGroupTempTex[1];
            m_currentPing = 0;
            clearTexture(m_pingPongTex[0]);

            // 1. Composite the clip base into the isolated buffer (src-over
            //    from transparent — blend mode has no effect here but is
            //    preserved for the final group→canvas blend step).
            {
                std::vector<CompositeLayerInfo> baseVec(1, layer);
                baseVec[0].opacity = 1.0f; // effective opacity applied at group blend
                compositeLayerStack(key, baseVec, tileRenderer, 1.0f,
                    /*useSrcAtop=*/false, Color::transparent());
            }

            // 2. Composite clipped layers with src-atop.
            //    src-atop: ao = base.a = clip_base.a at every pixel, so the
            //    group alpha never exceeds the clip base alpha.
            if (clipEnd > idx + 1) {
                std::vector<CompositeLayerInfo> clippedVec(
                    layers.begin() + static_cast<ptrdiff_t>(idx + 1),
                    layers.begin() + static_cast<ptrdiff_t>(clipEnd));
                compositeLayerStack(key, clippedVec, tileRenderer, 1.0f,
                    /*useSrcAtop=*/true, Color::transparent());
            }

            const GLuint groupResultTex = currentBase();

            // Restore main ping-pong state
            m_pingPongTex[0] = savedTex0;
            m_pingPongTex[1] = savedTex1;
            m_currentPing = savedPing;

            // 3. Blend the clip-group result onto the main canvas using the
            //    clip base's blend mode and effective opacity.  Any external
            //    clip mask on the clip base was already applied inside the
            //    isolated pass, so we don't add an extra mask here.
            {
                BlendPassParams bp;
                bp.baseTex = currentBase();
                bp.srcTex = groupResultTex;
                bp.key = key;
                bp.blendMode = layer.blendMode;
                bp.opacity = effectiveOpacity;
                bp.preserveBaseAlpha = layer.preserveBaseAlpha;
                bp.replaceBase = layer.replaceBase;
                bp.srcAtop = useSrcAtop;
                bp.useRadialReveal = layer.useRadialReveal;
                bp.radialRevealInvert = layer.radialRevealInvert;
                bp.radialRevealOrigin = layer.radialRevealOrigin;
                bp.radialRevealRadius = layer.radialRevealRadius;
                bp.radialRevealFeather = layer.radialRevealFeather;
                bp.backdropColor = backdropColor;
                blendPass(bp);
            }
            swapPingPong();

            activeClipBaseTex = groupResultTex;
            idx = clipEnd;
            continue;
        }
        // ── End clip-group detection ──────────────────────────────────────

        bool useClipMask = false;
        GLuint clipMaskTex = 0;
        bool clipMaskAlphaOnly = false;
        if (layer.externalClipMaskGrid) {
            TileData* maskTile = layer.externalClipMaskGrid->getTile(key);
            if (maskTile) {
                if (maskTile->isSolid()) {
                    // Uniform-color mask tile: bind its color directly without
                    // ever allocating/uploading a 256 KB pixel buffer.
                    useClipMask = true;
                    clipMaskTex = solidClipColorTexture(
                        m_solidClipTex, colorFromPackedPremul(maskTile->solidColorPacked()));
                    clipMaskAlphaOnly = layer.clipMaskAlphaOnly;
                } else {
                    if (!maskTile->hasTexture()) {
                        tileRenderer->ensureTileTexture(*maskTile);
                        tileRenderer->uploadTileData(*maskTile);
                        ++m_dbgUploadCount;
                    } else if (maskTile->isDirty()) {
                        tileRenderer->uploadTileData(*maskTile);
                        ++m_dbgUploadCount;
                    }
                    if (maskTile->hasTexture()) {
                        useClipMask = true;
                        clipMaskTex = maskTile->textureId();
                        clipMaskAlphaOnly = layer.clipMaskAlphaOnly;
                    }
                }
            }
            if (!useClipMask && layer.clipMaskReplaceFallback && layer.clipMaskGrid2) {
                // Replace-mode mask preview: the stroke buffer holds finished
                // tiles, so a missing tile means "mask unchanged here" — sample the
                // committed mask as the clip rather than the transparent default
                // (which would read as fully revealed and erase the existing mask).
                TileData* committedTile = layer.clipMaskGrid2->getTile(key);
                if (committedTile) {
                    if (committedTile->isSolid()) {
                        useClipMask = true;
                        clipMaskTex = solidClipColorTexture(m_solidClipTex,
                            colorFromPackedPremul(committedTile->solidColorPacked()));
                        clipMaskAlphaOnly = layer.clipMaskAlphaOnly;
                    } else {
                        if (!committedTile->hasTexture()) {
                            tileRenderer->ensureTileTexture(*committedTile);
                            tileRenderer->uploadTileData(*committedTile);
                            ++m_dbgUploadCount;
                        } else if (committedTile->isDirty()) {
                            tileRenderer->uploadTileData(*committedTile);
                            ++m_dbgUploadCount;
                        }
                        if (committedTile->hasTexture()) {
                            useClipMask = true;
                            clipMaskTex = committedTile->textureId();
                            clipMaskAlphaOnly = layer.clipMaskAlphaOnly;
                        }
                    }
                }
            }
            if (!useClipMask) {
                // No tile here: the grid's default fill is the implicit value of
                // every absent tile (e.g. opaque black = hide-all background).
                // In replace-mode mask preview a missing stroke tile means "mask
                // unchanged here", so the background comes from the committed mask
                // grid instead of the (transparent) stroke buffer. When the fill is
                // transparent (legacy reveal-all) keep the shared transparent
                // texture to avoid a per-tile upload.
                useClipMask = true;
                const uint32_t fill = (layer.clipMaskReplaceFallback && layer.clipMaskGrid2)
                    ? layer.clipMaskGrid2->defaultFillPacked()
                    : layer.externalClipMaskGrid->defaultFillPacked();
                clipMaskTex = (fill == 0u)
                    ? transparentTex
                    : solidClipColorTexture(m_solidClipTex, colorFromPackedPremul(fill));
                clipMaskAlphaOnly = layer.clipMaskAlphaOnly;
            }
        } else if (layer.clippedToBelow && !useSrcAtop) {
            // Legacy / safety path: a clippedToBelow layer that was not
            // consumed by a clip-group pass (e.g. the very first layer in a
            // stack has clippedToBelow=true with nothing below it).
            useClipMask = true;
            clipMaskTex = activeClipBaseTex;
        }
        // When useSrcAtop==true the Porter-Duff op itself handles the clipping;
        // no additional alpha multiplication via clip mask is needed.

        // Secondary clip mask (mask-edit preview: the committed mask sampled
        // alongside the in-progress stroke). Absent tiles read as transparent,
        // which the preview shader interprets as "fully revealed" committed mask.
        GLuint clipMaskTex2 = transparentTex;
        bool useClipMask2 = false;
        if (layer.clipMaskGrid2) {
            TileData* maskTile2 = layer.clipMaskGrid2->getTile(key);
            if (maskTile2) {
                if (maskTile2->isSolid()) {
                    useClipMask2 = true;
                    clipMaskTex2 = solidClipColorTexture(
                        m_solidClipTex2, colorFromPackedPremul(maskTile2->solidColorPacked()));
                } else {
                    if (!maskTile2->hasTexture()) {
                        tileRenderer->ensureTileTexture(*maskTile2);
                        tileRenderer->uploadTileData(*maskTile2);
                        ++m_dbgUploadCount;
                    } else if (maskTile2->isDirty()) {
                        tileRenderer->uploadTileData(*maskTile2);
                        ++m_dbgUploadCount;
                    }
                    if (maskTile2->hasTexture()) {
                        useClipMask2 = true;
                        clipMaskTex2 = maskTile2->textureId();
                    }
                }
            }
            if (!useClipMask2) {
                useClipMask2 = true;
                const uint32_t fill2 = layer.clipMaskGrid2->defaultFillPacked();
                clipMaskTex2 = (fill2 == 0u)
                    ? transparentTex
                    : solidClipColorTexture(m_solidClipTex2, colorFromPackedPremul(fill2));
            }
        }

        GLuint clipBaseCandidateTex = transparentTex;

        // Build BlendPassParams populated from the current layer context.
        auto makeBlendParams = [&](GLuint srcTex) -> BlendPassParams {
            BlendPassParams bp;
            bp.baseTex = currentBase();
            bp.srcTex = srcTex;
            bp.key = key;
            bp.blendMode = layer.blendMode;
            bp.opacity = effectiveOpacity;
            bp.clipMaskTex = clipMaskTex;
            bp.useClipMask = useClipMask;
            bp.clipMaskAlphaOnly = clipMaskAlphaOnly;
            // Soft-selection alpha cap is meaningful only when the clip mask
            // came from the layer-level external grid (i.e. the selection
            // mask). The clippedToBelow legacy path uses a different mask
            // semantic (group clip), where capping would be wrong.
            bp.clipMaskAsAlphaCap
                = useClipMask && layer.externalClipMaskGrid != nullptr && layer.clipMaskAsAlphaCap;
            bp.clipMaskLuminanceReveal = layer.clipMaskLuminanceReveal;
            bp.clipMaskEditPreview = layer.clipMaskEditPreview;
            bp.clipMaskEditReplace = layer.clipMaskEditReplace;
            bp.clipMaskTex2 = clipMaskTex2;
            bp.useClipMask2 = useClipMask2;
            bp.clipMaskEditStrokeOpacity = layer.clipMaskEditStrokeOpacity;
            bp.subtractClipRevealFromSrc = layer.subtractClipRevealFromSrc;
            bp.preserveBaseAlpha = layer.preserveBaseAlpha;
            bp.replaceBase = layer.replaceBase;
            bp.srcAtop = useSrcAtop;
            bp.useRadialReveal = layer.useRadialReveal;
            bp.radialRevealInvert = layer.radialRevealInvert;
            bp.radialRevealOrigin = layer.radialRevealOrigin;
            bp.radialRevealRadius = layer.radialRevealRadius;
            bp.radialRevealFeather = layer.radialRevealFeather;
            bp.backdropColor = backdropColor;
            if (layer.useStrokeBlendBackdrop && strokeBlendOuterBaseTex) {
                bp.programmaticBlendBaseTex
                    = renderStrokeBlendBase(strokeBlendOuterBaseTex, currentBase(), key,
                        strokeBlendLayerMode, strokeBlendLayerOpacity, strokeBlendBackdropColor);
                bp.useProgrammaticBlendBase = bp.programmaticBlendBaseTex != 0;
            }
            return bp;
        };

        if (layer.isAdjustment) {
            // Adjustment layer: run the effect chain on the composite BELOW
            // (currentBase) and replace the base with the effected result,
            // gated by opacity, an optional layer mask, and clip-to-below. The
            // layer owns no pixels, so an empty/disabled chain is a pass-through.
            const bool hasEffects = m_effectRenderer
                && m_effectRenderer->hasRenderableEffects(
                    layer.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
            if (hasEffects) {
                // The visible composite below (currentBase) has the opaque canvas
                // background BAKED into every tile (see composite.frag.glsl, the
                // `backdrop.a >= 0.99999` branch). An adjustment must transform
                // only the actual content, never the canvas background, so the
                // effect runs on a background-FREE recomposite of the layers below
                // and the background is re-applied underneath afterwards.
                const bool bgBaked = backdropColor.a >= 0.99999f;
                const int pad = layerNeighborhoodPad(layer);
                std::vector<CompositeLayerInfo> belowLayers;
                auto ensureBelow = [&]() {
                    if (belowLayers.empty()) {
                        belowLayers.assign(
                            layers.begin(), layers.begin() + static_cast<ptrdiff_t>(idx));
                    }
                };

                // Background-free effected content of the layers below.
                GLuint effectedTex = 0;
                bool bgFree = false; // effectedTex computed from background-free content
                if (pad > 0 && !useSrcAtop) {
                    // Bounds-expanding adjustments (e.g. blur) must read across
                    // tile borders or they seam per tile: recomposite the below
                    // stack at the surrounding tiles for the padded source.
                    ensureBelow();
                    effectedTex = applyAdjustmentNeighborhoodEffects(
                        key, belowLayers, layer, tileRenderer, pad);
                    if (effectedTex) {
                        bgFree = true;
                    }
                }
                if (!effectedTex) {
                    GLuint contentBelow = 0;
                    if (bgBaked && !useSrcAtop) {
                        ensureBelow();
                        contentBelow = recompositeBelowBgFree(key, belowLayers, tileRenderer);
                        if (contentBelow) {
                            bgFree = true;
                        }
                    }
                    // No bg baked (transparent/semi backdrop), or recomposite not
                    // possible: currentBase is the content to transform. It is
                    // background-free unless an opaque bg is baked in (the fallback
                    // case, where we then skip the re-bake to avoid double bg).
                    if (!contentBelow) {
                        contentBelow = currentBase();
                        bgFree = !bgBaked;
                    }
                    effectedTex = applyLayerEffects(key, contentBelow, layer,
                        ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                }

                if (effectedTex != 0) {
                    GLuint effectedVisible = effectedTex;
                    // Re-apply the opaque canvas background under the effected
                    // content so the background itself stays unmodified. Use a
                    // dedicated temp so we never clobber a group/clip isolation
                    // buffer the adjustment may be nested inside.
                    if (bgBaked && bgFree) {
                        BlendPassParams over;
                        over.baseTex = solidColorTexture(backdropColor);
                        over.srcTex = effectedTex;
                        over.targetTex = m_programmaticBlendBaseTex;
                        over.key = key;
                        over.blendMode = 0;
                        over.opacity = 1.0f;
                        blendPass(over);
                        effectedVisible = m_programmaticBlendBaseTex;
                    }
                    BlendPassParams bp = makeBlendParams(effectedVisible);
                    bp.replaceBase = true;
                    bp.replaceBaseMixReveal = true;
                    blendPass(bp);
                    swapPingPong();
                }
            }
            // An adjustment contributes no content for layers clipping above it.
            clipBaseCandidateTex = transparentTex;

        } else if (layer.isGroup) {
            if (m_recomposeStopGroup) {
                if (&layer == m_recomposeStopGroup) {
                    compositeLayerStack(key, layer.children, tileRenderer, 1.0f,
                        /*useSrcAtop=*/false, backdropColor);
                    m_recomposeStopReached = true;
                    return currentBase();
                }
                if (groupSubtreeContains(layer.children, m_recomposeStopGroup)) {
                    compositeLayerStack(key, layer.children, tileRenderer, 1.0f,
                        /*useSrcAtop=*/false, backdropColor);
                    return currentBase();
                }
            }

            const bool usedAsClipBase = !layer.clippedToBelow && (idx + 1 < layers.size())
                && layers[idx + 1].clippedToBelow;
            const bool groupHasEffects = m_effectRenderer
                && m_effectRenderer->hasRenderableEffects(
                    layer.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
            const bool groupHasFinalMask = layer.externalClipMaskGrid || layer.useRadialReveal;
            const bool useGroupComposite = !layer.forceIsolation
                && (layer.blendMode != 0 || groupHasEffects || groupHasFinalMask
                    || layer.clippedToBelow || usedAsClipBase);

            if (useGroupComposite) {
                const int savedPing = m_currentPing;
                const GLuint savedTex0 = m_pingPongTex[0];
                const GLuint savedTex1 = m_pingPongTex[1];
                const GLuint outerCompositeTex = currentBase();
                const size_t frameDepth = m_groupCompositeDepth++;
                GroupCompositeFrame& frame = ensureGroupCompositeFrame(frameDepth);

                m_pingPongTex[0] = frame.ping[0];
                m_pingPongTex[1] = frame.ping[1];
                m_currentPing = 0;
                m_gl->glCopyImageSubData(outerCompositeTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                    frame.ping[0], GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE),
                    static_cast<GLsizei>(TILE_SIZE), 1);
                compositeLayerStack(key, layer.children, tileRenderer, 1.0f,
                    /*useSrcAtop=*/false, backdropColor);
                m_gl->glCopyImageSubData(currentBase(), GL_TEXTURE_2D, 0, 0, 0, 0,
                    frame.passThrough, GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE),
                    static_cast<GLsizei>(TILE_SIZE), 1);

                m_pingPongTex[0] = savedTex0;
                m_pingPongTex[1] = savedTex1;
                m_currentPing = savedPing;

                GLuint effectedVisual = 0;
                const int groupPad = layerNeighborhoodPad(layer);
                if (groupPad > 0) {
                    auto passThroughTileTexture = [&](const TileKey& neighborKey) -> GLuint {
                        if (neighborKey == key) {
                            return frame.passThrough;
                        }
                        return recomposePassThroughToGroup(neighborKey, &layer, tileRenderer,
                            backdropColor, frame.ping[0], frame.ping[1]);
                    };
                    effectedVisual = applyGroupNeighborhoodEffects(key, layer, tileRenderer,
                        groupPad, frame.passThrough,
                        /*allowCachedPaths=*/true, passThroughTileTexture);
                }
                if (!effectedVisual) {
                    effectedVisual = applyLayerEffects(key, frame.passThrough, layer,
                        ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                }
                m_gl->glCopyImageSubData(effectedVisual, GL_TEXTURE_2D, 0, 0, 0, 0, frame.effected,
                    GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE),
                    static_cast<GLsizei>(TILE_SIZE), 1);

                m_pingPongTex[0] = frame.ping[0];
                m_pingPongTex[1] = frame.ping[1];
                m_currentPing = 0;
                clearTexture(frame.ping[0]);
                compositeLayerStack(key, layer.children, tileRenderer, 1.0f,
                    /*useSrcAtop=*/false, Color::transparent());
                GLuint coverageResult = currentBase();
                m_gl->glCopyImageSubData(coverageResult, GL_TEXTURE_2D, 0, 0, 0, 0,
                    frame.sourceCoverage, GL_TEXTURE_2D, 0, 0, 0, 0,
                    static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE), 1);

                m_pingPongTex[0] = savedTex0;
                m_pingPongTex[1] = savedTex1;
                m_currentPing = savedPing;

                if (groupPad > 0) {
                    auto coverageTileTexture = [&](const TileKey& neighborKey) -> GLuint {
                        if (neighborKey == key) {
                            return frame.sourceCoverage;
                        }
                        const int coverageSavedPing = m_currentPing;
                        const GLuint coverageSavedTex0 = m_pingPongTex[0];
                        const GLuint coverageSavedTex1 = m_pingPongTex[1];
                        m_pingPongTex[0] = frame.ping[0];
                        m_pingPongTex[1] = frame.ping[1];
                        m_currentPing = 0;
                        clearTexture(frame.ping[0]);
                        compositeLayerStack(neighborKey, layer.children, tileRenderer, 1.0f,
                            /*useSrcAtop=*/false, Color::transparent());
                        const GLuint result = currentBase();
                        m_pingPongTex[0] = coverageSavedTex0;
                        m_pingPongTex[1] = coverageSavedTex1;
                        m_currentPing = coverageSavedPing;
                        return result;
                    };
                    const GLuint effectedCoverage = applyGroupNeighborhoodEffects(key, layer,
                        tileRenderer, groupPad, coverageResult,
                        /*allowCachedPaths=*/true, coverageTileTexture,
                        /*cacheIdentity=*/static_cast<const void*>(&layer.effects),
                        /*liveEditSourceVariant=*/1);
                    if (effectedCoverage) {
                        coverageResult = effectedCoverage;
                    }
                } else {
                    coverageResult = applyLayerEffects(key, coverageResult, layer,
                        ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                }
                m_gl->glCopyImageSubData(coverageResult, GL_TEXTURE_2D, 0, 0, 0, 0, frame.coverage,
                    GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE),
                    static_cast<GLsizei>(TILE_SIZE), 1);

                BlendPassParams groupBlend = makeBlendParams(frame.effected);
                groupBlend.useGroupComposite = true;
                groupBlend.groupPassThroughTex = frame.passThrough;
                groupBlend.groupSourceCoverageTex = frame.sourceCoverage;
                groupBlend.groupCoverageTex = frame.coverage;
                blendPass(groupBlend);
                swapPingPong();
                clipBaseCandidateTex = frame.coverage;
                --m_groupCompositeDepth;

            } else if (layer.blendMode != 0 || layer.forceIsolation || groupHasEffects
                || groupHasFinalMask || layer.clippedToBelow || usedAsClipBase) {
                int savedPing = m_currentPing;
                GLuint savedTex0 = m_pingPongTex[0];
                GLuint savedTex1 = m_pingPongTex[1];
                const GLuint outerCompositeTex = currentBase();
                const size_t frameDepth = m_groupCompositeDepth++;
                GroupCompositeFrame& frame = ensureGroupCompositeFrame(frameDepth);

                // Ordinary document groups can be nested arbitrarily. A frame per
                // depth keeps a nested group from overwriting an ancestor's partial
                // isolated composite.
                m_pingPongTex[0] = frame.ping[0];
                m_pingPongTex[1] = frame.ping[1];
                m_currentPing = 0;
                clearTexture(m_pingPongTex[0]);

                compositeLayerStack(key, layer.children, tileRenderer, 1.0f,
                    /*useSrcAtop=*/false, Color::transparent(), outerCompositeTex, layer.blendMode,
                    effectiveOpacity, backdropColor);

                GLuint groupResult = currentBase();

                m_pingPongTex[0] = savedTex0;
                m_pingPongTex[1] = savedTex1;
                m_currentPing = savedPing;

                // Bounds-expanding effects on the group (e.g. the brush-stroke
                // preview group carrying a blur) must read across tile borders or
                // they seam per tile — the neighbourhood path re-composites the
                // group's children at the surrounding tiles for correct padding.
                // Falls back to the per-tile path for unsupported groups.
                GLuint effectedGroupResult = 0;
                GLuint groupClipBaseTex = groupResult;
                const int groupPad = layerNeighborhoodPad(layer);
                if (groupPad > 0) {
                    effectedGroupResult = applyGroupNeighborhoodEffects(
                        key, layer, tileRenderer, groupPad, groupResult);
                    if (effectedGroupResult) {
                        // The neighbourhood path keeps a stable copy of the centre
                        // before running its re-entrant neighbour composites.
                        groupClipBaseTex = m_neighborhoodCenterTex;
                    }
                }
                if (!effectedGroupResult) {
                    effectedGroupResult = applyLayerEffects(key, groupResult, layer,
                        ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                }
                blendPass(makeBlendParams(effectedGroupResult));
                swapPingPong();
                clipBaseCandidateTex = groupClipBaseTex;
                --m_groupCompositeDepth;

            } else {
                compositeLayerStack(key, layer.children, tileRenderer, effectiveOpacity,
                    /*useSrcAtop=*/false, backdropColor);
            }

        } else if (layer.transform && layer.transformRenderer
            && layer.transformRenderer->hasAtlas()) {
            GLuint transformedTex = layer.transformRenderer->renderTransformedTile(
                key, *layer.transform, layer.transformPreserveMaskedSource);
            if (transformedTex != 0) {
                const GLuint effectedTex = applyLayerEffects(key, transformedTex, layer,
                    ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                blendPass(makeBlendParams(effectedTex));
                swapPingPong();
                clipBaseCandidateTex = transformedTex;
            }

        } else if (layer.hasSolidColor) {
            GLuint srcTex = solidColorTexture(layer.solidColor);
            if (srcTex != 0) {
                const GLuint effectedTex = applyLayerEffects(key, srcTex, layer,
                    ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                blendPass(makeBlendParams(effectedTex));
                swapPingPong();
                clipBaseCandidateTex = srcTex;
            }

        } else if (layer.retainedPayload && m_retainedRenderer) {
            const int pad = layerNeighborhoodPad(layer);
            if (pad > 0) {
                const int ring
                    = (pad + static_cast<int>(TILE_SIZE) - 1) / static_cast<int>(TILE_SIZE);
                if (!retainedNeighborhoodHasContent(key, *layer.retainedPayload, ring)) {
                    if (!layer.clippedToBelow) {
                        activeClipBaseTex = transparentTex;
                    }
                    ++idx;
                    continue;
                }

                const GLuint effectedTex
                    = applyRetainedEffectSource(key, layer, pad, currentBase());
                if (effectedTex) {
                    blendPass(makeBlendParams(effectedTex));
                    swapPingPong();
                }

                clipBaseCandidateTex
                    = m_retainedRenderer->renderPayloadTile(*layer.retainedPayload, key);
                if (!clipBaseCandidateTex) {
                    clipBaseCandidateTex = transparentTex;
                }
                if (!layer.clippedToBelow) {
                    activeClipBaseTex = clipBaseCandidateTex;
                }
                ++idx;
                continue;
            }

            const GLuint retainedTex
                = m_retainedRenderer->renderPayloadTile(*layer.retainedPayload, key);
            if (retainedTex != 0) {
                const GLuint effectedTex = applyLayerEffects(key, retainedTex, layer,
                    ruwa::core::effects::EffectEvaluationSpace::DocumentTile, false);
                blendPass(makeBlendParams(effectedTex));
                swapPingPong();
                clipBaseCandidateTex = retainedTex;
            }

        } else if (layer.tileGrid) {
            auto rawTileTexture = [&](const TileKey& tileKey) -> GLuint {
                return effectSourceTileTexture(*layer.tileGrid, tileRenderer, tileKey,
                    /*countUploadStats=*/true);
            };

            // Bounds-expanding effects (blur/shadow) read across tile borders and
            // bleed into otherwise-empty tiles, so they take a padded-neighbour
            // path that also runs where this layer has no tile of its own.
            const int pad = layerNeighborhoodPad(layer);
            if (pad > 0) {
                const int ring
                    = (pad + static_cast<int>(TILE_SIZE) - 1) / static_cast<int>(TILE_SIZE);
                if (!neighborhoodHasContent(key, layer, ring)) {
                    if (!layer.clippedToBelow) {
                        activeClipBaseTex = transparentTex;
                    }
                    ++idx;
                    continue;
                }

                const GLuint effectedTex = applyTileEffectSource(key, layer.effects, pad,
                    rawTileTexture, currentBase(),
                    /*realtimeOnly=*/false, layer.tileGrid,
                    /*wholeLayerGrid=*/layer.tileGrid,
                    /*backdropRevision=*/backdropRevision(layers, idx), layer.liveEditedEffectId,
                    liveEditCacheVariant(layer.liveEffectEditGeneration,
                        layer.tileGrid ? layer.tileGrid->contentVersion() : 0));
                if (effectedTex) {
                    blendPass(makeBlendParams(effectedTex));
                    swapPingPong();
                }
                // Clip base for layers above stays the raw centre content
                // (matching the non-effect path); empty centre clips to nothing.
                clipBaseCandidateTex = rawTileTexture(key);
                if (!clipBaseCandidateTex) {
                    clipBaseCandidateTex = transparentTex;
                }
                if (!layer.clippedToBelow) {
                    activeClipBaseTex
                        = clipBaseCandidateTex != 0 ? clipBaseCandidateTex : transparentTex;
                }
                ++idx;
                continue;
            }

            const GLuint sourceTex = rawTileTexture(key);
            if (!sourceTex) {
                if (!layer.clippedToBelow) {
                    activeClipBaseTex = transparentTex;
                }
                ++idx;
                continue;
            }

            const GLuint effectedTex = applyTileEffectSource(key, layer.effects,
                /*padPixels=*/0, rawTileTexture, currentBase(),
                /*realtimeOnly=*/false,
                /*blockCacheIdentity=*/layer.tileGrid,
                /*wholeLayerGrid=*/layer.tileGrid,
                /*backdropRevision=*/backdropRevision(layers, idx), layer.liveEditedEffectId,
                liveEditCacheVariant(layer.liveEffectEditGeneration,
                    layer.tileGrid ? layer.tileGrid->contentVersion() : 0));
            blendPass(makeBlendParams(effectedTex));
            swapPingPong();
            clipBaseCandidateTex = sourceTex;
        }

        if (!layer.clippedToBelow) {
            activeClipBaseTex = clipBaseCandidateTex != 0 ? clipBaseCandidateTex : transparentTex;
        }

        ++idx;
    }

    return currentBase();
}

void GLCompositor::compositeAllDirty(const std::vector<CompositeLayerInfo>& layers,
    CompositionCache& cache, GLTileRenderer* tileRenderer, const Color& backdropColor)
{
    resetFrameStats();
    if (!m_initialized || !cache.hasDirtyPositions())
        return;

    auto& dirtySet = cache.dirtyPositions();
    m_lastCandidateTiles = static_cast<uint32_t>(dirtySet.size());

    std::vector<TileKey> keysToProcess(dirtySet.begin(), dirtySet.end());
    compositeDirtyKeys(layers, cache, tileRenderer, keysToProcess, backdropColor);
}

void GLCompositor::compositeDirtyKeys(const std::vector<CompositeLayerInfo>& layers,
    CompositionCache& cache, GLTileRenderer* tileRenderer, const std::vector<TileKey>& keys,
    const Color& backdropColor)
{
    resetFrameStats();
    m_lastCandidateTiles = static_cast<uint32_t>(keys.size());
    if (!m_initialized || keys.empty()) {
        return;
    }

    // Block-evaluated effect results are only valid while the underlying
    // tile/stroke content is frozen, i.e. within one batch.
    resetEffectBlockCache();
    // New batch: any per-batch materialised group distortion region from a
    // previous batch is now stale (group content recomposites every frame).
    ++m_batchSerial;

    // Reset per-tile debug accumulators
    m_dbgTotalGuardUs = 0;
    m_dbgTotalClearUs = 0;
    m_dbgTotalStackUs = 0;
    m_dbgTotalSwapUs = 0;
    m_dbgTotalTileUs = 0;
    m_dbgTileCount = 0;
    m_dbgUploadCount = 0;

    QElapsedTimer dbgBatchTimer;
    dbgBatchTimer.start();

    // Save/restore FBO + viewport ONCE for the whole batch. compositeTile()
    // used to do this per tile, but the glGetIntegerv it issues forces a
    // CPU<->driver sync (~17 us/tile) that dominated frame time when many tiles
    // were composited.
    GLFboViewportGuard batchGuard(m_gl);

    std::vector<TileKey> keysToProcess = keys;
    for (const auto& key : keysToProcess) {
        compositeTile(key, layers, cache, tileRenderer, backdropColor);
        cache.clearDirtyPosition(key);
        ++m_lastCompositedTiles;
    }

    const qint64 dbgBatchUs = dbgBatchTimer.nsecsElapsed() / 1000;
    if (dbgBatchUs > 2000 && m_dbgTileCount > 0) { }
}

void GLCompositor::resetFrameStats()
{
    m_lastCompositedTiles = 0;
    m_lastCandidateTiles = 0;
    m_lastCompositeDrawCalls = 0;
}

// ==========================================================================
//   G P U   O P E R A T I O N S
// ==========================================================================

void GLCompositor::clearTexture(GLuint tex)
{
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
}

GLuint GLCompositor::renderStrokeBlendBase(GLuint outerBaseTex, GLuint layerContentTex,
    const TileKey& key, int layerBlendMode, float layerOpacity, const Color& backdropColor)
{
    if (!outerBaseTex || !layerContentTex || !m_programmaticBlendBaseTex) {
        return 0;
    }

    BlendPassParams bp;
    bp.baseTex = outerBaseTex;
    bp.srcTex = layerContentTex;
    bp.targetTex = m_programmaticBlendBaseTex;
    bp.key = key;
    bp.blendMode = layerBlendMode;
    bp.opacity = layerOpacity;
    bp.backdropColor = backdropColor;
    blendPass(bp);
    return m_programmaticBlendBaseTex;
}

void GLCompositor::blendPass(const BlendPassParams& p)
{
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        p.targetTex ? p.targetTex : currentTarget(), 0);

    // No GL blending — shader does the compositing math
    m_gl->glDisable(GL_BLEND);

    m_compositeProgram->use();
    m_compositeProgram->setUniform("uBaseTexture", 0);
    m_compositeProgram->setUniform("uSrcTexture", 1);
    m_compositeProgram->setUniform("uClipMaskTexture", 2);
    m_compositeProgram->setUniform("uBlendMode", p.blendMode);
    m_compositeProgram->setUniform("uOpacity", p.opacity);
    m_compositeProgram->setUniform("uUseClipMask", p.useClipMask ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskAlphaOnly", p.clipMaskAlphaOnly ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskAsAlphaCap", p.clipMaskAsAlphaCap ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskLuminanceReveal", p.clipMaskLuminanceReveal ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskTexture2", 4);
    m_compositeProgram->setUniform("uClipMaskEditPreview", p.clipMaskEditPreview ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskEditReplace", p.clipMaskEditReplace ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskEditStrokeOpacity", p.clipMaskEditStrokeOpacity);
    m_compositeProgram->setUniform(
        "uSubtractClipRevealFromSrc", p.subtractClipRevealFromSrc ? 1 : 0);
    m_compositeProgram->setUniform("uPreserveBaseAlpha", p.preserveBaseAlpha ? 1 : 0);
    m_compositeProgram->setUniform("uReplaceBase", p.replaceBase ? 1 : 0);
    m_compositeProgram->setUniform("uReplaceBaseMixReveal", p.replaceBaseMixReveal ? 1 : 0);
    m_compositeProgram->setUniform("uUseGroupComposite", p.useGroupComposite ? 1 : 0);
    m_compositeProgram->setUniform("uGroupPassThroughTexture", 5);
    m_compositeProgram->setUniform("uGroupCoverageTexture", 6);
    m_compositeProgram->setUniform("uGroupSourceCoverageTexture", 7);
    m_compositeProgram->setUniform("uUseProgrammaticBlendBase", p.useProgrammaticBlendBase ? 1 : 0);
    m_compositeProgram->setUniform("uProgrammaticBlendBaseTexture", 3);
    m_compositeProgram->setUniform("uSrcAtop", p.srcAtop ? 1 : 0);
    m_compositeProgram->setUniform("uUseRadialReveal", p.useRadialReveal ? 1 : 0);
    m_compositeProgram->setUniform("uRadialRevealInvert", p.radialRevealInvert ? 1 : 0);
    m_compositeProgram->setUniform(
        "uRadialRevealOrigin", p.radialRevealOrigin.x, p.radialRevealOrigin.y);
    m_compositeProgram->setUniform("uRadialRevealRadius", p.radialRevealRadius);
    m_compositeProgram->setUniform("uRadialRevealFeather", p.radialRevealFeather);
    m_compositeProgram->setUniform("uBackdropColor", p.backdropColor.r, p.backdropColor.g,
        p.backdropColor.b, p.backdropColor.a);
    float tileOriginX = 0.0f;
    float tileOriginY = 0.0f;
    tileWorldOrigin(p.key, tileOriginX, tileOriginY);
    m_compositeProgram->setUniform("uTileWorldOrigin", tileOriginX, tileOriginY);

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.baseTex);

    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.srcTex);

    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.useClipMask ? p.clipMaskTex : 0);

    m_gl->glActiveTexture(GL_TEXTURE3);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.useProgrammaticBlendBase ? p.programmaticBlendBaseTex : 0);

    m_gl->glActiveTexture(GL_TEXTURE4);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.useClipMask2 ? p.clipMaskTex2 : 0);

    m_gl->glActiveTexture(GL_TEXTURE5);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.useGroupComposite ? p.groupPassThroughTex : 0);
    m_gl->glActiveTexture(GL_TEXTURE6);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.useGroupComposite ? p.groupCoverageTex : 0);
    m_gl->glActiveTexture(GL_TEXTURE7);
    m_gl->glBindTexture(GL_TEXTURE_2D, p.useGroupComposite ? p.groupSourceCoverageTex : 0);

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
    ++m_lastCompositeDrawCalls;

    m_gl->glActiveTexture(GL_TEXTURE7);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE6);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE5);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE4);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE3);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
}

ruwa::core::effects::EffectRegionFrame GLCompositor::effectRegionForTile(const TileKey& key) const
{
    // Document-pixel frame of this tile: a tile texel is one document pixel and
    // the tile's (0,0) sits at key*TILE_SIZE. Lets positional effects (gradient
    // overlay, vignette) stay continuous across tile borders.
    ruwa::core::effects::EffectRegionFrame region;
    region.originX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE);
    region.originY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE);
    region.documentPxPerTexel = 1.0f;
    region.valid = true;
    return region;
}

GLuint GLCompositor::effectSourceTileTexture(
    TileGrid& grid, GLTileRenderer* tileRenderer, const TileKey& key, bool countUploadStats)
{
    if (!tileRenderer) {
        return 0;
    }
    TileData* tile = grid.getTile(key);
    if (!tile) {
        return 0;
    }
    bool uploaded = false;
    if (!tile->hasTexture()) {
        tileRenderer->ensureTileTexture(*tile);
        tileRenderer->uploadTileData(*tile);
        uploaded = true;
    } else if (tile->isDirty()) {
        tileRenderer->uploadTileData(*tile);
        uploaded = true;
    }
    if (uploaded && countUploadStats) {
        ++m_dbgUploadCount;
    }
    return tile->hasTexture() ? tile->textureId() : 0;
}

std::unordered_set<TileKey, TileKeyHash> GLCompositor::effectOutputKeysForGrid(
    const TileGrid& grid, const QList<ruwa::core::effects::LayerEffectState>& effects) const
{
    std::unordered_set<TileKey, TileKeyHash> sourceKeys;
    for (const auto& entry : grid.tiles()) {
        sourceKeys.insert(entry.first);
    }
    return ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(
        sourceKeys, effects);
}

GLuint GLCompositor::applyLayerEffects(const TileKey& key, GLuint sourceTexture,
    const CompositeLayerInfo& layer, ruwa::core::effects::EffectEvaluationSpace space,
    bool realtimeOnly, GLuint finalTargetTexture)
{
    if (!m_effectRenderer || !m_effectRenderer->isInitialized() || layer.effects.isEmpty()) {
        return sourceTexture;
    }

    // The current ping-pong base is the composite of everything below this
    // layer, i.e. the backdrop effects like drop-shadow may sample.
    EffectChainRequest req;
    req.sourceTexture = sourceTexture;
    req.width = TILE_SIZE;
    req.height = TILE_SIZE;
    req.effects = &layer.effects;
    req.space = space;
    req.realtimeOnly = realtimeOnly;
    req.finalTargetTexture = finalTargetTexture;
    req.backdropTexture = currentBase();
    req.region = effectRegionForTile(key);
    req.liveEditedEffectId = layer.liveEditedEffectId;
    const uint64_t sourceRevision = layer.tileGrid
        ? layer.tileGrid->contentVersion()
        : static_cast<uint64_t>(reinterpret_cast<uintptr_t>(layer.retainedPayload));
    req.liveEditSourceVariant
        = liveEditCacheVariant(layer.liveEffectEditGeneration, sourceRevision);
    return m_effectRenderer->applyEffects(req);
}

GLuint GLCompositor::applyTileEffectSource(const TileKey& key,
    const QList<ruwa::core::effects::LayerEffectState>& effects, int padPixels,
    const std::function<GLuint(const TileKey&)>& tileTexture, GLuint backdropTexture,
    bool realtimeOnly, const void* blockCacheIdentity, const TileGrid* wholeLayerGrid,
    uint64_t backdropRevision, const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    if (!tileTexture) {
        return 0;
    }
    const quint64 prefixCacheVariant
        = liveEditCacheVariant(liveEditSourceVariant, backdropRevision);
    const bool canRenderEffects
        = m_effectRenderer && m_effectRenderer->isInitialized() && !effects.isEmpty();

    // A dirty final-composition tile does not imply that every contributing
    // layer changed. Reuse the effected output of static raster layers across
    // batches; this is the common case when painting on a different layer.
    const bool backdropDependent = effectsRequireBackdrop(effects);
    const bool canUsePersistentCache = canRenderEffects && blockCacheIdentity && wholeLayerGrid
        && !realtimeOnly && (!chainNeedsWholeLayer(effects) || backdropDependent)
        && (!backdropDependent || backdropRevision != 0);
    if (canUsePersistentCache) {
        const GLuint cached = findCachedLayerEffectTile(
            blockCacheIdentity, *wholeLayerGrid, key, effects, backdropRevision);
        if (cached) {
            return cached;
        }
    }
    const auto persistResult = [&](GLuint result) -> GLuint {
        if (!result || !canUsePersistentCache) {
            return result;
        }
        return storeCachedLayerEffectTile(
            blockCacheIdentity, *wholeLayerGrid, key, effects, backdropRevision, result);
    };

    // Distortion class: a readsWholeLayer effect samples the layer at arbitrary
    // positions, so materialise the whole layer once (cached per batch) and slice
    // this tile out of it. requiresBackdrop is unsupported here (no per-tile
    // backdrop on the materialised source), and beyond the VRAM cap the helper
    // returns 0 so we fall through to the bounded neighbourhood path.
    if (canRenderEffects && wholeLayerGrid && !effectsRequireBackdrop(effects)
        && chainNeedsWholeLayer(effects)) {
        const void* identity
            = blockCacheIdentity ? blockCacheIdentity : static_cast<const void*>(wholeLayerGrid);
        const GLuint wholeTile = wholeLayerEffectTile(identity, *wholeLayerGrid, key, padPixels,
            effects, tileTexture, backdropTexture, liveEditedEffectId, prefixCacheVariant);
        if (wholeTile) {
            return wholeTile;
        }
    }

    if (padPixels > 0 && canRenderEffects) {
        if (blockCacheIdentity && !effectsRequireBackdrop(effects)) {
            const GLuint blockTile = blockNeighborhoodEffectTile(blockCacheIdentity, key, padPixels,
                effects, tileTexture, liveEditedEffectId, prefixCacheVariant);
            if (blockTile) {
                return persistResult(blockTile);
            }
        }

        auto neighborTexture = [&](int dx, int dy) -> GLuint {
            return tileTexture(TileKey { key.x + dx, key.y + dy });
        };
        return persistResult(
            m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
                effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile, realtimeOnly,
                backdropTexture, effectRegionForTile(key), liveEditedEffectId, prefixCacheVariant));
    }

    const GLuint sourceTexture = tileTexture(key);
    if (!sourceTexture) {
        return 0;
    }
    if (!canRenderEffects) {
        return sourceTexture;
    }
    EffectChainRequest req;
    req.sourceTexture = sourceTexture;
    req.width = TILE_SIZE;
    req.height = TILE_SIZE;
    req.effects = &effects;
    req.space = ruwa::core::effects::EffectEvaluationSpace::DocumentTile;
    req.realtimeOnly = realtimeOnly;
    req.backdropTexture = backdropTexture;
    req.region = effectRegionForTile(key);
    req.liveEditedEffectId = liveEditedEffectId;
    req.liveEditSourceVariant = prefixCacheVariant;
    return persistResult(m_effectRenderer->applyEffects(req));
}

int GLCompositor::layerNeighborhoodPad(const CompositeLayerInfo& layer) const
{
    if (!m_effectRenderer || !m_effectRenderer->isInitialized() || layer.effects.isEmpty()) {
        return 0;
    }
    if (!m_effectRenderer->hasRenderableEffects(layer.effects,
            ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
            /*realtimeOnly=*/false)) {
        return 0;
    }
    return ruwa::core::effects::EffectCoverageResolver::stableLiveEditNeighborhoodPadPixels(
        layer.effects, layer.liveEditedEffectId, layer.liveEditedEffectParamKey);
}

bool GLCompositor::neighborhoodHasContent(
    const TileKey& key, const CompositeLayerInfo& layer, int ring) const
{
    if (!layer.tileGrid) {
        return false;
    }
    for (int dy = -ring; dy <= ring; ++dy) {
        for (int dx = -ring; dx <= ring; ++dx) {
            if (layer.tileGrid->getTile(TileKey { key.x + dx, key.y + dy })) {
                return true;
            }
        }
    }
    return false;
}

bool GLCompositor::retainedNeighborhoodHasContent(
    const TileKey& key, const RetainedRenderPayload& payload, int ring) const
{
    if (payload.empty()) {
        return false;
    }
    for (int dy = -ring; dy <= ring; ++dy) {
        for (int dx = -ring; dx <= ring; ++dx) {
            if (retainedPayloadIntersectsTile(payload, TileKey { key.x + dx, key.y + dy })) {
                return true;
            }
        }
    }
    return false;
}

GLuint GLCompositor::applyRetainedEffectSource(
    const TileKey& key, const CompositeLayerInfo& layer, int padPixels, GLuint backdropTexture)
{
    if (!m_retainedRenderer || !layer.retainedPayload) {
        return 0;
    }

    auto retainedTileTexture = [&](const TileKey& tileKey) -> GLuint {
        return m_retainedRenderer->renderPayloadTile(*layer.retainedPayload, tileKey);
    };

    if (m_effectRenderer && m_effectRenderer->isInitialized() && !layer.effects.isEmpty()
        && !effectsRequireBackdrop(layer.effects) && chainNeedsWholeLayer(layer.effects)) {
        const void* identity = layer.retainedPayload;
        const GLuint wholeTile
            = wholeRetainedEffectTile(identity, *layer.retainedPayload, key, padPixels,
                layer.effects, retainedTileTexture, backdropTexture, layer.liveEditedEffectId,
                liveEditCacheVariant(layer.liveEffectEditGeneration,
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(layer.retainedPayload))));
        if (wholeTile) {
            return wholeTile;
        }
    }

    if (padPixels > 0 && m_effectRenderer && m_effectRenderer->isInitialized()) {
        if (!effectsRequireBackdrop(layer.effects)) {
            const GLuint blockTile = blockNeighborhoodEffectTile(layer.retainedPayload, key,
                padPixels, layer.effects, retainedTileTexture, layer.liveEditedEffectId,
                liveEditCacheVariant(layer.liveEffectEditGeneration,
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(layer.retainedPayload))));
            if (blockTile) {
                return blockTile;
            }
        }

        auto neighborTexture = [&](int dx, int dy) -> GLuint {
            return retainedTileTexture(TileKey { key.x + dx, key.y + dy });
        };
        return m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
            layer.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
            /*realtimeOnly=*/false, backdropTexture, effectRegionForTile(key),
            layer.liveEditedEffectId,
            liveEditCacheVariant(layer.liveEffectEditGeneration,
                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(layer.retainedPayload))));
    }

    const GLuint sourceTexture = retainedTileTexture(key);
    if (!sourceTexture) {
        return 0;
    }
    return applyLayerEffects(key, sourceTexture, layer,
        ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false);
}

bool GLCompositor::bakeEffectsIntoGrid(TileGrid& grid,
    const QList<ruwa::core::effects::LayerEffectState>& effects, GLTileRenderer* tileRenderer,
    const std::function<void(const TileKey&)>& beforeTileWrite,
    std::vector<TileKey>& outTouchedKeys)
{
    outTouchedKeys.clear();
    if (!m_effectRenderer || !m_effectRenderer->isInitialized() || !tileRenderer
        || effects.isEmpty()) {
        return false;
    }
    if (!m_effectRenderer->hasRenderableEffects(effects,
            ruwa::core::effects::EffectEvaluationSpace::DocumentTile, /*realtimeOnly=*/false)) {
        return false;
    }

    CompositeLayerInfo layerInfo;
    layerInfo.tileGrid = &grid;
    layerInfo.effects = effects;

    const int padPixels = layerNeighborhoodPad(layerInfo);

    const auto keysToBake = effectOutputKeysForGrid(grid, effects);
    if (keysToBake.empty()) {
        return false;
    }

    if (!m_fbo) {
        m_gl->glGenFramebuffers(1, &m_fbo);
    }
    GLint prevFbo = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    const TilePixelFormat fmt = grid.format();
    const size_t bytesPerTile = tileByteSize(fmt);
    const GLenum pixelType = tileGLPixelType(fmt);
    std::vector<uint8_t> buffer(bytesPerTile);

    // A real, zero-initialized 1x1 texture stands in for "nothing behind this
    // layer" — the same idiom compositeLayerStack() uses for its base clip
    // texture. Binding texture id 0 instead left the sampler reading whatever
    // the driver considers an "unbound" fetch, which some passes turned into
    // fully-transparent output instead of the intended zero backdrop.
    const GLuint transparentBackdrop = transparentTexture();

    // Ensures a tile's GPU texture reflects its current CPU pixels; used both
    // as the direct source (no padding) and as the neighbour-gather callback
    // (with padding). Returns 0 for an absent tile.
    auto rawTileTexture = [&](const TileKey& tileKey) -> GLuint {
        return effectSourceTileTexture(grid, tileRenderer, tileKey,
            /*countUploadStats=*/false);
    };

    // Phase 1: compute every tile's baked result and stage it in CPU memory,
    // WITHOUT writing anything back into the grid yet. Neighbouring tiles'
    // GPU textures are read (and only re-uploaded if dirty) purely as effect
    // *inputs* here — if an already-baked tile were written back into the grid
    // mid-loop, a later tile's neighbour gather would re-upload and sample
    // that ALREADY-EFFECTED content instead of the original pixels, cascading
    // the effect (e.g. blur-of-blur-of-blur) across the whole grid.
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> staged;
    for (const TileKey& key : keysToBake) {
        // Baking runs outside a live composite frame, so there is no
        // meaningful "backdrop" (currentBase() would hold stale garbage from
        // whatever last composited on this context) — always pass transparent.
        const GLuint resultTex
            = applyTileEffectSource(key, effects, padPixels, rawTileTexture, transparentBackdrop,
                /*realtimeOnly=*/false,
                /*blockCacheIdentity=*/nullptr,
                /*wholeLayerGrid=*/&grid);
        if (!resultTex) {
            continue;
        }

        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resultTex, 0);
        if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            continue;
        }
        m_gl->glViewport(0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE));
        m_gl->glReadPixels(0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE),
            GL_RGBA, pixelType, buffer.data());

        if (beforeTileWrite) {
            beforeTileWrite(key);
        }
        staged[key] = buffer;
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // Phase 2: now that every tile's baked result has been computed against
    // ORIGINAL neighbour content, commit them all into the grid.
    for (auto& [key, pixels] : staged) {
        TileData& dstTile = grid.getOrCreateTile(key);
        std::memcpy(dstTile.pixels(), pixels.data(), bytesPerTile);
        dstTile.markDirty();
        outTouchedKeys.push_back(key);
    }

    grid.pruneEmpty();
    return !staged.empty();
}

bool GLCompositor::groupSubtreeContains(
    const std::vector<CompositeLayerInfo>& layers, const CompositeLayerInfo* target) const
{
    for (const CompositeLayerInfo& candidate : layers) {
        if (&candidate == target || groupSubtreeContains(candidate.children, target)) {
            return true;
        }
    }
    return false;
}

GLuint GLCompositor::recomposePassThroughToGroup(const TileKey& key,
    const CompositeLayerInfo* target, GLTileRenderer* tileRenderer, const Color& backdropColor,
    GLuint scratch0, GLuint scratch1)
{
    if (!m_activeRootLayers || !target || !tileRenderer || !scratch0 || !scratch1) {
        return 0;
    }

    const int savedPing = m_currentPing;
    const GLuint savedTex0 = m_pingPongTex[0];
    const GLuint savedTex1 = m_pingPongTex[1];
    const CompositeLayerInfo* savedStopGroup = m_recomposeStopGroup;
    const bool savedStopReached = m_recomposeStopReached;

    m_pingPongTex[0] = scratch0;
    m_pingPongTex[1] = scratch1;
    m_currentPing = 0;
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    clearTexture(scratch0);
    m_recomposeStopGroup = target;
    m_recomposeStopReached = false;
    compositeLayerStack(key, *m_activeRootLayers, tileRenderer, 1.0f,
        /*useSrcAtop=*/false, backdropColor);
    const GLuint result = currentBase();

    m_recomposeStopGroup = savedStopGroup;
    m_recomposeStopReached = savedStopReached;
    m_pingPongTex[0] = savedTex0;
    m_pingPongTex[1] = savedTex1;
    m_currentPing = savedPing;
    return result;
}

GLuint GLCompositor::applyGroupNeighborhoodEffects(const TileKey& key,
    const CompositeLayerInfo& layer, GLTileRenderer* tileRenderer, int padPixels,
    GLuint groupResultTexture, bool allowCachedPaths,
    const std::function<GLuint(const TileKey&)>& passThroughTileTexture, const void* cacheIdentity,
    quint64 liveEditSourceVariant)
{
    if (!m_effectRenderer || !tileRenderer || padPixels <= 0 || !groupResultTexture) {
        return 0;
    }
    uint64_t sourceRevision = 0x84222325cbf29ce4ULL;
    for (const auto& child : layer.children) {
        const uint64_t childRevision = layerContentRevision(child);
        sourceRevision ^= childRevision + 0x9e3779b97f4a7c15ULL + (sourceRevision << 6)
            + (sourceRevision >> 2);
    }
    const quint64 prefixCacheVariant = liveEditCacheVariant(
        layer.liveEffectEditGeneration, sourceRevision, liveEditSourceVariant);

    // The centre group result is the (0,0) padding tile / centre input for both
    // paths below, and the re-entrant per-tile composites clobber the buffer it
    // lives in, so copy it to a stable texture first.
    m_gl->glCopyImageSubData(groupResultTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_neighborhoodCenterTex,
        GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE),
        1);

    // ---- Distortion (whole-group) path ----
    // A distortion samples a disk anchored at an arbitrary document centre, which
    // the bounded block/neighbourhood padding cannot hold once a tile is far from
    // that centre; materialise the whole group as one region. Unlike the blur
    // path this supports ARBITRARY group structure — in particular a nested
    // stroke-preview group, present when painting on a layer INSIDE this distorted
    // group — because it re-composites the FULL group (recursing through the child
    // hierarchy) at every region tile instead of assuming flat content+overlay
    // grids. Falls through to the bounded path only over the VRAM cap.
    if (allowCachedPaths && chainNeedsWholeLayer(layer.effects)
        && !effectsRequireBackdrop(layer.effects)) {
        // Content bbox over the group's whole subtree (every tile-backed grid:
        // nested layers, stroke buffers, ...).
        bool haveBounds = false;
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
        std::function<void(const std::vector<CompositeLayerInfo>&)> scanBounds
            = [&](const std::vector<CompositeLayerInfo>& children) {
                  for (const auto& child : children) {
                      if (child.tileGrid) {
                          for (const auto& entry : child.tileGrid->tiles()) {
                              const TileKey& tk = entry.first;
                              if (!haveBounds) {
                                  minX = maxX = tk.x;
                                  minY = maxY = tk.y;
                                  haveBounds = true;
                              } else {
                                  minX = std::min(minX, tk.x);
                                  minY = std::min(minY, tk.y);
                                  maxX = std::max(maxX, tk.x);
                                  maxY = std::max(maxY, tk.y);
                              }
                          }
                      }
                      if (!child.children.empty()) {
                          scanBounds(child.children);
                      }
                  }
              };
        scanBounds(layer.children);

        if (haveBounds) {
            // Per-tile source: the full group re-composited into a DEDICATED
            // isolation buffer. Nested groups use depth-indexed frames, so they
            // cannot clobber the region being assembled. The centre tile reuses
            // the stable centre copy.
            auto groupCompositeTile = [&](const TileKey& nk) -> GLuint {
                if (passThroughTileTexture) {
                    return passThroughTileTexture(nk);
                }
                if (nk == key) {
                    return m_neighborhoodCenterTex;
                }
                const int savedPing = m_currentPing;
                const GLuint savedTex0 = m_pingPongTex[0];
                const GLuint savedTex1 = m_pingPongTex[1];
                m_pingPongTex[0] = m_wholeGroupTempTex[0];
                m_pingPongTex[1] = m_wholeGroupTempTex[1];
                m_currentPing = 0;
                m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
                clearTexture(m_pingPongTex[0]);
                compositeLayerStack(nk, layer.children, tileRenderer, 1.0f);
                const GLuint result = currentBase();
                m_pingPongTex[0] = savedTex0;
                m_pingPongTex[1] = savedTex1;
                m_currentPing = savedPing;
                return result;
            };
            const void* identity = cacheIdentity ? cacheIdentity : static_cast<const void*>(&layer);
            const GLuint wholeTile = wholeGroupEffectTile(identity, minX, minY, maxX, maxY, key,
                padPixels, layer.effects, groupCompositeTile, currentBase(),
                layer.liveEditedEffectId, prefixCacheVariant);
            if (wholeTile) {
                return wholeTile;
            }
        }
    }

    if (passThroughTileTexture) {
        auto groupContentTexture
            = [&](const TileKey& nk) -> GLuint { return passThroughTileTexture(nk); };
        if (allowCachedPaths && !effectsRequireBackdrop(layer.effects)) {
            const GLuint blockTile = blockNeighborhoodEffectTile(
                cacheIdentity ? cacheIdentity : static_cast<const void*>(&layer), key, padPixels,
                layer.effects, groupContentTexture, layer.liveEditedEffectId, prefixCacheVariant);
            if (blockTile) {
                return blockTile;
            }
        }
        auto neighborTexture = [&](int dx, int dy) -> GLuint {
            return groupContentTexture(TileKey { key.x + dx, key.y + dy });
        };
        return m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
            layer.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
            /*realtimeOnly=*/false, currentBase(), effectRegionForTile(key),
            layer.liveEditedEffectId, prefixCacheVariant);
    }

    // ---- Bounded neighbourhood path ----
    // Flat content+overlay groups keep the cheap raw-tile fast path. During a
    // live stroke, however, LayerCompositingBuilder wraps the painted layer in
    // an isolated preview group. Effects such as Ripple still require correctly
    // composited neighbour tiles in that state, so non-flat groups take the
    // recursive fallback below instead of degrading to a single-tile effect pass.
    TileGrid* contentGrid = nullptr;
    std::vector<TileGrid*> overlayGrids;
    bool simpleFlatGroup = true;
    for (const auto& child : layer.children) {
        if (child.isGroup || child.clippedToBelow) {
            simpleFlatGroup = false;
            break;
        }
        if (child.tileGrid) {
            if (!contentGrid) {
                contentGrid = child.tileGrid;
            } else {
                overlayGrids.push_back(child.tileGrid);
            }
        }
    }

    if (!simpleFlatGroup || !contentGrid) {
        auto fullGroupTexture = [&](const TileKey& nk) -> GLuint {
            const int savedPing = m_currentPing;
            const GLuint savedTex0 = m_pingPongTex[0];
            const GLuint savedTex1 = m_pingPongTex[1];
            const size_t frameDepth = m_groupCompositeDepth++;
            GroupCompositeFrame& frame = ensureGroupCompositeFrame(frameDepth);
            m_pingPongTex[0] = frame.ping[0];
            m_pingPongTex[1] = frame.ping[1];
            m_currentPing = 0;
            m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
            clearTexture(m_pingPongTex[0]);
            compositeLayerStack(nk, layer.children, tileRenderer, 1.0f);
            const GLuint result = currentBase();
            m_pingPongTex[0] = savedTex0;
            m_pingPongTex[1] = savedTex1;
            m_currentPing = savedPing;
            --m_groupCompositeDepth;
            return result;
        };

        const void* identity = cacheIdentity ? cacheIdentity : static_cast<const void*>(&layer);
        if (allowCachedPaths && !effectsRequireBackdrop(layer.effects)) {
            const GLuint blockTile = blockNeighborhoodEffectTile(identity, key, padPixels,
                layer.effects, fullGroupTexture, layer.liveEditedEffectId, prefixCacheVariant);
            if (blockTile) {
                return blockTile;
            }
        }

        auto neighborTexture = [&](int dx, int dy) -> GLuint {
            return fullGroupTexture(TileKey { key.x + dx, key.y + dy });
        };
        return m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
            layer.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
            /*realtimeOnly=*/false, currentBase(), effectRegionForTile(key),
            layer.liveEditedEffectId, prefixCacheVariant);
    }

    auto rawContentTexture = [&](const TileKey& nk) -> GLuint {
        return effectSourceTileTexture(*contentGrid, tileRenderer, nk,
            /*countUploadStats=*/true);
    };

    auto groupContentTexture = [&](const TileKey& nk) -> GLuint {
        if (nk == key) {
            // Centre already composited into the stable copy above (the re-entrant
            // composites below clobber the buffer it originally lived in).
            return m_neighborhoodCenterTex;
        }
        bool overlayHere = false;
        for (TileGrid* overlay : overlayGrids) {
            if (overlay->getTile(nk)) {
                overlayHere = true;
                break;
            }
        }
        if (!overlayHere) {
            // No stroke here -> raw committed content (cheap; identical to a full
            // composite, and matches what the committed cache blurred with).
            return rawContentTexture(nk);
        }
        // Stroke present -> re-entrant composite of content + stroke at the
        // neighbour key into the group temp buffers (centre saved off above).
        const int savedPing = m_currentPing;
        const GLuint savedTex0 = m_pingPongTex[0];
        const GLuint savedTex1 = m_pingPongTex[1];
        m_pingPongTex[0] = m_groupTempTex[0];
        m_pingPongTex[1] = m_groupTempTex[1];
        m_currentPing = 0;
        m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
        clearTexture(m_pingPongTex[0]);
        compositeLayerStack(nk, layer.children, tileRenderer, 1.0f);
        const GLuint result = currentBase();
        m_pingPongTex[0] = savedTex0;
        m_pingPongTex[1] = savedTex1;
        m_currentPing = savedPing;
        return result;
    };

    // Block-cached fast path: one chain evaluation covers a whole block of
    // tiles for this batch — during a stroke this is what keeps painting under
    // a large-radius blur from re-blurring the padded region per tile.
    if (allowCachedPaths && !effectsRequireBackdrop(layer.effects)) {
        const GLuint blockTile = blockNeighborhoodEffectTile(contentGrid, key, padPixels,
            layer.effects, groupContentTexture, layer.liveEditedEffectId, prefixCacheVariant);
        if (blockTile) {
            return blockTile;
        }
    }

    auto neighborTexture = [&](int dx, int dy) -> GLuint {
        return groupContentTexture(TileKey { key.x + dx, key.y + dy });
    };

    return m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
        layer.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, currentBase(), effectRegionForTile(key), layer.liveEditedEffectId,
        prefixCacheVariant);
}

bool GLCompositor::adjustmentBelowStackSupported(
    const std::vector<CompositeLayerInfo>& belowLayers) const
{
    // The bg-free recomposite borrows m_adjustmentTempTex as its outer ping-pong.
    // Group isolation itself is safe at arbitrary depth because it uses the
    // depth-indexed frame pool. Nested adjustments, clip groups and
    // bounds-expanding group effects still need separate transient state and
    // fall back to the regular per-tile path here.
    auto pairBusy = [&](GLuint a, GLuint b) {
        return m_pingPongTex[0] == a || m_pingPongTex[0] == b || m_pingPongTex[1] == a
            || m_pingPongTex[1] == b;
    };
    if (pairBusy(m_adjustmentTempTex[0], m_adjustmentTempTex[1])) {
        return false; // no nested adjustment recomposite
    }
    std::function<bool(const CompositeLayerInfo&)> isSupported
        = [&](const CompositeLayerInfo& l) -> bool {
        if (l.isAdjustment || l.clippedToBelow) {
            return false;
        }
        if (!l.isGroup) {
            return true;
        }
        if (layerNeighborhoodPad(l) > 0) {
            return false;
        }
        for (const auto& c : l.children) {
            if (!isSupported(c)) {
                return false;
            }
        }
        return true;
    };

    for (const auto& l : belowLayers) {
        if (!isSupported(l)) {
            return false;
        }
    }
    return true;
}

GLuint GLCompositor::recompositeBelowBgFree(const TileKey& key,
    const std::vector<CompositeLayerInfo>& belowLayers, GLTileRenderer* tileRenderer)
{
    if (!tileRenderer || !adjustmentBelowStackSupported(belowLayers)) {
        return 0;
    }

    const int savedPing = m_currentPing;
    const GLuint savedTex0 = m_pingPongTex[0];
    const GLuint savedTex1 = m_pingPongTex[1];
    m_pingPongTex[0] = m_adjustmentTempTex[0];
    m_pingPongTex[1] = m_adjustmentTempTex[1];
    m_currentPing = 0;
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    clearTexture(m_pingPongTex[0]);
    // Transparent backdrop → the result is the content only (no baked canvas bg).
    compositeLayerStack(key, belowLayers, tileRenderer, 1.0f);
    const GLuint result = currentBase();
    // Copy to a stable texture: callers (and per-neighbour recomposites) clobber
    // the borrowed buffer afterwards.
    m_gl->glCopyImageSubData(result, GL_TEXTURE_2D, 0, 0, 0, 0, m_neighborhoodCenterTex,
        GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE),
        1);
    m_pingPongTex[0] = savedTex0;
    m_pingPongTex[1] = savedTex1;
    m_currentPing = savedPing;
    return m_neighborhoodCenterTex;
}

bool GLCompositor::effectsRequireBackdrop(
    const QList<ruwa::core::effects::LayerEffectState>& effects) const
{
    for (const auto& effect : effects) {
        if (!effect.enabled) {
            continue;
        }
        const auto* descriptor
            = ruwa::core::effects::LayerEffectRegistry::instance().descriptor(effect.typeId);
        if (descriptor && descriptor->capabilities.requiresBackdrop) {
            return true;
        }
    }
    return false;
}

bool GLCompositor::chainNeedsWholeLayer(
    const QList<ruwa::core::effects::LayerEffectState>& effects) const
{
    for (const auto& effect : effects) {
        if (!effect.enabled) {
            continue;
        }
        const auto* descriptor
            = ruwa::core::effects::LayerEffectRegistry::instance().descriptor(effect.typeId);
        if (descriptor && descriptor->capabilities.readsWholeLayer) {
            return true;
        }
    }
    return false;
}

void GLCompositor::dropWholeLayerCacheEntry(const void* contentIdentity)
{
    auto it = m_wholeLayerCache.find(contentIdentity);
    if (it == m_wholeLayerCache.end()) {
        return;
    }
    deleteTexture(m_gl, it->second.texture);
    m_wholeLayerCache.erase(it);
}

void GLCompositor::evictWholeLayerCacheIfNeeded(const void* keepIdentity)
{
    while (m_wholeLayerCache.size() > kMaxWholeLayerEntries) {
        auto victim = m_wholeLayerCache.end();
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        for (auto it = m_wholeLayerCache.begin(); it != m_wholeLayerCache.end(); ++it) {
            if (it->first == keepIdentity) {
                continue; // never evict the entry we just produced
            }
            if (it->second.lastUseSerial < oldest) {
                oldest = it->second.lastUseSerial;
                victim = it;
            }
        }
        if (victim == m_wholeLayerCache.end()) {
            break;
        }
        deleteTexture(m_gl, victim->second.texture);
        m_wholeLayerCache.erase(victim);
    }
}

GLuint GLCompositor::wholeLayerEffectTile(const void* contentIdentity, const TileGrid& grid,
    const TileKey& key, int maxDisplacementPx,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    const std::function<GLuint(const TileKey&)>& tileTexture, GLuint backdropTexture,
    const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    if (!m_effectRenderer || !contentIdentity || !tileTexture) {
        return 0;
    }

    const uint64_t contentVersion = grid.contentVersion();
    const size_t tileCount = grid.tileCount();

    auto sliceFrom = [&](const WholeLayerCacheEntry& entry) -> GLuint {
        const int tileX = key.x - entry.originTileX;
        const int tileY = key.y - entry.originTileY;
        if (tileX < 0 || tileY < 0 || static_cast<uint32_t>(tileX) >= entry.tilesW
            || static_cast<uint32_t>(tileY) >= entry.tilesH) {
            return 0; // outside the materialised region (nothing distorts here)
        }
        return m_effectRenderer->extractWholeLayerTile(entry.texture, entry.tilesW, entry.tilesH,
            TILE_SIZE, static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
    };

    // Reuse a still-valid cached region across batches. Validity = same content
    // (version + tile count) AND same effect chain (params); either changing
    // (an edit, or a slider tweak) forces re-materialisation.
    auto cached = m_wholeLayerCache.find(contentIdentity);
    if (cached != m_wholeLayerCache.end()) {
        WholeLayerCacheEntry& entry = cached->second;
        if (entry.texture && entry.contentVersion == contentVersion && entry.tileCount == tileCount
            && entry.effects == effects) {
            entry.lastUseSerial = ++m_wholeLayerUseSerial;
            return sliceFrom(entry);
        }
    }

    // Populated-tile bbox of the layer.
    const auto& tiles = grid.tiles();
    if (tiles.empty()) {
        return 0;
    }
    bool first = true;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (const auto& entry : tiles) {
        const TileKey& tk = entry.first;
        if (first) {
            minX = maxX = tk.x;
            minY = maxY = tk.y;
            first = false;
        } else {
            minX = std::min(minX, tk.x);
            minY = std::min(minY, tk.y);
            maxX = std::max(maxX, tk.x);
            maxY = std::max(maxY, tk.y);
        }
    }

    // Dilate by the max output displacement so distorted content that lands
    // outside the original bbox has tiles to occupy.
    const int ring = maxDisplacementPx > 0
        ? (maxDisplacementPx + static_cast<int>(TILE_SIZE) - 1) / static_cast<int>(TILE_SIZE)
        : 0;
    const int rMinX = minX - ring;
    const int rMinY = minY - ring;
    const uint32_t tilesW = static_cast<uint32_t>(maxX + ring - rMinX + 1);
    const uint32_t tilesH = static_cast<uint32_t>(maxY + ring - rMinY + 1);
    if (tilesW * TILE_SIZE > kMaxWholeLayerDim || tilesH * TILE_SIZE > kMaxWholeLayerDim) {
        return 0; // exceeds the VRAM cap -> caller falls back to the bounded path
    }

    // Region document frame: texel (0,0) is the top-left tile's origin.
    ruwa::core::effects::EffectRegionFrame region;
    region.originX = static_cast<float>(rMinX) * static_cast<float>(TILE_SIZE);
    region.originY = static_cast<float>(rMinY) * static_cast<float>(TILE_SIZE);
    region.documentPxPerTexel = 1.0f;
    region.valid = true;

    const GLuint regionResult = m_effectRenderer->applyEffectsWholeLayer(
        TILE_SIZE, tilesW, tilesH,
        [&](int dx, int dy) -> GLuint { return tileTexture(TileKey { rMinX + dx, rMinY + dy }); },
        effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, backdropTexture, region,
        /*useGroupPool=*/false, liveEditedEffectId, liveEditSourceVariant);
    if (!regionResult) {
        return 0;
    }

    // Copy the transient region result into this identity's owned cache texture
    // (resized in place when the region bounds change), so it survives batches.
    const uint32_t regionW = tilesW * TILE_SIZE;
    const uint32_t regionH = tilesH * TILE_SIZE;
    WholeLayerCacheEntry& entry = m_wholeLayerCache[contentIdentity];
    if (entry.texture && (entry.textureW != regionW || entry.textureH != regionH)) {
        deleteTexture(m_gl, entry.texture);
        entry.texture = 0;
    }
    if (!entry.texture) {
        const TextureParams linear { GL_LINEAR, GL_LINEAR };
        entry.texture = createTexture2D(m_gl, regionW, regionH, linear);
        entry.textureW = regionW;
        entry.textureH = regionH;
    }
    if (!entry.texture) {
        // Out of memory: drop the entry and serve this tile from the transient
        // region result directly.
        m_wholeLayerCache.erase(contentIdentity);
        const int tileX = key.x - rMinX;
        const int tileY = key.y - rMinY;
        if (tileX < 0 || tileY < 0 || static_cast<uint32_t>(tileX) >= tilesW
            || static_cast<uint32_t>(tileY) >= tilesH) {
            return 0;
        }
        return m_effectRenderer->extractWholeLayerTile(regionResult, tilesW, tilesH, TILE_SIZE,
            static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
    }
    m_gl->glCopyImageSubData(regionResult, GL_TEXTURE_2D, 0, 0, 0, 0, entry.texture, GL_TEXTURE_2D,
        0, 0, 0, 0, static_cast<GLsizei>(regionW), static_cast<GLsizei>(regionH), 1);

    entry.originTileX = rMinX;
    entry.originTileY = rMinY;
    entry.tilesW = tilesW;
    entry.tilesH = tilesH;
    entry.contentVersion = contentVersion;
    entry.tileCount = tileCount;
    entry.effects = effects;
    entry.lastUseSerial = ++m_wholeLayerUseSerial;

    evictWholeLayerCacheIfNeeded(contentIdentity);
    // Re-find: eviction may have rehashed... unordered_map erase does NOT
    // invalidate other elements, but the reference above could dangle only if
    // `entry` itself were evicted, which evictWholeLayerCacheIfNeeded excludes.
    return sliceFrom(m_wholeLayerCache.at(contentIdentity));
}

GLuint GLCompositor::wholeRetainedEffectTile(const void* identity,
    const RetainedRenderPayload& payload, const TileKey& key, int maxDisplacementPx,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    const std::function<GLuint(const TileKey&)>& tileTexture, GLuint backdropTexture,
    const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    if (!m_effectRenderer || !identity || payload.empty() || !tileTexture) {
        return 0;
    }

    const auto rawKeys = retainedCoverageTileKeys(payload.worldBounds);
    if (rawKeys.empty()) {
        return 0;
    }

    bool first = true;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    for (const TileKey& tk : rawKeys) {
        if (first) {
            minX = maxX = tk.x;
            minY = maxY = tk.y;
            first = false;
        } else {
            minX = std::min(minX, tk.x);
            minY = std::min(minY, tk.y);
            maxX = std::max(maxX, tk.x);
            maxY = std::max(maxY, tk.y);
        }
    }

    const int ring = maxDisplacementPx > 0
        ? (maxDisplacementPx + static_cast<int>(TILE_SIZE) - 1) / static_cast<int>(TILE_SIZE)
        : 0;
    const int rMinX = minX - ring;
    const int rMinY = minY - ring;
    const uint32_t tilesW = static_cast<uint32_t>(maxX + ring - rMinX + 1);
    const uint32_t tilesH = static_cast<uint32_t>(maxY + ring - rMinY + 1);
    if (tilesW * TILE_SIZE > kMaxWholeLayerDim || tilesH * TILE_SIZE > kMaxWholeLayerDim) {
        return 0;
    }

    auto sliceFrom = [&](const GroupRegionEntry& entry) -> GLuint {
        const int tileX = key.x - entry.originTileX;
        const int tileY = key.y - entry.originTileY;
        if (tileX < 0 || tileY < 0 || static_cast<uint32_t>(tileX) >= entry.tilesW
            || static_cast<uint32_t>(tileY) >= entry.tilesH) {
            return 0;
        }
        return m_effectRenderer->extractWholeLayerTile(entry.texture, entry.tilesW, entry.tilesH,
            TILE_SIZE, static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
    };

    auto cached = m_retainedRegionCache.find(identity);
    if (cached != m_retainedRegionCache.end()) {
        GroupRegionEntry& entry = cached->second;
        if (entry.texture && entry.batchSerial == m_batchSerial && entry.originTileX == rMinX
            && entry.originTileY == rMinY && entry.tilesW == tilesW && entry.tilesH == tilesH
            && entry.effects == effects) {
            return sliceFrom(entry);
        }
    }

    ruwa::core::effects::EffectRegionFrame region;
    region.originX = static_cast<float>(rMinX) * static_cast<float>(TILE_SIZE);
    region.originY = static_cast<float>(rMinY) * static_cast<float>(TILE_SIZE);
    region.documentPxPerTexel = 1.0f;
    region.valid = true;

    const GLuint regionResult = m_effectRenderer->applyEffectsWholeLayer(
        TILE_SIZE, tilesW, tilesH,
        [&](int dx, int dy) -> GLuint { return tileTexture(TileKey { rMinX + dx, rMinY + dy }); },
        effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, backdropTexture, region,
        /*useGroupPool=*/true, liveEditedEffectId, liveEditSourceVariant);
    if (!regionResult) {
        return 0;
    }

    const uint32_t regionW = tilesW * TILE_SIZE;
    const uint32_t regionH = tilesH * TILE_SIZE;
    GroupRegionEntry& entry = m_retainedRegionCache[identity];
    if (entry.texture && (entry.textureW != regionW || entry.textureH != regionH)) {
        deleteTexture(m_gl, entry.texture);
        entry.texture = 0;
    }
    if (!entry.texture) {
        const TextureParams linear { GL_LINEAR, GL_LINEAR };
        entry.texture = createTexture2D(m_gl, regionW, regionH, linear);
        entry.textureW = regionW;
        entry.textureH = regionH;
    }
    if (!entry.texture) {
        m_retainedRegionCache.erase(identity);
        const int tileX = key.x - rMinX;
        const int tileY = key.y - rMinY;
        if (tileX < 0 || tileY < 0 || static_cast<uint32_t>(tileX) >= tilesW
            || static_cast<uint32_t>(tileY) >= tilesH) {
            return 0;
        }
        return m_effectRenderer->extractWholeLayerTile(regionResult, tilesW, tilesH, TILE_SIZE,
            static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
    }

    m_gl->glCopyImageSubData(regionResult, GL_TEXTURE_2D, 0, 0, 0, 0, entry.texture, GL_TEXTURE_2D,
        0, 0, 0, 0, static_cast<GLsizei>(regionW), static_cast<GLsizei>(regionH), 1);

    entry.originTileX = rMinX;
    entry.originTileY = rMinY;
    entry.tilesW = tilesW;
    entry.tilesH = tilesH;
    entry.batchSerial = m_batchSerial;
    entry.effects = effects;

    return sliceFrom(entry);
}

GLuint GLCompositor::wholeGroupEffectTile(const void* identity, int minTileX, int minTileY,
    int maxTileX, int maxTileY, const TileKey& key, int maxDisplacementPx,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    const std::function<GLuint(const TileKey&)>& groupTileTexture, GLuint backdropTexture,
    const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    if (!m_effectRenderer || !identity || !groupTileTexture || maxTileX < minTileX
        || maxTileY < minTileY) {
        return 0;
    }

    // Populated-tile bbox of the group's content, computed by the caller from the
    // group's whole subtree (nested layers + stroke buffers).
    const int minX = minTileX;
    const int minY = minTileY;
    const int maxX = maxTileX;
    const int maxY = maxTileY;

    // Dilate by the max output displacement so distorted content that lands
    // outside the original bbox has tiles to occupy — same ring as the raster
    // whole-layer path.
    const int ring = maxDisplacementPx > 0
        ? (maxDisplacementPx + static_cast<int>(TILE_SIZE) - 1) / static_cast<int>(TILE_SIZE)
        : 0;
    const int rMinX = minX - ring;
    const int rMinY = minY - ring;
    const uint32_t tilesW = static_cast<uint32_t>(maxX + ring - rMinX + 1);
    const uint32_t tilesH = static_cast<uint32_t>(maxY + ring - rMinY + 1);
    if (tilesW * TILE_SIZE > kMaxWholeLayerDim || tilesH * TILE_SIZE > kMaxWholeLayerDim) {
        return 0; // exceeds the VRAM cap -> caller falls back to the bounded path
    }

    auto sliceFrom = [&](const GroupRegionEntry& entry) -> GLuint {
        const int tileX = key.x - entry.originTileX;
        const int tileY = key.y - entry.originTileY;
        if (tileX < 0 || tileY < 0 || static_cast<uint32_t>(tileX) >= entry.tilesW
            || static_cast<uint32_t>(tileY) >= entry.tilesH) {
            return 0; // outside the materialised region (nothing distorts here)
        }
        return m_effectRenderer->extractWholeLayerTile(entry.texture, entry.tilesW, entry.tilesH,
            TILE_SIZE, static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
    };

    // Reuse this batch's already-built region (same identity, bounds, effects).
    auto cached = m_groupRegionCache.find(identity);
    if (cached != m_groupRegionCache.end()) {
        GroupRegionEntry& entry = cached->second;
        if (entry.texture && entry.batchSerial == m_batchSerial && entry.originTileX == rMinX
            && entry.originTileY == rMinY && entry.tilesW == tilesW && entry.tilesH == tilesH
            && entry.effects == effects) {
            return sliceFrom(entry);
        }
    }

    // Region document frame: texel (0,0) is the top-left tile's origin.
    ruwa::core::effects::EffectRegionFrame region;
    region.originX = static_cast<float>(rMinX) * static_cast<float>(TILE_SIZE);
    region.originY = static_cast<float>(rMinY) * static_cast<float>(TILE_SIZE);
    region.documentPxPerTexel = 1.0f;
    region.valid = true;

    // Assemble by compositing the group at every region tile, then run the whole
    // chain with wholeLayerSource=true. The callback may run a re-entrant group
    // composite that returns a transient buffer, so applyEffectsWholeLayer stamps
    // each tile immediately after its callback. useGroupPool=true routes this to
    // the renderer's SECOND whole-region pool, so a group child that is itself a
    // raster whole-layer distortion (running re-entrantly during this assembly,
    // on the default pool) cannot clobber this region's source texture.
    const GLuint regionResult = m_effectRenderer->applyEffectsWholeLayer(
        TILE_SIZE, tilesW, tilesH,
        [&](int dx, int dy) -> GLuint {
            return groupTileTexture(TileKey { rMinX + dx, rMinY + dy });
        },
        effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, backdropTexture, region,
        /*useGroupPool=*/true, liveEditedEffectId, liveEditSourceVariant);
    if (!regionResult) {
        return 0;
    }

    // Copy the transient region result into this identity's owned batch texture
    // (resized in place when the region bounds change).
    const uint32_t regionW = tilesW * TILE_SIZE;
    const uint32_t regionH = tilesH * TILE_SIZE;
    GroupRegionEntry& entry = m_groupRegionCache[identity];
    if (entry.texture && (entry.textureW != regionW || entry.textureH != regionH)) {
        deleteTexture(m_gl, entry.texture);
        entry.texture = 0;
    }
    if (!entry.texture) {
        const TextureParams linear { GL_LINEAR, GL_LINEAR };
        entry.texture = createTexture2D(m_gl, regionW, regionH, linear);
        entry.textureW = regionW;
        entry.textureH = regionH;
    }
    if (!entry.texture) {
        // Out of memory: drop the entry and serve this tile from the transient
        // region result directly.
        m_groupRegionCache.erase(identity);
        const int tileX = key.x - rMinX;
        const int tileY = key.y - rMinY;
        if (tileX < 0 || tileY < 0 || static_cast<uint32_t>(tileX) >= tilesW
            || static_cast<uint32_t>(tileY) >= tilesH) {
            return 0;
        }
        return m_effectRenderer->extractWholeLayerTile(regionResult, tilesW, tilesH, TILE_SIZE,
            static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
    }
    m_gl->glCopyImageSubData(regionResult, GL_TEXTURE_2D, 0, 0, 0, 0, entry.texture, GL_TEXTURE_2D,
        0, 0, 0, 0, static_cast<GLsizei>(regionW), static_cast<GLsizei>(regionH), 1);

    entry.originTileX = rMinX;
    entry.originTileY = rMinY;
    entry.tilesW = tilesW;
    entry.tilesH = tilesH;
    entry.batchSerial = m_batchSerial;
    entry.effects = effects;

    return sliceFrom(entry);
}

uint64_t GLCompositor::layerContentRevision(const CompositeLayerInfo& layer) const
{
    uint64_t revision = 0xcbf29ce484222325ULL;
    const auto combine = [&revision](uint64_t value) {
        revision ^= value + 0x9e3779b97f4a7c15ULL + (revision << 6) + (revision >> 2);
    };
    const auto floatBits = [](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    const auto addGrid = [&combine](const TileGrid* grid) {
        combine(reinterpret_cast<uintptr_t>(grid));
        if (grid) {
            combine(grid->contentVersion());
            combine(grid->tileCount());
        }
    };

    combine(qHash(layer.id));
    combine(layer.visible);
    combine(floatBits(layer.opacity));
    combine(static_cast<uint64_t>(layer.blendMode));
    combine(layer.clippedToBelow);
    if (!layer.visible || layer.opacity <= 0.0f) {
        return revision;
    }

    combine(layer.effectChainRevision);
    for (const auto& effect : layer.effects) {
        combine(qHash(effect.instanceId));
        combine(qHash(effect.typeId));
        combine(effect.version);
        combine(effect.enabled);
        combine(effect.realtimePreviewEnabled);
    }
    addGrid(layer.tileGrid);
    addGrid(layer.externalClipMaskGrid);
    addGrid(layer.clipMaskGrid2);
    combine(reinterpret_cast<uintptr_t>(layer.retainedPayload));
    combine(layer.hasSolidColor);
    if (layer.hasSolidColor) {
        combine(floatBits(layer.solidColor.r));
        combine(floatBits(layer.solidColor.g));
        combine(floatBits(layer.solidColor.b));
        combine(floatBits(layer.solidColor.a));
    }

    // Transient procedural inputs are not versioned independently. Mark their
    // contribution batch-local rather than risk reusing a stale backdrop.
    if (layer.transform || layer.useRadialReveal || layer.subtractClipRevealFromSrc) {
        combine(m_batchSerial);
    }
    for (const auto& child : layer.children) {
        combine(layerContentRevision(child));
    }
    return revision;
}

uint64_t GLCompositor::backdropRevision(
    const std::vector<CompositeLayerInfo>& layers, size_t layerIndex) const
{
    uint64_t revision = 0x84222325cbf29ce4ULL;
    const size_t prefixSize = std::min(layerIndex, layers.size());
    for (size_t i = 0; i < prefixSize; ++i) {
        const uint64_t layerRevision = layerContentRevision(layers[i]);
        revision ^= layerRevision + 0x9e3779b97f4a7c15ULL + (revision << 6) + (revision >> 2);
    }
    return revision;
}

GLuint GLCompositor::findCachedLayerEffectTile(const void* contentIdentity, const TileGrid& grid,
    const TileKey& key, const QList<ruwa::core::effects::LayerEffectState>& effects,
    uint64_t backdropRevision)
{
    const LayerEffectTileKey cacheKey { contentIdentity, key };
    auto it = m_layerEffectTileCache.find(cacheKey);
    if (it == m_layerEffectTileCache.end()) {
        return 0;
    }

    LayerEffectTileCacheEntry& entry = it->second;
    if (entry.contentVersion != grid.contentVersion() || entry.tileCount != grid.tileCount()
        || entry.effects != effects || entry.backdropRevision != backdropRevision) {
        return 0;
    }

    entry.lastUsedSerial = m_batchSerial;
    return entry.texture;
}

GLuint GLCompositor::storeCachedLayerEffectTile(const void* contentIdentity, const TileGrid& grid,
    const TileKey& key, const QList<ruwa::core::effects::LayerEffectState>& effects,
    uint64_t backdropRevision, GLuint resultTexture)
{
    const LayerEffectTileKey cacheKey { contentIdentity, key };
    if (m_layerEffectTileCache.find(cacheKey) == m_layerEffectTileCache.end()) {
        trimLayerEffectTileCache();
    }
    LayerEffectTileCacheEntry& entry = m_layerEffectTileCache[cacheKey];
    if (!entry.texture) {
        static constexpr TextureParams kCacheTextureParams { GL_LINEAR, GL_NEAREST };
        entry.texture = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, kCacheTextureParams);
        if (!entry.texture) {
            m_layerEffectTileCache.erase(cacheKey);
            return resultTexture;
        }
    }

    if (entry.texture != resultTexture) {
        m_gl->glCopyImageSubData(resultTexture, GL_TEXTURE_2D, 0, 0, 0, 0, entry.texture,
            GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(TILE_SIZE),
            static_cast<GLsizei>(TILE_SIZE), 1);
    }
    entry.contentVersion = grid.contentVersion();
    entry.tileCount = grid.tileCount();
    entry.effects = effects;
    entry.backdropRevision = backdropRevision;
    entry.lastUsedSerial = m_batchSerial;
    return entry.texture;
}

void GLCompositor::trimLayerEffectTileCache()
{
    // 512 RGBA8 tiles at the current 256px tile size occupy 128 MiB. Keep the
    // cache bounded and evict the least recently used results as one batch.
    static constexpr size_t kMaxCachedTiles = 512;
    static constexpr size_t kTargetCachedTiles = 448;
    if (m_layerEffectTileCache.size() < kMaxCachedTiles) {
        return;
    }

    std::vector<std::pair<LayerEffectTileKey, uint64_t>> oldest;
    oldest.reserve(m_layerEffectTileCache.size());
    for (const auto& [cacheKey, entry] : m_layerEffectTileCache) {
        oldest.emplace_back(cacheKey, entry.lastUsedSerial);
    }
    std::sort(oldest.begin(), oldest.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

    const size_t removeCount = oldest.size() - kTargetCachedTiles;
    for (size_t i = 0; i < removeCount; ++i) {
        auto it = m_layerEffectTileCache.find(oldest[i].first);
        if (it != m_layerEffectTileCache.end()) {
            deleteTexture(m_gl, it->second.texture);
            m_layerEffectTileCache.erase(it);
        }
    }
}

void GLCompositor::resetEffectBlockCache()
{
    m_effectBlockCache.clear();
    m_effectBlockPoolCursor = 0;
    for (auto& pair : m_retainedRegionCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_retainedRegionCache.clear();
    // NOTE: m_wholeLayerCache is deliberately NOT cleared here — unlike the block
    // cache it is cross-batch and self-validating (by contentVersion + effects),
    // so a static distorted layer stays materialised across pan/idle frames.
    // Bound the pool so a one-off all-dirty sweep of a huge document does not
    // pin its worst-case block count in VRAM forever (each entry is a
    // (kEffectBlockTiles*TILE_SIZE)^2 RGBA8 texture, 16 MB at 8 tiles).
    constexpr size_t kMaxPooledBlocks = 8;
    while (m_effectBlockPool.size() > kMaxPooledBlocks) {
        deleteTexture(m_gl, m_effectBlockPool.back());
        m_effectBlockPool.pop_back();
    }
}

void GLCompositor::destroyEffectBlockCache()
{
    m_effectBlockCache.clear();
    m_effectBlockPoolCursor = 0;
    for (GLuint& texture : m_effectBlockPool) {
        deleteTexture(m_gl, texture);
    }
    m_effectBlockPool.clear();

    for (auto& pair : m_layerEffectTileCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_layerEffectTileCache.clear();

    for (auto& pair : m_wholeLayerCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_wholeLayerCache.clear();

    for (auto& pair : m_groupRegionCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_groupRegionCache.clear();

    for (auto& pair : m_retainedRegionCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_retainedRegionCache.clear();
}

GLuint GLCompositor::blockNeighborhoodEffectTile(const void* contentIdentity, const TileKey& key,
    int padPixels, const QList<ruwa::core::effects::LayerEffectState>& effects,
    const std::function<GLuint(const TileKey&)>& tileContent, const QUuid& liveEditedEffectId,
    quint64 liveEditSourceVariant)
{
    if (!m_effectRenderer || !contentIdentity || padPixels <= 0 || !tileContent) {
        return 0;
    }

    constexpr uint32_t kBlockPx = static_cast<uint32_t>(kEffectBlockTiles) * TILE_SIZE;
    // Floor division so negative tile coords (infinite canvas) land in the
    // right block.
    const auto floorDiv = [](int value, int divisor) {
        return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
    };
    const int blockX = floorDiv(key.x, kEffectBlockTiles);
    const int blockY = floorDiv(key.y, kEffectBlockTiles);
    const TileKey anchor { blockX * kEffectBlockTiles, blockY * kEffectBlockTiles };
    const uint32_t tileX = static_cast<uint32_t>(key.x - anchor.x);
    const uint32_t tileY = static_cast<uint32_t>(key.y - anchor.y);

    const EffectBlockKey cacheKey { contentIdentity, blockX, blockY };
    auto it = m_effectBlockCache.find(cacheKey);
    if (it != m_effectBlockCache.end()) {
        return m_effectRenderer->extractNeighborhoodTile(
            it->second, kBlockPx, TILE_SIZE, tileX, tileY);
    }

    const GLuint blockResult = m_effectRenderer->applyEffectsNeighborhoodBlock(
        TILE_SIZE, static_cast<uint32_t>(kEffectBlockTiles), padPixels,
        [&](int dx, int dy) -> GLuint {
            return tileContent(TileKey { anchor.x + dx, anchor.y + dy });
        },
        effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false,
        /*backdropTexture=*/0, effectRegionForTile(anchor), liveEditedEffectId,
        liveEditSourceVariant);
    if (!blockResult) {
        return 0;
    }

    // The renderer's block output is transient (overwritten by the next
    // evaluation), so cache a pooled copy.
    if (m_effectBlockPoolCursor >= m_effectBlockPool.size()) {
        const TextureParams linear { GL_LINEAR, GL_LINEAR };
        const GLuint pooled = createTexture2D(m_gl, kBlockPx, kBlockPx, linear);
        if (!pooled) {
            // Out of memory: still serve this tile from the transient result.
            return m_effectRenderer->extractNeighborhoodTile(
                blockResult, kBlockPx, TILE_SIZE, tileX, tileY);
        }
        m_effectBlockPool.push_back(pooled);
    }
    const GLuint pooled = m_effectBlockPool[m_effectBlockPoolCursor++];
    m_gl->glCopyImageSubData(blockResult, GL_TEXTURE_2D, 0, 0, 0, 0, pooled, GL_TEXTURE_2D, 0, 0, 0,
        0, static_cast<GLsizei>(kBlockPx), static_cast<GLsizei>(kBlockPx), 1);
    m_effectBlockCache.emplace(cacheKey, pooled);

    return m_effectRenderer->extractNeighborhoodTile(pooled, kBlockPx, TILE_SIZE, tileX, tileY);
}

GLuint GLCompositor::applyAdjustmentNeighborhoodEffects(const TileKey& key,
    const std::vector<CompositeLayerInfo>& belowLayers, const CompositeLayerInfo& adjustment,
    GLTileRenderer* tileRenderer, int padPixels)
{
    if (!m_effectRenderer || !tileRenderer || padPixels <= 0) {
        return 0;
    }

    uint64_t sourceRevision = 0x84222325cbf29ce4ULL;
    for (const auto& below : belowLayers) {
        const uint64_t belowRevision = layerContentRevision(below);
        sourceRevision ^= belowRevision + 0x9e3779b97f4a7c15ULL + (sourceRevision << 6)
            + (sourceRevision >> 2);
    }
    const quint64 prefixCacheVariant
        = liveEditCacheVariant(adjustment.liveEffectEditGeneration, sourceRevision);
    const bool canMaterializeBelow = adjustmentBelowStackSupported(belowLayers);
    auto belowCompositeTexture = [&](const TileKey& sourceKey) -> GLuint {
        return recompositeBelowBgFree(sourceKey, belowLayers, tileRenderer);
    };

    // Distortions such as Twirl read arbitrary positions from the entire input.
    // Raster layers already route those chains through wholeLayerEffectTile(); an
    // adjustment needs the equivalent whole-region source, with each source tile
    // being the background-free composite of the stack below. Reuse the existing
    // group-region materialisation/cache so the lower stack is composited once per
    // source tile and the effect chain is evaluated once per batch, rather than
    // gathering and evaluating the same large neighbourhood for every output tile.
    if (chainNeedsWholeLayer(adjustment.effects) && !effectsRequireBackdrop(adjustment.effects)
        && canMaterializeBelow) {
        std::unordered_set<TileKey, TileKeyHash> sourceKeys;
        collectCompositeLayerKeys(belowLayers, sourceKeys);
        if (!sourceKeys.empty()) {
            auto first = sourceKeys.begin();
            int minX = first->x;
            int minY = first->y;
            int maxX = first->x;
            int maxY = first->y;
            for (const TileKey& sourceKey : sourceKeys) {
                minX = std::min(minX, sourceKey.x);
                minY = std::min(minY, sourceKey.y);
                maxX = std::max(maxX, sourceKey.x);
                maxY = std::max(maxY, sourceKey.y);
            }

            const GLuint wholeTile = wholeGroupEffectTile(static_cast<const void*>(&adjustment),
                minX, minY, maxX, maxY, key, padPixels, adjustment.effects, belowCompositeTexture,
                transparentTexture(), adjustment.liveEditedEffectId, prefixCacheVariant);
            if (wholeTile) {
                return wholeTile;
            }
        }
    }

    // Bounded neighbour-reading chains (blur, ripple, etc.) use the same
    // batch-scoped block cache as raster layers and groups. This avoids rebuilding
    // overlapping padded sources and re-running the chain independently per tile.
    if (canMaterializeBelow && !effectsRequireBackdrop(adjustment.effects)) {
        const GLuint blockTile = blockNeighborhoodEffectTile(static_cast<const void*>(&adjustment),
            key, padPixels, adjustment.effects, belowCompositeTexture,
            adjustment.liveEditedEffectId, prefixCacheVariant);
        if (blockTile) {
            return blockTile;
        }
    }

    // Background-free centre (also the (0,0) padding tile and the effect backdrop).
    const GLuint centre = recompositeBelowBgFree(key, belowLayers, tileRenderer);
    if (!centre) {
        return 0;
    }

    auto neighborTexture = [&](int dx, int dy) -> GLuint {
        if (dx == 0 && dy == 0) {
            return m_neighborhoodCenterTex;
        }
        const TileKey neighborKey { key.x + dx, key.y + dy };
        const int savedPing = m_currentPing;
        const GLuint savedTex0 = m_pingPongTex[0];
        const GLuint savedTex1 = m_pingPongTex[1];
        m_pingPongTex[0] = m_adjustmentTempTex[0];
        m_pingPongTex[1] = m_adjustmentTempTex[1];
        m_currentPing = 0;
        m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
        clearTexture(m_pingPongTex[0]);
        compositeLayerStack(neighborKey, belowLayers, tileRenderer, 1.0f);
        const GLuint result = currentBase();
        m_pingPongTex[0] = savedTex0;
        m_pingPongTex[1] = savedTex1;
        m_currentPing = savedPing;
        return result;
    };

    return m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
        adjustment.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, m_neighborhoodCenterTex, effectRegionForTile(key),
        adjustment.liveEditedEffectId, prefixCacheVariant);
}

void GLCompositor::copyTextureToCache(GLuint srcTex, TileData& cacheTile)
{
    if (!cacheTile.hasTexture())
        return;

    // Attach source texture to FBO and copy to cache tile texture
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);

    m_gl->glBindTexture(GL_TEXTURE_2D, cacheTile.textureId());
    m_gl->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

GLuint GLCompositor::solidClipColorTexture(GLuint& slot, const Color& color)
{
    if (!slot) {
        slot = createTexture2D(m_gl, 1, 1);
        if (!slot)
            return 0;
    }
    m_gl->glBindTexture(GL_TEXTURE_2D, slot);

    const float clampedR = std::clamp(color.r, 0.0f, 1.0f);
    const float clampedG = std::clamp(color.g, 0.0f, 1.0f);
    const float clampedB = std::clamp(color.b, 0.0f, 1.0f);
    const float clampedA = std::clamp(color.a, 0.0f, 1.0f);

    const uint8_t rgba[4] = { static_cast<uint8_t>(clampedR * 255.0f + 0.5f),
        static_cast<uint8_t>(clampedG * 255.0f + 0.5f),
        static_cast<uint8_t>(clampedB * 255.0f + 0.5f),
        static_cast<uint8_t>(clampedA * 255.0f + 0.5f) };
    m_gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    return slot;
}

GLuint GLCompositor::solidColorTexture(const Color& color)
{
    return solidClipColorTexture(m_solidColorTex, color);
}

GLuint GLCompositor::transparentTexture()
{
    if (!m_transparentTex) {
        const uint8_t rgba[4] = { 0, 0, 0, 0 };
        m_transparentTex = createTexture2D(m_gl, 1, 1, {}, rgba);
    }
    return m_transparentTex;
}

} // namespace aether
