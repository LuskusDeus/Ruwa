// SPDX-License-Identifier: MPL-2.0

#include "features/fill/FillProgressivePolicy.h"

#include "shell/top-bar/MessagePopupManager.h"

#include <QCoreApplication>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace aether {

namespace {

constexpr int kProgressiveFillTolerance = 6;
constexpr float kFillRadiusProbeStepPx = 96.0f;
constexpr int kFillRadiusProbeDirections = 16;

} // namespace

aether::PremultPixel progressiveCompositeOver(
    const aether::PremultPixel& src, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    const int invSrcA = 255 - static_cast<int>(src.a);
    aether::PremultPixel out;
    out.r = static_cast<uint8_t>(std::clamp(
        static_cast<int>(src.r) + ((static_cast<int>(fillR) * invSrcA + 127) / 255), 0, 255));
    out.g = static_cast<uint8_t>(std::clamp(
        static_cast<int>(src.g) + ((static_cast<int>(fillG) * invSrcA + 127) / 255), 0, 255));
    out.b = static_cast<uint8_t>(std::clamp(
        static_cast<int>(src.b) + ((static_cast<int>(fillB) * invSrcA + 127) / 255), 0, 255));
    out.a = static_cast<uint8_t>(std::clamp(
        static_cast<int>(src.a) + ((static_cast<int>(fillA) * invSrcA + 127) / 255), 0, 255));
    return out;
}

bool progressiveWithinCanvas(int x, int y, int canvasW, int canvasH)
{
    return x >= 0 && y >= 0 && x < canvasW && y < canvasH;
}

bool progressiveMaskHasPixel(const aether::FloodFillResult::RawTileMap& maskTiles, int x, int y)
{
    // Coverage masks are RGBA8.
    return aether::sampleRawAlpha(maskTiles, x, y, aether::TilePixelFormat::RGBA8) != 0;
}

bool canFillProgressivePixel(const aether::FloodFillResult::RawTileMap& sourceTiles,
    const aether::FloodFillResult::RawTileMap& selectionMaskTiles,
    const aether::FloodFillResult::RawTileMap& filledMaskTiles,
    const aether::PremultPixel& seedPixel, aether::OpenGLCanvasWidget::FillAlgorithm algorithm,
    int x, int y, int canvasW, int canvasH, aether::TilePixelFormat contentFormat)
{
    if (!progressiveWithinCanvas(x, y, canvasW, canvasH)) {
        return false;
    }
    if (progressiveMaskHasPixel(filledMaskTiles, x, y)) {
        return false;
    }
    // Selection mask is RGBA8; source tiles are CONTENT (contentFormat).
    if (!selectionMaskTiles.empty()
        && aether::sampleRawAlpha(selectionMaskTiles, x, y, aether::TilePixelFormat::RGBA8) == 0) {
        return false;
    }
    if (algorithm == aether::OpenGLCanvasWidget::FillAlgorithm::Classic) {
        const aether::PremultPixel px = aether::sampleRawPixel(sourceTiles, x, y, contentFormat);
        return px.r == seedPixel.r && px.g == seedPixel.g && px.b == seedPixel.b
            && px.a == seedPixel.a;
    }
    if (seedPixel.a > 0 && aether::sampleRawAlpha(sourceTiles, x, y, contentFormat) == 0) {
        return false;
    }

    return aether::progressivePixelDistance(
               aether::sampleRawPixel(sourceTiles, x, y, contentFormat), seedPixel)
        <= kProgressiveFillTolerance;
}

bool canApproxFillPixel(const aether::TileGrid* sourceGrid, const aether::TileGrid* selectionMask,
    const aether::PremultPixel& seedPixel, aether::OpenGLCanvasWidget::FillAlgorithm algorithm,
    int x, int y, int canvasW, int canvasH)
{
    if (!sourceGrid || x < 0 || y < 0 || x >= canvasW || y >= canvasH) {
        return false;
    }
    if (selectionMask && fillMaskAlphaAt(selectionMask, x, y) == 0) {
        return false;
    }

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    if (!samplePixelAt(sourceGrid, x, y, r, g, b, a)) {
        return false;
    }

    const aether::PremultPixel pixel { r, g, b, a };
    if (algorithm == aether::OpenGLCanvasWidget::FillAlgorithm::Classic) {
        return pixel.r == seedPixel.r && pixel.g == seedPixel.g && pixel.b == seedPixel.b
            && pixel.a == seedPixel.a;
    }
    if (seedPixel.a > 0 && pixel.a == 0) {
        return false;
    }
    return aether::progressivePixelDistance(pixel, seedPixel) <= kProgressiveFillTolerance;
}

