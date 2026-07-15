// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   Q U I C K   S H A P E   M O R P H
// ==========================================================================

#include "features/canvas/quick-shape/QuickShapeMorph.h"

#include "features/settings/SettingsManager.h"
#include "shared/types/Types.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <limits>

namespace aether {

using namespace QuickShapeConstants;
using ruwa::core::brushes::BrushStrokeReplayPoint;
using ruwa::core::brushes::IEditableBrushStrokeReplayData;

namespace {

constexpr float kCursorFollowLerp = 0.35f;
constexpr float kCursorSnapEpsilon = 0.2f;

Vector2 triangleVertexForKind(QuickShapeMorph::Kind kind, float centerX, float centerY,
    float halfWidth, float halfHeight, int index)
{
    switch (kind) {
    case QuickShapeMorph::Kind::TriangleUp:
        switch (index) {
        case 0:
            return { centerX, centerY - halfHeight };
        case 1:
            return { centerX - halfWidth, centerY + halfHeight };
        default:
            return { centerX + halfWidth, centerY + halfHeight };
        }
    case QuickShapeMorph::Kind::TriangleDown:
        switch (index) {
        case 0:
            return { centerX, centerY + halfHeight };
        case 1:
            return { centerX - halfWidth, centerY - halfHeight };
        default:
            return { centerX + halfWidth, centerY - halfHeight };
        }
    case QuickShapeMorph::Kind::TriangleLeft:
        switch (index) {
        case 0:
            return { centerX - halfWidth, centerY };
        case 1:
            return { centerX + halfWidth, centerY - halfHeight };
        default:
            return { centerX + halfWidth, centerY + halfHeight };
        }
    case QuickShapeMorph::Kind::TriangleRight:
    default:
        switch (index) {
        case 0:
            return { centerX + halfWidth, centerY };
        case 1:
            return { centerX - halfWidth, centerY - halfHeight };
        default:
            return { centerX - halfWidth, centerY + halfHeight };
        }
    }
}

Vector2 pointOnTrianglePerimeter(QuickShapeMorph::Kind kind, float centerX, float centerY,
    float halfWidth, float halfHeight, float u)
{
    const Vector2 v0 = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, 0);
    const Vector2 v1 = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, 1);
    const Vector2 v2 = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, 2);

    const auto segLen = [](const Vector2& a, const Vector2& b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    };
    const float len01 = segLen(v0, v1);
    const float len12 = segLen(v1, v2);
    const float len20 = segLen(v2, v0);
    const float perimeter = std::max(0.001f, len01 + len12 + len20);
    const float dist = std::clamp(u, 0.0f, 1.0f) * perimeter;

    auto lerpPoint = [](const Vector2& a, const Vector2& b, float t) {
        return Vector2 { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    };

    if (dist <= len01) {
        return lerpPoint(v0, v1, len01 <= 0.0001f ? 0.0f : dist / len01);
    }
    if (dist <= len01 + len12) {
        const float local = dist - len01;
        return lerpPoint(v1, v2, len12 <= 0.0001f ? 0.0f : local / len12);
    }
    const float local = dist - len01 - len12;
    return lerpPoint(v2, v0, len20 <= 0.0001f ? 0.0f : local / len20);
}

Vector2 closestPointOnSegment(const Vector2& p, const Vector2& a, const Vector2& b)
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float lenSq = abx * abx + aby * aby;
    if (lenSq <= 0.000001f) {
        return a;
    }

    const float apx = p.x - a.x;
    const float apy = p.y - a.y;
    const float t = std::clamp((apx * abx + apy * aby) / lenSq, 0.0f, 1.0f);
    return { a.x + abx * t, a.y + aby * t };
}

Vector2 closestPointOnTriangle(QuickShapeMorph::Kind kind, float centerX, float centerY,
    float halfWidth, float halfHeight, const Vector2& sourcePoint)
{
    const Vector2 v0 = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, 0);
    const Vector2 v1 = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, 1);
    const Vector2 v2 = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, 2);

    const Vector2 p01 = closestPointOnSegment(sourcePoint, v0, v1);
    const Vector2 p12 = closestPointOnSegment(sourcePoint, v1, v2);
    const Vector2 p20 = closestPointOnSegment(sourcePoint, v2, v0);

    const auto distSq = [&sourcePoint](const Vector2& p) {
        const float dx = sourcePoint.x - p.x;
        const float dy = sourcePoint.y - p.y;
        return dx * dx + dy * dy;
    };

    const float d01 = distSq(p01);
    const float d12 = distSq(p12);
    const float d20 = distSq(p20);

    if (d01 <= d12 && d01 <= d20)
        return p01;
    if (d12 <= d20)
        return p12;
    return p20;
}

