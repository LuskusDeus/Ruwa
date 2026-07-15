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
        /// Use the SECOND whole-region pool (source + scratch) instead of the
        /// default one. The compositor's whole-GROUP distortion path sets this so
        /// its region assembly does not share textures with a nested raster
        /// whole-layer distortion (a child layer) that runs re-entrantly while
        /// this region is still being assembled — the two would otherwise clobber
        /// each other's single shared source texture. Recursion is bounded to
        /// these two levels (a group cannot contain another whole-region group).
        bool useGroupPool = false, const QUuid& liveEditedEffectId = {},
        quint64 liveEditSourceVariant = 0);

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
    // Rectangular source + ping-pong scratch (+ extra RGBA8/RGBA16F pools) for
    // one whole-region distortion evaluation. Two independent instances exist so
    // a group's whole-region assembly and a nested raster layer's whole-layer
    // materialisation (running re-entrantly during that assembly) never share a
    // source texture. Sized on demand to the region's WxH.
    struct WholeRegionPool {
        GLuint source = 0;
        GLuint scratch[2] = { 0, 0 };
        std::vector<GLuint> extra;
        uint32_t extraCursor = 0;
        std::vector<GLuint> extraF16;
        uint32_t extraCursorF16 = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    bool ensureScratch(uint32_t width, uint32_t height);
    bool ensurePadScratch(uint32_t paddedSize);
    bool ensureWholeRegionPool(WholeRegionPool& pool, uint32_t width, uint32_t height);
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
    // within one composite does not thrash texture reallocation.
    GLuint m_padScratchTextures[2] = { 0, 0 };
    GLuint m_padSourceTexture = 0;
    std::vector<GLuint> m_padExtraTextures;
    uint32_t m_padExtraCursor = 0;
    std::vector<GLuint> m_padExtraTexturesF16;
    uint32_t m_padExtraCursorF16 = 0;
    uint32_t m_padScratchSize = 0;
    // Owned output textures for the neighbourhood paths, keyed by pixel size:
    // block results (blockTiles*tileSize) and per-tile crops (tileSize) coexist
    // within one frame, so a single sized slot would thrash reallocation.
    std::unordered_map<uint32_t, GLuint> m_neighborhoodOutputs;
    // Rectangular scratch/source for the whole-layer distortion path. Separate
    // from the square tile/pad pools because a materialised layer is an arbitrary
    // WxH region, resized on demand. m_wholePool serves the raster whole-layer
    // path; m_groupPool serves the whole-GROUP path so the two can be live
    // simultaneously (group assembly compositing a child that is itself a raster
    // distortion) without aliasing.
    WholeRegionPool m_wholePool;
    WholeRegionPool m_groupPool;
    bool m_usingPadScratch = false;
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
