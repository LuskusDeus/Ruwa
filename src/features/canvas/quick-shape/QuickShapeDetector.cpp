// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   Q U I C K   S H A P E   D E T E C T O R
// ==========================================================================

#include "features/canvas/quick-shape/QuickShapeDetector.h"
#include "shared/types/Types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aether {

using namespace QuickShapeConstants;
using ruwa::core::brushes::BrushStrokeReplayPoint;

namespace {

constexpr float kShapeClosedLoopMaxChordByAxis = 0.58f;
constexpr float kCornerPeakAngle = 0.72f;
constexpr float kCornerStrongAngle = 0.95f;
constexpr float kCirclePolygonCornerAngle = 1.08f;
constexpr float kCornerResampleSpacingFactor = 0.055f;
constexpr float kCornerMinResampleSpacing = 1.5f;
constexpr float kCornerMaxResampleSpacing = 8.0f;
constexpr size_t kCornerMinSampleCount = 12;
constexpr size_t kCornerMaxSampleCount = 192;
constexpr float kSquareVertexHitDistanceNorm = 0.48f;
constexpr float kSquareSideHitDistanceNorm = 0.22f;
constexpr float kTriangleScoreThreshold = 1.60f;
constexpr float kSquareScoreThreshold = 1.45f;
constexpr float kCircleScoreThreshold = 1.50f;
constexpr float kSquareRelaxedAspectRatio = 1.14f;
constexpr float kSquarePreferredOverTriangleAspectRatio = 1.18f;
constexpr float kSquarePreferredTriangleScoreMargin = 0.34f;

struct StrokeBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minAxis = 0.0f;
    float maxAxis = 0.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float aspectRatio = 1.0f;
};

struct CornerPeak {
    size_t index = 0;
    float angle = 0.0f;
};

struct StrokeShapeFeatures {
    StrokeBounds bounds;
    float straightness = 0.0f;
    float chordByAxis = 1.0f;
    bool closedLoopLike = false;
    std::vector<Vector2> samples;
    std::vector<CornerPeak> cornerPeaksByIndex;
    std::vector<CornerPeak> cornerPeaksByStrength;
    int prominentCornerCount = 0;
    int strongCornerCount = 0;
    float maxCornerAngle = 0.0f;
    float thirdCornerAngle = 0.0f;
    float fourthCornerAngle = 0.0f;
};

struct TriangleFitResult {
    QuickTriangleDirection direction = QuickTriangleDirection::Up;
    float avgDeviationNorm = std::numeric_limits<float>::max();
    float maxDeviationNorm = std::numeric_limits<float>::max();
    int vertexHitCount = 0;
};

struct SquareFitResult {
    float avgDeviationNorm = std::numeric_limits<float>::max();
    float maxDeviationNorm = std::numeric_limits<float>::max();
    int vertexHitCount = 0;
    int sideHitCount = 0;
};

struct ShapeCandidateScore {
    bool valid = false;
    float score = std::numeric_limits<float>::max();
    int topologyDistance = std::numeric_limits<int>::max();
    float centerX = 0.0f;
    float centerY = 0.0f;
    float squareHalfExtent = 0.0f;
    float triangleHalfWidth = 0.0f;
    float triangleHalfHeight = 0.0f;
    QuickTriangleDirection triangleDirection = QuickTriangleDirection::Up;
};

int circularSampleDistance(int a, int b, int count)
{
    if (count <= 0)
        return 0;
    const int diff = std::abs(a - b);
    return std::min(diff, count - diff);
}

StrokeBounds computeStrokeBounds(const std::vector<BrushStrokeReplayPoint>& dabs)
{
    StrokeBounds bounds;
    if (dabs.empty())
        return bounds;

    bounds.minX = bounds.maxX = dabs.front().worldX;
    bounds.minY = bounds.maxY = dabs.front().worldY;
    for (const auto& dab : dabs) {
        bounds.minX = std::min(bounds.minX, dab.worldX);
        bounds.maxX = std::max(bounds.maxX, dab.worldX);
        bounds.minY = std::min(bounds.minY, dab.worldY);
        bounds.maxY = std::max(bounds.maxY, dab.worldY);
    }

    bounds.width = std::max(0.0f, bounds.maxX - bounds.minX);
    bounds.height = std::max(0.0f, bounds.maxY - bounds.minY);
    bounds.minAxis = std::min(bounds.width, bounds.height);
    bounds.maxAxis = std::max(bounds.width, bounds.height);
    bounds.centerX = (bounds.minX + bounds.maxX) * 0.5f;
    bounds.centerY = (bounds.minY + bounds.maxY) * 0.5f;
    bounds.aspectRatio = bounds.minAxis > 0.001f ? (bounds.maxAxis / bounds.minAxis)
                                                 : std::numeric_limits<float>::max();
    return bounds;
}

std::vector<Vector2> resampleStroke(
    const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath, float scaleRef)
{
    std::vector<Vector2> samples;
    if (dabs.empty())
        return samples;

    if (dabs.size() == 1 || totalPath <= 0.001f) {
        samples.push_back({ dabs.front().worldX, dabs.front().worldY });
        return samples;
    }

    const float spacing = std::clamp(scaleRef * kCornerResampleSpacingFactor,
        kCornerMinResampleSpacing, kCornerMaxResampleSpacing);
    size_t targetCount = static_cast<size_t>(std::ceil(totalPath / std::max(0.001f, spacing))) + 1;
    targetCount = std::clamp(targetCount, kCornerMinSampleCount, kCornerMaxSampleCount);

    std::vector<float> cumulative(dabs.size(), 0.0f);
    for (size_t i = 1; i < dabs.size(); ++i) {
        const float dx = dabs[i].worldX - dabs[i - 1].worldX;
        const float dy = dabs[i].worldY - dabs[i - 1].worldY;
        cumulative[i] = cumulative[i - 1] + std::sqrt(dx * dx + dy * dy);
    }

    samples.reserve(targetCount);
    size_t seg = 0;
    for (size_t i = 0; i < targetCount; ++i) {
        const float dist = (targetCount <= 1)
            ? 0.0f
            : (totalPath * static_cast<float>(i) / static_cast<float>(targetCount - 1));
        while (seg + 1 < cumulative.size() && cumulative[seg + 1] < dist) {
            ++seg;
        }

        const size_t next = std::min(seg + 1, dabs.size() - 1);
        const float d0 = cumulative[seg];
        const float d1 = cumulative[next];
        const float denom = d1 - d0;
        const float t = (next == seg || std::abs(denom) <= 0.000001f)
            ? 0.0f
            : std::clamp((dist - d0) / denom, 0.0f, 1.0f);

        const float x = dabs[seg].worldX + (dabs[next].worldX - dabs[seg].worldX) * t;
        const float y = dabs[seg].worldY + (dabs[next].worldY - dabs[seg].worldY) * t;
        samples.push_back({ x, y });
    }

    return samples;
}