float distanceSq(const Vector2& a, const Vector2& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float signedLoopArea(const std::vector<BrushStrokeReplayPoint>& dabs)
{
    if (dabs.size() < 3)
        return 0.0f;

    float area = 0.0f;
    for (size_t i = 0; i < dabs.size(); ++i) {
        const auto& a = dabs[i];
        const auto& b = dabs[(i + 1) % dabs.size()];
        area += a.worldX * b.worldY - a.worldY * b.worldX;
    }
    return area * 0.5f;
}

float signedTriangleArea(const std::array<Vector2, 3>& points)
{
    float area = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vector2& a = points[i];
        const Vector2& b = points[(i + 1) % points.size()];
        area += a.x * b.y - a.y * b.x;
    }
    return area * 0.5f;
}

struct TriangleStrokeMapping {
    std::array<int, 3> vertexOrder { 0, 1, 2 };
    std::array<float, 3> vertexParams { 0.0f, 0.33f, 0.66f };
    float score = std::numeric_limits<float>::max();
    bool valid = false;
};

TriangleStrokeMapping buildTriangleStrokeMapping(QuickShapeMorph::Kind kind, float centerX,
    float centerY, float halfWidth, float halfHeight,
    const std::vector<BrushStrokeReplayPoint>& sourceDabs, const std::vector<float>& shapeParams)
{
    TriangleStrokeMapping best;
    if (sourceDabs.empty() || sourceDabs.size() != shapeParams.size()) {
        return best;
    }

    const float sourceArea = signedLoopArea(sourceDabs);
    const float sourceSign = (sourceArea < 0.0f) ? -1.0f : 1.0f;

    const std::array<std::array<int, 3>, 2> candidateOrders { { { { 0, 1, 2 } },
        { { 0, 2, 1 } } } };
    for (const auto& order : candidateOrders) {
        std::array<float, 3> params {};
        float totalDist = 0.0f;
        std::array<Vector2, 3> orderedVertices {};

        for (int i = 0; i < 3; ++i) {
            const Vector2 vertex
                = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, order[i]);
            orderedVertices[i] = vertex;
            size_t bestIndex = 0;
            float bestDist = std::numeric_limits<float>::max();
            for (size_t j = 0; j < sourceDabs.size(); ++j) {
                const Vector2 sourcePoint { sourceDabs[j].worldX, sourceDabs[j].worldY };
                const float dist = distanceSq(sourcePoint, vertex);
                const bool isClearlyBetter = dist < (bestDist - 0.0001f);
                const bool isTieButEarlier
                    = std::abs(dist - bestDist) <= 4.0f && shapeParams[j] < shapeParams[bestIndex];
                if (isClearlyBetter || isTieButEarlier) {
                    bestDist = dist;
                    bestIndex = j;
                }
            }
            params[i] = shapeParams[bestIndex];
            totalDist += bestDist;
        }

        std::array<float, 3> unwrapped = params;
        while (unwrapped[1] < unwrapped[0])
            unwrapped[1] += 1.0f;
        while (unwrapped[2] < unwrapped[1])
            unwrapped[2] += 1.0f;

        const float gap01 = unwrapped[1] - unwrapped[0];
        const float gap12 = unwrapped[2] - unwrapped[1];
        const float gap20 = (unwrapped[0] + 1.0f) - unwrapped[2];
        if (gap01 <= 0.04f || gap12 <= 0.04f || gap20 <= 0.04f) {
            continue;
        }

        const float balancePenalty = std::abs(gap01 - (1.0f / 3.0f))
            + std::abs(gap12 - (1.0f / 3.0f)) + std::abs(gap20 - (1.0f / 3.0f));
        const float targetArea = signedTriangleArea(orderedVertices);
        const float targetSign = (targetArea < 0.0f) ? -1.0f : 1.0f;
        const float windingPenalty = (targetSign == sourceSign) ? 0.0f : 1000000.0f;
        const float score = totalDist + balancePenalty * 10000.0f + windingPenalty;

        if (!best.valid || score < best.score) {
            best.vertexOrder = order;
            best.vertexParams = params;
            best.score = score;
            best.valid = true;
        }
    }

    return best;
}

Vector2 pointOnTriangleByStrokeMapping(QuickShapeMorph::Kind kind, float centerX, float centerY,
    float halfWidth, float halfHeight, const std::array<int, 3>& vertexOrder,
    const std::array<float, 3>& vertexParams, float sourceParam)
{
    const Vector2 v0
        = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, vertexOrder[0]);
    const Vector2 v1
        = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, vertexOrder[1]);
    const Vector2 v2
        = triangleVertexForKind(kind, centerX, centerY, halfWidth, halfHeight, vertexOrder[2]);

    std::array<float, 3> params = vertexParams;
    while (params[1] < params[0])
        params[1] += 1.0f;
    while (params[2] < params[1])
        params[2] += 1.0f;

    float u = sourceParam;
    while (u < params[0])
        u += 1.0f;

    auto lerpPoint = [](const Vector2& a, const Vector2& b, float t) {
        return Vector2 { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    };

    if (u <= params[1]) {
        const float denom = std::max(0.0001f, params[1] - params[0]);
        return lerpPoint(v0, v1, std::clamp((u - params[0]) / denom, 0.0f, 1.0f));
    }
    if (u <= params[2]) {
        const float denom = std::max(0.0001f, params[2] - params[1]);
        return lerpPoint(v1, v2, std::clamp((u - params[1]) / denom, 0.0f, 1.0f));
    }

    const float endParam = params[0] + 1.0f;
    const float denom = std::max(0.0001f, endParam - params[2]);
    return lerpPoint(v2, v0, std::clamp((u - params[2]) / denom, 0.0f, 1.0f));
}

