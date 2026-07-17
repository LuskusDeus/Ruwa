// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H   C U R S O R   C O N T O U R   B U I L D E R
// ==========================================================================

#include "features/canvas/rendering/BrushCursorContourBuilder.h"

#include <QCoreApplication>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace aether {

namespace {

constexpr int kContourPoints = 96;
// ~25% alpha: includes the faint antialiased edge instead of tracing only the opaque core.
constexpr uint8_t kAlphaThreshold = 64;
constexpr float kSimplifyTolerancePixels = 0.75f;
constexpr int kSmoothingPassCount = 2;
constexpr size_t kMaxContourPoints = 512;
constexpr size_t kMaxContourCount = 32;
constexpr size_t kMaxTotalContourPoints = 2048;
constexpr float kMinRelativeContourArea = 0.001f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

std::vector<Vector2> buildCircleContour()
{
    std::vector<Vector2> out;
    out.reserve(kContourPoints);
    for (int i = 0; i < kContourPoints; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kContourPoints);
        out.emplace_back(std::cos(a), std::sin(a));
    }
    return out;
}

struct GridPoint {
    int x = 0;
    int y = 0;

    bool operator==(const GridPoint& other) const { return x == other.x && y == other.y; }
};

struct BoundaryEdge {
    GridPoint from;
    GridPoint to;
    bool used = false;
};

struct ContourCandidate {
    std::vector<Vector2> points;
    float area = 0.0f;
};

uint64_t gridPointKey(const GridPoint& point)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(point.x)) << 32u)
        | static_cast<uint32_t>(point.y);
}

int directionIndex(const GridPoint& from, const GridPoint& to)
{
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    if (dx > 0)
        return 0;
    if (dy > 0)
        return 1;
    if (dx < 0)
        return 2;
    return 3;
}

int continuationRank(const BoundaryEdge& incoming, const BoundaryEdge& candidate)
{
    const int inDirection = directionIndex(incoming.from, incoming.to);
    const int outDirection = directionIndex(candidate.from, candidate.to);
    const int clockwiseTurn = (outDirection - inDirection + 4) % 4;
    switch (clockwiseTurn) {
    case 1:
        return 0; // Keep diagonally touching islands as separate tight loops.
    case 0:
        return 1;
    case 3:
        return 2;
    default:
        return 3;
    }
}

float distanceToSegmentSquared(const Vector2& point, const Vector2& a, const Vector2& b)
{
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float abLengthSquared = abX * abX + abY * abY;
    if (abLengthSquared <= std::numeric_limits<float>::epsilon()) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return dx * dx + dy * dy;
    }

    const float projection
        = std::clamp(((point.x - a.x) * abX + (point.y - a.y) * abY) / abLengthSquared,
            0.0f, 1.0f);
    const float dx = point.x - (a.x + abX * projection);
    const float dy = point.y - (a.y + abY * projection);
    return dx * dx + dy * dy;
}

std::vector<Vector2> simplifyOpenPolyline(
    const std::vector<Vector2>& points, float toleranceSquared)
{
    if (points.size() <= 2) {
        return points;
    }

    std::vector<uint8_t> keep(points.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> spans;
    spans.emplace_back(0, points.size() - 1);

    while (!spans.empty()) {
        const auto [first, last] = spans.back();
        spans.pop_back();

        float maxDistanceSquared = toleranceSquared;
        size_t split = last;
        for (size_t i = first + 1; i < last; ++i) {
            const float distanceSquared
                = distanceToSegmentSquared(points[i], points[first], points[last]);
            if (distanceSquared > maxDistanceSquared) {
                maxDistanceSquared = distanceSquared;
                split = i;
            }
        }

        if (split != last) {
            keep[split] = 1;
            spans.emplace_back(first, split);
            spans.emplace_back(split, last);
        }
    }

    std::vector<Vector2> simplified;
    simplified.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            simplified.push_back(points[i]);
        }
    }
    return simplified;
}

std::vector<Vector2> simplifyClosedContour(
    const std::vector<Vector2>& points, float tolerancePixels)
{
    if (points.size() <= 4) {
        return points;
    }

    size_t opposite = 1;
    float farthestDistanceSquared = 0.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        const float dx = points[i].x - points[0].x;
        const float dy = points[i].y - points[0].y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared > farthestDistanceSquared) {
            farthestDistanceSquared = distanceSquared;
            opposite = i;
        }
    }

    std::vector<Vector2> firstArc(points.begin(), points.begin() + opposite + 1);
    std::vector<Vector2> secondArc(points.begin() + opposite, points.end());
    secondArc.push_back(points.front());

    const float toleranceSquared = tolerancePixels * tolerancePixels;
    firstArc = simplifyOpenPolyline(firstArc, toleranceSquared);
    secondArc = simplifyOpenPolyline(secondArc, toleranceSquared);

    std::vector<Vector2> simplified;
    simplified.reserve(firstArc.size() + secondArc.size() - 2);
    simplified.insert(simplified.end(), firstArc.begin(), firstArc.end() - 1);
    simplified.insert(simplified.end(), secondArc.begin(), secondArc.end() - 1);
    if (simplified.size() >= 3) {
        return simplified;
    }

    size_t third = 1;
    float thirdDistanceSquared = -1.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        if (i == opposite)
            continue;
        const float distanceSquared
            = distanceToSegmentSquared(points[i], points[0], points[opposite]);
        if (distanceSquared > thirdDistanceSquared) {
            thirdDistanceSquared = distanceSquared;
            third = i;
        }
    }
    if (third < opposite) {
        return { points[0], points[third], points[opposite] };
    }
    return { points[0], points[opposite], points[third] };
}