std::vector<CornerPeak> detectCornerPeaks(const std::vector<Vector2>& samples, bool closedLoopLike)
{
    std::vector<CornerPeak> peaks;
    const int count = static_cast<int>(samples.size());
    if (count < 8)
        return peaks;

    const int window = std::clamp(count / 18, 2, 6);
    const int start = closedLoopLike ? 0 : window;
    const int end = closedLoopLike ? (count - 1) : (count - window - 1);
    if (end < start)
        return peaks;

    auto sampleAt = [&](int index) -> const Vector2& {
        if (closedLoopLike) {
            index %= count;
            if (index < 0)
                index += count;
            return samples[static_cast<size_t>(index)];
        }
        index = std::clamp(index, 0, count - 1);
        return samples[static_cast<size_t>(index)];
    };

    std::vector<float> angles(static_cast<size_t>(count), 0.0f);
    for (int i = start; i <= end; ++i) {
        const Vector2 prev = sampleAt(i - window);
        const Vector2 curr = sampleAt(i);
        const Vector2 next = sampleAt(i + window);

        const Vector2 v0 { curr.x - prev.x, curr.y - prev.y };
        const Vector2 v1 { next.x - curr.x, next.y - curr.y };
        const float len0 = std::sqrt(v0.x * v0.x + v0.y * v0.y);
        const float len1 = std::sqrt(v1.x * v1.x + v1.y * v1.y);
        if (len0 <= 0.0001f || len1 <= 0.0001f)
            continue;

        const float dot = std::clamp((v0.x * v1.x + v0.y * v1.y) / (len0 * len1), -1.0f, 1.0f);
        angles[static_cast<size_t>(i)] = std::acos(dot);
    }

    std::vector<CornerPeak> raw;
    raw.reserve(static_cast<size_t>(count / 2));
    for (int i = start; i <= end; ++i) {
        const float angle = angles[static_cast<size_t>(i)];
        if (angle < kCornerPeakAngle)
            continue;

        const int prevIndex = closedLoopLike ? ((i - 1 + count) % count) : (i - 1);
        const int nextIndex = closedLoopLike ? ((i + 1) % count) : (i + 1);
        if (!closedLoopLike && (prevIndex < start || nextIndex > end))
            continue;

        const float prevAngle = angles[static_cast<size_t>(prevIndex)];
        const float nextAngle = angles[static_cast<size_t>(nextIndex)];
        if (angle + 0.01f < prevAngle)
            continue;
        if (angle < nextAngle)
            continue;

        raw.push_back({ static_cast<size_t>(i), angle });
    }

    if (raw.empty())
        return peaks;

    std::sort(raw.begin(), raw.end(),
        [](const CornerPeak& a, const CornerPeak& b) { return a.index < b.index; });

    const int minGap = std::max(2, window * 2);
    for (const CornerPeak& peak : raw) {
        if (peaks.empty()) {
            peaks.push_back(peak);
            continue;
        }

        CornerPeak& last = peaks.back();
        const int dist = closedLoopLike
            ? circularSampleDistance(
                  static_cast<int>(last.index), static_cast<int>(peak.index), count)
            : std::abs(static_cast<int>(last.index) - static_cast<int>(peak.index));
        if (dist <= minGap) {
            if (peak.angle > last.angle)
                last = peak;
        } else {
            peaks.push_back(peak);
        }
    }

    if (closedLoopLike && peaks.size() > 1) {
        const int wrapDist = circularSampleDistance(
            static_cast<int>(peaks.front().index), static_cast<int>(peaks.back().index), count);
        if (wrapDist <= minGap) {
            if (peaks.back().angle > peaks.front().angle) {
                peaks.front() = peaks.back();
            }
            peaks.pop_back();
        }
    }

    return peaks;
}

StrokeShapeFeatures analyzeStrokeShape(
    const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath, float chordLength)
{
    StrokeShapeFeatures features;
    features.bounds = computeStrokeBounds(dabs);
    features.straightness = (totalPath > 0.001f) ? (chordLength / totalPath) : 0.0f;
    features.chordByAxis
        = (features.bounds.maxAxis > 0.001f) ? (chordLength / features.bounds.maxAxis) : 1.0f;
    features.closedLoopLike = features.chordByAxis <= kShapeClosedLoopMaxChordByAxis;
    features.samples = resampleStroke(dabs, totalPath, std::max(1.0f, features.bounds.maxAxis));
    features.cornerPeaksByIndex = detectCornerPeaks(features.samples, features.closedLoopLike);
    features.cornerPeaksByStrength = features.cornerPeaksByIndex;
    std::sort(features.cornerPeaksByStrength.begin(), features.cornerPeaksByStrength.end(),
        [](const CornerPeak& a, const CornerPeak& b) { return a.angle > b.angle; });

    for (const CornerPeak& peak : features.cornerPeaksByStrength) {
        ++features.prominentCornerCount;
        if (peak.angle >= kCornerStrongAngle)
            ++features.strongCornerCount;
    }

    if (!features.cornerPeaksByStrength.empty()) {
        features.maxCornerAngle = features.cornerPeaksByStrength[0].angle;
    }
    if (features.cornerPeaksByStrength.size() >= 3) {
        features.thirdCornerAngle = features.cornerPeaksByStrength[2].angle;
    }
    if (features.cornerPeaksByStrength.size() >= 4) {
        features.fourthCornerAngle = features.cornerPeaksByStrength[3].angle;
    }

    return features;
}

