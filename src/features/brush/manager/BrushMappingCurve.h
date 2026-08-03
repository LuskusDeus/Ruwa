// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHMAPPINGCURVE_H
#define RUWA_CORE_BRUSHES_BRUSHMAPPINGCURVE_H

#include "features/brush/manager/BrushDynamicsTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ruwa::core::brushes {

/// Data model of a parameter curve: the shared representation used by the brush
/// engine and by every curve editor UI. Points are sorted by x, x is always the
/// normalized input (0-1) and y lives in the value domain of the bound setting
/// and blend mode.
struct BrushMappingPoint {
    float x = 0.0f;
    float y = 1.0f;
    float smoothness = 1.0f;
};

struct BrushMappingCurve {
    std::vector<BrushMappingPoint> points;

    bool empty() const { return points.empty(); }

    void sortByX()
    {
        std::sort(points.begin(), points.end(),
            [](const BrushMappingPoint& a, const BrushMappingPoint& b) { return a.x < b.x; });
    }

    void normalize(BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None,
        BrushDynamicsBlendMode mode = BrushDynamicsBlendMode::Multiply)
    {
        for (auto& point : points) {
            point.x = clamp01(point.x);
            point.y = clampBrushDynamicsBindingValue(setting, mode, point.y);
            point.smoothness = clamp01(point.smoothness);
        }
        sortByX();
    }

    /// Raw curve value, without the setting/mode value clamp. Editors that draw
    /// the curve in their own value range use this.
    float evaluateUnclamped(float inputValue, float fallback = 1.0f) const
    {
        if (points.empty()) {
            return fallback;
        }
        if (points.size() == 1) {
            return points.front().y;
        }

        const float boundedInput = clamp01(inputValue);
        if (boundedInput <= points.front().x) {
            return points.front().y;
        }
        if (boundedInput >= points.back().x) {
            return points.back().y;
        }

        for (std::size_t i = 1; i < points.size(); ++i) {
            const BrushMappingPoint& p0 = points[i - 1];
            const BrushMappingPoint& p1 = points[i];
            if (boundedInput > p1.x) {
                continue;
            }

            const float dx = std::max(0.0001f, p1.x - p0.x);
            const float t = (boundedInput - p0.x) / dx;
            return evaluateSegment(i - 1, t);
        }

        return points.back().y;
    }

    float evaluate(float inputValue, float fallback = 1.0f,
        BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None,
        BrushDynamicsBlendMode mode = BrushDynamicsBlendMode::Multiply) const
    {
        return clampBrushDynamicsBindingValue(
            setting, mode, evaluateUnclamped(inputValue, fallback));
    }

    /// Value of the segment starting at `index`, parametrized by t in [0,1].
    float evaluateSegment(std::size_t index, float t) const
    {
        const BrushMappingPoint& p0 = points[index];
        const BrushMappingPoint& p1 = points[index + 1];
        const float dx = std::max(0.0001f, p1.x - p0.x);
        const float startTangent = pchipTangent(index);
        const float endTangent = pchipTangent(index + 1);

        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = (2.0f * t3) - (3.0f * t2) + 1.0f;
        const float h10 = t3 - (2.0f * t2) + t;
        const float h01 = (-2.0f * t3) + (3.0f * t2);
        const float h11 = t3 - t2;
        return h00 * p0.y + h10 * dx * startTangent + h01 * p1.y + h11 * dx * endTangent;
    }

private:
    float pchipTangent(std::size_t index) const
    {
        if (points.size() <= 2) {
            return simpleSlope(0, 1);
        }
        if (index == 0) {
            return endpointTangent(0);
        }
        if (index >= points.size() - 1) {
            return endpointTangent(points.size() - 1);
        }

        const float leftSlope = simpleSlope(index - 1, index);
        const float rightSlope = simpleSlope(index, index + 1);
        if (slopeSign(leftSlope) != slopeSign(rightSlope)) {
            return 0.0f;
        }
        if (slopeSign(leftSlope) == 0) {
            return 0.0f;
        }

        const float leftDx = segmentWidth(index - 1, index);
        const float rightDx = segmentWidth(index, index + 1);
        const float w1 = 2.0f * rightDx + leftDx;
        const float w2 = rightDx + 2.0f * leftDx;
        return (w1 + w2) / ((w1 / leftSlope) + (w2 / rightSlope));
    }

    float endpointTangent(std::size_t index) const
    {
        const bool leftEndpoint = index == 0;
        const std::size_t edgeIndex = leftEndpoint ? 0 : points.size() - 2;
        const std::size_t nextIndex = leftEndpoint ? 1 : points.size() - 3;
        const float edgeDx = segmentWidth(edgeIndex, edgeIndex + 1);
        const float nextDx = segmentWidth(nextIndex, nextIndex + 1);
        const float edgeSlope = simpleSlope(edgeIndex, edgeIndex + 1);
        const float nextSlope = simpleSlope(nextIndex, nextIndex + 1);
        const float denominator = std::max(0.0001f, edgeDx + nextDx);
        float tangent = ((2.0f * edgeDx + nextDx) * edgeSlope - edgeDx * nextSlope) / denominator;

        if (slopeSign(tangent) != slopeSign(edgeSlope)) {
            return 0.0f;
        }
        if (slopeSign(edgeSlope) != slopeSign(nextSlope)
            && std::abs(tangent) > std::abs(3.0f * edgeSlope)) {
            return 3.0f * edgeSlope;
        }
        return tangent;
    }

    float simpleSlope(std::size_t leftIndex, std::size_t rightIndex) const
    {
        const BrushMappingPoint& left = points[leftIndex];
        const BrushMappingPoint& right = points[rightIndex];
        return (right.y - left.y) / segmentWidth(leftIndex, rightIndex);
    }

    float segmentWidth(std::size_t leftIndex, std::size_t rightIndex) const
    {
        return std::max(0.0001f, points[rightIndex].x - points[leftIndex].x);
    }

    int slopeSign(float value) const
    {
        if (std::abs(value) <= 0.000001f) {
            return 0;
        }
        return value > 0.0f ? 1 : -1;
    }
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHMAPPINGCURVE_H