float contourArea(const std::vector<Vector2>& points)
{
    float twiceArea = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vector2& a = points[i];
        const Vector2& b = points[(i + 1) % points.size()];
        twiceArea += a.x * b.y - b.x * a.y;
    }
    return std::abs(twiceArea) * 0.5f;
}

std::vector<Vector2> smoothClosedContour(std::vector<Vector2> points)
{
    if (points.size() < 8) {
        return points;
    }

    std::vector<Vector2> smoothed(points.size());
    for (int pass = 0; pass < kSmoothingPassCount; ++pass) {
        for (size_t i = 0; i < points.size(); ++i) {
            const Vector2& previous = points[(i + points.size() - 1) % points.size()];
            const Vector2& current = points[i];
            const Vector2& next = points[(i + 1) % points.size()];
            smoothed[i] = { previous.x * 0.25f + current.x * 0.5f + next.x * 0.25f,
                previous.y * 0.25f + current.y * 0.5f + next.y * 0.25f };
        }
        points.swap(smoothed);
    }
    return points;
}

// Trace the real pixel-cell boundaries of the thresholded alpha mask. This preserves concavities,
// holes, thin protrusions, and disconnected islands. The resulting loops are simplified by less
// than one source texel and capped to a fixed rendering budget.
std::vector<std::vector<Vector2>> traceAlphaMaskContours(
    const BrushCursorContourBuilder::Request& req)
{
    const int W = req.maskWidth;
    const int H = req.maskHeight;
    const auto& mask = req.alphaMask;
    if (W < 2 || H < 2 || static_cast<int>(mask.size()) < W * H) {
        return { buildCircleContour() };
    }

    std::vector<BoundaryEdge> edges;
    edges.reserve(static_cast<size_t>(W + H) * 4u);
    const auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < W && y < H
            && mask[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)]
                >= kAlphaThreshold;
    };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!inside(x, y))
                continue;
            if (!inside(x, y - 1))
                edges.push_back({ { x, y }, { x + 1, y } });
            if (!inside(x + 1, y))
                edges.push_back({ { x + 1, y }, { x + 1, y + 1 } });
            if (!inside(x, y + 1))
                edges.push_back({ { x + 1, y + 1 }, { x, y + 1 } });
            if (!inside(x - 1, y))
                edges.push_back({ { x, y + 1 }, { x, y } });
        }
    }

    if (edges.empty()) {
        return { buildCircleContour() };
    }

    std::unordered_map<uint64_t, std::vector<size_t>> outgoing;
    outgoing.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i) {
        outgoing[gridPointKey(edges[i].from)].push_back(i);
    }

    std::vector<ContourCandidate> candidates;
    for (size_t firstEdge = 0; firstEdge < edges.size(); ++firstEdge) {
        if (edges[firstEdge].used)
            continue;

        std::vector<Vector2> loop;
        loop.reserve(128);
        const GridPoint start = edges[firstEdge].from;
        size_t edgeIndex = firstEdge;
        bool closed = false;

        for (size_t guard = 0; guard <= edges.size(); ++guard) {
            BoundaryEdge& edge = edges[edgeIndex];
            if (edge.used)
                break;
            edge.used = true;
            loop.emplace_back(static_cast<float>(edge.from.x), static_cast<float>(edge.from.y));

            if (edge.to == start) {
                closed = true;
                break;
            }

            const auto nextIt = outgoing.find(gridPointKey(edge.to));
            if (nextIt == outgoing.end())
                break;

            size_t nextEdge = edges.size();
            int bestRank = std::numeric_limits<int>::max();
            for (size_t candidateIndex : nextIt->second) {
                if (edges[candidateIndex].used)
                    continue;
                const int rank = continuationRank(edge, edges[candidateIndex]);
                if (rank < bestRank) {
                    bestRank = rank;
                    nextEdge = candidateIndex;
                }
            }
            if (nextEdge == edges.size())
                break;
            edgeIndex = nextEdge;
        }

        if (!closed || loop.size() < 3)
            continue;
        const float area = contourArea(loop);
        if (area >= 4.0f) {
            candidates.push_back({ std::move(loop), area });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.area > b.area;
    });

    std::vector<std::vector<Vector2>> result;
    result.reserve(std::min(candidates.size(), kMaxContourCount));
    const float minContourArea = candidates.empty()
        ? 8.0f
        : std::max(8.0f, candidates.front().area * kMinRelativeContourArea);
    size_t totalPointCount = 0;
    for (auto& candidate : candidates) {
        if (result.size() >= kMaxContourCount || totalPointCount + 3 > kMaxTotalContourPoints)
            break;
        if (candidate.area < minContourArea)
            break;

        std::vector<Vector2> smoothed = smoothClosedContour(std::move(candidate.points));
        float tolerance = kSimplifyTolerancePixels;
        std::vector<Vector2> simplified = simplifyClosedContour(smoothed, tolerance);
        const size_t pointBudget
            = std::min(kMaxContourPoints, kMaxTotalContourPoints - totalPointCount);
        while (simplified.size() > pointBudget) {
            tolerance *= 1.5f;
            simplified = simplifyClosedContour(smoothed, tolerance);
        }
        if (simplified.size() < 3)
            continue;

        const float invWidth = 1.0f / static_cast<float>(W);
        const float invHeight = 1.0f / static_cast<float>(H);
        for (Vector2& point : simplified) {
            point.x = point.x * invWidth * 2.0f - 1.0f;
            point.y = point.y * invHeight * 2.0f - 1.0f;
        }
        totalPointCount += simplified.size();
        result.push_back(std::move(simplified));
    }

    if (result.empty()) {
        return { buildCircleContour() };
    }
    return result;
}

