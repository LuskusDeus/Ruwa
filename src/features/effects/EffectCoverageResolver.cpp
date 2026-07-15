// SPDX-License-Identifier: MPL-2.0

#include "features/effects/EffectCoverageResolver.h"

#include "features/effects/LayerEffectRegistry.h"
#include "shared/tiles/TileTypes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ruwa::core::effects {

namespace {

constexpr float kPi = 3.14159265358979323846f;

int floorDiv(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

bool valueInRange(float value, float minValue, float maxValue)
{
    return value >= minValue && value <= maxValue;
}

bool segmentIntersectsRect(
    float x0, float y0, float x1, float y1, float minX, float minY, float maxX, float maxY)
{
    if ((valueInRange(x0, minX, maxX) && valueInRange(y0, minY, maxY))
        || (valueInRange(x1, minX, maxX) && valueInRange(y1, minY, maxY))) {
        return true;
    }

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float t0 = 0.0f;
    float t1 = 1.0f;
    const auto clip = [&](float p, float q) {
        if (p == 0.0f) {
            return q >= 0.0f;
        }
        const float r = q / p;
        if (p < 0.0f) {
            if (r > t1) {
                return false;
            }
            t0 = std::max(t0, r);
        } else {
            if (r < t0) {
                return false;
            }
            t1 = std::min(t1, r);
        }
        return true;
    };

    return clip(-dx, x0 - minX) && clip(dx, maxX - x0) && clip(-dy, y0 - minY)
        && clip(dy, maxY - y0);
}

} // namespace

EffectCoverageResolver::TileKeySet EffectCoverageResolver::expandedDocumentCoverage(
    const TileKeySet& sourceCoverage, const QList<LayerEffectState>& effects)
{
    TileKeySet coverage = sourceCoverage;
    expandDocumentCoverageInPlace(coverage, effects);
    return coverage;
}

void EffectCoverageResolver::expandDocumentCoverageInPlace(
    TileKeySet& coverage, const QList<LayerEffectState>& effects)
{
    for (const LayerEffectState& effect : effects) {
        if (!effect.enabled) {
            continue;
        }
        const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
        if (descriptor && descriptor->coverageResolver) {
            coverage = descriptor->coverageResolver(effect, coverage);
            continue;
        }
        expandByOffsets(coverage, coverageOffsets(effect));
    }
}

QList<EffectCoverageTileOffset> EffectCoverageResolver::rectangularCoverageOffsets(
    int leftPixels, int topPixels, int rightPixels, int bottomPixels)
{
    leftPixels = std::max(0, leftPixels);
    topPixels = std::max(0, topPixels);
    rightPixels = std::max(0, rightPixels);
    bottomPixels = std::max(0, bottomPixels);

    const int tileSize = static_cast<int>(aether::TILE_SIZE);
    const int minDx = floorDiv(-leftPixels, tileSize);
    const int maxDx = floorDiv(tileSize - 1 + rightPixels, tileSize);
    const int minDy = floorDiv(-topPixels, tileSize);
    const int maxDy = floorDiv(tileSize - 1 + bottomPixels, tileSize);

    QList<EffectCoverageTileOffset> offsets;
    offsets.reserve((maxDx - minDx + 1) * (maxDy - minDy + 1));
    for (int dy = minDy; dy <= maxDy; ++dy) {
        for (int dx = minDx; dx <= maxDx; ++dx) {
            if (dx != 0 || dy != 0) {
                offsets.append({ dx, dy });
            }
        }
    }
    return offsets;
}

QList<EffectCoverageTileOffset> EffectCoverageResolver::radiusCoverageOffsets(int radiusPixels)
{
    radiusPixels = std::max(0, radiusPixels);
    return rectangularCoverageOffsets(radiusPixels, radiusPixels, radiusPixels, radiusPixels);
}

QList<EffectCoverageTileOffset> EffectCoverageResolver::lineCoverageOffsets(
    float angleDegrees, int negativeReachPixels, int positiveReachPixels)
{
    negativeReachPixels = std::max(0, negativeReachPixels);
    positiveReachPixels = std::max(0, positiveReachPixels);
    if (negativeReachPixels == 0 && positiveReachPixels == 0) {
        return {};
    }

    const float angleRadians = angleDegrees * kPi / 180.0f;
    const float dirX = std::cos(angleRadians);
    const float dirY = std::sin(angleRadians);
    const float x0 = -dirX * static_cast<float>(negativeReachPixels);
    const float y0 = -dirY * static_cast<float>(negativeReachPixels);
    const float x1 = dirX * static_cast<float>(positiveReachPixels);
    const float y1 = dirY * static_cast<float>(positiveReachPixels);
    const float minSegmentX = std::min(x0, x1);
    const float maxSegmentX = std::max(x0, x1);
    const float minSegmentY = std::min(y0, y1);
    const float maxSegmentY = std::max(y0, y1);

    const int tileSize = static_cast<int>(aether::TILE_SIZE);
    const int minDx = floorDiv(static_cast<int>(std::floor(-maxSegmentX)), tileSize) - 1;
    const int maxDx = floorDiv(static_cast<int>(std::ceil(tileSize - minSegmentX)), tileSize) + 1;
    const int minDy = floorDiv(static_cast<int>(std::floor(-maxSegmentY)), tileSize) - 1;
    const int maxDy = floorDiv(static_cast<int>(std::ceil(tileSize - minSegmentY)), tileSize) + 1;

    QList<EffectCoverageTileOffset> offsets;
    for (int dy = minDy; dy <= maxDy; ++dy) {
        for (int dx = minDx; dx <= maxDx; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }

            const float minX = -static_cast<float>(dx + 1) * tileSize + 1.0f;
            const float maxX = static_cast<float>(1 - dx) * tileSize - 1.0f;
            const float minY = -static_cast<float>(dy + 1) * tileSize + 1.0f;
            const float maxY = static_cast<float>(1 - dy) * tileSize - 1.0f;
            if (segmentIntersectsRect(x0, y0, x1, y1, minX, minY, maxX, maxY)) {
                offsets.append({ dx, dy });
            }
        }
    }
    return offsets;
}

