// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_FILL_PROGRESSIVE_POLICY_H
#define AETHER_FILL_PROGRESSIVE_POLICY_H

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/fill/FillRawTileOps.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <thread>
#include <unordered_set>
#include <vector>

#include <QtGlobal>

class QWidget;

namespace aether {

inline constexpr float kSmartFillMaxEstimatedRadiusPx = 3000.0f;
inline constexpr size_t kFillPreviewStartBatchBudget = 4;
inline constexpr qint64 kFillPreviewStartBatchBudgetMs = 12;

inline size_t fillPreviewWorkerCount(size_t workItemCount)
{
    if (workItemCount == 0) {
        return 0;
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t availableComputeThreads
        = hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : static_cast<size_t>(1);
    return std::min(workItemCount, std::max<size_t>(1, availableComputeThreads));
}

template <typename Fn>
void parallelForFillPreviewChunks(size_t workItemCount, size_t minItemsPerWorker, Fn&& fn)
{
    if (workItemCount == 0) {
        return;
    }

    const size_t workerCount = fillPreviewWorkerCount(workItemCount);
    if (workerCount <= 1 || workItemCount < workerCount * std::max<size_t>(1, minItemsPerWorker)) {
        fn(0, 0, workItemCount);
        return;
    }

    const size_t chunkSize = (workItemCount + workerCount - 1) / workerCount;
    std::vector<std::future<void>> futures;
    futures.reserve(workerCount - 1);

    for (size_t workerIndex = 1; workerIndex < workerCount; ++workerIndex) {
        const size_t begin = workerIndex * chunkSize;
        const size_t end = std::min(begin + chunkSize, workItemCount);
        if (begin >= end) {
            break;
        }

        futures.push_back(std::async(
            std::launch::async, [begin, end, workerIndex, &fn]() { fn(workerIndex, begin, end); }));
    }

    fn(0, 0, std::min(chunkSize, workItemCount));

    for (auto& future : futures) {
        future.get();
    }
}

struct FillPreviewMetrics {
    float maxRadius = 0.0f;
    int pixelCount = 0;
};

struct FillPreviewRadiusRange {
    float minRadius = std::numeric_limits<float>::max();
    float maxRadius = 0.0f;
    int pixelCount = 0;
};

template <typename RawTileMap>
FillPreviewRadiusRange computeFillPreviewRadiusRange(
    const RawTileMap& maskTiles, const aether::Vector2& origin)
{
    FillPreviewRadiusRange range;

    for (const auto& [key, raw] : maskTiles) {
        if (raw.size() != aether::TILE_BYTE_SIZE) {
            continue;
        }

        const float tileOriginX = static_cast<float>(key.x) * static_cast<float>(aether::TILE_SIZE);
        const float tileOriginY = static_cast<float>(key.y) * static_cast<float>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const uint32_t idx = (localY * aether::TILE_SIZE + localX) * aether::TILE_CHANNELS;
                if (raw[idx + 3] == 0) {
                    continue;
                }

                const float worldX = tileOriginX + static_cast<float>(localX) + 0.5f;
                const float worldY = tileOriginY + static_cast<float>(localY) + 0.5f;
                const float dx = worldX - origin.x;
                const float dy = worldY - origin.y;
                const float distanceToOrigin = std::sqrt(dx * dx + dy * dy);
                range.minRadius = std::min(range.minRadius, distanceToOrigin);
                range.maxRadius = std::max(range.maxRadius, distanceToOrigin);
                ++range.pixelCount;
            }
        }
    }

    if (range.pixelCount <= 0) {
        range.minRadius = 0.0f;
        range.maxRadius = 0.0f;
        return range;
    }

    constexpr float kPixelCornerPadding = 0.71f;
    range.maxRadius += kPixelCornerPadding;
    range.minRadius = std::max(0.0f, range.minRadius - kPixelCornerPadding);
    return range;
}

bool progressiveWithinCanvas(int x, int y, int canvasW, int canvasH);
bool progressiveMaskHasPixel(const FloodFillResult::RawTileMap& maskTiles, int x, int y);
bool canFillProgressivePixel(const FloodFillResult::RawTileMap& sourceTiles,
    const FloodFillResult::RawTileMap& selectionMaskTiles,
    const FloodFillResult::RawTileMap& filledMaskTiles, const PremultPixel& seedPixel,
    OpenGLCanvasWidget::FillAlgorithm algorithm, int x, int y, int canvasW, int canvasH,
    TilePixelFormat contentFormat = kDefaultTileFormat);
bool canApproxFillPixel(const TileGrid* sourceGrid, const TileGrid* selectionMask,
    const PremultPixel& seedPixel, OpenGLCanvasWidget::FillAlgorithm algorithm, int x, int y,
    int canvasW, int canvasH);
float estimateFillRadiusFromSeed(const TileGrid* sourceGrid, const TileGrid* selectionMask,
    OpenGLCanvasWidget::FillAlgorithm algorithm, int seedX, int seedY, int canvasW, int canvasH,
    float radiusLimit);
void showFillRadiusLimitPopup(
    QWidget* context, OpenGLCanvasWidget::FillAlgorithm algorithm, float estimatedRadius);
void writeProgressiveFillPixel(const FloodFillResult::RawTileMap& sourceTiles,
    FloodFillResult& result, int x, int y, uint8_t fillR, uint8_t fillG, uint8_t fillB,
    uint8_t fillA, bool preserveSourceEdge, TilePixelFormat contentFormat = kDefaultTileFormat);
bool progressiveBoundsFromKeys(const std::unordered_set<TileKey, TileKeyHash>& keys, int& minTileX,
    int& minTileY, int& maxTileX, int& maxTileY);
bool progressiveBoundsFromKeys(
    const std::vector<TileKey>& keys, int& minTileX, int& minTileY, int& maxTileX, int& maxTileY);
float fillPreviewTileStartRadius(const std::vector<TileKey>& keys, const Vector2& origin);
FillPreviewMetrics computeFillPreviewMetrics(const TileGrid& maskGrid, const Vector2& origin);
FillPreviewRadiusRange computeFillPreviewRadiusRange(
    const TileGrid& maskGrid, const Vector2& origin);
int fillPreviewDurationMs(const FillPreviewMetrics& metrics);
float fillPreviewFeatherPx(const FillPreviewMetrics& metrics);
float fillPreviewRevealEase(float t);
void appendStepTouchedTileKeys(float fromX, float fromY, float toX, float toY, float radius,
    uint32_t canvasWidth, uint32_t canvasHeight, std::unordered_set<TileKey, TileKeyHash>& outKeys);

} // namespace aether

#endif // AETHER_FILL_PROGRESSIVE_POLICY_H