// Mirrors TileBrush::sampleDabFalloff transform chain so the cursor outline
// matches the painted footprint:
//   shape-uv -> (dabXScale, dabYScale) -> rot(m_dabRotation)
//   -> y *= roundness -> rot(m_angleDegrees) -> world (in units of radius)
void applyBrushTransform(
    std::vector<Vector2>& contour, const BrushCursorContourBuilder::Request& req)
{
    const float roundness = std::max(0.01f, std::clamp(req.roundness, 0.0f, 1.0f));
    const float brushA = req.angleDegrees * kDegToRad;
    const float bCos = std::cos(brushA);
    const float bSin = std::sin(brushA);

    const bool applyMaskTransforms = req.dabType > 0;
    const float shapeA = req.dabRotation * kDegToRad;
    const float sCos = std::cos(shapeA);
    const float sSin = std::sin(shapeA);
    const float xScale = std::clamp(req.dabXScale, 0.0001f, 1.0f);
    const float yScale = std::clamp(req.dabYScale, 0.0001f, 1.0f);

    for (auto& p : contour) {
        float sx = p.x;
        float sy = p.y;

        if (applyMaskTransforms) {
            sx *= xScale;
            sy *= yScale;
            const float rx = sx * sCos + sy * sSin;
            const float ry = -sx * sSin + sy * sCos;
            sx = rx;
            sy = ry;
        }

        const float bx = sx;
        const float by = sy * roundness;
        p.x = bx * bCos - by * bSin;
        p.y = bx * bSin + by * bCos;
    }
}

std::vector<std::vector<Vector2>> computeContours(BrushCursorContourBuilder::Request req)
{
    std::vector<std::vector<Vector2>> contours;
    if (req.dabType <= 0) {
        contours.push_back(buildCircleContour());
    } else {
        contours = traceAlphaMaskContours(req);
    }
    for (auto& c : contours) {
        applyBrushTransform(c, req);
    }
    return contours;
}

} // namespace

BrushCursorContourBuilder::BrushCursorContourBuilder(QObject* parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(16);
    connect(&m_debounce, &QTimer::timeout, this, &BrushCursorContourBuilder::onDebounceFired);
}

BrushCursorContourBuilder::~BrushCursorContourBuilder() = default;

void BrushCursorContourBuilder::submit(Request request)
{
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending = std::move(request);
    }
    if (!m_debounce.isActive()) {
        m_debounce.start();
    }
}

void BrushCursorContourBuilder::onDebounceFired()
{
    std::optional<Request> req;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        req.swap(m_pending);
    }
    if (!req)
        return;

    const quint64 generation = m_latestGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    QPointer<BrushCursorContourBuilder> self(this);
    Request request = std::move(*req);

    (void) QtConcurrent::run([self, request = std::move(request), generation]() mutable {
        auto contours = computeContours(std::move(request));

        // Hop back to the owner thread for emission; check liveness + freshness there.
        QCoreApplication* app = QCoreApplication::instance();
        QObject* hop = self ? static_cast<QObject*>(self.data()) : static_cast<QObject*>(app);
        if (!hop)
            return;
        QMetaObject::invokeMethod(
            hop,
            [self, generation, contours = std::move(contours)]() mutable {
                if (!self)
                    return;
                if (generation != self->m_latestGeneration.load(std::memory_order_acquire)) {
                    return; // stale
                }
                emit self->contoursReady(contours);
            },
            Qt::QueuedConnection);
    });
}

} // namespace aether
