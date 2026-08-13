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
    deleteTexture(m_gl, m_programmaticBlendBaseTex);
    for (const auto& frame : m_isolationFrames) {
        deleteTexture(m_gl, frame->ping[0]);
        deleteTexture(m_gl, frame->ping[1]);
        deleteTexture(m_gl, frame->centre);
    }
    m_isolationFrames.clear();
    m_isolationDepth = 0;
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
    // Every one of these is a plain single-level tile. The main ping-pong pair
    // used to carry a full display mip chain because it is swapped into the
    // composition cache and the cache tiles were the ones drawn minified; the
    // display pyramid does that job now and keeps its own storage, so the chain
    // would be pure VRAM.
    const TextureParams kWorkingTileParams = tileTextureParams(kDefaultTileFormat);
    auto ensureTex = [this](GLuint& tex, const TextureParams& params) {
        if (!tex)
            tex = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, params);
    };

    for (int i = 0; i < 2; ++i)
        ensureTex(m_pingPongTex[i], kWorkingTileParams);
    ensureTex(m_programmaticBlendBaseTex, kWorkingTileParams);
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

    const TextureParams kTileParams = tileTextureParams(kDefaultTileFormat);
    GroupCompositeFrame& frame = *m_groupCompositeFrames[depth];
    auto ensureTex = [this, &kTileParams](GLuint& texture) {
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

GLCompositor::IsolationFrame& GLCompositor::ensureIsolationFrame(size_t depth)
{
    if (m_isolationFrames.size() <= depth) {
        m_isolationFrames.resize(depth + 1);
    }
    if (!m_isolationFrames[depth]) {
        m_isolationFrames[depth] = std::make_unique<IsolationFrame>();
    }

    const TextureParams kTileParams = tileTextureParams(kDefaultTileFormat);
    IsolationFrame& frame = *m_isolationFrames[depth];
    auto ensureTex = [this, &kTileParams](GLuint& texture) {
        if (!texture) {
            texture = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, kTileParams);
        }
    };
    ensureTex(frame.ping[0]);
    ensureTex(frame.ping[1]);
    return frame;
}

GLuint GLCompositor::isolationCentre(IsolationFrame& frame)
{
    // Only the neighbourhood paths need a centre copy; a plain isolated
    // composite (clip group, group source tile) never asks for one.
    if (!frame.centre) {
        frame.centre
            = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, tileTextureParams(kDefaultTileFormat));
    }
    return frame.centre;
}

GLCompositor::IsolationScope::IsolationScope(GLCompositor& owner, Mode mode)
    : m_owner(owner)
    , m_frame(owner.ensureIsolationFrame(owner.m_isolationDepth++))
    , m_mode(mode)
{
    if (m_mode != Redirect) {
        return;
    }
    m_savedTex[0] = owner.m_pingPongTex[0];
    m_savedTex[1] = owner.m_pingPongTex[1];
    m_savedPing = owner.m_currentPing;
    owner.m_pingPongTex[0] = m_frame.ping[0];
    owner.m_pingPongTex[1] = m_frame.ping[1];
    owner.m_currentPing = 0;
    owner.m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    owner.clearTexture(m_frame.ping[0]);
}

GLuint GLCompositor::IsolationScope::centre() const
{
    return m_owner.isolationCentre(m_frame);
}