int EffectCoverageResolver::tileExpansionRadius(const LayerEffectState& effect)
{
    const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
    if (!descriptor || !descriptor->capabilities.expandsBounds) {
        return 0;
    }
    // Prefer the pixel-space radius (rounded up to whole tiles); fall back to a
    // legacy tile-granularity callback for effects that predate it.
    if (descriptor->pixelExpansionRadius) {
        const int pixels = std::max(0, descriptor->pixelExpansionRadius(effect));
        return (pixels + static_cast<int>(aether::TILE_SIZE) - 1)
            / static_cast<int>(aether::TILE_SIZE);
    }
    if (descriptor->tileExpansionRadius) {
        return std::max(0, descriptor->tileExpansionRadius(effect));
    }
    return 0;
}

int EffectCoverageResolver::effectPixelExpansion(const LayerEffectState& effect)
{
    const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
    if (!descriptor || !descriptor->pixelExpansionRadius) {
        return 0;
    }
    return std::max(0, descriptor->pixelExpansionRadius(effect));
}

int EffectCoverageResolver::neighborhoodPadPixels(
    const QList<LayerEffectState>& effects, bool realtimeOnly)
{
    int pad = 0;
    for (const LayerEffectState& effect : effects) {
        if (!effect.enabled) {
            continue;
        }
        if (realtimeOnly && !effect.realtimePreviewEnabled) {
            continue;
        }
        const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
        if (!descriptor || !descriptor->capabilities.requiresNeighborTiles) {
            continue;
        }
        pad += effectPixelExpansion(effect);
    }
    return pad;
}

int EffectCoverageResolver::stableLiveEditNeighborhoodPadPixels(
    const QList<LayerEffectState>& effects, const QUuid& editedEffectId,
    const QString& editedParamKey, bool realtimeOnly)
{
    const int currentPad = neighborhoodPadPixels(effects, realtimeOnly);
    if (editedEffectId.isNull() || editedParamKey.isEmpty()) {
        return currentPad;
    }

    int editedIndex = -1;
    for (int i = 0; i < effects.size(); ++i) {
        if (effects.at(i).instanceId == editedEffectId) {
            editedIndex = i;
            break;
        }
    }
    // There is no prefix to cache when the edited effect is first in the chain.
    if (editedIndex <= 0) {
        return currentPad;
    }

    const LayerEffectState& effect = effects.at(editedIndex);
    if (!effect.enabled || (realtimeOnly && !effect.realtimePreviewEnabled)) {
        return currentPad;
    }
    const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
    if (!descriptor || !descriptor->capabilities.requiresNeighborTiles
        || !descriptor->pixelExpansionRadius) {
        return currentPad;
    }

    const auto paramIt = std::find_if(descriptor->params.cbegin(), descriptor->params.cend(),
        [&](const EffectParamDefinition& param) { return param.key == editedParamKey; });
    if (paramIt == descriptor->params.cend()
        || (paramIt->type != EffectParamType::Int && paramIt->type != EffectParamType::Real)
        || !paramIt->minimumValue.isValid() || !paramIt->maximumValue.isValid()) {
        return currentPad;
    }

    const int currentReach = effectPixelExpansion(effect);
    int maximumReach = currentReach;
    LayerEffectState endpointState = effect;
    endpointState.params.insert(editedParamKey, paramIt->minimumValue);
    maximumReach = std::max(maximumReach, effectPixelExpansion(endpointState));
    endpointState.params.insert(editedParamKey, paramIt->maximumValue);
    maximumReach = std::max(maximumReach, effectPixelExpansion(endpointState));
    return currentPad - currentReach + maximumReach;
}

EffectCoverageResolver::TileKeySet EffectCoverageResolver::coverageUnion(
    const TileKeySet& before, const TileKeySet& after)
{
    TileKeySet result = before;
    result.insert(after.begin(), after.end());
    return result;
}

QList<EffectCoverageTileOffset> EffectCoverageResolver::coverageOffsets(
    const LayerEffectState& effect)
{
    const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
    if (!descriptor || !descriptor->capabilities.expandsBounds) {
        return {};
    }
    if (descriptor->coverageTileOffsets) {
        return descriptor->coverageTileOffsets(effect);
    }
    if (descriptor->pixelExpansionRadius) {
        return radiusCoverageOffsets(descriptor->pixelExpansionRadius(effect));
    }
    if (descriptor->tileExpansionRadius) {
        const int radius = std::max(0, descriptor->tileExpansionRadius(effect));
        QList<EffectCoverageTileOffset> offsets;
        offsets.reserve((2 * radius + 1) * (2 * radius + 1) - 1);
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx != 0 || dy != 0) {
                    offsets.append({ dx, dy });
                }
            }
        }
        return offsets;
    }
    return {};
}

void EffectCoverageResolver::expandByOffsets(
    TileKeySet& coverage, const QList<EffectCoverageTileOffset>& offsets)
{
    if (offsets.empty() || coverage.empty()) {
        return;
    }

    TileKeySet expanded = coverage;
    for (const aether::TileKey& key : coverage) {
        for (const EffectCoverageTileOffset& offset : offsets) {
            expanded.insert(aether::TileKey { key.x + offset.dx, key.y + offset.dy });
        }
    }
    coverage = std::move(expanded);
}

} // namespace ruwa::core::effects