Vector2 triangleVertex(QuickTriangleDirection direction, int index)
{
    switch (direction) {
    case QuickTriangleDirection::Up:
        switch (index) {
        case 0:
            return { 0.0f, -1.0f };
        case 1:
            return { 1.0f, 1.0f };
        default:
            return { -1.0f, 1.0f };
        }
    case QuickTriangleDirection::Down:
        switch (index) {
        case 0:
            return { 0.0f, 1.0f };
        case 1:
            return { -1.0f, -1.0f };
        default:
            return { 1.0f, -1.0f };
        }
    case QuickTriangleDirection::Left:
        switch (index) {
        case 0:
            return { -1.0f, 0.0f };
        case 1:
            return { 1.0f, -1.0f };
        default:
            return { 1.0f, 1.0f };
        }
    case QuickTriangleDirection::Right:
    default:
        switch (index) {
        case 0:
            return { 1.0f, 0.0f };
        case 1:
            return { -1.0f, 1.0f };
        default:
            return { -1.0f, -1.0f };
        }
    }
}

float pointToSegmentDistance(const Vector2& p, const Vector2& a, const Vector2& b)
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float lenSq = abx * abx + aby * aby;
    if (lenSq <= 0.000001f) {
        const float dx = p.x - a.x;
        const float dy = p.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    const float apx = p.x - a.x;
    const float apy = p.y - a.y;
    const float t = std::clamp((apx * abx + apy * aby) / lenSq, 0.0f, 1.0f);
    const float projX = a.x + abx * t;
    const float projY = a.y + aby * t;
    const float dx = p.x - projX;
    const float dy = p.y - projY;
    return std::sqrt(dx * dx + dy * dy);
}

TriangleFitResult evaluateTriangleFit(const std::vector<BrushStrokeReplayPoint>& dabs,
    float centerX, float centerY, float halfWidth, float halfHeight,
    QuickTriangleDirection direction)
{
    TriangleFitResult result;
    result.direction = direction;

    const Vector2 v0 = triangleVertex(direction, 0);
    const Vector2 v1 = triangleVertex(direction, 1);
    const Vector2 v2 = triangleVertex(direction, 2);

    bool hit0 = false;
    bool hit1 = false;
    bool hit2 = false;
    float sumDev = 0.0f;
    float maxDev = 0.0f;

    for (const auto& dab : dabs) {
        const Vector2 p { (dab.worldX - centerX) / halfWidth, (dab.worldY - centerY) / halfHeight };

        const float d01 = pointToSegmentDistance(p, v0, v1);
        const float d12 = pointToSegmentDistance(p, v1, v2);
        const float d20 = pointToSegmentDistance(p, v2, v0);
        const float dev = std::min({ d01, d12, d20 });
        sumDev += dev;
        maxDev = std::max(maxDev, dev);

        if (!hit0 && pointToSegmentDistance(p, v0, v0) <= kTriangleVertexHitDistanceNorm)
            hit0 = true;
        if (!hit1 && pointToSegmentDistance(p, v1, v1) <= kTriangleVertexHitDistanceNorm)
            hit1 = true;
        if (!hit2 && pointToSegmentDistance(p, v2, v2) <= kTriangleVertexHitDistanceNorm)
            hit2 = true;
    }

    result.avgDeviationNorm = sumDev / static_cast<float>(dabs.size());
    result.maxDeviationNorm = maxDev;
    result.vertexHitCount
        = static_cast<int>(hit0) + static_cast<int>(hit1) + static_cast<int>(hit2);
    return result;
}

SquareFitResult evaluateSquareFit(
    const std::vector<BrushStrokeReplayPoint>& dabs, float centerX, float centerY, float halfExtent)
{
    SquareFitResult result;
    const std::array<Vector2, 4> verts { { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, -1.0f } } };

    std::array<bool, 4> vertexHits { { false, false, false, false } };
    std::array<bool, 4> sideHits { { false, false, false, false } };

    float sumDev = 0.0f;
    float maxDev = 0.0f;
    for (const auto& dab : dabs) {
        const Vector2 p { (dab.worldX - centerX) / halfExtent,
            (dab.worldY - centerY) / halfExtent };

        const float d0 = pointToSegmentDistance(p, verts[0], verts[1]);
        const float d1 = pointToSegmentDistance(p, verts[1], verts[2]);
        const float d2 = pointToSegmentDistance(p, verts[2], verts[3]);
        const float d3 = pointToSegmentDistance(p, verts[3], verts[0]);
        const std::array<float, 4> edgeDistances { { d0, d1, d2, d3 } };
        const auto minIt = std::min_element(edgeDistances.begin(), edgeDistances.end());
        const float dev = *minIt;
        sumDev += dev;
        maxDev = std::max(maxDev, dev);

        const size_t edgeIndex = static_cast<size_t>(std::distance(edgeDistances.begin(), minIt));
        if (dev <= kSquareSideHitDistanceNorm) {
            sideHits[edgeIndex] = true;
        }

        for (size_t i = 0; i < verts.size(); ++i) {
            if (!vertexHits[i]
                && pointToSegmentDistance(p, verts[i], verts[i]) <= kSquareVertexHitDistanceNorm) {
                vertexHits[i] = true;
            }
        }
    }

    result.avgDeviationNorm = sumDev / static_cast<float>(dabs.size());
    result.maxDeviationNorm = maxDev;
    result.vertexHitCount = static_cast<int>(vertexHits[0]) + static_cast<int>(vertexHits[1])
        + static_cast<int>(vertexHits[2]) + static_cast<int>(vertexHits[3]);
    result.sideHitCount = static_cast<int>(sideHits[0]) + static_cast<int>(sideHits[1])
        + static_cast<int>(sideHits[2]) + static_cast<int>(sideHits[3]);
    return result;
}