struct TriangleControlState {
    QuickShapeMorph::Kind kind = QuickShapeMorph::Kind::TriangleUp;
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
};

float signedExtent(float value, float minAbs = 0.5f)
{
    if (std::abs(value) < minAbs) {
        return (value < 0.0f) ? -minAbs : minAbs;
    }
    return value;
}

TriangleControlState resolveTriangleControlState(const QuickShapeMorph::State& state)
{
    TriangleControlState control;
    control.kind = state.kind;
    control.halfWidth = std::max(0.5f, state.triangleHalfWidth);
    control.halfHeight = std::max(0.5f, state.triangleHalfHeight);

    const float dx = state.cursorX - state.circleCenterX;
    const float dy = state.cursorY - state.circleCenterY;

    switch (state.kind) {
    case QuickShapeMorph::Kind::TriangleUp:
    case QuickShapeMorph::Kind::TriangleDown: {
        const float currentSign = (dy < 0.0f) ? -1.0f : 1.0f;
        const bool flipped = (currentSign != state.trianglePrimaryAxisSign);
        const bool baseIsUp = (state.kind == QuickShapeMorph::Kind::TriangleUp);
        control.kind = (baseIsUp != flipped) ? QuickShapeMorph::Kind::TriangleUp
                                             : QuickShapeMorph::Kind::TriangleDown;
        control.halfWidth = signedExtent(dx);
        control.halfHeight = std::max(0.5f, std::abs(dy));
        break;
    }
    case QuickShapeMorph::Kind::TriangleLeft:
    case QuickShapeMorph::Kind::TriangleRight: {
        const float currentSign = (dx < 0.0f) ? -1.0f : 1.0f;
        const bool flipped = (currentSign != state.trianglePrimaryAxisSign);
        const bool baseIsLeft = (state.kind == QuickShapeMorph::Kind::TriangleLeft);
        control.kind = (baseIsLeft != flipped) ? QuickShapeMorph::Kind::TriangleLeft
                                               : QuickShapeMorph::Kind::TriangleRight;
        control.halfWidth = std::max(0.5f, std::abs(dx));
        control.halfHeight = signedExtent(dy);
        break;
    }
    default:
        break;
    }

    return control;
}

Vector2 squareVertex(float centerX, float centerY, float halfExtent, int index)
{
    switch (index) {
    case 0:
        return { centerX - halfExtent, centerY - halfExtent };
    case 1:
        return { centerX - halfExtent, centerY + halfExtent };
    case 2:
        return { centerX + halfExtent, centerY + halfExtent };
    default:
        return { centerX + halfExtent, centerY - halfExtent };
    }
}

Vector2 closestPointOnSquare(
    float centerX, float centerY, float halfExtent, const Vector2& sourcePoint)
{
    const Vector2 v0 = squareVertex(centerX, centerY, halfExtent, 0);
    const Vector2 v1 = squareVertex(centerX, centerY, halfExtent, 1);
    const Vector2 v2 = squareVertex(centerX, centerY, halfExtent, 2);
    const Vector2 v3 = squareVertex(centerX, centerY, halfExtent, 3);

    const Vector2 p01 = closestPointOnSegment(sourcePoint, v0, v1);
    const Vector2 p12 = closestPointOnSegment(sourcePoint, v1, v2);
    const Vector2 p23 = closestPointOnSegment(sourcePoint, v2, v3);
    const Vector2 p30 = closestPointOnSegment(sourcePoint, v3, v0);

    const float d01 = distanceSq(sourcePoint, p01);
    const float d12 = distanceSq(sourcePoint, p12);
    const float d23 = distanceSq(sourcePoint, p23);
    const float d30 = distanceSq(sourcePoint, p30);

    if (d01 <= d12 && d01 <= d23 && d01 <= d30)
        return p01;
    if (d12 <= d23 && d12 <= d30)
        return p12;
    if (d23 <= d30)
        return p23;
    return p30;
}

struct SquareStrokeMapping {
    std::array<int, 4> vertexOrder { 0, 1, 2, 3 };
    std::array<float, 4> vertexParams { 0.0f, 0.25f, 0.5f, 0.75f };
    float score = std::numeric_limits<float>::max();
    bool valid = false;
};