GLCompositor::IsolationScope::~IsolationScope()
{
    if (m_mode == Redirect) {
        m_owner.m_pingPongTex[0] = m_savedTex[0];
        m_owner.m_pingPongTex[1] = m_savedTex[1];
        m_owner.m_currentPing = m_savedPing;
    }
    --m_owner.m_isolationDepth;
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
    m_isolationDepth = 0;
    m_srcAtopClipBase = nullptr;
    m_activeRootLayers = &layers;
    m_activeBackdropColor = backdropColor;
    GLuint resultTex = compositeLayerStack(key, layers, tileRenderer, 1.0f, false, backdropColor);
    m_activeRootLayers = nullptr;
    m_groupCompositeDepth = 0;
    m_isolationDepth = 0;
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
    // The one place where a cache tile's pixels are known to have changed.
    // The display pyramid keys off this, never off texture identity — the swap
    // above hands the tile a different texture object on every composite.
    //
    // Two signals, on purpose. The feed (push) is what makes the pyramid cheap:
    // it rebuilds exactly what moved. The version stamp (pull) is what makes it
    // CORRECT: the pyramid re-derives staleness from it whenever it thinks it
    // has settled, so a mutation path that forgets to fire the feed costs a
    // frame of lag instead of a ghost tile that survives until the user paints
    // over it.
    cacheTile.bumpContentVersion();
    cache.noteTileContentChanged(key);
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
        // A skipped non-clipped layer stops being a clip base: layers clipped to
        // it must clip against nothing, not against whatever was left in
        // activeClipBaseTex from further below (that is what made a clip group
        // with a hidden — or fully transparent — base render against an
        // unrelated layer).
        if (!layer.visible) {
            if (!layer.clippedToBelow) {
                activeClipBaseTex = transparentTex;
            }
            ++idx;
            continue;
        }

        float effectiveOpacity = layer.opacity * parentOpacity;
        if (effectiveOpacity <= 0.0f) {
            if (!layer.clippedToBelow) {
                activeClipBaseTex = transparentTex;
            }
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
            && hasRenderedClippedFollower(layers, idx, parentOpacity);

        if (isClipBase) {
            // Collect the full clip group: base at idx, clipped at idx+1..clipEnd-1.
            // Skipped (hidden / fully transparent) clipped layers stay inside the
            // range — the recursive pass skips them individually, and cutting the
            // group at one of them would detach the clipped layers above it.
            size_t clipEnd = idx + 1;
            while (clipEnd < layers.size() && layers[clipEnd].clippedToBelow)
                ++clipEnd;

            GLuint groupResultTex = 0;
            {
                // Isolated buffer for the clip group. A depth-indexed frame (not a
                // single shared pair) so a clip group nested inside this one — a
                // pass-through group in the clip base holding its own clip
                // group — cannot clear the composite being accumulated here.
                IsolationScope iso(*this);

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
                    // A clipped ADJUSTMENT needs to know the clip base to rebuild
                    // its own source at neighbouring tiles (bounds-expanding
                    // chains); it is not part of the sub-stack it is composited in.
                    const CompositeLayerInfo* savedClipBase = m_srcAtopClipBase;
                    m_srcAtopClipBase = &layer;
                    compositeLayerStack(key, clippedVec, tileRenderer, 1.0f,
                        /*useSrcAtop=*/true, Color::transparent());
                    m_srcAtopClipBase = savedClipBase;
                }

                groupResultTex = currentBase();
            }

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
                    if (!belowLayers.empty()) {
                        return;
                    }
                    if (useSrcAtop && m_srcAtopClipBase) {
                        // Inside a clip group: what this adjustment sees is the
                        // clip BASE with the clipped layers under it composited
                        // src-atop — and the base lives one level up, outside
                        // `layers`. Prepending it reproduces exactly the isolated
                        // buffer's content at any tile (the recursive pass turns
                        // [base, clipped...] back into a clip group).
                        belowLayers.reserve(idx + 1);
                        belowLayers.push_back(*m_srcAtopClipBase);
                        // The base's own opacity is applied when the finished clip
                        // group is blended onto the canvas, exactly as in the
                        // isolated pass this mirrors.
                        belowLayers.front().opacity = 1.0f;
                    } else {
                        belowLayers.reserve(idx);
                    }
                    belowLayers.insert(belowLayers.end(), layers.begin(),
                        layers.begin() + static_cast<ptrdiff_t>(idx));
                };
                // A clipped adjustment can rebuild its own source stack (above);
                // one whose clip base is unknown cannot, and keeps the per-tile
                // path over currentBase().
                const bool canRebuildBelow = !useSrcAtop || m_srcAtopClipBase != nullptr;

                // Background-free effected content of the layers below.
                GLuint effectedTex = 0;
                bool bgFree = false; // effectedTex computed from background-free content
                if (pad > 0 && canRebuildBelow) {
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

            const bool usedAsClipBase = !layer.clippedToBelow
                && hasRenderedClippedFollower(layers, idx, parentOpacity);
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
                        return recomposePassThroughToGroup(
                            neighborKey, &layer, tileRenderer, backdropColor);
                    };
                    effectedVisual = applyGroupNeighborhoodEffects(key, layer, tileRenderer,
                        groupPad, frame.passThrough,
                        /*allowCachedPaths=*/true, passThroughTileTexture,
                        GroupEffectSlot::PassThroughVisual);
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
                        GroupEffectSlot::PassThroughCoverage,
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
                    // The neighbourhood path keeps a stable copy of the centre
                    // before running its re-entrant neighbour composites; the
                    // un-effected group result is the clip base for the layers
                    // above, so it has to be read from that copy.
                    GLuint centreTex = 0;
                    effectedGroupResult = applyGroupNeighborhoodEffects(key, layer, tileRenderer,
                        groupPad, groupResult, /*allowCachedPaths=*/true,
                        /*passThroughTileTexture=*/{}, GroupEffectSlot::IsolatedResult,
                        /*liveEditSourceVariant=*/0, &centreTex);
                    if (effectedGroupResult && centreTex) {
                        groupClipBaseTex = centreTex;
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

    // Individual passes fully specify their inputs before drawing. Return the
    // optional texture units and VAO to the compositor's established neutral
    // boundary state once per batch instead of once per layer draw.
    m_gl->glBindTextureUnit(7, 0);
    m_gl->glBindTextureUnit(6, 0);
    m_gl->glBindTextureUnit(5, 0);
    m_gl->glBindTextureUnit(4, 0);
    m_gl->glBindTextureUnit(3, 0);
    m_gl->glBindTextureUnit(2, 0);
    m_gl->glBindVertexArray(0);

    const qint64 dbgBatchUs = dbgBatchTimer.nsecsElapsed() / 1000;
    if (dbgBatchUs > 2000 && m_dbgTileCount > 0) { }
}

bool GLCompositor::compositeStackIntoGrid(const std::vector<CompositeLayerInfo>& layers,
    const std::unordered_set<TileKey, TileKeyHash>& keys, TileGrid& outGrid,
    GLTileRenderer* tileRenderer, const Color& backdropColor)
{
    if (!m_initialized || !tileRenderer || layers.empty() || keys.empty()) {
        return false;
    }

    outGrid.clear();
    // See the header: the composite path hands its ping-pong texture to the
    // cache tile, and that texture is RGBA8.
    outGrid.setFormat(TilePixelFormat::RGBA8);

    // A throwaway cache, so the composite writes nowhere near the document's.
    // Its grid is RGBA8 by default, which is what the tile textures must be for
    // the swap in compositeTile() to stay type-correct.
    CompositionCache cache;
    cache.markDirty(keys);
    const std::vector<TileKey> keyList(keys.begin(), keys.end());
    compositeDirtyKeys(layers, cache, tileRenderer, keyList, backdropColor);

    if (!m_fbo) {
        m_gl->glGenFramebuffers(1, &m_fbo);
    }

    GLFboViewportGuard guard(m_gl);
    std::vector<uint8_t> buffer(TILE_BYTE_SIZE);
    size_t readTiles = 0;
    for (auto& [key, tile] : cache.grid().tiles()) {
        if (!tile.hasTexture()) {
            continue;
        }
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);
        if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            continue;
        }
        m_gl->glViewport(0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE));
        m_gl->glReadPixels(0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE),
            GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

        TileData& dstTile = outGrid.getOrCreateTile(key);
        std::memcpy(dstTile.pixels(), buffer.data(), TILE_BYTE_SIZE);
        // Through the GRID, not the tile: that is the documented "these pixels
        // changed" signal, and it is what moves contentVersion() — which every
        // cache derived from this grid (projections, effects) validates against.
        outGrid.markDirty(key);
        ++readTiles;
    }

    // Hand the composited textures back to the pool rather than leaving them to
    // the orphan collector: this cache is temporary and its tiles would
    // otherwise each cost a fresh allocation on the next composite.
    for (auto& entry : cache.grid().tiles()) {
        tileRenderer->destroyTileTexture(entry.second);
    }

    // Fully transparent tiles carry no information and would make the content
    // look non-empty to every bounds/coverage consumer.
    outGrid.pruneEmpty();
    return readTiles > 0;
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
    m_compositeProgram->setUniform("uBlendMode", p.blendMode);
    m_compositeProgram->setUniform("uOpacity", p.opacity);
    m_compositeProgram->setUniform("uUseClipMask", p.useClipMask ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskAlphaOnly", p.clipMaskAlphaOnly ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskAsAlphaCap", p.clipMaskAsAlphaCap ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskLuminanceReveal", p.clipMaskLuminanceReveal ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskEditPreview", p.clipMaskEditPreview ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskEditReplace", p.clipMaskEditReplace ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskEditStrokeOpacity", p.clipMaskEditStrokeOpacity);
    m_compositeProgram->setUniform(
        "uSubtractClipRevealFromSrc", p.subtractClipRevealFromSrc ? 1 : 0);
    m_compositeProgram->setUniform("uPreserveBaseAlpha", p.preserveBaseAlpha ? 1 : 0);
    m_compositeProgram->setUniform("uReplaceBase", p.replaceBase ? 1 : 0);
    m_compositeProgram->setUniform("uReplaceBaseMixReveal", p.replaceBaseMixReveal ? 1 : 0);
    m_compositeProgram->setUniform("uUseGroupComposite", p.useGroupComposite ? 1 : 0);
    m_compositeProgram->setUniform("uUseProgrammaticBlendBase", p.useProgrammaticBlendBase ? 1 : 0);
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

    // The ping-pong and cache tiles are kDefaultTileFormat, so the composite
    // target is 8-bit even for a 16F/32F document.
    m_compositeProgram->setUniform(
        "uQuantizeTo8Bit", kDefaultTileFormat == TilePixelFormat::RGBA8 ? 1 : 0);
    if (!(p.key == m_ditherPassKey)) {
        m_ditherPassKey = p.key;
        m_ditherPassIndex = 0;
    }
    // Golden-ratio step: consecutive passes land far apart in [0, 1) after the
    // shader's fract, so their rounding errors cancel rather than accumulate.
    m_compositeProgram->setUniform(
        "uDitherSeed", static_cast<float>(m_ditherPassIndex++) * 0.6180339887f);

    m_gl->glBindTextureUnit(0, p.baseTex);

    m_gl->glBindTextureUnit(1, p.srcTex);

    m_gl->glBindTextureUnit(2, p.useClipMask ? p.clipMaskTex : 0);

    m_gl->glBindTextureUnit(3, p.useProgrammaticBlendBase ? p.programmaticBlendBaseTex : 0);

    m_gl->glBindTextureUnit(4, p.useClipMask2 ? p.clipMaskTex2 : 0);

    m_gl->glBindTextureUnit(5, p.useGroupComposite ? p.groupPassThroughTex : 0);
    m_gl->glBindTextureUnit(6, p.useGroupComposite ? p.groupCoverageTex : 0);
    m_gl->glBindTextureUnit(7, p.useGroupComposite ? p.groupSourceCoverageTex : 0);

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    ++m_lastCompositeDrawCalls;
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
            // Raster layers keep the per-batch block validity: their effected
            // output is already reused across batches per tile by
            // m_layerEffectTileCache (persistResult below).
            const GLuint blockTile
                = blockNeighborhoodEffectTile(reinterpret_cast<uintptr_t>(blockCacheIdentity), key,
                    padPixels, effects, tileTexture, liveEditedEffectId, prefixCacheVariant);
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
            const GLuint blockTile
                = blockNeighborhoodEffectTile(reinterpret_cast<uintptr_t>(layer.retainedPayload),
                    key, padPixels, layer.effects, retainedTileTexture, layer.liveEditedEffectId,
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
    const CompositeLayerInfo* target, GLTileRenderer* tileRenderer, const Color& backdropColor)
{
    if (!m_activeRootLayers || !target || !tileRenderer) {
        return 0;
    }

    const CompositeLayerInfo* savedStopGroup = m_recomposeStopGroup;
    const bool savedStopReached = m_recomposeStopReached;

    // Own isolation frame rather than buffers borrowed from the caller's group
    // frame: this recomposite walks the whole root stack, so it can re-enter any
    // isolated path (including the caller's own group) at a deeper level.
    IsolationScope iso(*this);
    m_recomposeStopGroup = target;
    m_recomposeStopReached = false;
    compositeLayerStack(key, *m_activeRootLayers, tileRenderer, 1.0f,
        /*useSrcAtop=*/false, backdropColor);
    const GLuint result = currentBase();

    m_recomposeStopGroup = savedStopGroup;
    m_recomposeStopReached = savedStopReached;
    return result;
}