bool solveLinear3x4(double m[3][4])
{
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        double maxAbs = std::abs(m[col][col]);
        for (int r = col + 1; r < 3; ++r) {
            const double v = std::abs(m[r][col]);
            if (v > maxAbs) {
                maxAbs = v;
                pivot = r;
            }
        }
        if (maxAbs < 1e-9)
            return false;
        if (pivot != col) {
            for (int c = col; c < 4; ++c) {
                std::swap(m[col][c], m[pivot][c]);
            }
        }

        const double invPivot = 1.0 / m[col][col];
        for (int c = col; c < 4; ++c) {
            m[col][c] *= invPivot;
        }

        for (int r = 0; r < 3; ++r) {
            if (r == col)
                continue;
            const double factor = m[r][col];
            if (std::abs(factor) < 1e-12)
                continue;
            for (int c = col; c < 4; ++c) {
                m[r][c] -= factor * m[col][c];
            }
        }
    }
    return true;
}

bool fitCircleLeastSquares(
    const std::vector<BrushStrokeReplayPoint>& dabs, float& outCenterX, float& outCenterY)
{
    if (dabs.size() < 3)
        return false;

    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    double sxxx = 0.0;
    double syyy = 0.0;
    double sxxy = 0.0;
    double sxyy = 0.0;

    for (const auto& dab : dabs) {
        const double x = dab.worldX;
        const double y = dab.worldY;
        const double x2 = x * x;
        const double y2 = y * y;

        sx += x;
        sy += y;
        sxx += x2;
        syy += y2;
        sxy += x * y;
        sxxx += x2 * x;
        syyy += y2 * y;
        sxxy += x2 * y;
        sxyy += x * y2;
    }

    double eq[3][4] = { { sxx, sxy, sx, -(sxxx + sxyy) }, { sxy, syy, sy, -(sxxy + syyy) },
        { sx, sy, static_cast<double>(dabs.size()), -(sxx + syy) } };

    if (!solveLinear3x4(eq))
        return false;

    const double A = eq[0][3];
    const double B = eq[1][3];
    const double C = eq[2][3];
    const double cx = -A * 0.5;
    const double cy = -B * 0.5;
    const double r2 = cx * cx + cy * cy - C;
    if (!std::isfinite(cx) || !std::isfinite(cy) || r2 <= 1e-6)
        return false;

    outCenterX = static_cast<float>(cx);
    outCenterY = static_cast<float>(cy);
    return true;
}

ShapeCandidateScore evaluateTriangleCandidate(const std::vector<BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, const StrokeShapeFeatures& features)
{
    ShapeCandidateScore candidate;
    if (dabs.size() < 4)
        return candidate;
    if (totalPath <= 0.001f)
        return candidate;
    if (features.straightness >= kTriangleMaxStraightness)
        return candidate;
    if (features.bounds.minAxis < kTriangleMinAxis || features.bounds.maxAxis <= 0.001f)
        return candidate;
    if (chordLength / features.bounds.maxAxis > kTriangleMaxChordByAxis)
        return candidate;
    if (features.prominentCornerCount < 2 || features.prominentCornerCount > 5)
        return candidate;

    const float centerX = features.bounds.centerX;
    const float centerY = features.bounds.centerY;
    const float halfWidth = std::max(0.001f, features.bounds.width * 0.5f);
    const float halfHeight = std::max(0.001f, features.bounds.height * 0.5f);

    TriangleFitResult bestFit;
    bool hasFit = false;
    for (QuickTriangleDirection direction :
        { QuickTriangleDirection::Up, QuickTriangleDirection::Down, QuickTriangleDirection::Left,
            QuickTriangleDirection::Right }) {
        TriangleFitResult fit
            = evaluateTriangleFit(dabs, centerX, centerY, halfWidth, halfHeight, direction);
        if (!hasFit || fit.avgDeviationNorm < bestFit.avgDeviationNorm
            || (std::abs(fit.avgDeviationNorm - bestFit.avgDeviationNorm) <= 0.0001f
                && fit.vertexHitCount > bestFit.vertexHitCount)
            || (std::abs(fit.avgDeviationNorm - bestFit.avgDeviationNorm) <= 0.0001f
                && fit.vertexHitCount == bestFit.vertexHitCount
                && fit.maxDeviationNorm < bestFit.maxDeviationNorm)) {
            bestFit = fit;
            hasFit = true;
        }
    }

    if (!hasFit)
        return candidate;
    if (bestFit.vertexHitCount < 2)
        return candidate;
    if (bestFit.avgDeviationNorm > kTriangleMaxAvgDeviationNorm)
        return candidate;
    if (bestFit.maxDeviationNorm > kTriangleMaxMaxDeviationNorm)
        return candidate;

    float expectedPerimeter = 0.0f;
    if (bestFit.direction == QuickTriangleDirection::Up
        || bestFit.direction == QuickTriangleDirection::Down) {
        expectedPerimeter = features.bounds.width
            + 2.0f
                * std::sqrt(features.bounds.width * features.bounds.width * 0.25f
                    + features.bounds.height * features.bounds.height);
    } else {
        expectedPerimeter = features.bounds.height
            + 2.0f
                * std::sqrt(features.bounds.height * features.bounds.height * 0.25f
                    + features.bounds.width * features.bounds.width);
    }

    const float perimeterCoverage = totalPath / std::max(0.001f, expectedPerimeter);
    if (perimeterCoverage < kTriangleMinPerimeterCoverage
        || perimeterCoverage > kTriangleMaxPerimeterCoverage) {
        return candidate;
    }

    const float squareHalfExtent
        = std::max(0.5f, (features.bounds.width + features.bounds.height) * 0.25f);
    const SquareFitResult squareFit = evaluateSquareFit(dabs, centerX, centerY, squareHalfExtent);

    float cornerPenalty
        = std::abs(static_cast<float>(features.prominentCornerCount) - 3.0f) * 0.30f;
    if (features.prominentCornerCount >= 4 && features.fourthCornerAngle >= kCornerStrongAngle) {
        cornerPenalty += 0.35f;
    }
    if (features.thirdCornerAngle > 0.0f && features.thirdCornerAngle < kCornerPeakAngle) {
        cornerPenalty += 0.20f;
    }

    const bool squareShadowByCoverage = squareFit.sideHitCount >= 4
        || (features.bounds.aspectRatio <= kSquareRelaxedAspectRatio && squareFit.sideHitCount >= 3
            && squareFit.vertexHitCount >= 3);
    const bool squareShadowByDeviation
        = squareFit.avgDeviationNorm <= (bestFit.avgDeviationNorm + 0.05f)
        && squareFit.maxDeviationNorm <= (bestFit.maxDeviationNorm + 0.10f);
    if (squareShadowByCoverage && squareShadowByDeviation) {
        cornerPenalty += 0.75f;
    } else if (squareShadowByCoverage) {
        cornerPenalty += 0.35f;
    }

    const float score = bestFit.avgDeviationNorm * 1.90f + bestFit.maxDeviationNorm * 0.35f
        + std::abs(perimeterCoverage - 1.0f) * 0.28f + cornerPenalty
        + std::max(0.0f, 3.0f - static_cast<float>(bestFit.vertexHitCount)) * 0.10f;
    if (score > kTriangleScoreThreshold)
        return candidate;

    candidate.valid = true;
    candidate.score = score;
    candidate.topologyDistance = std::abs(features.prominentCornerCount - 3);
    candidate.centerX = centerX;
    candidate.centerY = centerY;
    candidate.triangleHalfWidth = halfWidth;
    candidate.triangleHalfHeight = halfHeight;
    candidate.triangleDirection = bestFit.direction;
    return candidate;
}