SquareStrokeMapping buildSquareStrokeMapping(float centerX, float centerY, float halfExtent,
    const std::vector<BrushStrokeReplayPoint>& sourceDabs, const std::vector<float>& shapeParams)
{
    SquareStrokeMapping best;
    if (sourceDabs.empty() || sourceDabs.size() != shapeParams.size()) {
        return best;
    }

    const float sourceArea = signedLoopArea(sourceDabs);
    const float sourceSign = (sourceArea < 0.0f) ? -1.0f : 1.0f;
    const std::array<std::array<int, 4>, 2> candidateOrders { { { { 0, 1, 2, 3 } },
        { { 0, 3, 2, 1 } } } };

    for (const auto& order : candidateOrders) {
        std::array<float, 4> params {};
        std::array<Vector2, 4> orderedVertices {};
        float totalDist = 0.0f;

        for (int i = 0; i < 4; ++i) {
            const Vector2 vertex = squareVertex(centerX, centerY, halfExtent, order[i]);
            orderedVertices[i] = vertex;
            size_t bestIndex = 0;
            float bestDist = std::numeric_limits<float>::max();
            for (size_t j = 0; j < sourceDabs.size(); ++j) {
                const Vector2 sourcePoint { sourceDabs[j].worldX, sourceDabs[j].worldY };
                const float dist = distanceSq(sourcePoint, vertex);
                const bool isClearlyBetter = dist < (bestDist - 0.0001f);
                const bool isTieButEarlier
                    = std::abs(dist - bestDist) <= 4.0f && shapeParams[j] < shapeParams[bestIndex];
                if (isClearlyBetter || isTieButEarlier) {
                    bestDist = dist;
                    bestIndex = j;
                }
            }
            params[i] = shapeParams[bestIndex];
            totalDist += bestDist;
        }

        std::array<float, 4> unwrapped = params;
        for (size_t i = 1; i < unwrapped.size(); ++i) {
            while (unwrapped[i] < unwrapped[i - 1])
                unwrapped[i] += 1.0f;
        }

        bool hasCollapsedEdge = false;
        float balancePenalty = 0.0f;
        for (size_t i = 0; i < unwrapped.size(); ++i) {
            const float next
                = (i + 1 < unwrapped.size()) ? unwrapped[i + 1] : (unwrapped[0] + 1.0f);
            const float gap = next - unwrapped[i];
            if (gap <= 0.03f) {
                hasCollapsedEdge = true;
                break;
            }
            balancePenalty += std::abs(gap - 0.25f);
        }
        if (hasCollapsedEdge)
            continue;

        float targetArea = 0.0f;
        for (size_t i = 0; i < orderedVertices.size(); ++i) {
            const Vector2& a = orderedVertices[i];
            const Vector2& b = orderedVertices[(i + 1) % orderedVertices.size()];
            targetArea += a.x * b.y - a.y * b.x;
        }
        const float targetSign = (targetArea < 0.0f) ? -1.0f : 1.0f;
        const float windingPenalty = (targetSign == sourceSign) ? 0.0f : 1000000.0f;
        const float score = totalDist + balancePenalty * 10000.0f + windingPenalty;

        if (!best.valid || score < best.score) {
            best.vertexOrder = order;
            best.vertexParams = params;
            best.score = score;
            best.valid = true;
        }
    }

    return best;
}

Vector2 pointOnSquareByStrokeMapping(float centerX, float centerY, float halfExtent,
    const std::array<int, 4>& vertexOrder, const std::array<float, 4>& vertexParams,
    float sourceParam)
{
    const Vector2 v0 = squareVertex(centerX, centerY, halfExtent, vertexOrder[0]);
    const Vector2 v1 = squareVertex(centerX, centerY, halfExtent, vertexOrder[1]);
    const Vector2 v2 = squareVertex(centerX, centerY, halfExtent, vertexOrder[2]);
    const Vector2 v3 = squareVertex(centerX, centerY, halfExtent, vertexOrder[3]);

    std::array<float, 4> params = vertexParams;
    for (size_t i = 1; i < params.size(); ++i) {
        while (params[i] < params[i - 1])
            params[i] += 1.0f;
    }

    float u = sourceParam;
    while (u < params[0])
        u += 1.0f;

    auto lerpPoint = [](const Vector2& a, const Vector2& b, float t) {
        return Vector2 { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    };
    auto segmentPoint
        = [&lerpPoint](const Vector2& a, const Vector2& b, float u0, float u1, float u) {
              const float denom = std::max(0.0001f, u1 - u0);
              return lerpPoint(a, b, std::clamp((u - u0) / denom, 0.0f, 1.0f));
          };

    if (u <= params[1])
        return segmentPoint(v0, v1, params[0], params[1], u);
    if (u <= params[2])
        return segmentPoint(v1, v2, params[1], params[2], u);
    if (u <= params[3])
        return segmentPoint(v2, v3, params[2], params[3], u);
    return segmentPoint(v3, v0, params[3], params[0] + 1.0f, u);
}

} // namespace

QuickShapeMorph::QuickShapeMorph(QObject* parent, Callbacks callbacks)
    : QObject(parent)
    , m_callbacks(std::move(callbacks))
{
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kHoldDelayMs);
    connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
        if (onHoldTimeout()) {
            m_morphTimer.start();
        }
    });

    m_morphTimer.setSingleShot(false);
    m_morphTimer.setInterval(kMorphIntervalMs);
    connect(&m_morphTimer, &QTimer::timeout, this, [this]() {
        float lastX = 0.0f, lastY = 0.0f;
        if (m_callbacks.getLastStrokePosition) {
            auto p = m_callbacks.getLastStrokePosition();
            lastX = p.first;
            lastY = p.second;
        } else if (m_callbacks.getStrokeReplayData) {
            if (auto replayData = m_callbacks.getStrokeReplayData()) {
                const auto points = replayData->points();
                if (!points.empty()) {
                    lastX = points.back().worldX;
                    lastY = points.back().worldY;
                }
            }
        }
        onMorphTick(lastX, lastY);
    });
}