float estimateFillRadiusFromSeed(const aether::TileGrid* sourceGrid,
    const aether::TileGrid* selectionMask, aether::OpenGLCanvasWidget::FillAlgorithm algorithm,
    int seedX, int seedY, int canvasW, int canvasH, float radiusLimit)
{
    if (!sourceGrid || radiusLimit <= 0.0f) {
        return 0.0f;
    }

    uint8_t seedR = 0;
    uint8_t seedG = 0;
    uint8_t seedB = 0;
    uint8_t seedA = 0;
    if (!samplePixelAt(sourceGrid, seedX, seedY, seedR, seedG, seedB, seedA)) {
        return 0.0f;
    }

    const aether::PremultPixel seedPixel { seedR, seedG, seedB, seedA };
    if (!canApproxFillPixel(
            sourceGrid, selectionMask, seedPixel, algorithm, seedX, seedY, canvasW, canvasH)) {
        return 0.0f;
    }

    constexpr float kPi = 3.14159265358979323846f;
    float maxRadius = 0.0f;

    for (int dirIndex = 0; dirIndex < kFillRadiusProbeDirections; ++dirIndex) {
        const float angle = (2.0f * kPi * static_cast<float>(dirIndex))
            / static_cast<float>(kFillRadiusProbeDirections);
        const float dirX = std::cos(angle);
        const float dirY = std::sin(angle);

        float distance = 0.0f;
        while (distance < radiusLimit) {
            distance += kFillRadiusProbeStepPx;
            const int sampleX
                = static_cast<int>(std::lround(static_cast<float>(seedX) + dirX * distance));
            const int sampleY
                = static_cast<int>(std::lround(static_cast<float>(seedY) + dirY * distance));
            if (!canApproxFillPixel(sourceGrid, selectionMask, seedPixel, algorithm, sampleX,
                    sampleY, canvasW, canvasH)) {
                break;
            }
            maxRadius = std::max(maxRadius, distance);
            if (maxRadius >= radiusLimit) {
                return maxRadius;
            }
        }
    }

    return maxRadius;
}

void showFillRadiusLimitPopup(
    QWidget* context, aether::OpenGLCanvasWidget::FillAlgorithm algorithm, float estimatedRadius)
{
    if (!context) {
        return;
    }

    const int roundedRadius = static_cast<int>(std::round(std::max(estimatedRadius, 0.0f)));
    QString message;
    if (algorithm == aether::OpenGLCanvasWidget::FillAlgorithm::Smart) {
        message = QCoreApplication::translate("OpenGLCanvasWidget",
            "Smart Fill is blocked for very large regions.\n"
            "Estimated radius: about %1 px (limit: 3000 px).\n"
            "Try Square Selection first, or use Classic Fill for this area.")
                      .arg(roundedRadius);
    } else {
        message = QCoreApplication::translate("OpenGLCanvasWidget",
            "Classic Fill is blocked for extremely large regions.\n"
            "Estimated radius: about %1 px (limit: 8000 px).\n"
            "Try Square Selection first to restrict the area.")
                      .arg(roundedRadius);
    }

    ruwa::ui::widgets::MessagePopupManager::show(context, message,
        { { QCoreApplication::translate("OpenGLCanvasWidget", "OK"), true, []() { } } }, 420);
}