ShapeCandidateScore evaluateSquareCandidate(const std::vector<BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, const StrokeShapeFeatures& features)
{
    ShapeCandidateScore candidate;
    if (dabs.size() < 4)
        return candidate;
    if (totalPath <= 0.001f)
        return candidate;
    if (features.straightness >= kSquareMaxStraightness)
        return candidate;
    if (features.bounds.minAxis < kSquareMinAxis || features.bounds.maxAxis <= 0.001f)
        return candidate;
    if (features.bounds.aspectRatio > kSquareMaxAspectRatio)
        return candidate;
    if (chordLength / features.bounds.maxAxis > kSquareMaxChordByAxis)
        return candidate;
    if (features.prominentCornerCount < 3 || features.prominentCornerCount > 6)
        return candidate;

    const float centerX = features.bounds.centerX;
    const float centerY = features.bounds.centerY;
    const float halfExtent
        = std::max(0.5f, (features.bounds.width + features.bounds.height) * 0.25f);
    const SquareFitResult fit = evaluateSquareFit(dabs, centerX, centerY, halfExtent);

    const bool strongCoverage = fit.sideHitCount >= 4;
    const bool relaxedCoverage = features.bounds.aspectRatio <= kSquareRelaxedAspectRatio
        && fit.sideHitCount >= 3 && fit.vertexHitCount >= 3;
    if (!strongCoverage && !relaxedCoverage)
        return candidate;
    if (fit.avgDeviationNorm > kSquareMaxAvgDeviationNorm)
        return candidate;
    if (fit.maxDeviationNorm > kSquareMaxMaxDeviationNorm)
        return candidate;

    const float perimeterCoverage = totalPath / std::max(0.001f, halfExtent * 8.0f);
    if (perimeterCoverage < kSquareMinPerimeterCoverage
        || perimeterCoverage > kSquareMaxPerimeterCoverage) {
        return candidate;
    }

    const float score = fit.avgDeviationNorm * 1.90f + fit.maxDeviationNorm * 0.35f
        + std::abs(perimeterCoverage - 1.0f) * 0.22f
        + std::abs(static_cast<float>(features.prominentCornerCount) - 4.0f) * 0.26f
        + std::abs(features.bounds.aspectRatio - 1.0f) * 0.90f
        + std::max(0.0f, 4.0f - static_cast<float>(fit.sideHitCount)) * 0.14f
        + std::max(0.0f, 4.0f - static_cast<float>(fit.vertexHitCount)) * 0.08f;
    if (score > kSquareScoreThreshold)
        return candidate;

    candidate.valid = true;
    candidate.score = score;
    candidate.topologyDistance = std::abs(features.prominentCornerCount - 4);
    candidate.centerX = centerX;
    candidate.centerY = centerY;
    candidate.squareHalfExtent = halfExtent;
    return candidate;
}