void QuickShapeMorph::restartHoldTimer()
{
    if (!ruwa::core::SettingsManager::instance().settings().editor.quickshapesEnabled)
        return;
    if (!m_callbacks.getStrokeReplayData || !m_callbacks.getStrokeReplayData())
        return;
    m_holdTimer.start();
}

void QuickShapeMorph::stopHoldTimer()
{
    m_holdTimer.stop();
}

void QuickShapeMorph::stop()
{
    m_holdTimer.stop();
    m_morphTimer.stop();
    m_state = {};
}

bool QuickShapeMorph::onHoldTimeout()
{
    return start();
}

void QuickShapeMorph::updateCursorTarget(float x, float y)
{
    if (!m_state.active)
        return;
    m_state.cursorTargetX = x;
    m_state.cursorTargetY = y;
    m_state.cursorDirty = true;
    if (m_state.kind == Kind::Line) {
        m_state.cursorX = x;
        m_state.cursorY = y;
    }
    if (!m_morphTimer.isActive()) {
        m_morphTimer.start();
    }
}

void QuickShapeMorph::translate(float dx, float dy)
{
    if (!m_state.active)
        return;
    m_state.anchorX += dx;
    m_state.anchorY += dy;
    m_state.cursorX += dx;
    m_state.cursorY += dy;
    m_state.cursorTargetX += dx;
    m_state.cursorTargetY += dy;
    m_state.circleCenterX += dx;
    m_state.circleCenterY += dy;
    for (auto& dab : m_state.sourceDabs) {
        dab.worldX += dx;
        dab.worldY += dy;
        dab.baseWorldX += dx;
        dab.baseWorldY += dy;
    }
    for (auto& dab : m_state.targetDabs) {
        dab.worldX += dx;
        dab.worldY += dy;
        dab.baseWorldX += dx;
        dab.baseWorldY += dy;
    }
    apply(1.0f, true);
}

void QuickShapeMorph::onMorphTick(float lastStrokeX, float lastStrokeY)
{
    if (!m_state.active) {
        m_morphTimer.stop();
        return;
    }

    m_state.cursorTargetX = lastStrokeX;
    m_state.cursorTargetY = lastStrokeY;

    bool cursorInFlight = false;
    if (m_state.kind == Kind::Circle) {
        const float dx = m_state.cursorTargetX - m_state.cursorX;
        const float dy = m_state.cursorTargetY - m_state.cursorY;
        const float distSq = dx * dx + dy * dy;
        const float snapSq = kCursorSnapEpsilon * kCursorSnapEpsilon;
        if (distSq > snapSq) {
            m_state.cursorX += dx * kCursorFollowLerp;
            m_state.cursorY += dy * kCursorFollowLerp;
            cursorInFlight = true;
        } else {
            m_state.cursorX = m_state.cursorTargetX;
            m_state.cursorY = m_state.cursorTargetY;
        }
    } else {
        m_state.cursorX = m_state.cursorTargetX;
        m_state.cursorY = m_state.cursorTargetY;
    }

    if (m_state.progress < 1.0f) {
        const float delta = static_cast<float>(kMorphIntervalMs) / kMorphDurationMs;
        m_state.progress = std::clamp(m_state.progress + delta, 0.0f, 1.0f);
        float t = m_state.progress;
        float eased = t * t * (3.0f - 2.0f * t);
        apply(eased, true);
        m_state.cursorDirty = cursorInFlight;
        return;
    }

    if (!m_state.cursorDirty && !cursorInFlight) {
        m_morphTimer.stop();
        return;
    }
    apply(1.0f, true);
    m_state.cursorDirty = cursorInFlight;
}

