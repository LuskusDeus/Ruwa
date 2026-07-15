// SPDX-License-Identifier: MPL-2.0

#include "features/brush/engine/BrushEngine.h"

#include "features/brush/rendering/GLBrushRenderer.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "shared/tiles/TileGrid.h"

namespace aether {

void BrushExecutionBackend::configureGpuBackend(
    GLBrushRenderer* brushRenderer, GLTileRenderer* tileRenderer)
{
    m_brushRenderer = brushRenderer;
    m_tileRenderer = tileRenderer;
}

void BrushExecutionBackend::clearGpuBackend()
{
    m_brushRenderer = nullptr;
    m_tileRenderer = nullptr;
    m_canvasWidth = 0;
    m_canvasHeight = 0;
}

void BrushExecutionBackend::setCanvasBounds(uint32_t width, uint32_t height)
{
    m_canvasWidth = width;
    m_canvasHeight = height;
}

void BrushExecutionBackend::resetSmudgeState()
{
    if (m_brushRenderer) {
        m_brushRenderer->resetSmudgeState();
    }
}

void BrushExecutionBackend::resetLiquifyState()
{
    if (m_brushRenderer) {
        m_brushRenderer->resetLiquifyState();
    }
}

void BrushExecutionBackend::setExecutionOptions(const BrushExecutionOptions& options)
{
    m_options = options;
}

BrushEngineCapabilities BrushExecutionBackend::capabilities() const
{
    BrushEngineCapabilities caps {};
    caps.gpuStamping = (m_brushRenderer != nullptr && m_tileRenderer != nullptr);
    caps.gpuFlatten = caps.gpuStamping;
    caps.asyncReadback = caps.gpuStamping;
    return caps;
}

bool BrushExecutionBackend::shouldUseGpu() const
{
    if (!m_options.preferGpu)
        return false;
    if (!hasGpuBackend())
        return false;
    return true;
}

bool BrushExecutionBackend::stamp(TileBrush& brush, TileGrid& layerGrid, float worldX, float worldY,
    TileGrid* selectionMask, bool preferGpu, float strokeElapsedSeconds, bool strokeTimeAvailable)
{
    brush.setStrokeElapsedSeconds(strokeElapsedSeconds, strokeTimeAvailable);
    if (preferGpu && hasGpuBackend()) {
        TileBrush::DabPoint dab = brush.recordDabPoint(worldX, worldY);
        if (dab.alpha == 0)
            return true;

        brush.setPressure(dab.pressure);
        brush.setStrokeElapsedSeconds(dab.strokeElapsedSeconds, dab.strokeTimeAvailable);

        // Liquify warps by the movement between consecutive dabs; a single
        // isolated stamp has no displacement, so the dab is recorded (for tile
        // coverage) but no warp is applied here — that happens in strokeTo().
        if (brush.isLiquifyMode()) {
            return true;
        }

        m_brushRenderer->stampGPU(brush.strokeBuffer(), m_tileRenderer, brush, dab.worldX,
            dab.worldY, dab.radius, dab.hardness, dab.roundness, dab.angleDegrees, dab.useMaxBlend,
            dab.colorR, dab.colorG, dab.colorB, dab.alpha, selectionMask, selectionMask != nullptr,
            m_canvasWidth, m_canvasHeight,
            (brush.isBlurMode() || brush.isSmudgeMode() || brush.isWetMode()
                || brush.isLiquifyMode())
                ? &layerGrid
                : nullptr);
        return true;
    }

    if (brush.isBlurMode() || brush.isSmudgeMode() || brush.isWetMode() || brush.isLiquifyMode()) {
        return false;
    }

    brush.stamp(layerGrid, worldX, worldY, selectionMask);
    return false;
}

bool BrushExecutionBackend::strokeTo(TileBrush& brush, TileGrid& layerGrid, float fromX,
    float fromY, float toX, float toY, float fromPressure, float toPressure,
    TileGrid* selectionMask, bool preferGpu, float fromStrokeElapsedSeconds,
    float toStrokeElapsedSeconds, bool strokeTimeAvailable)
{
    if (preferGpu && hasGpuBackend()) {
        std::vector<TileBrush::DabPoint> segmentDabs;
        brush.appendInterpolatedStrokeDabs(fromX, fromY, toX, toY, fromPressure, toPressure,
            segmentDabs, fromStrokeElapsedSeconds, toStrokeElapsedSeconds, strokeTimeAvailable);

        TileGrid* blurLayerGrid = (brush.isBlurMode() || brush.isSmudgeMode() || brush.isWetMode()
                                      || brush.isLiquifyMode())
            ? &layerGrid
            : nullptr;

        // Liquify (geometric warp) is GPU-only and reads the layer ROI like
        // smudge, but routes through its own segment path. No CPU fallback —
        // if GPU prerequisites fail the segment is simply skipped.
        if (brush.isLiquifyMode() && !segmentDabs.empty()) {
            m_brushRenderer->stampLiquifySegmentGPU(brush.strokeBuffer(), m_tileRenderer, brush,
                segmentDabs, m_canvasWidth, m_canvasHeight, blurLayerGrid, selectionMask,
                selectionMask != nullptr);
            const auto& last = segmentDabs.back();
            brush.setPressure(last.pressure);
            brush.setStrokeElapsedSeconds(last.strokeElapsedSeconds, last.strokeTimeAvailable);
            return true;
        }

        // Smudge has a CPU-heavy per-dab path (snapshot + tile-by-tile
        // rendering); for a stroke segment we batch all dabs into one ROI
        // ping-pong pass to avoid driver-call overhead saturating the main
        // thread during fast strokes.
        if ((brush.isSmudgeMode() || brush.isWetMode()) && !segmentDabs.empty()) {
            if (m_brushRenderer->stampSmudgeSegmentGPU(brush.strokeBuffer(), m_tileRenderer, brush,
                    segmentDabs, m_canvasWidth, m_canvasHeight, blurLayerGrid, selectionMask,
                    selectionMask != nullptr)) {
                if (!segmentDabs.empty()) {
                    const auto& last = segmentDabs.back();
                    brush.setPressure(last.pressure);
                    brush.setStrokeElapsedSeconds(
                        last.strokeElapsedSeconds, last.strokeTimeAvailable);
                }
                return true;
            }
            // Fall through to the legacy looped path on failure.
        }

        if (!brush.isBlurMode() && !brush.isSmudgeMode() && !brush.isWetMode()
            && !brush.isLiquifyMode() && segmentDabs.size() >= 8
            && m_brushRenderer->stampDabSegmentGPU(brush.strokeBuffer(), m_tileRenderer, brush,
                segmentDabs, selectionMask, selectionMask != nullptr, m_canvasWidth,
                m_canvasHeight)) {
            const auto& last = segmentDabs.back();
            brush.setPressure(last.pressure);
            brush.setStrokeElapsedSeconds(last.strokeElapsedSeconds, last.strokeTimeAvailable);
            return true;
        }

        m_brushRenderer->beginStampBatch();
        for (const auto& dab : segmentDabs) {
            brush.setPressure(dab.pressure);
            brush.setStrokeElapsedSeconds(dab.strokeElapsedSeconds, dab.strokeTimeAvailable);
            m_brushRenderer->stampGPU(brush.strokeBuffer(), m_tileRenderer, brush, dab.worldX,
                dab.worldY, dab.radius, dab.hardness, dab.roundness, dab.angleDegrees,
                dab.useMaxBlend, dab.colorR, dab.colorG, dab.colorB, dab.alpha, selectionMask,
                selectionMask != nullptr, m_canvasWidth, m_canvasHeight, blurLayerGrid);
        }
        m_brushRenderer->endStampBatch();
        return true;
    }

    if (brush.isBlurMode() || brush.isSmudgeMode() || brush.isWetMode() || brush.isLiquifyMode()) {
        return false;
    }

    brush.strokeToInterpolatedSize(layerGrid, fromX, fromY, toX, toY, fromPressure, toPressure,
        selectionMask, fromStrokeElapsedSeconds, toStrokeElapsedSeconds, strokeTimeAvailable);
    return false;
}

bool BrushExecutionBackend::rebuildStrokeFromDabs(
    TileBrush& brush, TileGrid* selectionMask, size_t maxDabs, bool preferGpu)
{
    if (preferGpu && hasGpuBackend() && !brush.hasDynamicsRequiringCpuReplay()) {
        m_brushRenderer->rebuildStrokeBufferFromDabsGPU(brush.strokeBuffer(), m_tileRenderer, brush,
            brush.strokeDabs(), maxDabs, selectionMask, selectionMask != nullptr, m_canvasWidth,
            m_canvasHeight);
        return true;
    }

    brush.rebuildStrokeBufferFromDabs(selectionMask, maxDabs);
    return false;
}

bool BrushExecutionBackend::rebuildStrokeRangeFromDabs(TileBrush& brush, size_t startDabIndex,
    size_t dabCount, TileGrid* selectionMask, bool preferGpu)
{
    if (dabCount == 0) {
        return false;
    }

    if (preferGpu && hasGpuBackend() && !brush.hasDynamicsRequiringCpuReplay()) {
        m_brushRenderer->rebuildStrokeBufferRangeFromDabsGPU(brush.strokeBuffer(), m_tileRenderer,
            brush, brush.strokeDabs(), startDabIndex, dabCount, selectionMask,
            selectionMask != nullptr, m_canvasWidth, m_canvasHeight);
        return true;
    }

    brush.rebuildStrokeBufferRangeFromDabs(startDabIndex, dabCount, selectionMask);
    return false;
}

std::unordered_set<TileKey, TileKeyHash> BrushExecutionBackend::flattenStroke(TileBrush& brush,
    TileGrid& layerGrid, bool usedGpuPath, bool alphaLock, TileGrid* strokeBlendBackdrop,
    const Color& strokeBlendBackdropColor, TileGrid* finalSourceMask, bool selectionAlphaCap,
    bool maskErase)
{
    if (usedGpuPath && m_brushRenderer && m_tileRenderer) {
        return m_brushRenderer->flattenStrokeGPU(brush.strokeBuffer(), layerGrid, m_tileRenderer,
            brush.isEraseMode(), brush.strokeOpacity(), brush.strokeBlendMode(), alphaLock,
            brush.isBlurMode() || brush.isSmudgeMode() || brush.isWetMode()
                || brush.isLiquifyMode(),
            strokeBlendBackdrop, strokeBlendBackdropColor, finalSourceMask, selectionAlphaCap,
            maskErase);
    }

    return brush.endStroke(layerGrid, alphaLock, strokeBlendBackdrop, strokeBlendBackdropColor,
        finalSourceMask, selectionAlphaCap, maskErase);
}

GLsync BrushExecutionBackend::startAsyncReadback(
    TileGrid& grid, const std::vector<TileKey>& keys, bool usedGpuPath)
{
    if (usedGpuPath && m_brushRenderer) {
        return m_brushRenderer->startAsyncReadback(grid, keys);
    }
    return nullptr;
}

bool BrushExecutionBackend::isReadbackComplete(GLsync fence) const
{
    if (!fence)
        return true;
    if (!m_brushRenderer)
        return true;
    return m_brushRenderer->isReadbackComplete(fence);
}

void BrushExecutionBackend::finishReadback(
    GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys, bool usedGpuPath)
{
    if (!fence)
        return;
    if (!usedGpuPath || !m_brushRenderer)
        return;
    m_brushRenderer->finishReadback(fence, grid, keys);
}

void BrushExecutionBackend::deleteFence(GLsync fence) const
{
    if (!fence)
        return;
    if (!m_brushRenderer)
        return;
    m_brushRenderer->deleteFence(fence);
}

bool BrushExecutionBackend::hasGpuBackend() const
{
    return m_brushRenderer != nullptr && m_tileRenderer != nullptr;
}

} // namespace aether