ShapeCandidateScore evaluateCircleCandidate(const std::vector<BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, const StrokeShapeFeatures& features,
    QuickCircleDebugInfo* debugInfo)
{
    ShapeCandidateScore candidate;
    QuickCircleDebugInfo localDebug;
    QuickCircleDebugInfo& dbg = debugInfo ? *debugInfo : localDebug;
    dbg = {};
    dbg.rejectReason = "not_rejected";
    dbg.dabCount = dabs.size();
    dbg.totalPath = totalPath;
    dbg.chordLength = chordLength;
    dbg.straightness = features.straightness;
    dbg.width = features.bounds.width;
    dbg.height = features.bounds.height;
    dbg.minAxis = features.bounds.minAxis;
    dbg.maxAxis = features.bounds.maxAxis;
    dbg.aspectRatio = features.bounds.aspectRatio;
    dbg.chordByAxis = features.chordByAxis;
    dbg.closedLoopLike = features.closedLoopLike;
    dbg.prominentCorners = features.prominentCornerCount;
    dbg.strongCorners = features.strongCornerCount;
    dbg.maxCornerAngle = features.maxCornerAngle;

    if (dabs.size() < 4) {
        dbg.rejectReason = "too_few_dabs";
        return candidate;
    }
    if (totalPath <= 0.001f) {
        dbg.rejectReason = "total_path_too_small";
        return candidate;
    }
    if (features.straightness >= kCircleMaxStraightness) {
        dbg.rejectReason = "too_straight";
        return candidate;
    }
    if (features.bounds.minAxis < (kCircleMinRadius * 2.0f)) {
        dbg.rejectReason = "min_axis_too_small";
        return candidate;
    }
    if (features.bounds.minAxis <= 0.001f) {
        dbg.rejectReason = "degenerate_bbox";
        return candidate;
    }

    const float aspectLimit
        = features.closedLoopLike ? kCircleClosedLoopAspectRatio : kCircleMaxAspectRatio;
    dbg.aspectLimit = aspectLimit;
    if (features.bounds.aspectRatio > aspectLimit) {
        dbg.rejectReason = "aspect_ratio_too_high";
        return candidate;
    }
    if (features.prominentCornerCount >= 4) {
        dbg.rejectReason = "too_many_prominent_corners";
        return candidate;
    }
    if (features.strongCornerCount >= 3 && features.maxCornerAngle > kCirclePolygonCornerAngle) {
        dbg.rejectReason = "shape_is_too_polygonal";
        return candidate;
    }

    float sumX = 0.0f;
    float sumY = 0.0f;
    for (const auto& dab : dabs) {
        sumX += dab.worldX;
        sumY += dab.worldY;
    }

    const float centroidX = sumX / static_cast<float>(dabs.size());
    const float centroidY = sumY / static_cast<float>(dabs.size());
    const float bboxCenterX = features.bounds.centerX;
    const float bboxCenterY = features.bounds.centerY;
    float fitCenterX = 0.0f;
    float fitCenterY = 0.0f;
    const bool hasFitCenter = fitCircleLeastSquares(dabs, fitCenterX, fitCenterY);

    auto evaluateCenter
        = [&dabs](float cx, float cy, float& outAvgRadius, float& outAvgDev, float& outMaxDev) {
              float sumRadius = 0.0f;
              for (const auto& dab : dabs) {
                  const float dx = dab.worldX - cx;
                  const float dy = dab.worldY - cy;
                  sumRadius += std::sqrt(dx * dx + dy * dy);
              }
              outAvgRadius = sumRadius / static_cast<float>(dabs.size());

              outMaxDev = 0.0f;
              float sumDev = 0.0f;
              for (const auto& dab : dabs) {
                  const float dx = dab.worldX - cx;
                  const float dy = dab.worldY - cy;
                  const float radius = std::sqrt(dx * dx + dy * dy);
                  const float dev = std::abs(radius - outAvgRadius);
                  outMaxDev = std::max(outMaxDev, dev);
                  sumDev += dev;
              }
              outAvgDev = sumDev / static_cast<float>(dabs.size());
          };

    float centroidAvgRadius = 0.0f;
    float centroidAvgDev = 0.0f;
    float centroidMaxDev = 0.0f;
    evaluateCenter(centroidX, centroidY, centroidAvgRadius, centroidAvgDev, centroidMaxDev);

    float bboxAvgRadius = 0.0f;
    float bboxAvgDev = 0.0f;
    float bboxMaxDev = 0.0f;
    evaluateCenter(bboxCenterX, bboxCenterY, bboxAvgRadius, bboxAvgDev, bboxMaxDev);

    float centerX = centroidX;
    float centerY = centroidY;
    float avgRadius = centroidAvgRadius;
    float avgDev = centroidAvgDev;
    float maxDev = centroidMaxDev;
    if (bboxAvgDev < avgDev) {
        centerX = bboxCenterX;
        centerY = bboxCenterY;
        avgRadius = bboxAvgRadius;
        avgDev = bboxAvgDev;
        maxDev = bboxMaxDev;
    }
    if (hasFitCenter) {
        float fitAvgRadius = 0.0f;
        float fitAvgDev = 0.0f;
        float fitMaxDev = 0.0f;
        evaluateCenter(fitCenterX, fitCenterY, fitAvgRadius, fitAvgDev, fitMaxDev);
        if (fitAvgDev < avgDev) {
            centerX = fitCenterX;
            centerY = fitCenterY;
            avgRadius = fitAvgRadius;
            avgDev = fitAvgDev;
            maxDev = fitMaxDev;
        }
    }

    dbg.centerX = centerX;
    dbg.centerY = centerY;
    dbg.avgRadius = avgRadius;
    dbg.avgDev = avgDev;
    dbg.maxDev = maxDev;
    if (avgRadius < kCircleMinRadius) {
        dbg.rejectReason = "avg_radius_too_small";
        return candidate;
    }

    const float diameter = std::max(0.001f, avgRadius * 2.0f);
    const float chordByDiameter = chordLength / diameter;
    dbg.chordByDiameter = chordByDiameter;
    if (chordByDiameter > kCircleMaxChordByDiameter) {
        dbg.rejectReason = "stroke_not_closed_enough";
        return candidate;
    }

    const float perimeterCoverage = totalPath / std::max(0.001f, kCircleTau * avgRadius);
    dbg.perimeterCoverage = perimeterCoverage;
    if (perimeterCoverage < kCircleMinPerimeterCoverage) {
        dbg.rejectReason = "perimeter_coverage_too_low";
        return candidate;
    }
    if (perimeterCoverage > kCircleMaxPerimeterCoverage) {
        dbg.rejectReason = "perimeter_coverage_too_high";
        return candidate;
    }

    const float avgDeviationNorm = avgDev / std::max(0.001f, avgRadius);
    const float maxDeviationNorm = maxDev / std::max(0.001f, avgRadius);
    dbg.avgDeviationNorm = avgDeviationNorm;
    dbg.maxDeviationNorm = maxDeviationNorm;
    if (avgDeviationNorm > kCircleMaxAvgDeviationNorm) {
        dbg.rejectReason = "avg_radial_deviation_norm_too_high";
        return candidate;
    }
    if (maxDeviationNorm > kCircleMaxMaxDeviationNorm) {
        dbg.rejectReason = "max_radial_deviation_norm_too_high";
        return candidate;
    }

    if (totalPath < (avgRadius * kCircleMinPathByRadius)) {
        dbg.rejectReason = "path_too_short_for_radius";
        return candidate;
    }

    const float devBoost = features.closedLoopLike ? kCircleClosedLoopDeviationBoost : 1.0f;
    const float maxDevLimit
        = std::max(4.2f, avgRadius * kCircleMaxRadialDeviationFactor) * devBoost;
    const float avgDevLimit
        = std::max(2.4f, avgRadius * kCircleAvgRadialDeviationFactor) * devBoost;
    dbg.maxDevLimit = maxDevLimit;
    dbg.avgDevLimit = avgDevLimit;
    if (maxDev > maxDevLimit) {
        dbg.rejectReason = "max_radial_deviation_too_high";
        return candidate;
    }
    if (avgDev > avgDevLimit) {
        dbg.rejectReason = "avg_radial_deviation_too_high";
        return candidate;
    }

    const float minSegLen = std::max(1.0f, avgRadius * 0.10f);
    std::vector<Vector2> dirs;
    dirs.reserve(dabs.size());
    float segStartX = dabs.front().worldX;
    float segStartY = dabs.front().worldY;
    for (size_t i = 1; i < dabs.size(); ++i) {
        const float vx = dabs[i].worldX - segStartX;
        const float vy = dabs[i].worldY - segStartY;
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len < minSegLen)
            continue;
        dirs.push_back({ vx / len, vy / len });
        segStartX = dabs[i].worldX;
        segStartY = dabs[i].worldY;
    }

    int largeTurns = 0;
    int veryLargeTurns = 0;
    int turnSamples = 0;
    for (size_t i = 1; i < dirs.size(); ++i) {
        const float dot
            = std::clamp(dirs[i - 1].x * dirs[i].x + dirs[i - 1].y * dirs[i].y, -1.0f, 1.0f);
        const float turn = std::acos(dot);
        ++turnSamples;
        if (turn > kCircleLargeTurnAngle)
            ++largeTurns;
        if (turn > kCircleVeryLargeTurnAngle)
            ++veryLargeTurns;
    }
    dbg.turnSamples = turnSamples;
    dbg.largeTurns = largeTurns;
    dbg.veryLargeTurns = veryLargeTurns;
    if (turnSamples > 0) {
        const float largeTurnRatio
            = static_cast<float>(largeTurns) / static_cast<float>(turnSamples);
        dbg.largeTurnRatio = largeTurnRatio;
        if (largeTurnRatio > kCircleMaxLargeTurnRatio && avgDev > (avgDevLimit * 0.60f)) {
            dbg.rejectReason = "too_many_large_turns";
            return candidate;
        }
        if (veryLargeTurns > kCircleMaxVeryLargeTurns && avgDev > (avgDevLimit * 0.50f)) {
            dbg.rejectReason = "too_many_very_large_turns";
            return candidate;
        }
    }

    const float score = avgDeviationNorm * 2.10f + maxDeviationNorm * 0.45f
        + std::abs(perimeterCoverage - 1.0f) * 0.35f
        + static_cast<float>(features.prominentCornerCount) * 0.25f
        + std::max(0.0f, features.maxCornerAngle - 0.90f) * 0.40f;
    dbg.score = score;
    if (score > kCircleScoreThreshold) {
        dbg.rejectReason = "circle_score_too_high";
        return candidate;
    }

    candidate.valid = true;
    candidate.score = score;
    candidate.topologyDistance = features.prominentCornerCount;
    candidate.centerX = centerX;
    candidate.centerY = centerY;
    dbg.isCandidate = true;
    dbg.rejectReason = "ok";
    return candidate;
}

} // namespace

