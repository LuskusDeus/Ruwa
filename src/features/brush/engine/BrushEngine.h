// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHENGINE_BRUSHENGINE_H
#define RUWA_CORE_BRUSHENGINE_BRUSHENGINE_H

#include "features/brush/engine/BrushEngineTypes.h"
#include "shared/tiles/TileBrush.h"
#include "shared/types/Types.h"

#include <QOpenGLFunctions_4_5_Core>

#include <vector>
#include <unordered_set>
#include <cstdint>

namespace aether {

class GLBrushRenderer;
class GLTileRenderer;

class BrushExecutionBackend {
public:
    BrushExecutionBackend() = default;

    void configureGpuBackend(GLBrushRenderer* brushRenderer, GLTileRenderer* tileRenderer);
    void clearGpuBackend();
    void setCanvasBounds(uint32_t width, uint32_t height);
    void resetSmudgeState();
    void resetLiquifyState();

    void setExecutionOptions(const BrushExecutionOptions& options);
    const BrushExecutionOptions& executionOptions() const { return m_options; }

    BrushEngineCapabilities capabilities() const;
    bool shouldUseGpu() const;

    /// Pick the storage format of the stroke buffer for a stroke about to start
    /// against `targetGrid` (the grid the stroke will eventually flatten into —
    /// the layer pixels, or the mask grid for a mask-edit stroke).
    ///
    /// Call once per stroke, after TileBrush::beginStroke() has emptied the
    /// buffer and before the first dab. The stamp entry points cannot do it
    /// themselves: the batched and deferred paths never receive the stroke
    /// target, and the format must be settled before the first tile is created.
    ///
    /// The widening is GPU-only, deliberately: such a buffer is written by the
    /// stamp shaders and read by flatten, while TileBrush's CPU rasterizer is
    /// still hardcoded to 8-bit bytes. With `useGpu` false the buffer is pinned
    /// to the target format, which is what that rasterizer expects.
    void prepareStrokeBuffer(TileBrush& brush, const TileGrid& targetGrid, bool useGpu) const;

    // Returns true when the GPU path was used.
    bool stamp(TileBrush& brush, TileGrid& layerGrid, float worldX, float worldY,
        TileGrid* selectionMask, bool preferGpu, float strokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false);

    // Returns true when the GPU path was used.
    bool strokeTo(TileBrush& brush, TileGrid& layerGrid, float fromX, float fromY, float toX,
        float toY, float fromPressure, float toPressure, TileGrid* selectionMask, bool preferGpu,
        float fromStrokeElapsedSeconds = 0.0f, float toStrokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false,
        const ruwa::core::brushes::BrushInputDynamics& fromInputDynamics = {},
        const ruwa::core::brushes::BrushInputDynamics& toInputDynamics = {});

    /// Collect the dabs of several consecutive strokeTo() calls and stamp them
    /// in ONE GPU batch at endDabBatch().
    ///
    /// A curved input span is subdivided into up to 128 straight micro-segments
    /// (BrushStrokeHost::rasterizeCatmullRomStroke), each of which used to be
    /// its own strokeTo — and with a large brush each carries only one or two
    /// dabs, below the threshold for the batched path, so every dab paid the
    /// full per-call GL setup: framebuffer state queries, uniform location
    /// lookups, a dozen uniform uploads and a fresh dab vector. Accumulating
    /// the span collapses all of that into a single stamp.
    ///
    /// Only plain paint/erase dabs are collected; blur, smudge, wet and liquify
    /// keep their own segment paths and stamp immediately, so the pending dabs
    /// stay a pure prefix of the stroke and ordering is preserved.
    ///
    /// endDabBatch() MUST run before anything reads the stroke buffer —
    /// snapshotting, dirty marking, rebuilds, flatten, readback.
    void beginDabBatch(const TileBrush& brush);
    void endDabBatch(TileBrush& brush, TileGrid* selectionMask);

    // Rebuild in-progress stroke buffer from stored dabs.
    // Returns true when GPU path was used.
    bool rebuildStrokeFromDabs(
        TileBrush& brush, TileGrid* selectionMask, size_t maxDabs, bool preferGpu);

    // Rebuild only a contiguous dab range in the in-progress stroke buffer.
    // Returns true when GPU path was used.
    bool rebuildStrokeRangeFromDabs(TileBrush& brush, size_t startDabIndex, size_t dabCount,
        TileGrid* selectionMask, bool preferGpu);

    std::unordered_set<TileKey, TileKeyHash> flattenStroke(TileBrush& brush, TileGrid& layerGrid,
        bool usedGpuPath, bool alphaLock = false, TileGrid* strokeBlendBackdrop = nullptr,
        const Color& strokeBlendBackdropColor = Color::transparent(),
        TileGrid* finalSourceMask = nullptr, bool selectionAlphaCap = false,
        bool maskErase = false);

    GLsync startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys, bool usedGpuPath);
    bool isReadbackComplete(GLsync fence) const;
    void finishReadback(
        GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys, bool usedGpuPath);
    size_t finishReadbackBatch(GLsync& fence, TileGrid& grid, const std::vector<TileKey>& keys,
        size_t firstKey, size_t maxKeys, bool usedGpuPath);
    void deleteFence(GLsync fence) const;

private:
    bool hasGpuBackend() const;

private:
    GLBrushRenderer* m_brushRenderer = nullptr; // non-owning
    GLTileRenderer* m_tileRenderer = nullptr; // non-owning
    uint32_t m_canvasWidth = 0;
    uint32_t m_canvasHeight = 0;
    BrushExecutionOptions m_options {};

    // Dab coalescing across the micro-segments of one input span.
    std::vector<TileBrush::DabPoint> m_pendingBatchDabs;
    bool m_dabBatchActive = false;
};

using BrushEngine = BrushExecutionBackend;

} // namespace aether

#endif // RUWA_CORE_BRUSHENGINE_BRUSHENGINE_H
