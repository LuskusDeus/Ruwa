// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   B R U S H   R E N D E R E R
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_GLBRUSHRENDERER_H
#define AETHER_ENGINE_OPENGL_GLBRUSHRENDERER_H

#include "shared/types/Result.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileBrush.h"
#include "shared/types/Types.h"
#include "features/layers/model/BlendModeUtils.h"
#include "features/brush/rendering/WetPigmentGpuLayout.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace aether {

class GLShaderProgram;
class GLTileRenderer;

class GLBrushRenderer {
public:
    explicit GLBrushRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLBrushRenderer();

    GLBrushRenderer(const GLBrushRenderer&) = delete;
    GLBrushRenderer& operator=(const GLBrushRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    // Optional batching for many sequential GPU dabs.
    void beginStampBatch();
    void endStampBatch();

    // ----- GPU Stamp (during stroke) -----

    void stampGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer, const TileBrush& brush,
        float worldX, float worldY, float radius, float hardness, float roundness,
        float angleDegrees, bool useMaxBlend, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
        TileGrid* selectionMask = nullptr, bool useSelectionMask = false, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0, TileGrid* layerGrid = nullptr);

    /// Batch-append ordinary paint/erase dabs for one input segment.
    /// Blur and smudge keep using their dedicated paths.
    bool stampDabSegmentGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
        const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs,
        TileGrid* selectionMask = nullptr, bool useSelectionMask = false, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0);

    /// Batched smudge: process a whole stroke segment of dabs in one go,
    /// with a single ROI snapshot and a ping-pong work buffer instead of
    /// per-dab tile-by-tile rendering. Reduces per-dab GL call overhead
    /// from ~20 to ~3, which removes the CPU bottleneck for fast strokes.
    /// Returns false if smudge mode isn't active or GPU prerequisites fail
    /// — caller should fall back to looped stampGPU().
    bool stampSmudgeSegmentGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
        const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, uint32_t canvasWidth,
        uint32_t canvasHeight, TileGrid* layerGrid, TileGrid* selectionMask = nullptr,
        bool useSelectionMask = false);

    /// Liquify (geometric warp): process a stroke segment of dabs by snapshotting
    /// the layer/stroke ROI into a ping-pong work buffer and, per dab, inverse-
    /// sampling the source by a displacement vector weighted by the brush falloff.
    /// This is the "Push" (forward-warp) mode; future modes change only the
    /// per-dab displacement formula. Mirrors stampSmudgeSegmentGPU's ROI/tile
    /// scaffolding but uses no reservoir. Returns false if GPU prerequisites fail.
    bool stampLiquifySegmentGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
        const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, uint32_t canvasWidth,
        uint32_t canvasHeight, TileGrid* layerGrid, TileGrid* selectionMask = nullptr,
        bool useSelectionMask = false);

    /// Rebuild entire stroke buffer on GPU from prepared dab points.
    /// Optional maxDabs allows lightweight preview sampling.
    void rebuildStrokeBufferFromDabsGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
        const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, size_t maxDabs = 0,
        TileGrid* selectionMask = nullptr, bool useSelectionMask = false, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0);

    /// Rebuild only a contiguous dab range on GPU while preserving the rest
    /// of the stroke buffer.
    void rebuildStrokeBufferRangeFromDabsGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
        const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, size_t startDabIndex,
        size_t dabCount, TileGrid* selectionMask = nullptr, bool useSelectionMask = false,
        uint32_t canvasWidth = 0, uint32_t canvasHeight = 0);

    // ----- GPU Flatten (no readback — GPU textures only) -----

    /// Flatten stroke onto layer via FBO blend. Returns affected keys.
    /// Layer tile GPU textures updated. CPU pixels NOT synced.
    std::unordered_set<TileKey, TileKeyHash> flattenStrokeGPU(TileGrid& strokeBuffer,
        TileGrid& layerGrid, GLTileRenderer* tileRenderer, bool eraseMode, float strokeOpacity,
        ruwa::core::layers::BlendMode strokeBlendMode = ruwa::core::layers::BlendMode::Normal,
        bool alphaLock = false, bool blurMode = false, TileGrid* strokeBlendBackdrop = nullptr,
        const Color& strokeBlendBackdropColor = Color::transparent(),
        TileGrid* finalSourceMask = nullptr, bool selectionAlphaCap = false,
        bool maskErase = false);

    // ----- Async PBO Readback (split into start / poll / finish) -----

    /// Kick non-blocking PBO readback for given tiles. Returns GL fence.
    /// Must be called with GL context active.
    GLsync startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys);

    /// Non-blocking poll: is readback DMA complete?
    bool isReadbackComplete(GLsync fence);

    /// Finish readback: wait for fence, map PBO, copy pixels to tile CPU
    /// buffers, unmap, delete fence. Must be called with GL context active.
    void finishReadback(GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys);

    /// Delete a GL fence without doing readback.
    void deleteFence(GLsync fence);

    /// Reset the smudge tool's per-stroke carry-buffer state. Call at stroke
    /// start and after flatten. Flips the "loaded" flag AND requests an
    /// explicit clear of the reservoir texture contents on the next dab.
    /// The content clear is required (not just the flag) because the first
    /// dab's uInit reload only covers the logical reservoir region; any
    /// stale pigment in the larger physical texture (from a previous, bigger
    /// brush) would otherwise bleed onto the new stroke's edge via bilinear
    /// sampling — e.g. a fresh black stroke picking up a previous orange one.
    void resetSmudgeState()
    {
        m_smudgePrevValid = false;
        m_smudgeReservoirNeedsClear = true;
    }

    /// Reset the liquify tool's per-stroke displacement field. Call at stroke
    /// begin so the next segment re-initializes the field ROI / source snapshot
    /// from scratch (the textures are kept allocated and resized on demand).
    void resetLiquifyState()
    {
        m_liquifyActive = false;
        m_liquifyFieldSrcIdx = 0;
    }

    bool isInitialized() const { return m_initialized; }
    uint32_t takeAndResetDrawCallEstimate();

