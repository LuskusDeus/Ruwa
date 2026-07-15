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

    // Returns true when the GPU path was used.
    bool stamp(TileBrush& brush, TileGrid& layerGrid, float worldX, float worldY,
        TileGrid* selectionMask, bool preferGpu, float strokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false);

    // Returns true when the GPU path was used.
    bool strokeTo(TileBrush& brush, TileGrid& layerGrid, float fromX, float fromY, float toX,
        float toY, float fromPressure, float toPressure, TileGrid* selectionMask, bool preferGpu,
        float fromStrokeElapsedSeconds = 0.0f, float toStrokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false);

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
    void deleteFence(GLsync fence) const;

private:
    bool hasGpuBackend() const;

private:
    GLBrushRenderer* m_brushRenderer = nullptr; // non-owning
    GLTileRenderer* m_tileRenderer = nullptr; // non-owning
    uint32_t m_canvasWidth = 0;
    uint32_t m_canvasHeight = 0;
    BrushExecutionOptions m_options {};
};

using BrushEngine = BrushExecutionBackend;

} // namespace aether

#endif // RUWA_CORE_BRUSHENGINE_BRUSHENGINE_H