GLuint GLCompositor::applyGroupNeighborhoodEffects(const TileKey& key,
    const CompositeLayerInfo& layer, GLTileRenderer* tileRenderer, int padPixels,
    GLuint groupResultTexture, bool allowCachedPaths,
    const std::function<GLuint(const TileKey&)>& passThroughTileTexture, GroupEffectSlot cacheSlot,
    quint64 liveEditSourceVariant, GLuint* outCentreTexture)
{
    if (!m_effectRenderer || !tileRenderer || padPixels <= 0 || !groupResultTexture) {
        return 0;
    }
    // Content revision of everything the source callbacks below composite. The
    // group's OWN effect chain is deliberately NOT part of it — that is what lets
    // an effect-parameter change reuse the baked source and re-run only the
    // chain. The pass-through visual additionally sees the stack below the group
    // (its callback recomposites the document up to it), so that is folded in.
    uint64_t sourceRevision = 0x84222325cbf29ce4ULL;
    const auto combineRevision = [&sourceRevision](uint64_t value) {
        sourceRevision
            ^= value + 0x9e3779b97f4a7c15ULL + (sourceRevision << 6) + (sourceRevision >> 2);
    };
    for (const auto& child : layer.children) {
        combineRevision(layerContentRevision(child));
    }
    if (cacheSlot != GroupEffectSlot::PassThroughCoverage) {
        // Both the pass-through visual and an isolated group's result can carry
        // the stack below them (the pass-through composites over it; an isolated
        // group's stroke-blend backdrop reads it), so they are only valid while
        // that stack is unchanged. The coverage pass composites over pure
        // transparency and depends on the children alone.
        combineRevision(recomposePrefixRevision(&layer));
    }
    const uint64_t identity = layerCacheIdentity(layer.id, cacheSlot);
    const quint64 prefixCacheVariant = liveEditCacheVariant(
        layer.liveEffectEditGeneration, sourceRevision, liveEditSourceVariant);

    // The centre group result is the (0,0) padding tile / centre input for both
    // paths below, and the re-entrant per-tile composites clobber the buffer it
    // lives in, so copy it to a stable texture first. The frame is reserved (not
    // redirected: the paths below still read the OUTER currentBase() as their
    // effect backdrop) so that the neighbour composites, which take frames of
    // their own one level deeper, cannot land on this centre.
    IsolationScope centreScope(*this, IsolationScope::Reserve);
    const GLuint centreTexture = centreScope.centre();
    if (outCentreTexture) {
        *outCentreTexture = centreTexture;
    }
    m_gl->glCopyImageSubData(groupResultTexture, GL_TEXTURE_2D, 0, 0, 0, 0, centreTexture,
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
                    return centreTexture;
                }
                IsolationScope iso(*this);
                compositeLayerStack(nk, layer.children, tileRenderer, 1.0f);
                return currentBase();
            };
            const GLuint wholeTile = wholeGroupEffectTile(identity, sourceRevision, minX, minY,
                maxX, maxY, key, padPixels, layer.effects, groupCompositeTile, currentBase(),
                layer.liveEditedEffectId, prefixCacheVariant);
            if (wholeTile) {
                return wholeTile;
            }
        }
    }

    if (passThroughTileTexture) {
        // Every one of these tiles costs a full re-entrant composite, and the
        // padded gather asks for the same ones again for each overlapping block
        // and each batch — memoise them against the group's content revision.
        auto groupContentTexture = [&](const TileKey& nk) -> GLuint {
            return cachedGroupSourceTile(identity, nk, sourceRevision, passThroughTileTexture);
        };
        if (allowCachedPaths && !effectsRequireBackdrop(layer.effects)) {
            const GLuint blockTile = blockNeighborhoodEffectTile(identity, key, padPixels,
                layer.effects, groupContentTexture, layer.liveEditedEffectId, prefixCacheVariant,
                sourceRevision);
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
        auto composeFullGroup = [&](const TileKey& nk) -> GLuint {
            IsolationScope iso(*this);
            compositeLayerStack(nk, layer.children, tileRenderer, 1.0f);
            return currentBase();
        };
        auto fullGroupTexture = [&](const TileKey& nk) -> GLuint {
            return cachedGroupSourceTile(identity, nk, sourceRevision, composeFullGroup);
        };

        if (allowCachedPaths && !effectsRequireBackdrop(layer.effects)) {
            const GLuint blockTile
                = blockNeighborhoodEffectTile(identity, key, padPixels, layer.effects,
                    fullGroupTexture, layer.liveEditedEffectId, prefixCacheVariant, sourceRevision);
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
            return centreTexture;
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
        // neighbour key into a private isolation frame (centre saved off above).
        IsolationScope iso(*this);
        compositeLayerStack(nk, layer.children, tileRenderer, 1.0f);
        return currentBase();
    };

    // Block-cached fast path: one chain evaluation covers a whole block of
    // tiles — during a stroke this is what keeps painting under a large-radius
    // blur from re-blurring the padded region per tile, and while the group's
    // content revision holds the block also survives batches (so painting on an
    // unrelated layer no longer re-runs the chain).
    if (allowCachedPaths && !effectsRequireBackdrop(layer.effects)) {
        const GLuint blockTile
            = blockNeighborhoodEffectTile(identity, key, padPixels, layer.effects,
                groupContentTexture, layer.liveEditedEffectId, prefixCacheVariant, sourceRevision);
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

GLuint GLCompositor::recompositeBelowBgFree(const TileKey& key,
    const std::vector<CompositeLayerInfo>& belowLayers, GLTileRenderer* tileRenderer)
{
    if (!tileRenderer) {
        return 0;
    }

    // Any stack shape is supported: a clip group, a nested adjustment or a
    // bounds-expanding group effect encountered below simply takes an isolation
    // frame one level deeper than this one. This used to be refused (the
    // isolation buffers were single shared pairs), and refusing meant the
    // adjustment fell back to a PER-TILE effect evaluation over currentBase() —
    // a hard tile-grid seam for every bounds-expanding effect, plus the canvas
    // background leaking into the effect input.
    GLuint result = 0;
    GLuint centre = 0;
    {
        IsolationScope iso(*this);
        centre = iso.centre();
        // Transparent backdrop -> the result is the content only (no baked canvas bg).
        compositeLayerStack(key, belowLayers, tileRenderer, 1.0f);
        result = currentBase();
    }
    // Copy to the frame's stable texture: the ping-pong pair is handed to the
    // per-neighbour recomposites that follow, which clobber it.
    m_gl->glCopyImageSubData(result, GL_TEXTURE_2D, 0, 0, 0, 0, centre, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE), 1);
    return centre;
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
    if (auto it = m_wholeLayerCache.find(contentIdentity); it != m_wholeLayerCache.end()) {
        deleteTexture(m_gl, it->second.texture);
        m_wholeLayerCache.erase(it);
    }

    // The per-tile effect cache is keyed on the same identity and validates
    // against contentVersion + tileCount — values a DIFFERENT grid allocated at
    // the freed address can reproduce exactly (a fresh grid with the same tiles
    // is not far-fetched). Dropping both together is what makes "this address is
    // about to stop existing" a complete statement.
    for (auto it = m_layerEffectTileCache.begin(); it != m_layerEffectTileCache.end();) {
        if (it->first.identity == contentIdentity) {
            deleteTexture(m_gl, it->second.texture);
            it = m_layerEffectTileCache.erase(it);
        } else {
            ++it;
        }
    }
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

GLuint GLCompositor::wholeGroupEffectTile(uint64_t identity, uint64_t sourceRevision, int minTileX,
    int minTileY, int maxTileX, int maxTileY, const TileKey& key, int maxDisplacementPx,
    const QList<ruwa::core::effects::LayerEffectState>& effects,
    const std::function<GLuint(const TileKey&)>& groupTileTexture, GLuint backdropTexture,
    const QUuid& liveEditedEffectId, quint64 liveEditSourceVariant)
{
    if (!m_effectRenderer || !identity || !groupTileTexture || maxTileX < minTileX
        || maxTileY < minTileY) {
        return 0;
    }
    // A caller without a content revision (nothing does today) keeps the old
    // per-batch behaviour: tag the batch serial so it can never alias a hash.
    const uint64_t revision
        = sourceRevision != 0 ? sourceRevision : (m_batchSerial | 0x4000000000000000ULL);

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

    const uint32_t regionW = tilesW * TILE_SIZE;
    const uint32_t regionH = tilesH * TILE_SIZE;
    const auto boundsMatch = [&](const GroupRegionEntry& entry) {
        return entry.originTileX == rMinX && entry.originTileY == rMinY && entry.tilesW == tilesW
            && entry.tilesH == tilesH;
    };

    // Level 1: the effected region is still valid — same source content AND the
    // same chain. Nothing is recomputed; this is the common case when the dirty
    // tiles come from an unrelated layer, a pan, or a cache eviction.
    auto cached = m_groupRegionCache.find(identity);
    if (cached != m_groupRegionCache.end()) {
        GroupRegionEntry& entry = cached->second;
        if (entry.texture && entry.sourceRevision == revision && boundsMatch(entry)
            && entry.effects == effects) {
            entry.lastUseSerial = m_batchSerial;
            return sliceFrom(entry);
        }
    }

    // Region document frame: texel (0,0) is the top-left tile's origin.
    ruwa::core::effects::EffectRegionFrame region;
    region.originX = static_cast<float>(rMinX) * static_cast<float>(TILE_SIZE);
    region.originY = static_cast<float>(rMinY) * static_cast<float>(TILE_SIZE);
    region.documentPxPerTexel = 1.0f;
    region.valid = true;

    // Level 2: the source is still valid but the chain changed (an effect slider
    // drag) — re-run the chain on the BAKED source and skip the assembly, which
    // for a group is by far the expensive half: one full re-entrant composite per
    // region tile.
    GLuint assembledSource = 0;
    if (cached != m_groupRegionCache.end()) {
        // Claim the entry for this batch BEFORE assembling: a nested group's own
        // whole-region evaluation runs re-entrantly from the assembly callback
        // and may evict entries, which would free the very source texture the
        // level-2 path is about to run the chain on.
        cached->second.lastUseSerial = m_batchSerial;
        if (cached->second.sourceValid && cached->second.sourceTexture
            && cached->second.sourceRevision == revision && boundsMatch(cached->second)) {
            assembledSource = cached->second.sourceTexture;
        }
    }

    // Otherwise assemble by compositing the group at every region tile. The
    // callback may run a re-entrant group composite that returns a transient
    // buffer, so the assembly stamps each tile immediately after its callback.
    // useGroupPool=true routes this to the renderer's SECOND whole-region pool,
    // so a group child that is itself a raster whole-layer distortion (running
    // re-entrantly during this assembly, on the default pool) cannot clobber
    // this region's source texture.
    const bool assembledFresh = assembledSource == 0;
    if (!assembledSource) {
        assembledSource = m_effectRenderer->assembleWholeRegion(
            TILE_SIZE, tilesW, tilesH,
            [&](int dx, int dy) -> GLuint {
                return groupTileTexture(TileKey { rMinX + dx, rMinY + dy });
            },
            /*useGroupPool=*/true);
    }
    if (!assembledSource) {
        return 0;
    }

    // Then run the whole chain on it with wholeLayerSource=true.
    const GLuint regionResult = m_effectRenderer->runWholeRegionChain(assembledSource, regionW,
        regionH, effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, backdropTexture, region,
        /*useGroupPool=*/true, liveEditedEffectId, liveEditSourceVariant);
    if (!regionResult) {
        return 0;
    }

    // Copy the transient region result into this identity's owned texture
    // (resized in place when the region bounds change).
    GroupRegionEntry& entry = m_groupRegionCache[identity];
    if (entry.texture && (entry.textureW != regionW || entry.textureH != regionH)) {
        deleteTexture(m_gl, entry.texture);
        entry.texture = 0;
    }
    if (entry.sourceTexture && entry.sourceTexture != assembledSource
        && (entry.textureW != regionW || entry.textureH != regionH)) {
        deleteTexture(m_gl, entry.sourceTexture);
        entry.sourceTexture = 0;
        entry.sourceValid = false;
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
        deleteTexture(m_gl, entry.sourceTexture);
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

    // Bake the freshly assembled source so the NEXT parameter change only has to
    // re-run the chain. The renderer's pool source is transient (the next
    // whole-region evaluation overwrites it), hence the owned copy. Oversized
    // regions are left unbaked: the copy would cost more VRAM than the rebuild
    // costs time.
    const bool wantBake = regionW <= kMaxBakedRegionDim && regionH <= kMaxBakedRegionDim;
    if (assembledFresh && wantBake) {
        if (!entry.sourceTexture) {
            const TextureParams linear { GL_LINEAR, GL_LINEAR };
            entry.sourceTexture = createTexture2D(m_gl, regionW, regionH, linear);
        }
        if (entry.sourceTexture) {
            m_gl->glCopyImageSubData(assembledSource, GL_TEXTURE_2D, 0, 0, 0, 0,
                entry.sourceTexture, GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(regionW),
                static_cast<GLsizei>(regionH), 1);
            entry.sourceValid = true;
        }
    } else if (assembledFresh && !wantBake && entry.sourceTexture) {
        deleteTexture(m_gl, entry.sourceTexture);
        entry.sourceTexture = 0;
        entry.sourceValid = false;
    }

    entry.originTileX = rMinX;
    entry.originTileY = rMinY;
    entry.tilesW = tilesW;
    entry.tilesH = tilesH;
    entry.batchSerial = m_batchSerial;
    entry.sourceRevision = revision;
    entry.effects = effects;
    entry.lastUseSerial = m_batchSerial;

    const GLuint slice = sliceFrom(entry);
    evictGroupRegionCacheIfNeeded();
    return slice;
}

void GLCompositor::evictGroupRegionCacheIfNeeded()
{
    while (m_groupRegionCache.size() > kMaxGroupRegionEntries) {
        auto victim = m_groupRegionCache.end();
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        for (auto it = m_groupRegionCache.begin(); it != m_groupRegionCache.end(); ++it) {
            if (it->second.lastUseSerial == m_batchSerial) {
                continue; // still in use by the batch in flight
            }
            if (it->second.lastUseSerial < oldest) {
                oldest = it->second.lastUseSerial;
                victim = it;
            }
        }
        if (victim == m_groupRegionCache.end()) {
            break;
        }
        deleteTexture(m_gl, victim->second.texture);
        deleteTexture(m_gl, victim->second.sourceTexture);
        m_groupRegionCache.erase(victim);
    }
}

uint64_t GLCompositor::layerCacheIdentity(const QUuid& id, GroupEffectSlot slot)
{
    const uint64_t hi = static_cast<uint64_t>(qHash(id, 0x9e3779b9u));
    const uint64_t lo = static_cast<uint64_t>(qHash(id, 0x85ebca6bu));
    uint64_t value = (hi << 32) ^ lo;
    value ^= (static_cast<uint64_t>(slot) + 1ULL) * 0x9e3779b97f4a7c15ULL;
    // Bit 63 marks a uuid-derived identity so it can never collide with the
    // pointer-derived identities the raster paths use.
    return value | 0x8000000000000000ULL;
}

uint64_t GLCompositor::recomposePrefixRevision(const CompositeLayerInfo* target) const
{
    uint64_t revision = 0x2545f4914f6cdd1dULL;
    const auto combine = [&revision](uint64_t value) {
        revision ^= value + 0x9e3779b97f4a7c15ULL + (revision << 6) + (revision >> 2);
    };
    if (!m_activeRootLayers || !target) {
        return revision;
    }

    // The canvas backdrop colour is composited into the pass-through source.
    const auto floatBits = [](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    combine(floatBits(m_activeBackdropColor.r));
    combine(floatBits(m_activeBackdropColor.g));
    combine(floatBits(m_activeBackdropColor.b));
    combine(floatBits(m_activeBackdropColor.a));

    // Walk exactly the way recomposePassThroughToGroup composites: layer by
    // layer from the root, descending into the group that contains the target
    // and stopping AT the target (whose own contribution — content and effect
    // chain alike — is not part of its own source).
    std::function<void(const std::vector<CompositeLayerInfo>&)> walk
        = [&](const std::vector<CompositeLayerInfo>& layers) {
              for (const CompositeLayerInfo& candidate : layers) {
                  if (&candidate == target) {
                      return;
                  }
                  if (groupSubtreeContains(candidate.children, target)) {
                      walk(candidate.children);
                      return;
                  }
                  combine(layerContentRevision(candidate));
              }
          };
    walk(*m_activeRootLayers);
    return revision;
}

GLuint GLCompositor::cachedGroupSourceTile(uint64_t identity, const TileKey& key, uint64_t revision,
    const std::function<GLuint(const TileKey&)>& produce)
{
    if (!produce) {
        return 0;
    }
    if (!identity || revision == 0) {
        return produce(key);
    }

    const GroupSourceTileKey cacheKey { identity, key };
    auto it = m_groupSourceTileCache.find(cacheKey);
    if (it != m_groupSourceTileCache.end() && it->second.texture
        && it->second.revision == revision) {
        it->second.lastUsedSerial = m_batchSerial;
        return it->second.texture;
    }

    const GLuint fresh = produce(key);
    if (!fresh) {
        return 0;
    }

    GroupSourceTileEntry& entry = m_groupSourceTileCache[cacheKey];
    if (!entry.texture) {
        static constexpr TextureParams kCacheTextureParams { GL_LINEAR, GL_NEAREST };
        entry.texture = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, kCacheTextureParams);
        if (!entry.texture) {
            m_groupSourceTileCache.erase(cacheKey);
            return fresh; // out of memory: use the transient composite directly
        }
    }
    // The produced texture is a transient composite buffer reused by the very
    // next call, so keep an owned copy. glCopyImageSubData does not disturb the
    // FBO/viewport state the caller is mid-assembly in.
    m_gl->glCopyImageSubData(fresh, GL_TEXTURE_2D, 0, 0, 0, 0, entry.texture, GL_TEXTURE_2D, 0, 0,
        0, 0, static_cast<GLsizei>(TILE_SIZE), static_cast<GLsizei>(TILE_SIZE), 1);
    entry.revision = revision;
    entry.lastUsedSerial = m_batchSerial;
    return entry.texture;
}

void GLCompositor::trimGroupSourceTileCache()
{
    // Drop entries no batch has touched since the previous trim first, then the
    // least recently used, until the cache is back under its cap. Never called
    // mid-batch, so nothing the current batch is assembling can be evicted.
    if (m_groupSourceTileCache.size() <= kMaxGroupSourceTiles) {
        return;
    }

    std::vector<std::pair<GroupSourceTileKey, uint64_t>> oldest;
    oldest.reserve(m_groupSourceTileCache.size());
    for (const auto& [cacheKey, entry] : m_groupSourceTileCache) {
        oldest.emplace_back(cacheKey, entry.lastUsedSerial);
    }
    std::sort(oldest.begin(), oldest.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

    const size_t removeCount = oldest.size() - kMaxGroupSourceTiles;
    for (size_t i = 0; i < removeCount; ++i) {
        auto it = m_groupSourceTileCache.find(oldest[i].first);
        if (it != m_groupSourceTileCache.end()) {
            deleteTexture(m_gl, it->second.texture);
            m_groupSourceTileCache.erase(it);
        }
    }
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

    // Structural compositing flags. They only change when the layer stack is
    // rebuilt, but the cross-batch caches keyed off this revision must see that
    // rebuild — a toggle such as alpha lock changes the composited result
    // without touching any grid version.
    combine(layer.isGroup);
    combine(layer.isAdjustment);
    combine(layer.forceIsolation);
    combine(layer.preserveBaseAlpha);
    combine(layer.replaceBase);
    combine(layer.useStrokeBlendBackdrop);
    combine(layer.clipMaskAlphaOnly);
    combine(layer.clipMaskAsAlphaCap);
    combine(layer.clipMaskLuminanceReveal);
    combine(layer.clipMaskEditPreview);
    combine(layer.clipMaskReplaceFallback);
    combine(layer.clipMaskEditReplace);
    combine(floatBits(layer.clipMaskEditStrokeOpacity));

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
    if (layer.retainedPayload) {
        // Payloads are rebuilt on edit; the revision guards the case where a new
        // one lands on the address a freed one had.
        combine(layer.retainedPayload->revision);
    }
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
    // Called once per batch. Revision-keyed block entries (groups, adjustment
    // layers) SURVIVE the batch — they are self-validating, so their chain is
    // not re-run while nothing that feeds them changed. Revision-less entries
    // (raster layers, which have their own persistent per-tile cache) were only
    // ever valid inside their batch and are dropped here; their textures go back
    // to the free list rather than being deleted.
    std::vector<EffectBlockKey> expired;
    std::vector<std::pair<EffectBlockKey, uint64_t>> reusable;
    for (const auto& [cacheKey, entry] : m_effectBlockCache) {
        if (entry.sourceRevision == 0) {
            expired.push_back(cacheKey);
        } else {
            reusable.emplace_back(cacheKey, entry.lastUsedSerial);
        }
    }
    if (reusable.size() > kMaxCachedBlocks) {
        std::sort(reusable.begin(), reusable.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
        const size_t removeCount = reusable.size() - kMaxCachedBlocks;
        for (size_t i = 0; i < removeCount; ++i) {
            expired.push_back(reusable[i].first);
        }
    }
    for (const EffectBlockKey& cacheKey : expired) {
        auto it = m_effectBlockCache.find(cacheKey);
        if (it == m_effectBlockCache.end()) {
            continue;
        }
        if (it->second.texture) {
            m_effectBlockPool.push_back(it->second.texture);
        }
        m_effectBlockCache.erase(it);
    }

    trimGroupSourceTileCache();

    for (auto& pair : m_retainedRegionCache) {
        deleteTexture(m_gl, pair.second.texture);
        deleteTexture(m_gl, pair.second.sourceTexture);
    }
    m_retainedRegionCache.clear();
    // NOTE: m_wholeLayerCache and m_groupRegionCache are deliberately NOT cleared
    // here — unlike the per-batch entries above they are cross-batch and
    // self-validating (by content revision + effects), so a static distorted
    // layer or group stays materialised across pan/idle frames.
    // Bound the free list so a one-off all-dirty sweep of a huge document does
    // not pin its worst-case block count in VRAM forever (each texture is a
    // (kEffectBlockTiles*TILE_SIZE)^2 RGBA8, 16 MB at 8 tiles).
    while (m_effectBlockPool.size() > kMaxPooledBlocks) {
        deleteTexture(m_gl, m_effectBlockPool.back());
        m_effectBlockPool.pop_back();
    }
}

void GLCompositor::destroyEffectBlockCache()
{
    for (auto& pair : m_effectBlockCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_effectBlockCache.clear();
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
        deleteTexture(m_gl, pair.second.sourceTexture);
    }
    m_groupRegionCache.clear();

    for (auto& pair : m_groupSourceTileCache) {
        deleteTexture(m_gl, pair.second.texture);
    }
    m_groupSourceTileCache.clear();

    for (auto& pair : m_retainedRegionCache) {
        deleteTexture(m_gl, pair.second.texture);
        deleteTexture(m_gl, pair.second.sourceTexture);
    }
    m_retainedRegionCache.clear();
}

GLuint GLCompositor::blockNeighborhoodEffectTile(uint64_t contentIdentity, const TileKey& key,
    int padPixels, const QList<ruwa::core::effects::LayerEffectState>& effects,
    const std::function<GLuint(const TileKey&)>& tileContent, const QUuid& liveEditedEffectId,
    quint64 liveEditSourceVariant, uint64_t sourceRevision)
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
    if (it != m_effectBlockCache.end() && it->second.texture) {
        // A revision-keyed entry stays valid for as long as its source content
        // and effect chain do; a revision-less one only within its own batch.
        const bool valid = sourceRevision != 0
            ? (it->second.sourceRevision == sourceRevision && it->second.effects == effects)
            : (it->second.sourceRevision == 0 && it->second.batchSerial == m_batchSerial);
        if (valid) {
            it->second.lastUsedSerial = m_batchSerial;
            return m_effectRenderer->extractNeighborhoodTile(
                it->second.texture, kBlockPx, TILE_SIZE, tileX, tileY);
        }
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
    // evaluation), so keep an owned copy — reusing the entry's own texture when
    // it is only being refreshed, otherwise one released by an evicted entry.
    EffectBlockEntry& entry = m_effectBlockCache[cacheKey];
    if (!entry.texture && !m_effectBlockPool.empty()) {
        entry.texture = m_effectBlockPool.back();
        m_effectBlockPool.pop_back();
    }
    if (!entry.texture) {
        const TextureParams linear { GL_LINEAR, GL_LINEAR };
        entry.texture = createTexture2D(m_gl, kBlockPx, kBlockPx, linear);
    }
    if (!entry.texture) {
        // Out of memory: still serve this tile from the transient result.
        m_effectBlockCache.erase(cacheKey);
        return m_effectRenderer->extractNeighborhoodTile(
            blockResult, kBlockPx, TILE_SIZE, tileX, tileY);
    }
    m_gl->glCopyImageSubData(blockResult, GL_TEXTURE_2D, 0, 0, 0, 0, entry.texture, GL_TEXTURE_2D,
        0, 0, 0, 0, static_cast<GLsizei>(kBlockPx), static_cast<GLsizei>(kBlockPx), 1);
    entry.sourceRevision = sourceRevision;
    entry.batchSerial = m_batchSerial;
    entry.lastUsedSerial = m_batchSerial;
    entry.effects = sourceRevision != 0 ? effects : QList<ruwa::core::effects::LayerEffectState> {};

    return m_effectRenderer->extractNeighborhoodTile(
        entry.texture, kBlockPx, TILE_SIZE, tileX, tileY);
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
    const uint64_t adjustmentIdentity
        = layerCacheIdentity(adjustment.id, GroupEffectSlot::AdjustmentBelow);
    auto composeBelow = [&](const TileKey& sourceKey) -> GLuint {
        return recompositeBelowBgFree(sourceKey, belowLayers, tileRenderer);
    };
    // Same memoisation as for groups: each source tile is a full recomposite of
    // the stack below, asked for once per overlapping block and once per batch.
    auto belowCompositeTexture = [&](const TileKey& sourceKey) -> GLuint {
        return cachedGroupSourceTile(adjustmentIdentity, sourceKey, sourceRevision, composeBelow);
    };

    // Distortions such as Twirl read arbitrary positions from the entire input.
    // Raster layers already route those chains through wholeLayerEffectTile(); an
    // adjustment needs the equivalent whole-region source, with each source tile
    // being the background-free composite of the stack below. Reuse the existing
    // group-region materialisation/cache so the lower stack is composited once per
    // source tile and the effect chain is evaluated once per batch, rather than
    // gathering and evaluating the same large neighbourhood for every output tile.
    if (chainNeedsWholeLayer(adjustment.effects) && !effectsRequireBackdrop(adjustment.effects)) {
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

            const GLuint wholeTile = wholeGroupEffectTile(adjustmentIdentity, sourceRevision, minX,
                minY, maxX, maxY, key, padPixels, adjustment.effects, belowCompositeTexture,
                transparentTexture(), adjustment.liveEditedEffectId, prefixCacheVariant);
            if (wholeTile) {
                return wholeTile;
            }
        }
    }

    // Bounded neighbour-reading chains (blur, ripple, etc.) use the same
    // batch-scoped block cache as raster layers and groups. This avoids rebuilding
    // overlapping padded sources and re-running the chain independently per tile.
    if (!effectsRequireBackdrop(adjustment.effects)) {
        const GLuint blockTile = blockNeighborhoodEffectTile(adjustmentIdentity, key, padPixels,
            adjustment.effects, belowCompositeTexture, adjustment.liveEditedEffectId,
            prefixCacheVariant, sourceRevision);
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
            return centre;
        }
        const TileKey neighborKey { key.x + dx, key.y + dy };
        IsolationScope iso(*this);
        compositeLayerStack(neighborKey, belowLayers, tileRenderer, 1.0f);
        return currentBase();
    };

    return m_effectRenderer->applyEffectsNeighborhood(TILE_SIZE, padPixels, neighborTexture,
        adjustment.effects, ruwa::core::effects::EffectEvaluationSpace::DocumentTile,
        /*realtimeOnly=*/false, centre, effectRegionForTile(key), adjustment.liveEditedEffectId,
        prefixCacheVariant);
}

GLuint GLCompositor::solidClipColorTexture(GLuint& slot, const Color& color)
{
    if (!slot) {
        slot = createTexture2D(m_gl, 1, 1);
        if (!slot)
            return 0;
    }
    const float clampedR = std::clamp(color.r, 0.0f, 1.0f);
    const float clampedG = std::clamp(color.g, 0.0f, 1.0f);
    const float clampedB = std::clamp(color.b, 0.0f, 1.0f);
    const float clampedA = std::clamp(color.a, 0.0f, 1.0f);

    const uint8_t rgba[4] = { static_cast<uint8_t>(clampedR * 255.0f + 0.5f),
        static_cast<uint8_t>(clampedG * 255.0f + 0.5f),
        static_cast<uint8_t>(clampedB * 255.0f + 0.5f),
        static_cast<uint8_t>(clampedA * 255.0f + 0.5f) };
    m_gl->glTextureSubImage2D(slot, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
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