void writeProgressiveFillPixel(const aether::FloodFillResult::RawTileMap& sourceTiles,
    aether::FloodFillResult& result, int x, int y, uint8_t fillR, uint8_t fillG, uint8_t fillB,
    uint8_t fillA, bool preserveSourceEdge, aether::TilePixelFormat contentFormat)
{
    const aether::TileKey key { x / static_cast<int>(aether::TILE_SIZE),
        y / static_cast<int>(aether::TILE_SIZE) };
    const uint32_t localX = static_cast<uint32_t>(x % static_cast<int>(aether::TILE_SIZE));
    const uint32_t localY = static_cast<uint32_t>(y % static_cast<int>(aether::TILE_SIZE));

    std::vector<uint8_t>& resultTile
        = aether::ensureResultTile(sourceTiles, result, key, contentFormat);
    aether::PremultPixel out;
    if (preserveSourceEdge) {
        const aether::PremultPixel src = aether::sampleRawPixel(sourceTiles, x, y, contentFormat);
        out = progressiveCompositeOver(src, fillR, fillG, fillB, fillA);
    } else {
        out = aether::PremultPixel { fillR, fillG, fillB, fillA };
    }
    aether::setRawPixel(resultTile, localX, localY, out.r, out.g, out.b, out.a, contentFormat);
    aether::writeMaskPixel(result.fillMaskTiles, key, localX, localY);
    ++result.pixelsFilled;
}

bool progressiveBoundsFromKeys(const std::unordered_set<aether::TileKey, aether::TileKeyHash>& keys,
    int& minTileX, int& minTileY, int& maxTileX, int& maxTileY)
{
    if (keys.empty()) {
        return false;
    }

    minTileX = std::numeric_limits<int>::max();
    minTileY = std::numeric_limits<int>::max();
    maxTileX = std::numeric_limits<int>::min();
    maxTileY = std::numeric_limits<int>::min();
    for (const aether::TileKey& key : keys) {
        minTileX = std::min(minTileX, key.x);
        minTileY = std::min(minTileY, key.y);
        maxTileX = std::max(maxTileX, key.x);
        maxTileY = std::max(maxTileY, key.y);
    }
    return true;
}

bool progressiveBoundsFromKeys(const std::vector<aether::TileKey>& keys, int& minTileX,
    int& minTileY, int& maxTileX, int& maxTileY)
{
    if (keys.empty()) {
        return false;
    }

    minTileX = std::numeric_limits<int>::max();
    minTileY = std::numeric_limits<int>::max();
    maxTileX = std::numeric_limits<int>::min();
    maxTileY = std::numeric_limits<int>::min();
    for (const aether::TileKey& key : keys) {
        minTileX = std::min(minTileX, key.x);
        minTileY = std::min(minTileY, key.y);
        maxTileX = std::max(maxTileX, key.x);
        maxTileY = std::max(maxTileY, key.y);
    }
    return true;
}

float fillPreviewTileStartRadius(
    const std::vector<aether::TileKey>& keys, const aether::Vector2& origin)
{
    if (keys.empty()) {
        return 0.0f;
    }

    float minDistanceSq = std::numeric_limits<float>::max();
    for (const aether::TileKey& key : keys) {
        const float tileMinX = static_cast<float>(key.x) * static_cast<float>(aether::TILE_SIZE);
        const float tileMinY = static_cast<float>(key.y) * static_cast<float>(aether::TILE_SIZE);
        const float tileMaxX = tileMinX + static_cast<float>(aether::TILE_SIZE);
        const float tileMaxY = tileMinY + static_cast<float>(aether::TILE_SIZE);

        const float clampedX = std::clamp(origin.x, tileMinX, tileMaxX);
        const float clampedY = std::clamp(origin.y, tileMinY, tileMaxY);
        const float dx = clampedX - origin.x;
        const float dy = clampedY - origin.y;
        minDistanceSq = std::min(minDistanceSq, dx * dx + dy * dy);
    }

    if (!std::isfinite(minDistanceSq) || minDistanceSq <= 0.0f) {
        return 0.0f;
    }
    return std::sqrt(minDistanceSq);
}

