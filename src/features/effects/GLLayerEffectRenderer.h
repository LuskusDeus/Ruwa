// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_EFFECTS_GLLAYEREFFECTRENDERER_H
#define RUWA_FEATURES_EFFECTS_GLLAYEREFFECTRENDERER_H

#include "features/effects/GLLayerEffectRenderRegistry.h"
#include "features/effects/LayerEffectTypes.h"
#include "shared/types/Result.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QRect>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aether {

class IGLLayerEffectPass;
class GLShaderProgram;

class GLLayerEffectRenderer {
public:
    explicit GLLayerEffectRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLLayerEffectRenderer();

    GLLayerEffectRenderer(const GLLayerEffectRenderer&) = delete;
    GLLayerEffectRenderer& operator=(const GLLayerEffectRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    GLuint applyEffects(const EffectChainRequest& request);

    /// Document-tile path for bounds-expanding effects. Gathers a padded source
    /// (tileSize + 2*padPixels)^2 from the surrounding tiles via the
    /// neighbourTexture callback (offset dx,dy in tile units; 0 == empty),
    /// runs the whole chain on it, then crops the centre tileSize region.
    /// Returns a tileSize-sized texture owned by the renderer, or 0 if it could
    /// not run (caller should fall back to applyEffects on the centre tile).
    GLuint applyEffectsNeighborhood(uint32_t tileSize, int padPixels,
        const std::function<GLuint(int dx, int dy)>& neighborTexture,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly,
        GLuint backdropTexture = 0, ruwa::core::effects::EffectRegionFrame region = {},
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);

    /// Block generalisation of applyEffectsNeighborhood: evaluates the chain
    /// ONCE for a whole blockTiles x blockTiles group of tiles, amortising the
    /// padded gather + blur cost that the per-tile path repeats for every tile
    /// whose padding overlaps (at pad >> tileSize each document pixel would be
    /// re-blurred (1+2*pad/tileSize)^2 times). neighbourTexture offsets are in
    /// tile units RELATIVE TO THE BLOCK'S ORIGIN TILE (dx,dy may range
    /// [-ring, blockTiles+ring)); `region` describes the block origin tile.
    /// Returns a (blockTiles*tileSize)^2 texture owned by the renderer and
    /// reused by the next call — callers that cache must copy it out. Slice
    /// individual tiles off with extractNeighborhoodTile.
    GLuint applyEffectsNeighborhoodBlock(uint32_t tileSize, uint32_t blockTiles, int padPixels,
        const std::function<GLuint(int dx, int dy)>& neighborTexture,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly,
        GLuint backdropTexture = 0, ruwa::core::effects::EffectRegionFrame region = {},
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);

    /// Blits the (tileX, tileY) tileSize^2 slice out of a block texture
    /// produced by applyEffectsNeighborhoodBlock (or a caller-owned copy of
    /// one; blockPx is its full pixel size). Returns a tileSize-sized texture
    /// owned by the renderer, overwritten by the next extract call.
    GLuint extractNeighborhoodTile(
        GLuint blockTexture, uint32_t blockPx, uint32_t tileSize, uint32_t tileX, uint32_t tileY);

    /// Whole-layer materialisation path for distortion effects. Assembles the
    /// (tilesW x tilesH) tile region into ONE texture via tileTexture(dx,dy)
    /// (dx in [0,tilesW), dy in [0,tilesH); returns 0 for an empty slot), runs
    /// the ENTIRE chain on it with wholeLayerSource=true so distortion passes
    /// may sample anywhere in the layer, and returns the effected region texture
    /// (owned; overwritten by the next whole-layer call). `region` maps texel
    /// (0,0) to the region's document-pixel origin. Slice individual tiles with
    /// extractWholeLayerTile. Returns 0 when nothing was stamped or it could not
    /// run (caller falls back to the bounded neighbourhood path).
    GLuint applyEffectsWholeLayer(uint32_t tileSize, uint32_t tilesW, uint32_t tilesH,
        const std::function<GLuint(int dx, int dy)>& tileTexture,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly,
        GLuint backdropTexture = 0, ruwa::core::effects::EffectRegionFrame region = {},
        /// Take the pool from the group FAMILY instead of the raster one. The
        /// compositor's whole-GROUP / adjustment / retained paths set this so a
        /// nested raster whole-layer distortion never lands in the same family.
        /// Nesting WITHIN a family is safe on its own: each family hands out one
        /// pool per depth (see acquireWholeRegionPool), so a group inside a
        /// group, or an adjustment recompositing a stack that holds either, gets
        /// its own source and scratch.
        bool useGroupPool = false, const QUuid& liveEditedEffectId = {},
        quint64 liveEditSourceVariant = 0);

    /// Assembly half of applyEffectsWholeLayer: stamps the (tilesW x tilesH)
    /// tile region into the whole-region pool's source texture and returns it
    /// (0 when nothing was stamped). Split out so a caller can keep its own copy
    /// of the assembled source ("bake") and later re-run only the chain via
    /// runWholeRegionChain when nothing but the effect parameters changed — for
    /// a group that assembly is the expensive part, because every region tile
    /// costs a full re-entrant composite.
    GLuint assembleWholeRegion(uint32_t tileSize, uint32_t tilesW, uint32_t tilesH,
        const std::function<GLuint(int dx, int dy)>& tileTexture, bool useGroupPool);

    /// Chain half of applyEffectsWholeLayer: runs the whole chain on an
    /// already-assembled region source (which may be a caller-owned texture, not
    /// necessarily the pool's) with wholeLayerSource=true. Returns the effected
    /// region texture (owned by the renderer, overwritten by the next
    /// whole-region call) or 0.
    GLuint runWholeRegionChain(GLuint sourceTexture, uint32_t width, uint32_t height,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
        ruwa::core::effects::EffectRegionFrame region, bool useGroupPool,
        const QUuid& liveEditedEffectId = {}, quint64 liveEditSourceVariant = 0);

    /// Blits the (tileX, tileY) tileSize^2 slice out of a whole-layer region
    /// texture (regionTilesW x regionTilesH tiles) produced by
    /// applyEffectsWholeLayer or a caller-owned copy of one. Returns a
    /// tileSize-sized texture owned by the renderer, overwritten by the next
    /// extract call.
    GLuint extractWholeLayerTile(GLuint regionTexture, uint32_t regionTilesW, uint32_t regionTilesH,
        uint32_t tileSize, uint32_t tileX, uint32_t tileY);

    bool hasRenderableEffects(const QList<ruwa::core::effects::LayerEffectState>& effects,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly) const;

    bool isInitialized() const { return m_initialized; }

private:
    // Square padded source + ping-pong scratch (+ extra RGBA8/RGBA16F pools) for
    // ONE neighbourhood/block evaluation. Handed out per nesting depth by
    // acquirePadFrame rather than shared: the neighbour callback that assembles
    // the padded source runs a re-entrant composite, and any layer below with a
    // bounds-expanding chain of its own starts another neighbourhood evaluation
    // from inside that callback. Sharing one set let the nested run clear the
    // half-assembled source and — when its padding differed — free and resize it,
    // after which the outer chain read the new texture with its own stale
    // paddedSize (the whole assembled block rescaled into the region origin).
    struct PadFrame {
        GLuint source = 0;
        GLuint scratch[2] = { 0, 0 };
        std::vector<GLuint> extra;
        uint32_t extraCursor = 0;
        std::vector<GLuint> extraF16;
        uint32_t extraCursorF16 = 0;
        uint32_t size = 0;
        bool claimed = false; ///< in use by an evaluation on the stack
    };
    // Rectangular source + ping-pong scratch (+ extra RGBA8/RGBA16F pools) for
    // ONE whole-region distortion evaluation, handed out per nesting depth for
    // the same reason as PadFrame above. Two FAMILIES exist so a raster
    // whole-layer materialisation and the group/adjustment/retained whole-region
    // path keep their (very differently sized) textures apart; each family grows
    // to the depth actually reached. `claimed` spans assembly through the end of
    // the chain that consumes the assembled source, so a nested evaluation can
    // never resize or overwrite it. Sized on demand to the region's WxH.
    struct WholeRegionPool {
        GLuint source = 0;
        GLuint scratch[2] = { 0, 0 };
        std::vector<GLuint> extra;
        uint32_t extraCursor = 0;
        std::vector<GLuint> extraF16;
        uint32_t extraCursorF16 = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool claimed = false; ///< in use by an evaluation on the stack
    };
    bool ensureScratch(uint32_t width, uint32_t height);
    /// Claims a free pad frame sized `paddedSize` (allocating or resizing one
    /// only when no free frame already has that size). Returns nullptr when the
    /// textures could not be created; release it with releasePadFrame.
    PadFrame* acquirePadFrame(uint32_t paddedSize);
    static void releasePadFrame(PadFrame& frame) { frame.claimed = false; }
    void destroyPadFrame(PadFrame& frame);
    /// Claims a free pool of the requested family, sized WxH. Released by
    /// runWholeRegionChain once the chain that consumes the source is done.
    WholeRegionPool* acquireWholeRegionPool(bool groupFamily, uint32_t width, uint32_t height);
    /// The claimed pool whose source texture is `sourceTexture` — i.e. the pool
    /// an assembleWholeRegion call is still holding — or nullptr when the source
    /// is caller-owned (the baked-source path).
    WholeRegionPool* claimedPoolForSource(GLuint sourceTexture);
    static void releaseWholeRegionPool(WholeRegionPool& pool) { pool.claimed = false; }
    void destroyWholeRegionPool(WholeRegionPool& pool);
    GLuint ensureNeighborhoodOutput(uint32_t sizePx);
    GLuint allocateScratchTexture(bool highPrecision);
    GLuint runEffectChain(GLuint sourceTexture, uint32_t width, uint32_t height, GLuint scratch0,
        GLuint scratch1, const QList<ruwa::core::effects::LayerEffectState>& effects,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly, GLuint backdropTexture,
        GLuint finalTargetTexture, float spaceScale, ruwa::core::effects::EffectRegionFrame region,
        const QRect& finalRoi, bool wholeLayerSource, const QUuid& liveEditedEffectId,
        quint64 liveEditSourceVariant);
    void clearLiveEditPrefixCache();
    void blitTexture(GLuint sourceTexture, GLuint targetTexture, uint32_t targetWidth,
        uint32_t targetHeight, int viewportX, int viewportY, int viewportW, int viewportH,
        float scaleX, float scaleY, float offsetX, float offsetY);
    IGLLayerEffectPass* passFor(const QString& typeId) const;
    bool isEffectRenderable(const ruwa::core::effects::LayerEffectState& effect,
        ruwa::core::effects::EffectEvaluationSpace space, bool realtimeOnly) const;

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::vector<std::unique_ptr<IGLLayerEffectPass>> m_passes;
    std::unique_ptr<GLShaderProgram> m_blitProgram;
    GLuint m_fbo = 0;
    GLuint m_emptyVao = 0;
    GLuint m_scratchTextures[2] = { 0, 0 };
    std::vector<GLuint> m_extraScratchTextures;
    uint32_t m_extraScratchCursor = 0;
    // Parallel RGBA16F pool for effects that request a high-precision scratch.
    // Lazily grown, sized to m_scratchWidth/Height like the RGBA8 pool above.
    std::vector<GLuint> m_extraScratchTexturesF16;
    uint32_t m_extraScratchCursorF16 = 0;
    uint32_t m_scratchWidth = 0;
    uint32_t m_scratchHeight = 0;
    // Dedicated padded-source scratch for the neighbourhood path, kept separate
    // from the tile-size scratch above so mixing padded and non-padded effects
    // within one composite does not thrash texture reallocation. One frame per
    // nesting depth (see PadFrame).
    std::vector<std::unique_ptr<PadFrame>> m_padFrames;
    // Owned output textures for the neighbourhood paths, keyed by pixel size:
    // block results (blockTiles*tileSize) and per-tile crops (tileSize) coexist
    // within one frame, so a single sized slot would thrash reallocation.
    std::unordered_map<uint32_t, GLuint> m_neighborhoodOutputs;
    // Rectangular scratch/source for the whole-layer distortion path. Separate
    // from the square tile/pad pools because a materialised layer is an arbitrary
    // WxH region, resized on demand. m_wholeRegionPools serves the raster
    // whole-layer path; m_groupRegionPools serves the whole-GROUP / adjustment /
    // retained path. Each family holds one pool per nesting depth reached, so an
    // evaluation started from another one's assembly callback never aliases it.
    std::vector<std::unique_ptr<WholeRegionPool>> m_wholeRegionPools;
    std::vector<std::unique_ptr<WholeRegionPool>> m_groupRegionPools;
    // The pad frame an in-flight neighbourhood chain is drawing into, so
    // allocateScratchTexture hands out extra buffers from that frame
    // (nullptr == not inside a padded evaluation).
    PadFrame* m_activePadFrame = nullptr;
    // The whole-region pool an in-flight whole-region chain is drawing into, so
    // allocateScratchTexture hands out extra buffers from the matching pool
    // (nullptr == not inside a whole-region evaluation).
    WholeRegionPool* m_activeWholePool = nullptr;

    // Session-scoped GPU copies of the unchanged chain prefix before the effect
    // currently being dragged. Entries are exact-format copies, keyed by render
    // region/source revision and bounded by an LRU byte cap.
    struct LiveEditPrefixCacheEntry {
        QUuid effectId;
        GLuint sourceTexture = 0;
        GLuint backdropTexture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        ruwa::core::effects::EffectEvaluationSpace space
            = ruwa::core::effects::EffectEvaluationSpace::DocumentTile;
        bool realtimeOnly = false;
        bool wholeLayerSource = false;
        float spaceScale = 1.0f;
        ruwa::core::effects::EffectRegionFrame region;
        QRect finalRoi;
        quint64 sourceVariant = 0;
        QList<ruwa::core::effects::LayerEffectState> prefixEffects;
        GLuint texture = 0;
        size_t bytes = 0;
        uint64_t lastUse = 0;
    };
    std::vector<LiveEditPrefixCacheEntry> m_liveEditPrefixCache;
    QUuid m_cachedLiveEditEffectId;
    size_t m_liveEditPrefixCacheBytes = 0;
    uint64_t m_liveEditPrefixUseSerial = 0;
    static constexpr size_t kMaxLiveEditPrefixCacheBytes = 256ull * 1024ull * 1024ull;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_EFFECTS_GLLAYEREFFECTRENDERER_H
