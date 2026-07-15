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

namespace aether {

namespace {

constexpr int kContourPoints = 96;
// ~25% alpha: includes the soft-edge falloff zone so the cursor traces the
// actual visible footprint rather than only the opaque core.
constexpr uint8_t kAlphaThreshold = 64;
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

// Label 4-connected components of (alpha >= threshold) and, for each, trace
// a closed contour by casting kContourPoints rays from the component centroid.
// Rays are restricted to that component (other components / background are
// treated as outside), so multi-blob dabs (flower petals, scattered spots) are
// returned as separate loops instead of one self-intersecting polygon with
// spikes through the mask center.
//
// All output vertices are expressed in mask-uv relative to the MASK center
// (not the per-component centroid), so applyBrushTransform — which positions
// the cursor around the brush center — works unchanged.
std::vector<std::vector<Vector2>> traceAlphaMaskContours(
    const BrushCursorContourBuilder::Request& req)
{
    const int W = req.maskWidth;
    const int H = req.maskHeight;
    const auto& mask = req.alphaMask;
    if (W < 2 || H < 2 || static_cast<int>(mask.size()) < W * H) {
        return { buildCircleContour() };
    }

    // ---- Connected-component labelling (iterative DFS, 4-connectivity).
    const size_t N = static_cast<size_t>(W) * static_cast<size_t>(H);
    std::vector<int> labels(N, 0);

    struct Component {
        int id = 0;
        int area = 0;
        float cxSum = 0.0f;
        float cySum = 0.0f;
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
    };
    std::vector<Component> comps;

    std::vector<int> stack;
    stack.reserve(256);
    int nextId = 0;

    for (int y0 = 0; y0 < H; ++y0) {
        for (int x0 = 0; x0 < W; ++x0) {
            const int seed = y0 * W + x0;
            if (labels[seed] != 0)
                continue;
            if (mask[seed] < kAlphaThreshold)
                continue;

            ++nextId;
            Component c;
            c.id = nextId;
            c.minX = x0;
            c.maxX = x0;
            c.minY = y0;
            c.maxY = y0;

            labels[seed] = nextId;
            stack.clear();
            stack.push_back(seed);

            while (!stack.empty()) {
                const int idx = stack.back();
                stack.pop_back();
                const int px = idx % W;
                const int py = idx / W;
                ++c.area;
                c.cxSum += static_cast<float>(px);
                c.cySum += static_cast<float>(py);
                if (px < c.minX)
                    c.minX = px;
                if (py < c.minY)
                    c.minY = py;
                if (px > c.maxX)
                    c.maxX = px;
                if (py > c.maxY)
                    c.maxY = py;

                static const int dxs[4] = { 1, -1, 0, 0 };
                static const int dys[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; ++k) {
                    const int nx = px + dxs[k];
                    const int ny = py + dys[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H)
                        continue;
                    const int nidx = ny * W + nx;
                    if (labels[nidx] != 0)
                        continue;
                    if (mask[nidx] < kAlphaThreshold)
                        continue;
                    labels[nidx] = nextId;
                    stack.push_back(nidx);
                }
            }

            // Drop tiny specks (anti-alias noise).
            if (c.area >= 4) {
                comps.push_back(c);
            }
        }
    }

    if (comps.empty()) {
        return { buildCircleContour() };
    }

    const float maskCx = static_cast<float>(W - 1) * 0.5f;
    const float maskCy = static_cast<float>(H - 1) * 0.5f;
    const float invHalfW = 2.0f / static_cast<float>(W - 1);
    const float invHalfH = 2.0f / static_cast<float>(H - 1);

    std::vector<std::vector<Vector2>> result;
    result.reserve(comps.size());

    for (const auto& c : comps) {
        const float ccx = c.cxSum / static_cast<float>(c.area);
        const float ccy = c.cySum / static_cast<float>(c.area);

        // Ray length bounded by component bbox extent plus a safety margin.
        const float halfBW = static_cast<float>(c.maxX - c.minX + 1) * 0.5f + 1.0f;
        const float halfBH = static_cast<float>(c.maxY - c.minY + 1) * 0.5f + 1.0f;
        const float maxR = std::hypot(halfBW, halfBH) * 1.5f;
        const int steps = std::max(static_cast<int>(std::ceil(maxR)) * 2, 16);
        const float invSteps = 1.0f / static_cast<float>(steps);

        std::vector<Vector2> loop;
        loop.reserve(kContourPoints);
        bool anyHit = false;

        for (int i = 0; i < kContourPoints; ++i) {
            const float ang
                = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kContourPoints);
            const float dx = std::cos(ang);
            const float dy = std::sin(ang);

            float lastInsideR = 0.0f;
            for (int s = 0; s <= steps; ++s) {
                const float r = static_cast<float>(s) * invSteps * maxR;
                const int px = static_cast<int>(std::lround(ccx + dx * r));
                const int py = static_cast<int>(std::lround(ccy + dy * r));
                if (px < 0 || px >= W || py < 0 || py >= H) {
                    break;
                }
                if (labels[static_cast<size_t>(py) * static_cast<size_t>(W)
                        + static_cast<size_t>(px)]
                    == c.id) {
                    lastInsideR = r;
                }
            }

            if (lastInsideR > 0.0f)
                anyHit = true;

            // Vertex in pixel coords, then re-expressed relative to the MASK
            // center so the shared brush transform places it correctly.
            const float vx = ccx + dx * lastInsideR;
            const float vy = ccy + dy * lastInsideR;
            loop.emplace_back((vx - maskCx) * invHalfW, (vy - maskCy) * invHalfH);
        }

        if (anyHit) {
            result.push_back(std::move(loop));
        }
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