FillPreviewMetrics computeFillPreviewMetrics(
    const aether::TileGrid& maskGrid, const aether::Vector2& origin)
{
    FillPreviewMetrics metrics;
    float maxDistanceSq = 0.0f;

    for (const auto& [key, tile] : maskGrid.tiles()) {
        const uint8_t* px = tile.pixels();
        const float tileOriginX = static_cast<float>(key.x) * static_cast<float>(aether::TILE_SIZE);
        const float tileOriginY = static_cast<float>(key.y) * static_cast<float>(aether::TILE_SIZE);

        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const uint32_t idx = (localY * aether::TILE_SIZE + localX) * aether::TILE_CHANNELS;
                if (px[idx + 3] == 0) {
                    continue;
                }

                const float worldX = tileOriginX + static_cast<float>(localX) + 0.5f;
                const float worldY = tileOriginY + static_cast<float>(localY) + 0.5f;
                const float dx = worldX - origin.x;
                const float dy = worldY - origin.y;
                maxDistanceSq = std::max(maxDistanceSq, dx * dx + dy * dy);
                ++metrics.pixelCount;
            }
        }
    }

    if (metrics.pixelCount == 0) {
        return metrics;
    }

    constexpr float kPixelCornerPadding = 0.71f;
    metrics.maxRadius = std::sqrt(maxDistanceSq) + kPixelCornerPadding;
    return metrics;
}

FillPreviewRadiusRange computeFillPreviewRadiusRange(
    const aether::TileGrid& maskGrid, const aether::Vector2& origin)
{
    FillPreviewRadiusRange range;

    for (const auto& [key, tile] : maskGrid.tiles()) {
        const uint8_t* raw = tile.pixels();
        if (!raw) {
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

int fillPreviewDurationMs(const FillPreviewMetrics& metrics)
{
    if (metrics.pixelCount <= 0) {
        return 0;
    }

    const float radiusNorm = std::clamp(metrics.maxRadius / 1400.0f, 0.0f, 1.0f);
    const float areaNorm
        = std::clamp(static_cast<float>(metrics.pixelCount) / 220000.0f, 0.0f, 1.0f);
    const float curvedRadius = std::sqrt(radiusNorm);
    const float curvedArea = std::sqrt(areaNorm);
    const float duration = 150.0f + curvedRadius * 210.0f + curvedArea * 165.0f;
    return static_cast<int>(std::round(std::clamp(duration, 160.0f, 520.0f)));
}

float fillPreviewFeatherPx(const FillPreviewMetrics& metrics)
{
    if (metrics.pixelCount <= 0) {
        return 0.0f;
    }

    const float feather = 8.0f + std::sqrt(std::max(metrics.maxRadius, 0.0f)) * 1.35f;
    return std::clamp(feather, 10.0f, 28.0f);
}

float fillPreviewRevealEase(float t)
{
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const float smooth = clamped * clamped * clamped * (clamped * (clamped * 6.0f - 15.0f) + 10.0f);
    const float outCubic = 1.0f - std::pow(1.0f - clamped, 3.0f);
    return outCubic + (smooth - outCubic) * 0.35f;
}

void appendStepTouchedTileKeys(float fromX, float fromY, float toX, float toY, float radius,
    uint32_t canvasWidth, uint32_t canvasHeight,
    std::unordered_set<aether::TileKey, aether::TileKeyHash>& outKeys)
{
    if (radius <= 0.0f)
        return;

    float minWorldX = std::min(fromX, toX) - radius;
    float minWorldY = std::min(fromY, toY) - radius;
    float maxWorldX = std::max(fromX, toX) + radius;
    float maxWorldY = std::max(fromY, toY) + radius;

    int32_t tMinX = static_cast<int32_t>(std::floor(minWorldX / aether::TILE_SIZE));
    int32_t tMinY = static_cast<int32_t>(std::floor(minWorldY / aether::TILE_SIZE));
    int32_t tMaxX = static_cast<int32_t>(std::floor(maxWorldX / aether::TILE_SIZE));
    int32_t tMaxY = static_cast<int32_t>(std::floor(maxWorldY / aether::TILE_SIZE));

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    if (clipToCanvas) {
        const int32_t canvasMinTile = 0;
        const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / aether::TILE_SIZE);
        const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / aether::TILE_SIZE);

        tMinX = std::max(tMinX, canvasMinTile);
        tMinY = std::max(tMinY, canvasMinTile);
        tMaxX = std::min(tMaxX, canvasMaxX);
        tMaxY = std::min(tMaxY, canvasMaxY);

        if (tMinX > tMaxX || tMinY > tMaxY)
            return;
    }

    for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
        for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
            outKeys.insert(aether::TileKey { tx, ty });
        }
    }
}

} // namespace aether