void logQuickShapeHoldDecision(const QuickCircleDebugInfo& d, bool lineCandidate) { }

bool isQuickLineCandidate(
    const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath, float chordLength)
{
    if (dabs.size() < 3)
        return false;
    if (totalPath <= 0.001f || chordLength < kLineMinLength)
        return false;

    const float straightness = chordLength / totalPath;
    if (straightness < kLineStraightnessThreshold)
        return false;

    const auto& first = dabs.front();
    const auto& last = dabs.back();
    const float dx = last.worldX - first.worldX;
    const float dy = last.worldY - first.worldY;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq <= 0.001f)
        return false;

    float refRadius = 1.0f;
    for (const auto& dab : dabs) {
        refRadius = std::max(refRadius, dab.radius);
    }

    float maxDevLimit = std::max(3.0f, refRadius * kLineMaxDeviationFactor);
    maxDevLimit = std::max(maxDevLimit, chordLength * kLineDeviationByLength);
    const float avgDevLimit = maxDevLimit * kLineAvgDeviationFactor;

    float maxDev = 0.0f;
    float sumDev = 0.0f;
    for (const auto& dab : dabs) {
        const float px = dab.worldX - first.worldX;
        const float py = dab.worldY - first.worldY;
        const float t = std::clamp((px * dx + py * dy) / lenSq, 0.0f, 1.0f);
        const float lx = first.worldX + dx * t;
        const float ly = first.worldY + dy * t;
        const float ddx = dab.worldX - lx;
        const float ddy = dab.worldY - ly;
        const float dev = std::sqrt(ddx * ddx + ddy * ddy);
        maxDev = std::max(maxDev, dev);
        sumDev += dev;
    }

    const float avgDev = sumDev / static_cast<float>(dabs.size());
    if (maxDev > maxDevLimit)
        return false;
    if (avgDev > avgDevLimit)
        return false;
    return true;
}