private:
    void clearTexture(GLuint texId);
    void clearTexture(GLuint texId, uint32_t packedColor);
    // Copy a color region, using a raw image copy when formats match and an
    // exact texelFetch pass when converting document <-> Wet working formats.
    // Both paths preserve premultiplied channel values verbatim.
    bool copyColorRegion(GLuint sourceTexture, TilePixelFormat sourceFormat, GLint sourceX,
        GLint sourceY, GLuint targetTexture, TilePixelFormat targetFormat, GLint targetX,
        GLint targetY, GLsizei width, GLsizei height);
    void renderDabBatchForTile(const std::vector<TileBrush::DabPoint>& dabs,
        const std::vector<uint32_t>& indices, float tileOriginX, float tileOriginY,
        GLint locDabCount, GLint locBlendMode, GLint locDabCenter, GLint locDabParams,
        GLint locDabColor, std::vector<float>& centers, std::vector<float>& params,
        std::vector<float>& colors);
    bool ensureBlurScratchSize(GLsizei width, GLsizei height, TilePixelFormat contentFormat);
    // Re-format the fixed TILE_SIZE blur read texture to match a document tile
    // before glCopyImageSubData (format-compatibility). No-op when unchanged.
    void ensureBlurReadTexFormat(TilePixelFormat contentFormat);
    // RGBA8 selection-mask snapshot scratch (separate format from the content
    // blur scratch). Used by the smudge/liquify mask-snapshot paths.
    bool ensureMaskScratchSize(GLsizei width, GLsizei height);
    GLuint ensureProceduralTextureTile(const TileKey& key, const TileBrush& brush);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_brushProgram;
    std::unique_ptr<GLShaderProgram> m_blurPassProgram;
    std::unique_ptr<GLShaderProgram> m_blurProgram;
    std::unique_ptr<GLShaderProgram> m_smudgeProgram;
    std::unique_ptr<GLShaderProgram> m_smudgeBatchProgram;
    // Carry-buffer (reservoir) pickup shaders — one for the per-dab path
    // (samples a world-coord ROI snapshot) and one for the batched path
    // (samples the ROI work buffer in ROI-local coords). Both write into the
    // reservoir ping-pong target.
    std::unique_ptr<GLShaderProgram> m_smudgePickupProgram;
    std::unique_ptr<GLShaderProgram> m_smudgePickupBatchProgram;
    // Four-plane pigment-latent programs used only by Wet brushes. Ordinary
    // Smudge stays on the premultiplied-RGBA programs above.
    std::unique_ptr<GLShaderProgram> m_wetPickupProgram;
    std::unique_ptr<GLShaderProgram> m_wetApplyProgram;
    std::unique_ptr<GLShaderProgram> m_wetPickupBatchProgram;
    std::unique_ptr<GLShaderProgram> m_wetApplyBatchProgram;
    // Liquify (forward-warp) shaders. The field program advects a per-stroke
    // displacement field (RG32F ping-pong); the resolve program samples the
    // frozen source ONCE through the accumulated field. Both share
    // kSmudgeBatchVert. Sampling colors once (not per dab) is what keeps the
    // warp crisp instead of compounding into a smudge-like blur.
    std::unique_ptr<GLShaderProgram> m_liquifyFieldProgram;
    std::unique_ptr<GLShaderProgram> m_liquifyResolveProgram;
    std::unique_ptr<GLShaderProgram> m_formatCopyProgram;
    std::unique_ptr<GLShaderProgram> m_rebuildBatchProgram;
    std::unique_ptr<GLShaderProgram> m_flattenProgram;
    std::unique_ptr<GLShaderProgram> m_proceduralTextureProgram;

    GLuint m_fbo = 0;
    GLuint m_emptyVAO = 0;

    // Blur/smudge/liquify content scratch. Ordinary effects use the document
    // format. Wet uses workingColorFormat(documentFormat), with conversions at
    // the layer boundary handled by copyColorRegion().
    GLuint m_blurReadTex = 0;
    GLuint m_blurScratchSourceTex = 0;
    GLuint m_blurScratchTempTex = 0;
    GLuint m_blurLinearSampler = 0;

    // Selection-mask snapshot scratch for smudge/liquify. Masks are ALWAYS
    // RGBA8 coverage, so this stays RGBA8 and is kept separate from the content
    // scratch above (the two roles previously shared m_blurScratchTempTex,
    // which can no longer be both 16F content and an RGBA8 mask target).
    GLuint m_maskScratchTex = 0;
    GLsizei m_maskScratchWidth = 0;
    GLsizei m_maskScratchHeight = 0;

    // Smudge and Wet share the travelling reservoir geometry, but not its
    // color interpretation. Wet uses all four PigmentModel latent planes;
    // ordinary Smudge uses plane 0 as a premultiplied-RGBA carry buffer. It is
    // an axis-aligned texture that travels with the brush position —
    // reservoir pixel (rx, ry) corresponds to canvas position
    // (brushWorld.xy + (rx, ry) - reservoirHalf). Each dab does two passes:
    //   1. Pickup: reservoir = mix(reservoir, canvasSample, pickupRate*falloff)
    //   2. Apply : canvas    = mix(canvas,    reservoir,    strength*falloff)
    // The first dab of a stroke initializes the whole reservoir from the
    // canvas (rate = 1.0, no falloff mask) so subsequent dabs have a fully
    // loaded brush — that's what lets paint travel across uniform regions
    // even at low intensity instead of converging to a fixed point.
    //
    // m_smudgePrevValid doubles as the "reservoir is loaded for this stroke"
    // flag. Reset by resetSmudgeState() at stroke begin / rebuild.
    bool m_smudgePrevValid = false;
    // Set by resetSmudgeState(); honored on the next dab to wipe the reservoir
    // ping-pong textures to transparent so no pigment survives across strokes.
    bool m_smudgeReservoirNeedsClear = false;
    // World position of the most recent smudge/wet dab — used to compute the
    // per-dab travel distance for wet-rate spacing normalization (see
    // wetRatePerDab in the .cpp). Only meaningful while m_smudgePrevValid.
    float m_smudgePrevWorldX = 0.0f;
    float m_smudgePrevWorldY = 0.0f;
    // Wet brush finite paint supply (the "Drying" slider): 1.0 at stroke
    // start, decays exponentially with travel at TileBrush::colorDryRate()
    // (per half-radius, like the other wet rates) and scales the effective
    // colorSpread per dab. Only meaningful while m_smudgePrevValid; reset
    // on the first dab of a stroke.
    float m_wetPaintSupply = 1.0f;

    // Reservoir ping-pong + meta. Size is chosen on first dab of a stroke
    // to enclose the maximum brush footprint and is kept across dabs even
    // when the per-dab radius shrinks (smaller dabs simply mask out the
    // outer ring via the brush-coverage falloff).
    // Wet uses all four RGBA16F planes for PigmentModel::Latent. Smudge uses
    // only plane 0; the remaining planes stay cleared until a Wet stroke.
    GLuint m_smudgeReservoirTex[2][wet_pigment_gpu::kReservoirPlaneCount] = {};
    GLuint m_pigmentLutTex[wet_pigment_gpu::kLutTextureCount] = {};
    GLsizei m_pigmentLutSize = 0;
    GLsizei m_smudgeReservoirSize = 0; // physical texture side (square)
    GLsizei m_smudgeReservoirActive = 0; // logical side used this stroke
    int m_smudgeReservoirSrcIdx = 0;
    // Pickup rate per dab is read from TileBrush::wetMix() at draw time; see
    // stampGPU / stampSmudgeSegmentGPU.

    // Batched-smudge ping-pong work buffers (sized to the union ROI of a
    // stroke segment; reallocated on demand). Both smudge and wet/mixing pick
    // up from this evolving buffer so repeated dabs build up toward the pen.
    GLuint m_smudgeWorkTex[2] = { 0, 0 };
    GLsizei m_smudgeWorkWidth = 0;
    GLsizei m_smudgeWorkHeight = 0;
    // CONTENT format the smudge work textures were allocated with (per-document;
    // copied to/from document tiles so must stay format-compatible).
    TilePixelFormat m_smudgeWorkFormat = kDefaultTileFormat;

    // Liquify per-stroke displacement field (RG32F ping-pong) + the frozen
    // source snapshot (RGBA8, the original layer over the field ROI — the layer
    // is unmodified until the stroke flattens, so re-snapshotting on growth is
    // safe). The field is accumulated across all segments of a stroke; the ROI
    // grows to the stroke's bounding box (+ margin) as the brush travels.
    GLuint m_liquifyFieldTex[2] = { 0, 0 };
    GLuint m_liquifySourceTex = 0;
    // CONTENT format the frozen liquify source snapshot was allocated with.
    TilePixelFormat m_liquifySourceFormat = kDefaultTileFormat;
    GLsizei m_liquifyTexW = 0; // physical texture size (>= ROI)
    GLsizei m_liquifyTexH = 0;
    int32_t m_liquifyRoiX = 0; // ROI origin in canvas px
    int32_t m_liquifyRoiY = 0;
    GLsizei m_liquifyRoiW = 0; // ROI logical size
    GLsizei m_liquifyRoiH = 0;
    int m_liquifyFieldSrcIdx = 0; // ping-pong read index
    bool m_liquifyActive = false; // field valid for the current stroke
    // World position of the previous liquify dab, carried ACROSS segments so the
    // per-dab displacement = (curr - prev). Without this, slow strokes (short
    // segments of 1 dab) would compute a zero intra-segment delta and warp
    // nothing. Only meaningful while m_liquifyActive.
    float m_liquifyPrevWorldX = 0.0f;
    float m_liquifyPrevWorldY = 0.0f;

    GLuint m_pbo = 0;
    size_t m_pboSize = 0;
    GLsizei m_blurScratchWidth = 0;
    GLsizei m_blurScratchHeight = 0;
    // CONTENT format the blur scratch textures were allocated with.
    TilePixelFormat m_blurScratchFormat = kDefaultTileFormat;
    // CONTENT format the fixed TILE_SIZE m_blurReadTex currently holds.
    TilePixelFormat m_blurReadTexFormat = kDefaultTileFormat;
    uint32_t m_drawCallEstimate = 0;
    struct ProceduralTextureGpuTile {
        GLuint textureId = 0;
        uint32_t revision = 0;
        uint64_t lastUsedFrame = 0;
    };
    std::unordered_map<TileKey, ProceduralTextureGpuTile, TileKeyHash> m_proceduralTextureGpuTiles;
    uint64_t m_proceduralTextureCacheFrame = 0;

    bool m_stampBatchActive = false;
    GLint m_batchPrevFBO = 0;
    GLint m_batchPrevViewport[4] = { 0, 0, 0, 0 };

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLBRUSHRENDERER_H
