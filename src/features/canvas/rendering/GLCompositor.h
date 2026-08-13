// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   C O M P O S I T O R
// ==========================================================================
//
//   GPU-based layer compositor using FBO ping-pong.
//   Takes a stack of layers (each with its own TileGrid) and composites
//   them into a CompositionCache tile-by-tile using blend mode shaders.
//

#ifndef AETHER_ENGINE_OPENGL_GLCOMPOSITOR_H
#define AETHER_ENGINE_OPENGL_GLCOMPOSITOR_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileGrid.h"
#include "features/canvas/composition/CompositionCache.h"
#include "features/effects/LayerEffectTypes.h"
#include "features/canvas/rendering/RetainedRenderPayload.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QList>
#include <QUuid>

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether {

inline constexpr int kCompositeBlendModeErase = 100;

class GLShaderProgram;
class GLTileRenderer;
class GLTransformRenderer;
class GLRetainedRenderer;
class GLLayerEffectRenderer;
struct TransformState;

// Info about a single layer for compositing
struct CompositeLayerInfo {
    QUuid id;
    TileGrid* tileGrid = nullptr; // pixel data (Raster layers)
    float opacity = 1.0f;
    int blendMode = 0; // matches BlendMode enum
    bool visible = true;
    bool isGroup = false;
    /// Adjustment layer: has no pixel content of its own. Its effect chain is
    /// applied to the composite of everything BELOW it (currentBase) and the
    /// result replaces the base, gated by opacity / layer mask / clip-to-below.
    bool isAdjustment = false;
    bool clippedToBelow = false; // clip to nearest non-clipped layer below
    bool forceIsolation = false; // Composite children against transparency before the parent
    bool preserveBaseAlpha = false; // alpha-lock style preview compositing
    bool replaceBase = false; // src already stores final pixels for this tile
    bool useStrokeBlendBackdrop
        = false; // match commit-time stroke blend against visible layer backdrop
    TileGrid* externalClipMaskGrid = nullptr; // optional explicit clip mask grid
    bool clipMaskAlphaOnly = false; // apply explicit clip only to src alpha
    /// Treat externalClipMaskGrid as a per-pixel alpha *cap* on the result
    /// (soft-selection semantic) rather than only as src gating. See
    /// composite.frag.glsl :: uClipMaskAsAlphaCap for the exact rule.
    bool clipMaskAsAlphaCap = false;
    /// Treat externalClipMaskGrid as a painted luminance layer mask:
    /// reveal = luminance(premult rgb) + (1 - coverage). White reveals, black hides,
    /// uncovered tiles default to fully revealed.
    bool clipMaskLuminanceReveal = false;
    /// Mask-edit live preview: externalClipMaskGrid is the in-progress stroke buffer
    /// and clipMaskGrid2 is the committed mask; the shader combines them into the
    /// exact post-commit reveal in a single pass.
    bool clipMaskEditPreview = false;
    TileGrid* clipMaskGrid2 = nullptr;
    float clipMaskEditStrokeOpacity = 1.0f;
    /// Replace-mode mask preview (smudge/blur/liquify/wet): externalClipMaskGrid is
    /// the stroke buffer holding *finished* mask tiles, so where it has no tile the
    /// committed mask (clipMaskGrid2) is sampled as the clip instead of the
    /// transparent "fully revealed" default. Used with clipMaskLuminanceReveal.
    bool clipMaskReplaceFallback = false;
    /// Replace-mode mask preview formula: reveal = mix(committedReveal, strokeReveal, op).
    /// Commit does maskTile = mix(committed, stroke, strokeOpacity) and reveal is affine,
    /// so this reproduces the post-commit reveal exactly (including brush opacity < 1).
    bool clipMaskEditReplace = false;
    bool subtractClipRevealFromSrc = false; // remove original content where preview is revealed
    bool useRadialReveal = false;
    bool radialRevealInvert = false;
    Vector2 radialRevealOrigin {};
    float radialRevealRadius = 0.0f;
    float radialRevealFeather = 0.0f;

    // Transform preview: if set, this layer's content is rendered through
    // the transform using GLTransformRenderer instead of direct tile lookup.
    const TransformState* transform = nullptr;
    GLTransformRenderer* transformRenderer = nullptr;
    bool transformPreserveMaskedSource = false;
    bool hasSolidColor = false;
    Color solidColor {};
    std::shared_ptr<const RetainedRenderPayload> retainedPayloadOwner;
    const RetainedRenderPayload* retainedPayload = nullptr;
    uint64_t effectChainRevision = 0;
    QList<ruwa::core::effects::LayerEffectState> effects;
    QUuid liveEditedEffectId;
    QString liveEditedEffectParamKey;
    quint64 liveEffectEditGeneration = 0;

    // For groups: children in compositing order (bottom to top)
    std::vector<CompositeLayerInfo> children;
};

/// A layer a compositor will skip contributes nothing — no pixels, and no clip
/// base for the layers above it.
inline bool layerRenders(const CompositeLayerInfo& layer, float parentOpacity)
{
    return layer.visible && layer.opacity * parentOpacity > 0.0f;
}

/// True when the layer at `idx` actually acts as a clip base, i.e. the first
/// layer above it that is REALLY rendered is clipped to it.
///
/// Testing only `idx + 1` made a hidden (or fully transparent) clipping layer
/// force its base down the isolated clip-group path. That is not free: the base
/// stops being composited in place (an adjustment used as a clip base is then
/// applied to an empty isolated buffer, i.e. dropped), and it used to disqualify
/// the stack from the re-entrant paths that bounds-expanding effects need.
/// Skipped CLIPPED layers are looked past; a skipped non-clipped one ends the
/// search, matching the compositing rule that it resets the active clip base to
/// transparent (the clipped layers above it clip against nothing).
///
/// Shared by both compositors so the preview and the committed render cannot
/// disagree about what a clip group is.
inline bool hasRenderedClippedFollower(
    const std::vector<CompositeLayerInfo>& layers, size_t idx, float parentOpacity)
{
    for (size_t next = idx + 1; next < layers.size(); ++next) {
        const CompositeLayerInfo& follower = layers[next];
        if (!follower.clippedToBelow) {
            return false;
        }
        if (layerRenders(follower, parentOpacity)) {
            return true;
        }
    }
    return false;
}

/// Which of a group's several evaluated regions a cache entry belongs to. One
/// pass-through group evaluates its chain twice per tile (the visible result and
/// the background-free coverage), and an isolated group once, so the layer's id
/// alone is not a unique cache identity.
enum class GroupEffectSlot {
    PassThroughVisual = 0, ///< group composited over the stack below it
    PassThroughCoverage = 1, ///< same group over transparency (coverage/clip base)
    IsolatedResult = 2, ///< isolated group result (forceIsolation / blend group)
    AdjustmentBelow = 3, ///< adjustment layer: composite of the layers below it
};

class GLCompositor {
public:
    explicit GLCompositor(QOpenGLFunctions_4_5_Core* gl);
    ~GLCompositor();

    GLCompositor(const GLCompositor&) = delete;
    GLCompositor& operator=(const GLCompositor&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    /// Composite a single tile position from the layer stack into the cache.
    /// The tileRenderer is used to ensure layer tiles have GPU textures. This is
    /// a low-level entry point and intentionally does not restore GL state; the
    /// batch APIs below provide the normal guarded operation boundary.
    void compositeTile(const TileKey& key, const std::vector<CompositeLayerInfo>& layers,
        CompositionCache& cache, GLTileRenderer* tileRenderer,
        const Color& backdropColor = Color::transparent());

    /// Composite all dirty tiles from the layer stack into the cache.
    void compositeAllDirty(const std::vector<CompositeLayerInfo>& layers, CompositionCache& cache,
        GLTileRenderer* tileRenderer, const Color& backdropColor = Color::transparent());
    void compositeDirtyKeys(const std::vector<CompositeLayerInfo>& layers, CompositionCache& cache,
        GLTileRenderer* tileRenderer, const std::vector<TileKey>& keys,
        const Color& backdropColor = Color::transparent());
    void resetFrameStats();

    /**
     * @brief Composite an arbitrary layer stack into CPU pixels — the offscreen
     *        sibling of compositeDirtyKeys.
     *
     * Used to flatten a smart object's nested document into the composited cache
     * its instances are drawn from. The stack is composited exactly like the
     * document's own (same shaders, same clip/mask/effect rules), tile by tile,
     * and each result tile is read back into @p outGrid.
     *
     * @p outGrid is cleared and pinned to RGBA8: the ping-pong pair the whole
     * composite path runs through is 8-bit and its textures are SWAPPED into the
     * cache tiles, so a higher-precision output grid would end up owning an
     * 8-bit texture. A 16F/32F document therefore keeps its precision in its
     * LAYERS and flattens to 8 bits — the same result the canvas itself shows.
     *
     * Runs as its own composite batch (it resets the per-batch effect caches),
     * so it must NOT be called from inside a live composite batch or a paintGL
     * frame's compositing pass.
     *
     * @return true when at least one tile was read back.
     */
    bool compositeStackIntoGrid(const std::vector<CompositeLayerInfo>& layers,
        const std::unordered_set<TileKey, TileKeyHash>& keys, TileGrid& outGrid,
        GLTileRenderer* tileRenderer, const Color& backdropColor = Color::transparent());

    /// Permanently bakes `effects` into `grid`'s pixels (one-shot, not part of
    /// the live composite path). Runs the whole chain per affected tile —
    /// existing tiles plus, for bounds-expanding effects (blur/shadow), the
    /// ring of neighbouring tiles their padding can bleed into — and reads the
    /// result back into CPU pixel storage. `beforeTileWrite` is called once per
    /// tile right before it is overwritten, so the caller can snapshot the
    /// prior pixels (or note the tile did not exist) for undo; `outTouchedKeys`
    /// collects every tile key actually written. Returns false if there was
    /// nothing renderable to bake.
    bool bakeEffectsIntoGrid(TileGrid& grid,
        const QList<ruwa::core::effects::LayerEffectState>& effects, GLTileRenderer* tileRenderer,
        const std::function<void(const TileKey&)>& beforeTileWrite,
        std::vector<TileKey>& outTouchedKeys);

    /// Drops (and frees) every cross-batch cache entry keyed by
    /// `contentIdentity` (a TileGrid address) — the whole-layer distortion
    /// region and the per-tile effect results alike. Call before a throwaway
    /// grid is freed so no cache can be revalidated against whatever lands on
    /// its address next. No-op when nothing is cached for it.
    void dropWholeLayerCacheEntry(const void* contentIdentity);

    bool isInitialized() const { return m_initialized; }
    uint32_t lastCompositedTileCount() const { return m_lastCompositedTiles; }
    uint32_t lastCandidateTileCount() const { return m_lastCandidateTiles; }
    uint32_t lastCompositeDrawCallCount() const { return m_lastCompositeDrawCalls; }

private:
    // Composite layers recursively into ping-pong textures.
    // Returns the texture ID containing the result.
    // useSrcAtop: when true, every blendPass uses Porter-Duff src-atop instead
    //             of src-over (used for clipped-layer sub-passes inside a clip group).
    GLuint compositeLayerStack(const TileKey& key, const std::vector<CompositeLayerInfo>& layers,
        GLTileRenderer* tileRenderer, float parentOpacity, bool useSrcAtop = false,
        const Color& backdropColor = Color::transparent(), GLuint strokeBlendOuterBaseTex = 0,
        int strokeBlendLayerMode = 0, float strokeBlendLayerOpacity = 1.0f,
        const Color& strokeBlendBackdropColor = Color::transparent());

    void ensurePingPongTextures();
    void swapPingPong();
    GLuint currentBase() const { return m_pingPongTex[m_currentPing]; }
    GLuint currentTarget() const { return m_pingPongTex[1 - m_currentPing]; }

    void clearTexture(GLuint tex);

    // Dither bookkeeping for blendPass. Each layer pass writes an 8-bit tile,
    // so each one re-quantizes the composited ramp; the offset has to differ
    // between passes or their rounding errors stack into a fixed pattern. The
    // counter restarts on every new tile, which keeps a tile's composition
    // reproducible frame to frame no matter how many tiles preceded it.
    TileKey m_ditherPassKey { INT32_MIN, INT32_MIN };
    uint32_t m_ditherPassIndex = 0;

    struct BlendPassParams {
        GLuint baseTex = 0;
        GLuint srcTex = 0;
        GLuint targetTex = 0;
        TileKey key;
        int blendMode = 0;
        float opacity = 1.0f;
        GLuint clipMaskTex = 0;
        bool useClipMask = false;
        bool clipMaskAlphaOnly = false;
        bool clipMaskAsAlphaCap = false;
        bool clipMaskLuminanceReveal = false;
        bool clipMaskEditPreview = false;
        bool clipMaskEditReplace = false;
        GLuint clipMaskTex2 = 0;
        bool useClipMask2 = false;
        float clipMaskEditStrokeOpacity = 1.0f;
        bool subtractClipRevealFromSrc = false;
        bool preserveBaseAlpha = false;
        bool replaceBase = false;
        /// Adjustment-layer variant of replaceBase: srcTex holds the fully
        /// effected base, so the mask reveal scales the MIX FACTOR between the
        /// original base and the effected result (out = mix(base, src, op*reveal))
        /// instead of multiplying into src — reveal<1 then preserves the base
        /// rather than darkening it toward zero.
        bool replaceBaseMixReveal = false;
        /// Group-final composition: srcTex is the effected pass-through visual,
        /// groupPassThroughTex is the same visual before group effects, and
        /// groupCoverageTex contains the group's effected background-free coverage.
        bool useGroupComposite = false;
        GLuint groupPassThroughTex = 0;
        GLuint groupSourceCoverageTex = 0;
        GLuint groupCoverageTex = 0;
        bool useProgrammaticBlendBase = false;
        GLuint programmaticBlendBaseTex = 0;
        bool srcAtop = false;
        bool useRadialReveal = false;
        bool radialRevealInvert = false;
        Vector2 radialRevealOrigin {};
        float radialRevealRadius = 0.0f;
        float radialRevealFeather = 0.0f;
        Color backdropColor = Color::transparent();
    };
    void blendPass(const BlendPassParams& p);
    ruwa::core::effects::EffectRegionFrame effectRegionForTile(const TileKey& key) const;
    GLuint effectSourceTileTexture(
        TileGrid& grid, GLTileRenderer* tileRenderer, const TileKey& key, bool countUploadStats);
    std::unordered_set<TileKey, TileKeyHash> effectOutputKeysForGrid(
        const TileGrid& grid, const QList<ruwa::core::effects::LayerEffectState>& effects) const;
    GLuint applyLayerEffects(const TileKey& key, GLuint sourceTexture,
        const CompositeLayerInfo& layer, ruwa::core::effects::EffectEvaluationSpace space,
        bool realtimeOnly, GLuint finalTargetTexture = 0);
    GLuint applyTileEffectSource(const TileKey& key,
        const QList<ruwa::core::effects::LayerEffectState>& effects, int padPixels,
        const std::function<GLuint(const TileKey&)>& tileTexture, GLuint backdropTexture,
        bool realtimeOnly, const void* blockCacheIdentity = nullptr,
        const TileGrid* wholeLayerGrid = nullptr, uint64_t backdropRevision = 0,
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);
    /// Stable cache identity for the cross-batch group/adjustment region caches.
    /// The CompositeLayerInfo stack is rebuilt whenever the document changes, so
    /// its addresses cannot key a cache that must outlive a batch — the layer's
    /// QUuid can. Bit 63 is set so these can never collide with the
    /// pointer-derived identities used for raster grids.
    static uint64_t layerCacheIdentity(const QUuid& id, GroupEffectSlot slot);
    /// Revision of everything `recomposePassThroughToGroup` composites for
    /// `target`: the root stack walked up to (and stopping at) the group, plus
    /// the canvas backdrop colour. Layers ABOVE the group are excluded because
    /// the recompose stops before them, and the group's OWN effect chain is
    /// excluded so a slider drag does not invalidate the baked source.
    uint64_t recomposePrefixRevision(const CompositeLayerInfo* target) const;
    GLuint findCachedLayerEffectTile(const void* contentIdentity, const TileGrid& grid,
        const TileKey& key, const QList<ruwa::core::effects::LayerEffectState>& effects,
        uint64_t backdropRevision);
    GLuint storeCachedLayerEffectTile(const void* contentIdentity, const TileGrid& grid,
        const TileKey& key, const QList<ruwa::core::effects::LayerEffectState>& effects,
        uint64_t backdropRevision, GLuint resultTexture);
    void trimLayerEffectTileCache();
    uint64_t layerContentRevision(const CompositeLayerInfo& layer) const;
    uint64_t backdropRevision(
        const std::vector<CompositeLayerInfo>& layers, size_t layerIndex) const;
    /// True if the chain contains an enabled effect whose descriptor declares
    /// readsWholeLayer (distortion class) — the signal to take the whole-layer
    /// materialisation path instead of the bounded neighbourhood path.
    bool chainNeedsWholeLayer(const QList<ruwa::core::effects::LayerEffectState>& effects) const;
    /// Whole-layer distortion path: materialises `grid`'s populated-tile bbox
    /// (dilated by `maxDisplacementPx`, clamped to kMaxWholeLayerDim) into one
    /// texture, runs the whole chain once per content/effect revision (cached by
    /// identity), and slices `key`'s TILE_SIZE tile out. `tileTexture` returns a
    /// tile's content texture for an absolute key (0 == empty). Returns 0 when
    /// the layer is empty or exceeds the VRAM cap (caller falls back to the
    /// bounded neighbourhood path with clamped displacement).
    GLuint wholeLayerEffectTile(const void* contentIdentity, const TileGrid& grid,
        const TileKey& key, int maxDisplacementPx,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        const std::function<GLuint(const TileKey&)>& tileTexture, GLuint backdropTexture,
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);
    /// Evicts least-recently-used whole-layer cache entries down to
    /// kMaxWholeLayerEntries, never evicting `keepIdentity`.
    void evictWholeLayerCacheIfNeeded(const void* keepIdentity);
    /// Whole-GROUP distortion path — the group analogue of wholeLayerEffectTile.
    /// A distortion (readsWholeLayer) samples a disk anchored at an arbitrary
    /// document-space centre, so the bounded block/neighbourhood padding cannot
    /// hold it once the tile is far from that centre; the group must be
    /// materialised as one region, exactly like a raster layer. Composites the
    /// group (content + in-progress stroke) over the union bbox of `contentKeys`
    /// (dilated by `maxDisplacementPx`, capped at kMaxWholeLayerDim) into one
    /// texture via the `groupTileTexture` callback (returns the composited group
    /// tile for an absolute key, 0 == empty), runs the chain once, and slices
    /// `key`.
    ///
    /// The entry is cached ACROSS batches in m_groupRegionCache and validated by
    /// `sourceRevision` (a content hash of everything the callback composites)
    /// plus the effect chain, in two levels:
    ///   * same revision AND same effects -> the effected region is reused as is
    ///     (nothing at all is recomputed — the common case while painting on a
    ///     different layer, panning, or after an unrelated edit);
    ///   * same revision, different effects (an effect slider drag) -> only the
    ///     chain re-runs, on the BAKED source region kept alongside it, so the
    ///     per-region-tile group recomposites are skipped entirely.
    /// Returns 0 when empty or over the VRAM cap (caller falls back to the
    /// bounded path).
    GLuint wholeGroupEffectTile(uint64_t identity, uint64_t sourceRevision, int minTileX,
        int minTileY, int maxTileX, int maxTileY, const TileKey& key, int maxDisplacementPx,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        const std::function<GLuint(const TileKey&)>& groupTileTexture, GLuint backdropTexture,
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);
    /// Evicts least-recently-used group/adjustment region entries (each owns an
    /// effected region texture and, when small enough to be worth baking, its
    /// assembled source) down to kMaxGroupRegionEntries. Entries used in the
    /// current batch are never evicted.
    void evictGroupRegionCacheIfNeeded();
    /// Pixel padding the layer's effect chain needs from neighbouring source
    /// tiles (>0 only for renderable, enabled, neighbour-reading effects).
    /// 0 means the per-tile applyLayerEffects path is sufficient.
    int layerNeighborhoodPad(const CompositeLayerInfo& layer) const;
    /// True if the layer has any tile within `ring` tiles of `key` (so a
    /// bounds-expanding effect can produce bleed into/around `key`).
    bool neighborhoodHasContent(
        const TileKey& key, const CompositeLayerInfo& layer, int ring) const;
    bool retainedNeighborhoodHasContent(
        const TileKey& key, const RetainedRenderPayload& payload, int ring) const;
    GLuint applyRetainedEffectSource(
        const TileKey& key, const CompositeLayerInfo& layer, int padPixels, GLuint backdropTexture);
    GLuint wholeRetainedEffectTile(const void* identity, const RetainedRenderPayload& payload,
        const TileKey& key, int maxDisplacementPx,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        const std::function<GLuint(const TileKey&)>& tileTexture, GLuint backdropTexture,
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);
    /// Same, for a group's effect chain (e.g. the stroke-preview group): the
    /// centre is the already-composited groupResult, and the surrounding padding
    /// is the group re-composited at the neighbouring tiles — cheaply from the
    /// committed raw content where a flat content+overlay group has no stroke
    /// there, recursively otherwise. Returns 0 when nothing could be evaluated
    /// (caller falls back to the per-tile path).
    /// `outCentreTexture` receives the stable copy of the centre group result
    /// (the buffer `groupResultTexture` lived in is clobbered by the neighbour
    /// composites). A caller that keeps using the un-effected group result — as
    /// the clip base for the layers above — must read it from there.
    GLuint applyGroupNeighborhoodEffects(const TileKey& key, const CompositeLayerInfo& layer,
        GLTileRenderer* tileRenderer, int padPixels, GLuint groupResultTexture,
        bool allowCachedPaths = true,
        const std::function<GLuint(const TileKey&)>& passThroughTileTexture = {},
        GroupEffectSlot cacheSlot = GroupEffectSlot::IsolatedResult,
        quint64 liveEditSourceVariant = 0, GLuint* outCentreTexture = nullptr);
    /// Memoises one composited source tile of a group across batches. Producing
    /// it costs a full re-entrant composite (of the group, or of the whole stack
    /// up to it), and the padded/blocked neighbourhood paths ask for the same
    /// tiles again for every block and every batch. Entries are validated by
    /// `revision` (the group's content revision), so an effect-parameter change
    /// reuses them and only the chain re-runs. Returns a texture owned by the
    /// cache (stable until trimmed), or the produced texture when it cannot be
    /// cached.
    GLuint cachedGroupSourceTile(uint64_t identity, const TileKey& key, uint64_t revision,
        const std::function<GLuint(const TileKey&)>& produce);
    /// Drops stale/least-recently-used group source tiles down to
    /// kMaxGroupSourceTiles. Called at batch start only, so a batch that needs
    /// more tiles than the cap never evicts an entry it is still using.
    void trimGroupSourceTileCache();
    bool groupSubtreeContains(
        const std::vector<CompositeLayerInfo>& layers, const CompositeLayerInfo* target) const;
    GLuint recomposePassThroughToGroup(const TileKey& key, const CompositeLayerInfo* target,
        GLTileRenderer* tileRenderer, const Color& backdropColor);
    /// Recomposite `belowLayers` at `key` with a TRANSPARENT backdrop into a
    /// stable texture (returned), so the result is the background-free content of
    /// the layers below — never the opaque canvas background that the normal
    /// composite bakes into each tile. The stack below may contain anything
    /// (clip groups, nested adjustments, bounds-expanding group effects): every
    /// isolated composite it needs takes its own depth-indexed IsolationFrame.
    /// Returns 0 only when the compositor cannot render at all.
    GLuint recompositeBelowBgFree(const TileKey& key,
        const std::vector<CompositeLayerInfo>& belowLayers, GLTileRenderer* tileRenderer);
    /// Adjustment-layer effect path. The source is the background-free composite
    /// of the layers BELOW the adjustment. Whole-layer distortions reuse the
    /// whole-group materialisation cache, bounded neighbour effects reuse the
    /// block cache, and a backdrop-dependent chain (which neither cache can feed
    /// a per-tile backdrop to) falls back to an uncached neighbourhood
    /// evaluation. Returns a TILE_SIZE background-free effected texture, or 0
    /// when no source can be composed.
    GLuint applyAdjustmentNeighborhoodEffects(const TileKey& key,
        const std::vector<CompositeLayerInfo>& belowLayers, const CompositeLayerInfo& adjustment,
        GLTileRenderer* tileRenderer, int padPixels);
    /// Block-cached fast path shared by the raster/group neighbourhood-effect
    /// paths. Evaluates the chain once per kEffectBlockTiles^2 block of tiles
    /// (a batch-scoped cache keyed by content identity + block coords) and
    /// slices `key`'s TILE_SIZE tile out of it, instead of re-gathering and
    /// re-blurring a padded region per tile — at pad >> TILE_SIZE the per-tile
    /// path re-blurs each document pixel (1+2*pad/TILE_SIZE)^2 times, which is
    /// what made painting under a large-radius blur collapse. `tileContent`
    /// returns the layer/group content texture for an absolute tile key (0 ==
    /// empty). Returns a TILE_SIZE texture (owned by the effect renderer,
    /// valid until its next extract) or 0 to fall back to the per-tile path.
    ///
    /// `sourceRevision` != 0 makes the entry survive batches: it is then reused
    /// while that revision AND the effect chain are unchanged (groups and
    /// adjustment layers pass their content revision, so painting on an
    /// unrelated layer no longer re-runs their chain). 0 keeps the historical
    /// per-batch validity (raster layers, which have their own persistent
    /// per-tile cache).
    GLuint blockNeighborhoodEffectTile(uint64_t contentIdentity, const TileKey& key, int padPixels,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        const std::function<GLuint(const TileKey&)>& tileContent,
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0,
        uint64_t sourceRevision = 0);
    /// True if any enabled effect declares requiresBackdrop — those read a
    /// per-tile backdrop texture the block path cannot provide.
    bool effectsRequireBackdrop(const QList<ruwa::core::effects::LayerEffectState>& effects) const;
    void resetEffectBlockCache();
    void destroyEffectBlockCache();
    GLuint renderStrokeBlendBase(GLuint outerBaseTex, GLuint layerContentTex, const TileKey& key,
        int layerBlendMode, float layerOpacity, const Color& backdropColor);
    GLuint transparentTexture();
    GLuint solidColorTexture(const Color& color);
    // Dedicated 1x1 solid-color textures for the clip-mask slots. Separate from
    // m_solidColorTex (used for layer.solidColor src) so a solid mask tile or a
    // mask grid's default-fill background can be bound in the same blendPass as a
    // solid source layer without overwriting each other. Slot 2 mirrors the
    // secondary clip texture (committed mask in edit-preview).
    GLuint solidClipColorTexture(GLuint& slot, const Color& color);

    struct GroupCompositeFrame {
        GLuint ping[2] = { 0, 0 };
        GLuint passThrough = 0;
        GLuint effected = 0;
        GLuint sourceCoverage = 0;
        GLuint coverage = 0;
    };
    GroupCompositeFrame& ensureGroupCompositeFrame(size_t depth);

    /// One depth level of transient storage for a re-entrant isolated composite:
    /// a clip group, an adjustment's background-free recomposite of the stack
    /// below, or a neighbour gather for a bounds-expanding effect.
    ///
    /// Each of those used to own ONE fixed texture pair, which silently broke as
    /// soon as two of them nested — the inner composite cleared the buffer the
    /// outer one was still accumulating into. The nesting cases were therefore
    /// refused up front, and a refused adjustment fell back to a PER-TILE effect
    /// evaluation: visible tile seams for every bounds-expanding effect as soon
    /// as the document contained a clipping layer. Frames are handed out by
    /// depth instead, so nesting is correct at any depth and nothing has to be
    /// refused.
    struct IsolationFrame {
        GLuint ping[2] = { 0, 0 };
        /// Stable copy of a neighbourhood centre. The re-entrant neighbour
        /// composites clobber the ping-pong the centre was produced in, so it is
        /// copied here first and stays valid for the whole evaluation at this
        /// depth. Allocated on first use (see isolationCentre): only the
        /// neighbourhood paths need one.
        GLuint centre = 0;
    };
    IsolationFrame& ensureIsolationFrame(size_t depth);
    GLuint isolationCentre(IsolationFrame& frame);

    /// Claims the isolation frame for the current nesting depth. By default it
    /// also redirects the compositor's ping-pong onto that frame (clearing it)
    /// and restores the previous target on scope exit, so an isolated composite
    /// is written with plain `compositeLayerStack` calls. `Reserve` only claims
    /// the depth — for a caller that needs a private `centre()` while continuing
    /// to composite into (and read `currentBase()` from) the outer target.
    class IsolationScope {
    public:
        enum Mode { Redirect, Reserve };
        explicit IsolationScope(GLCompositor& owner, Mode mode = Redirect);
        ~IsolationScope();
        IsolationScope(const IsolationScope&) = delete;
        IsolationScope& operator=(const IsolationScope&) = delete;
        /// Stable per-depth scratch texture for a neighbourhood centre copy,
        /// allocated on first use.
        GLuint centre() const;

    private:
        GLCompositor& m_owner;
        IsolationFrame& m_frame;
        Mode m_mode;
        GLuint m_savedTex[2] = { 0, 0 };
        int m_savedPing = 0;
    };

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    // Composite shader
    std::unique_ptr<GLShaderProgram> m_compositeProgram;

    // FBO and ping-pong textures
    GLuint m_fbo = 0;
    GLuint m_pingPongTex[2] = { 0, 0 };
    int m_currentPing = 0;

    GLuint m_programmaticBlendBaseTex = 0;
    std::vector<std::unique_ptr<GroupCompositeFrame>> m_groupCompositeFrames;
    size_t m_groupCompositeDepth = 0;
    // Transient storage for clip groups, background-free recomposites and
    // neighbour gathers, one frame per nesting depth (see IsolationScope).
    std::vector<std::unique_ptr<IsolationFrame>> m_isolationFrames;
    size_t m_isolationDepth = 0;
    const std::vector<CompositeLayerInfo>* m_activeRootLayers = nullptr;
    /// Canvas backdrop colour of the batch in flight. A pass-through group's
    /// source is composited over it, so it takes part in that source's revision.
    Color m_activeBackdropColor = Color::transparent();
    /// Clip base of the src-atop sub-stack currently being composited, if any.
    /// Only meaningful while `useSrcAtop` is true: a clipped ADJUSTMENT layer
    /// needs it to rebuild the clip group's content at neighbouring tiles.
    const CompositeLayerInfo* m_srcAtopClipBase = nullptr;
    const CompositeLayerInfo* m_recomposeStopGroup = nullptr;
    bool m_recomposeStopReached = false;
    // Cache for block-evaluated neighbourhood effects (see
    // blockNeighborhoodEffectTile). Keyed by content identity (a TileGrid /
    // payload address for raster sources, a uuid-derived layerCacheIdentity for
    // groups and adjustment layers) + block coordinates. Textures are
    // (kEffectBlockTiles*TILE_SIZE)^2 RGBA8, owned by their entry. Entries
    // carrying a content revision survive batches (self-validating); the
    // revision-less raster ones are dropped at the next batch start, because
    // tile/stroke content changes between batches.
    //
    // 8 (not 4): each block gathers a fixed `pad` halo (768px at max blur) that
    // OVERLAPS its neighbours and is recomputed per block, so the halo is pure
    // overhead. Its share is 1 - (block/(block+2*pad))^2 — 84% at a 4-tile
    // (1024px) block, dropping to ~69% at 8 tiles (2048px). A large-radius
    // stroke that spans e.g. 14x13 tiles then evaluates ceil/8 = 2x2 = 4 blocks
    // instead of ceil/4 = 4x4 = 16, roughly halving the per-frame blur cost.
    // The ceiling is VRAM: the padded working set (and the box blur's RGBA32F
    // prefix) grow as (blockPx+2*pad)^2 — ~205MB for the prefix at 8 tiles,
    // acceptable; 16 tiles (~0.5GB) is not.
    static constexpr int kEffectBlockTiles = 8;
    struct EffectBlockKey {
        uint64_t identity = 0;
        int blockX = 0;
        int blockY = 0;
        bool operator==(const EffectBlockKey& other) const
        {
            return identity == other.identity && blockX == other.blockX && blockY == other.blockY;
        }
    };
    struct EffectBlockKeyHash {
        size_t operator()(const EffectBlockKey& key) const
        {
            size_t h = std::hash<uint64_t>()(key.identity);
            h ^= std::hash<int>()(key.blockX) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(key.blockY) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct EffectBlockEntry {
        GLuint texture = 0;
        /// 0 == per-batch entry (valid only while batchSerial == m_batchSerial);
        /// non-zero == content revision of the source, valid across batches while
        /// the revision and the effect chain both match.
        uint64_t sourceRevision = 0;
        uint64_t batchSerial = 0;
        QList<ruwa::core::effects::LayerEffectState> effects;
        uint64_t lastUsedSerial = 0;
    };
    std::unordered_map<EffectBlockKey, EffectBlockEntry, EffectBlockKeyHash> m_effectBlockCache;
    /// Free-list of block-sized textures released by evicted entries, reused
    /// before allocating a new one. Entries own their texture while cached.
    std::vector<GLuint> m_effectBlockPool;
    /// Revision-keyed block entries kept between batches (16 MB each at
    /// kEffectBlockTiles=8), plus the free list below them — about the VRAM
    /// budget the old per-batch pool had. A pass-through group evaluates two
    /// regions (visual + coverage), so 8 covers a few blocks of one such group;
    /// the map may grow past it within a batch and is trimmed at the next start.
    static constexpr size_t kMaxCachedBlocks = 8;
    static constexpr size_t kMaxPooledBlocks = 2;

    // Cross-batch cache for the effected output of an otherwise-static raster
    // layer. The final composition cache is invalidated when another layer is
    // painted, but these textures remain valid while this source grid and its
    // effect chain are unchanged. Backdrop-dependent entries additionally carry
    // a revision derived solely from the layers below the effected layer.
    struct LayerEffectTileKey {
        const void* identity = nullptr;
        TileKey tile {};
        bool operator==(const LayerEffectTileKey& other) const
        {
            return identity == other.identity && tile == other.tile;
        }
    };
    struct LayerEffectTileKeyHash {
        size_t operator()(const LayerEffectTileKey& key) const
        {
            size_t h = std::hash<const void*>()(key.identity);
            h ^= TileKeyHash {}(key.tile) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct LayerEffectTileCacheEntry {
        GLuint texture = 0;
        uint64_t contentVersion = 0;
        size_t tileCount = 0;
        QList<ruwa::core::effects::LayerEffectState> effects;
        uint64_t backdropRevision = 0;
        uint64_t lastUsedSerial = 0;
    };
    std::unordered_map<LayerEffectTileKey, LayerEffectTileCacheEntry, LayerEffectTileKeyHash>
        m_layerEffectTileCache;

    // Maximum pixel dimension (width or height) of a materialised whole-layer
    // distortion source. Beyond this the whole-layer path bails and the effect
    // degrades to the bounded neighbourhood path (clamped displacement). 8192^2
    // RGBA8 = 256 MB per working texture; a distortion chain needs a few of them
    // (source + 2 scratch + pooled copy), so ~1 GB transient at the cap.
    static constexpr uint32_t kMaxWholeLayerDim = 8192;
    // CROSS-BATCH cache for whole-layer materialised distortion results, keyed by
    // content identity (TileGrid*). Unlike m_effectBlockCache (per-batch), this
    // survives batches and self-validates: an entry is reused only while the
    // grid's contentVersion + tileCount AND the effect chain are unchanged, so a
    // static distorted layer is materialised ONCE and then reused across pan /
    // idle / painting-elsewhere frames instead of every batch. Each entry OWNS
    // its region texture (resized in place when the bounds change); the map is
    // capped (LRU-ish) to bound VRAM.
    struct WholeLayerCacheEntry {
        GLuint texture = 0; ///< owned effected region (whole layer materialised)
        uint32_t textureW = 0;
        uint32_t textureH = 0;
        int originTileX = 0; ///< tile coord of the region's top-left tile
        int originTileY = 0;
        uint32_t tilesW = 0;
        uint32_t tilesH = 0;
        uint64_t contentVersion = 0; ///< grid.contentVersion() when materialised
        size_t tileCount = 0; ///< grid.tileCount() when materialised
        QList<ruwa::core::effects::LayerEffectState> effects; ///< chain when materialised
        uint64_t lastUseSerial = 0; ///< for eviction
    };
    std::unordered_map<const void*, WholeLayerCacheEntry> m_wholeLayerCache;
    uint64_t m_wholeLayerUseSerial = 0;
    static constexpr size_t kMaxWholeLayerEntries = 4;

    // Cross-BATCH cache for whole composite-region distortions on groups and
    // adjustment layers. Their input is not a single TileGrid, so it cannot
    // self-validate by grid.contentVersion like the raster whole-layer cache;
    // instead the caller hashes everything that feeds the region into
    // `sourceRevision` (children content + the stack below for a pass-through
    // group) and the entry stays valid while that hash holds.
    //
    // Each entry OWNS its effected region texture and — when the region is small
    // enough to be worth the VRAM (kMaxBakedRegionDim) — the ASSEMBLED SOURCE it
    // was produced from. That bake is what makes editing a group effect cheap:
    // the source only depends on the group's content, so a parameter change
    // re-runs the chain alone instead of re-compositing the group at every
    // region tile.
    struct GroupRegionEntry {
        GLuint texture = 0; ///< owned effected region
        GLuint sourceTexture = 0; ///< owned pre-effect region bake (0 == none)
        uint32_t textureW = 0;
        uint32_t textureH = 0;
        int originTileX = 0;
        int originTileY = 0;
        uint32_t tilesW = 0;
        uint32_t tilesH = 0;
        uint64_t batchSerial = 0; ///< m_batchSerial when materialised
        uint64_t sourceRevision = 0; ///< content revision the source was built from
        bool sourceValid = false; ///< sourceTexture holds that revision's assembly
        QList<ruwa::core::effects::LayerEffectState> effects; ///< chain when materialised
        uint64_t lastUseSerial = 0; ///< m_batchSerial at last use (eviction order)
    };
    std::unordered_map<uint64_t, GroupRegionEntry> m_groupRegionCache;
    static constexpr size_t kMaxGroupRegionEntries = 4;
    /// Regions wider/taller than this are not baked (the source copy would cost
    /// more VRAM than the rebuild costs time); they still cache their effected
    /// result, they just re-assemble when the chain changes.
    static constexpr uint32_t kMaxBakedRegionDim = 4096;

    // Cross-batch cache of individual COMPOSITED GROUP TILES (see
    // cachedGroupSourceTile). The bounded neighbourhood/block paths ask for the
    // same group tiles over and over — once per block whose padding covers them,
    // once per batch — and each request is a full re-entrant composite. Keyed by
    // (group region identity, tile), validated by the group's content revision.
    struct GroupSourceTileKey {
        uint64_t identity = 0;
        TileKey tile {};
        bool operator==(const GroupSourceTileKey& other) const
        {
            return identity == other.identity && tile == other.tile;
        }
    };
    struct GroupSourceTileKeyHash {
        size_t operator()(const GroupSourceTileKey& key) const
        {
            size_t h = std::hash<uint64_t>()(key.identity);
            h ^= TileKeyHash {}(key.tile) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct GroupSourceTileEntry {
        GLuint texture = 0;
        uint64_t revision = 0;
        uint64_t lastUsedSerial = 0;
    };
    std::unordered_map<GroupSourceTileKey, GroupSourceTileEntry, GroupSourceTileKeyHash>
        m_groupSourceTileCache;
    /// 384 RGBA8 tiles = 96 MiB. One block gather reads (kEffectBlockTiles +
    /// 2*ring)^2 tiles — 100 at a one-ring pad — and neighbouring blocks share
    /// most of their halo, so this holds a screenful of blocks without thrashing.
    /// The cache may exceed it within one batch (never evicting what that batch
    /// is using) and is trimmed back at the next batch start.
    static constexpr size_t kMaxGroupSourceTiles = 384;
    // Retained/text whole-region cache is kept separate from group regions:
    // retained payload objects can be recreated while editing text, so entries
    // are cleared per batch instead of being keyed to stable LayerData objects.
    std::unordered_map<const void*, GroupRegionEntry> m_retainedRegionCache;
    // Monotonic per-batch counter (bumped in compositeDirtyKeys). Distinguishes a
    // group region built in one batch from the next; the group content differs
    // every frame so a stale entry must never be reused across batches.
    uint64_t m_batchSerial = 0;
    GLuint m_transparentTex = 0;
    GLuint m_solidColorTex = 0;
    GLuint m_solidClipTex = 0; // primary clip-mask solid color / default fill
    GLuint m_solidClipTex2 = 0; // secondary clip-mask solid color / default fill
    std::unique_ptr<GLRetainedRenderer> m_retainedRenderer;
    std::unique_ptr<GLLayerEffectRenderer> m_effectRenderer;

    // Empty VAO for fullscreen quad rendering
    GLuint m_emptyVAO = 0;

    uint32_t m_lastCompositedTiles = 0;
    uint32_t m_lastCandidateTiles = 0;
    uint32_t m_lastCompositeDrawCalls = 0;

    // Debug timing accumulators (per compositeDirtyKeys batch)
    qint64 m_dbgTotalGuardUs = 0;
    qint64 m_dbgTotalClearUs = 0;
    qint64 m_dbgTotalStackUs = 0;
    qint64 m_dbgTotalSwapUs = 0;
    qint64 m_dbgTotalTileUs = 0;
    uint32_t m_dbgTileCount = 0;
    uint32_t m_dbgUploadCount = 0;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLCOMPOSITOR_H