bool QuickShapeMorph::start()
{
    if (!m_callbacks.isDrawing || !m_callbacks.isDrawing())
        return false;
    TileGrid* grid = m_callbacks.activeLayerTileGrid ? m_callbacks.activeLayerTileGrid() : nullptr;
    if (!grid)
        return false;

    if (!m_callbacks.getStrokeReplayData)
        return false;
    std::shared_ptr<IEditableBrushStrokeReplayData> replayData = m_callbacks.getStrokeReplayData();
    if (!replayData)
        return false;

    const std::vector<BrushStrokeReplayPoint> liveDabs = replayData->points();
    if (liveDabs.size() < 3)
        return false;

    const auto& first = liveDabs.front();
    const auto& last = liveDabs.back();
    float dx = last.worldX - first.worldX;
    float dy = last.worldY - first.worldY;
    float lineLen = std::sqrt(dx * dx + dy * dy);

    std::vector<float> cumulative(liveDabs.size(), 0.0f);
    float totalPath = 0.0f;
    for (size_t i = 1; i < liveDabs.size(); ++i) {
        float sx = liveDabs[i].worldX - liveDabs[i - 1].worldX;
        float sy = liveDabs[i].worldY - liveDabs[i - 1].worldY;
        totalPath += std::sqrt(sx * sx + sy * sy);
        cumulative[i] = totalPath;
    }
    if (totalPath <= 0.001f)
        return false;
    if (totalPath < kMinPathLength)
        return false;

    Kind shapeKind = Kind::None;
    float circleCenterX = 0.0f;
    float circleCenterY = 0.0f;
    float squareHalfExtent = 0.0f;
    float triangleHalfWidth = 0.0f;
    float triangleHalfHeight = 0.0f;
    QuickTriangleDirection triangleDirection = QuickTriangleDirection::Up;
    QuickCircleDebugInfo circleDebug;
    QuickShapeDetectionResult detection;

    if (!detectQuickShapeCandidate(liveDabs, totalPath, lineLen, detection)) {
        logQuickShapeHoldDecision(detection.circleDebug, false);
        return false;
    }

    circleDebug = detection.circleDebug;
    circleCenterX = detection.centerX;
    circleCenterY = detection.centerY;
    squareHalfExtent = detection.squareHalfExtent;
    triangleHalfWidth = detection.triangleHalfWidth;
    triangleHalfHeight = detection.triangleHalfHeight;
    triangleDirection = detection.triangleDirection;

    const bool lineCandidate = detection.kind == QuickShapeDetectionResult::Kind::Line;
    switch (detection.kind) {
    case QuickShapeDetectionResult::Kind::Square:
        shapeKind = Kind::Square;
        break;
    case QuickShapeDetectionResult::Kind::Triangle:
        shapeKind = kindFromTriangleDirection(triangleDirection);
        break;
    case QuickShapeDetectionResult::Kind::Circle:
        shapeKind = Kind::Circle;
        break;
    case QuickShapeDetectionResult::Kind::Line:
        shapeKind = Kind::Line;
        break;
    case QuickShapeDetectionResult::Kind::None:
    default:
        logQuickShapeHoldDecision(circleDebug, lineCandidate);
        return false;
    }
    logQuickShapeHoldDecision(circleDebug, lineCandidate);

    m_state.active = true;
    m_state.kind = shapeKind;
    m_state.progress = 0.0f;
    m_state.anchorX = first.worldX;
    m_state.anchorY = first.worldY;
    m_state.cursorX = last.worldX;
    m_state.cursorY = last.worldY;
    m_state.cursorTargetX = last.worldX;
    m_state.cursorTargetY = last.worldY;
    m_state.circleCenterX = circleCenterX;
    m_state.circleCenterY = circleCenterY;
    m_state.circleAngleDirection = (shapeKind == Kind::Circle)
        ? computeQuickCircleAngleDirection(liveDabs, circleCenterX, circleCenterY)
        : 1.0f;
    m_state.squareHalfExtent = squareHalfExtent;
    m_state.triangleHalfWidth = triangleHalfWidth;
    m_state.triangleHalfHeight = triangleHalfHeight;
    if (shapeKind == Kind::TriangleUp || shapeKind == Kind::TriangleDown) {
        const float initialDy = last.worldY - circleCenterY;
        m_state.trianglePrimaryAxisSign = (initialDy < 0.0f) ? -1.0f : 1.0f;
    } else if (shapeKind == Kind::TriangleLeft || shapeKind == Kind::TriangleRight) {
        const float initialDx = last.worldX - circleCenterX;
        m_state.trianglePrimaryAxisSign = (initialDx < 0.0f) ? -1.0f : 1.0f;
    } else {
        m_state.trianglePrimaryAxisSign = 1.0f;
    }
    m_state.cursorDirty = true;
    m_state.sourceDabs = liveDabs;
    m_state.targetDabs = liveDabs;
    m_state.shapeParams.resize(cumulative.size(), 0.0f);
    for (size_t i = 0; i < cumulative.size(); ++i) {
        m_state.shapeParams[i] = cumulative[i] / totalPath;
    }
    if (shapeKind == Kind::TriangleUp || shapeKind == Kind::TriangleDown
        || shapeKind == Kind::TriangleLeft || shapeKind == Kind::TriangleRight) {
        const TriangleStrokeMapping mapping
            = buildTriangleStrokeMapping(shapeKind, circleCenterX, circleCenterY, triangleHalfWidth,
                triangleHalfHeight, m_state.sourceDabs, m_state.shapeParams);
        m_state.triangleVertexOrder = mapping.vertexOrder;
        m_state.triangleVertexParams = mapping.vertexParams;
        m_state.triangleMappingValid = mapping.valid;
    }

    apply(0.0f, true);
    return true;
}