float computeQuickCircleAngleDirection(
    const std::vector<BrushStrokeReplayPoint>& dabs, float centerX, float centerY)
{
    if (dabs.size() < 2)
        return 1.0f;

    float totalDelta = 0.0f;
    for (size_t i = 1; i < dabs.size(); ++i) {
        const float ax = dabs[i - 1].worldX - centerX;
        const float ay = dabs[i - 1].worldY - centerY;
        const float bx = dabs[i].worldX - centerX;
        const float by = dabs[i].worldY - centerY;

        const float cross = ax * by - ay * bx;
        const float dot = ax * bx + ay * by;
        totalDelta += std::atan2(cross, dot);
    }

    if (std::abs(totalDelta) < 0.001f)
        return 1.0f;
    return (totalDelta >= 0.0f) ? 1.0f : -1.0f;
}

bool isQuickCircleCandidate(const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath,
    float chordLength, float& outCenterX, float& outCenterY, QuickCircleDebugInfo* debugInfo)
{
    const StrokeShapeFeatures features = analyzeStrokeShape(dabs, totalPath, chordLength);
    const ShapeCandidateScore candidate
        = evaluateCircleCandidate(dabs, totalPath, chordLength, features, debugInfo);
    if (!candidate.valid)
        return false;

    outCenterX = candidate.centerX;
    outCenterY = candidate.centerY;
    return true;
}

bool isQuickTriangleCandidate(const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath,
    float chordLength, float& outCenterX, float& outCenterY, float& outHalfWidth,
    float& outHalfHeight, QuickTriangleDirection& outDirection)
{
    const StrokeShapeFeatures features = analyzeStrokeShape(dabs, totalPath, chordLength);
    const ShapeCandidateScore candidate
        = evaluateTriangleCandidate(dabs, totalPath, chordLength, features);
    if (!candidate.valid)
        return false;

    outCenterX = candidate.centerX;
    outCenterY = candidate.centerY;
    outHalfWidth = candidate.triangleHalfWidth;
    outHalfHeight = candidate.triangleHalfHeight;
    outDirection = candidate.triangleDirection;
    return true;
}

bool isQuickSquareCandidate(const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath,
    float chordLength, float& outCenterX, float& outCenterY, float& outHalfExtent)
{
    const StrokeShapeFeatures features = analyzeStrokeShape(dabs, totalPath, chordLength);
    const ShapeCandidateScore candidate
        = evaluateSquareCandidate(dabs, totalPath, chordLength, features);
    if (!candidate.valid)
        return false;

    outCenterX = candidate.centerX;
    outCenterY = candidate.centerY;
    outHalfExtent = candidate.squareHalfExtent;
    return true;
}

bool detectQuickShapeCandidate(const std::vector<BrushStrokeReplayPoint>& dabs, float totalPath,
    float chordLength, QuickShapeDetectionResult& outResult)
{
    outResult = {};
    const StrokeShapeFeatures features = analyzeStrokeShape(dabs, totalPath, chordLength);

    const ShapeCandidateScore square
        = evaluateSquareCandidate(dabs, totalPath, chordLength, features);
    const ShapeCandidateScore triangle
        = evaluateTriangleCandidate(dabs, totalPath, chordLength, features);
    const ShapeCandidateScore circle
        = evaluateCircleCandidate(dabs, totalPath, chordLength, features, &outResult.circleDebug);

    if (square.valid && triangle.valid
        && features.bounds.aspectRatio <= kSquarePreferredOverTriangleAspectRatio
        && square.score <= (triangle.score + kSquarePreferredTriangleScoreMargin)) {
        outResult.kind = QuickShapeDetectionResult::Kind::Square;
        outResult.centerX = square.centerX;
        outResult.centerY = square.centerY;
        outResult.squareHalfExtent = square.squareHalfExtent;
        return true;
    }

    struct RankedCandidate {
        QuickShapeDetectionResult::Kind kind = QuickShapeDetectionResult::Kind::None;
        ShapeCandidateScore score;
    };

    std::array<RankedCandidate, 3> ranked { { { QuickShapeDetectionResult::Kind::Square, square },
        { QuickShapeDetectionResult::Kind::Triangle, triangle },
        { QuickShapeDetectionResult::Kind::Circle, circle } } };

    std::sort(ranked.begin(), ranked.end(), [](const RankedCandidate& a, const RankedCandidate& b) {
        if (a.score.valid != b.score.valid)
            return a.score.valid > b.score.valid;
        if (!a.score.valid)
            return false;
        if (std::abs(a.score.score - b.score.score) > 0.05f)
            return a.score.score < b.score.score;
        if (a.score.topologyDistance != b.score.topologyDistance) {
            return a.score.topologyDistance < b.score.topologyDistance;
        }
        return a.score.score < b.score.score;
    });

    if (ranked.front().score.valid) {
        outResult.kind = ranked.front().kind;
        outResult.centerX = ranked.front().score.centerX;
        outResult.centerY = ranked.front().score.centerY;
        outResult.squareHalfExtent = ranked.front().score.squareHalfExtent;
        outResult.triangleHalfWidth = ranked.front().score.triangleHalfWidth;
        outResult.triangleHalfHeight = ranked.front().score.triangleHalfHeight;
        outResult.triangleDirection = ranked.front().score.triangleDirection;
        return true;
    }

    if (isQuickLineCandidate(dabs, totalPath, chordLength)) {
        outResult.kind = QuickShapeDetectionResult::Kind::Line;
        return true;
    }

    return false;
}

} // namespace aether