void QuickShapeMorph::apply(float easedT, bool rebuildPreview)
{
    if (!m_state.active)
        return;
    if (!m_callbacks.isDrawing || !m_callbacks.isDrawing())
        return;
    if (!m_callbacks.activeLayerTileGrid || !m_callbacks.activeLayerTileGrid())
        return;

    if (!m_callbacks.getStrokeReplayData)
        return;
    std::shared_ptr<IEditableBrushStrokeReplayData> replayData = m_callbacks.getStrokeReplayData();
    if (!replayData)
        return;

    if (m_state.sourceDabs.empty() || m_state.sourceDabs.size() != m_state.targetDabs.size()
        || m_state.sourceDabs.size() != m_state.shapeParams.size()) {
        stop();
        return;
    }

    const size_t sourceCount = m_state.sourceDabs.size();
    std::vector<float> morphedX(sourceCount, 0.0f);
    std::vector<float> morphedY(sourceCount, 0.0f);

    if (m_state.kind == Kind::Line) {
        const float lineDx = m_state.cursorX - m_state.anchorX;
        const float lineDy = m_state.cursorY - m_state.anchorY;

        for (size_t i = 0; i < sourceCount; ++i) {
            const auto& src = m_state.sourceDabs[i];
            const float u = m_state.shapeParams[i];
            const float lineX = m_state.anchorX + lineDx * u;
            const float lineY = m_state.anchorY + lineDy * u;
            morphedX[i] = src.worldX + (lineX - src.worldX) * easedT;
            morphedY[i] = src.worldY + (lineY - src.worldY) * easedT;
        }
    } else if (m_state.kind == Kind::Circle) {
        const float dx = m_state.cursorX - m_state.circleCenterX;
        const float dy = m_state.cursorY - m_state.circleCenterY;
        const float radius = std::max(0.5f, std::sqrt(dx * dx + dy * dy));
        const float angleOffset = std::atan2(dy, dx);

        for (size_t i = 0; i < sourceCount; ++i) {
            const auto& src = m_state.sourceDabs[i];
            const float u = m_state.shapeParams[i];
            const float angle = angleOffset + (u * kCircleTau * m_state.circleAngleDirection);
            const float circleX = m_state.circleCenterX + std::cos(angle) * radius;
            const float circleY = m_state.circleCenterY + std::sin(angle) * radius;
            morphedX[i] = src.worldX + (circleX - src.worldX) * easedT;
            morphedY[i] = src.worldY + (circleY - src.worldY) * easedT;
        }
    } else if (m_state.kind == Kind::Square) {
        const float dx = m_state.cursorX - m_state.circleCenterX;
        const float dy = m_state.cursorY - m_state.circleCenterY;
        const float halfExtent = std::max(0.5f, std::max(std::abs(dx), std::abs(dy)));
        const SquareStrokeMapping mapping = buildSquareStrokeMapping(m_state.circleCenterX,
            m_state.circleCenterY, halfExtent, m_state.sourceDabs, m_state.shapeParams);

        for (size_t i = 0; i < sourceCount; ++i) {
            const auto& src = m_state.sourceDabs[i];
            const Vector2 p = mapping.valid
                ? pointOnSquareByStrokeMapping(m_state.circleCenterX, m_state.circleCenterY,
                      halfExtent, mapping.vertexOrder, mapping.vertexParams, m_state.shapeParams[i])
                : closestPointOnSquare(m_state.circleCenterX, m_state.circleCenterY, halfExtent,
                      { src.worldX, src.worldY });
            morphedX[i] = src.worldX + (p.x - src.worldX) * easedT;
            morphedY[i] = src.worldY + (p.y - src.worldY) * easedT;
        }
    } else if (m_state.kind == Kind::TriangleUp || m_state.kind == Kind::TriangleDown
        || m_state.kind == Kind::TriangleLeft || m_state.kind == Kind::TriangleRight) {
        const TriangleControlState triangleControl = resolveTriangleControlState(m_state);

        for (size_t i = 0; i < sourceCount; ++i) {
            const auto& src = m_state.sourceDabs[i];
            const Vector2 p = m_state.triangleMappingValid
                ? pointOnTriangleByStrokeMapping(triangleControl.kind, m_state.circleCenterX,
                      m_state.circleCenterY, triangleControl.halfWidth, triangleControl.halfHeight,
                      m_state.triangleVertexOrder, m_state.triangleVertexParams,
                      m_state.shapeParams[i])
                : closestPointOnTriangle(triangleControl.kind, m_state.circleCenterX,
                      m_state.circleCenterY, triangleControl.halfWidth, triangleControl.halfHeight,
                      { src.worldX, src.worldY });
            morphedX[i] = src.worldX + (p.x - src.worldX) * easedT;
            morphedY[i] = src.worldY + (p.y - src.worldY) * easedT;
        }
    } else {
        stop();
        return;
    }

    auto computePathLength = [](const std::vector<BrushStrokeReplayPoint>& dabs,
                                 const std::vector<float>* xOverride = nullptr,
                                 const std::vector<float>* yOverride = nullptr) {
        if (dabs.size() < 2)
            return 0.0f;
        float length = 0.0f;
        for (size_t i = 1; i < dabs.size(); ++i) {
            const float ax = xOverride ? (*xOverride)[i - 1] : dabs[i - 1].worldX;
            const float ay = yOverride ? (*yOverride)[i - 1] : dabs[i - 1].worldY;
            const float bx = xOverride ? (*xOverride)[i] : dabs[i].worldX;
            const float by = yOverride ? (*yOverride)[i] : dabs[i].worldY;
            const float ddx = bx - ax;
            const float ddy = by - ay;
            length += std::sqrt(ddx * ddx + ddy * ddy);
        }
        return length;
    };

    const float sourcePathLength = computePathLength(m_state.sourceDabs);
    const float morphedPathLength = computePathLength(m_state.sourceDabs, &morphedX, &morphedY);
    const float sourceSpacing = (sourceCount > 1 && sourcePathLength > 0.001f)
        ? (sourcePathLength / static_cast<float>(sourceCount - 1))
        : std::max(1.0f,
              (m_callbacks.getBrushEffectiveRadius ? m_callbacks.getBrushEffectiveRadius() : 8.0f)
                  * (m_callbacks.getBrushSpacing ? m_callbacks.getBrushSpacing() : 0.25f));
    const float targetSpacing = std::max(kResampleMinSpacing, sourceSpacing);

    size_t desiredCount = sourceCount;
    if (morphedPathLength > 0.001f && targetSpacing > 0.001f) {
        const size_t pathBasedCount
            = static_cast<size_t>(std::ceil(morphedPathLength / targetSpacing)) + 1;
        desiredCount = std::max(sourceCount, pathBasedCount);
    }
    const size_t maxByMultiplier = sourceCount * kResampleMaxDabMultiplier;
    const size_t maxAllowed = std::min(kResampleHardCap, std::max(sourceCount, maxByMultiplier));
    desiredCount = std::clamp(desiredCount, sourceCount, maxAllowed);

    std::vector<BrushStrokeReplayPoint> morphedPoints;
    morphedPoints.reserve(desiredCount);
    size_t seg = 0;
    const auto& sourceParams = m_state.shapeParams;

    for (size_t i = 0; i < desiredCount; ++i) {
        const float u = (desiredCount <= 1)
            ? 0.0f
            : static_cast<float>(i) / static_cast<float>(desiredCount - 1);
        while (seg + 1 < sourceCount && sourceParams[seg + 1] < u) {
            ++seg;
        }

        const size_t next = std::min(seg + 1, sourceCount - 1);
        const float p0 = sourceParams[seg];
        const float p1 = sourceParams[next];
        const float denom = p1 - p0;
        const float t = (next == seg || std::abs(denom) <= 0.000001f)
            ? 0.0f
            : std::clamp((u - p0) / denom, 0.0f, 1.0f);

        const auto& a = m_state.sourceDabs[seg];
        const auto& b = m_state.sourceDabs[next];
        BrushStrokeReplayPoint point = a;
        point.worldX = morphedX[seg] + (morphedX[next] - morphedX[seg]) * t;
        point.worldY = morphedY[seg] + (morphedY[next] - morphedY[seg]) * t;
        point.baseWorldX = point.worldX;
        point.baseWorldY = point.worldY;
        point.pressure = a.pressure + (b.pressure - a.pressure) * t;
        point.radius = a.radius + (b.radius - a.radius) * t;
        point.baseRadius = a.baseRadius + (b.baseRadius - a.baseRadius) * t;
        point.hardness = a.hardness + (b.hardness - a.hardness) * t;
        point.roundness = a.roundness + (b.roundness - a.roundness) * t;
        point.angleDegrees = a.angleDegrees + (b.angleDegrees - a.angleDegrees) * t;
        point.alpha = static_cast<uint8_t>(std::clamp(static_cast<float>(a.alpha)
                + (static_cast<float>(b.alpha) - static_cast<float>(a.alpha)) * t,
            0.0f, 255.0f));
        point.baseAlpha = static_cast<uint8_t>(std::clamp(static_cast<float>(a.baseAlpha)
                + (static_cast<float>(b.baseAlpha) - static_cast<float>(a.baseAlpha)) * t,
            0.0f, 255.0f));
        morphedPoints.push_back(point);
    }

    if (!replayData->replacePoints(morphedPoints)) {
        stop();
        return;
    }

    if (!rebuildPreview)
        return;

    if (m_callbacks.rebuildPreview) {
        m_callbacks.rebuildPreview();
    }

    if (m_callbacks.onMorphApplied) {
        m_callbacks.onMorphApplied();
    }
}

QuickShapeMorph::Kind QuickShapeMorph::kindFromTriangleDirection(QuickTriangleDirection direction)
{
    switch (direction) {
    case QuickTriangleDirection::Up:
        return Kind::TriangleUp;
    case QuickTriangleDirection::Down:
        return Kind::TriangleDown;
    case QuickTriangleDirection::Left:
        return Kind::TriangleLeft;
    case QuickTriangleDirection::Right:
        return Kind::TriangleRight;
    default:
        return Kind::TriangleUp;
    }
}

} // namespace aether
