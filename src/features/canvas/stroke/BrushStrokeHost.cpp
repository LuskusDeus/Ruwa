// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/stroke/BrushStrokeHost.h"

#include "features/brush/engine/BrushEngine.h"
#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/canvas/quick-shape/QuickShapeMorph.h"
#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileBrush.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TileGrid.h"
#include <QOpenGLContext>
#include <QString>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr float kQuickLineMovementEpsilon = 0.05f;
constexpr double kRealtimePreviewSamplingEnableRateHz = 140.0;
constexpr double kRealtimePreviewSamplingTargetHz = 90.0;
constexpr size_t kRealtimePreviewSamplingMinDabs = 48;
constexpr size_t kRealtimePreviewSamplingMaxDabs = 768;
// Time one BoundedLatency tick may spend before the queue is decimated to fit.
// A fraction of a 60 Hz frame, so servicing the queue cannot eat the frame it
// draws into. It is a target for the capacity controller, not a hard cut: a
// tick always empties the queue, it only decides beforehand how many samples
// that queue is allowed to contain.
constexpr double kStrokeInputTickBudgetMs = 6.0;
// A tick must always make real progress, no matter how expensive a single
// sample turns out to be. Below this the stroke would be reconstructed from so
// few points that its shape would visibly change.
constexpr size_t kStrokeInputMinTickCapacity = 12;
constexpr size_t kStrokeInputMaxTickCapacity = 8192;
// Below this a tick carries no information about throughput: it drained a queue
// that was never full, so its duration measures one sample plus fixed overhead.
constexpr size_t kStrokeInputCapacityMeasureMinSamples = 4;
constexpr size_t kStrokeInputCompactionCadenceSamples = 8;
// Ordering-only nudge for recovered samples that share a timestamp. This keeps
// their geometry without pretending each point consumed a full input-frame.
constexpr float kStrokeInputMonotonicNudgeSec = 0.0005f;
constexpr float kAutoInputSmoothingMaxScreenPx = 1.25f;
constexpr float kAutoInputSmoothingMaxWorldRadius = 24.0f;

bool brushRequiresGpuEffect(const aether::TileBrush* brush)
{
    return brush
        && (brush->isBlurMode() || brush->isSmudgeMode() || brush->isWetMode()
            || brush->isLiquifyMode());
}

void appendStepTouchedTileKeys(float fromX, float fromY, float toX, float toY, float radius,
    uint32_t canvasWidth, uint32_t canvasHeight,
    std::unordered_set<aether::TileKey, aether::TileKeyHash>& outKeys)
{
    if (radius <= 0.0f) {
        return;
    }

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
        if (tMinX > tMaxX || tMinY > tMaxY) {
            return;
        }
    }

    for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
        for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
            outKeys.insert(aether::TileKey { tx, ty });
        }
    }
}

aether::Vector2 midpoint(const aether::Vector2& a, const aether::Vector2& b)
{
    return { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
}

aether::Vector2 quadraticPoint(const aether::Vector2& start, const aether::Vector2& control,
    const aether::Vector2& end, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;
    return { uu * start.x + 2.0f * u * t * control.x + tt * end.x,
        uu * start.y + 2.0f * u * t * control.y + tt * end.y };
}

aether::Vector2 cubicPoint(const aether::Vector2& b0, const aether::Vector2& b1,
    const aether::Vector2& b2, const aether::Vector2& b3, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float uuu = uu * u;
    const float tt = t * t;
    const float ttt = tt * t;
    return { uuu * b0.x + 3.0f * uu * t * b1.x + 3.0f * u * tt * b2.x + ttt * b3.x,
        uuu * b0.y + 3.0f * uu * t * b1.y + 3.0f * u * tt * b2.y + ttt * b3.y };
}

float lerpScalar(float a, float b, float t)
{
    return a + (b - a) * t;
}

float elapsedSeconds(const QElapsedTimer& timer)
{
    if (!timer.isValid()) {
        return 0.0f;
    }
    return std::max(0.0f, static_cast<float>(timer.nsecsElapsed()) / 1000000000.0f);
}

float vectorLength(const aether::Vector2& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

float monotonePathDerivative(float beforeValue, float value, float afterValue,
    float incomingDistance, float outgoingDistance)
{
    constexpr float kMinDistance = 0.000001f;
    const bool hasIncoming = incomingDistance > kMinDistance;
    const bool hasOutgoing = outgoingDistance > kMinDistance;
    if (!hasIncoming && !hasOutgoing) {
        return 0.0f;
    }
    if (!hasIncoming) {
        return (afterValue - value) / outgoingDistance;
    }
    if (!hasOutgoing) {
        return (value - beforeValue) / incomingDistance;
    }

    const float incomingSlope = (value - beforeValue) / incomingDistance;
    const float outgoingSlope = (afterValue - value) / outgoingDistance;
    if (incomingSlope * outgoingSlope <= 0.0f) {
        return 0.0f;
    }

    // PCHIP weighted harmonic mean. Unlike an arithmetic Catmull tangent it
    // cannot overshoot a local extremum, which is important when a speed curve
    // is mapped to a large radius range.
    const float incomingWeight = 2.0f * outgoingDistance + incomingDistance;
    const float outgoingWeight = outgoingDistance + 2.0f * incomingDistance;
    return (incomingWeight + outgoingWeight)
        / (incomingWeight / incomingSlope + outgoingWeight / outgoingSlope);
}

float normalizedDot(const aether::Vector2& a, const aether::Vector2& b)
{
    const float lenA = vectorLength(a);
    const float lenB = vectorLength(b);
    if (lenA <= 0.0001f || lenB <= 0.0001f) {
        return 1.0f;
    }
    return std::clamp((a.x * b.x + a.y * b.y) / (lenA * lenB), -1.0f, 1.0f);
}

float pointLineDistance(
    const aether::Vector2& point, const aether::Vector2& lineStart, const aether::Vector2& lineEnd)
{
    const float dx = lineEnd.x - lineStart.x;
    const float dy = lineEnd.y - lineStart.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq <= 0.0001f) {
        return vectorLength({ point.x - lineStart.x, point.y - lineStart.y });
    }
    const float t = std::clamp(
        ((point.x - lineStart.x) * dx + (point.y - lineStart.y) * dy) / lenSq, 0.0f, 1.0f);
    const float projX = lineStart.x + dx * t;
    const float projY = lineStart.y + dy * t;
    return vectorLength({ point.x - projX, point.y - projY });
}

// Stable one-step integration of a critically damped second-order follower.
// Carrying velocity across calls makes the output C1-continuous; responseWindow
// and step may be seconds, pixels, or any other matching domain. Both pressure
// and speed use this same dynamics primitive.
void advanceCriticallyDampedFollower(
    float target, float responseWindow, float step, float& value, float& velocity)
{
    if (!std::isfinite(target) || !std::isfinite(responseWindow) || !std::isfinite(step)
        || !std::isfinite(value) || !std::isfinite(velocity) || responseWindow <= 0.0f
        || step <= 0.0f) {
        return;
    }

    const float omega = 2.0f / responseWindow;
    const float x = omega * step;
    // Padé approximation of exp(-x): cheap and unconditionally stable for any
    // step. Keep this identical to the established pressure implementation.
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = value - target;
    const float temp = (velocity + omega * change) * step;
    velocity = (velocity - omega * temp) * decay;
    value = target + (change + temp) * decay;
}

} // namespace

namespace aether {

BrushStrokeHost::BrushStrokeHost(QObject* parent, Callbacks callbacks)
    : QObject(parent)
    , m_callbacks(std::move(callbacks))
{
    m_finalizeTimer.setSingleShot(true);
    m_finalizeTimer.setInterval(0);
    m_finalizeTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_finalizeTimer, &QTimer::timeout, this, &BrushStrokeHost::finalizeStroke);

    // Fallback service point only. The queue is normally drained by
    // drainStrokeInputForFrame at the top of each frame; this timer covers the
    // stretches where no frame is coming (nothing else requested a repaint yet,
    // or the widget is not visible). A zero interval used to be the primary
    // mechanism and was a poor one: Qt fires zero timers only once the window
    // system queue has drained, so a device streaming 200-266 Hz of packets
    // starved exactly the timer meant to consume them.
    m_strokeInputTimer.setSingleShot(true);
    m_strokeInputTimer.setInterval(8);
    m_strokeInputTimer.setTimerType(Qt::PreciseTimer);
    connect(
        &m_strokeInputTimer, &QTimer::timeout, this, &BrushStrokeHost::processQueuedStrokeInput);

    m_stabilizerCatchupTimer.setSingleShot(false);
    m_stabilizerCatchupTimer.setInterval(8);
    m_stabilizerCatchupTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_stabilizerCatchupTimer, &QTimer::timeout, this,
        &BrushStrokeHost::processStabilizerCatchup);

    // Liquify dwell: ~30 Hz time-based dab while the brush is held still.
    m_liquifyDwellTimer.setSingleShot(false);
    m_liquifyDwellTimer.setInterval(30);
    m_liquifyDwellTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_liquifyDwellTimer, &QTimer::timeout, this, &BrushStrokeHost::emitLiquifyDwell);
}

TileBrush* BrushStrokeHost::brush() const
{
    return m_callbacks.getBrush ? m_callbacks.getBrush() : nullptr;
}

TileGrid* BrushStrokeHost::activeLayerTileGrid() const
{
    return m_callbacks.getActiveLayerTileGrid ? m_callbacks.getActiveLayerTileGrid() : nullptr;
}

ruwa::core::layers::LayerData* BrushStrokeHost::activeLayer() const
{
    return m_callbacks.getActiveLayer ? m_callbacks.getActiveLayer() : nullptr;
}

TileGrid* BrushStrokeHost::effectivePaintMask(
    ruwa::core::layers::LayerData* layer, TileGrid* grid) const
{
    return m_callbacks.getEffectivePaintMask ? m_callbacks.getEffectivePaintMask(layer, grid)
                                             : nullptr;
}

bool BrushStrokeHost::shouldPreserveAlphaForPaintMask(
    const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) const
{
    if (m_callbacks.shouldPreserveAlphaForPaintMask) {
        return m_callbacks.shouldPreserveAlphaForPaintMask(layer, paintMask);
    }
    return layer && (layer->alphaLock || LayerCompositingBuilder::hasSoftMaskAlpha(paintMask));
}

void BrushStrokeHost::configureBrushSelectionMaskAlpha(
    TileBrush& brush, const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) const
{
    const bool preserveAlphaForStroke
        = !brush.isEraseMode() && shouldPreserveAlphaForPaintMask(layer, paintMask);
    brush.setSelectionMaskAffectsAlpha(paintMask && !preserveAlphaForStroke && !brush.isBlurMode()
        && !brush.isSmudgeMode() && !brush.isWetMode() && !brush.isLiquifyMode());
}

BrushExecutionBackend* BrushStrokeHost::brushExecutionBackend() const
{
    return m_callbacks.getBrushExecutionBackend ? m_callbacks.getBrushExecutionBackend() : nullptr;
}

QuickShapeMorph* BrushStrokeHost::quickShapeMorph() const
{
    return m_callbacks.getQuickShapeMorph ? m_callbacks.getQuickShapeMorph() : nullptr;
}

uint32_t BrushStrokeHost::documentBoundsWidth() const
{
    return m_callbacks.getDocumentBoundsWidth ? m_callbacks.getDocumentBoundsWidth() : 0;
}

uint32_t BrushStrokeHost::documentBoundsHeight() const
{
    return m_callbacks.getDocumentBoundsHeight ? m_callbacks.getDocumentBoundsHeight() : 0;
}

float BrushStrokeHost::viewportZoom() const
{
    if (!m_callbacks.getViewportZoom) {
        return 1.0f;
    }

    const float zoom = m_callbacks.getViewportZoom();
    if (!std::isfinite(zoom) || zoom <= 0.0f) {
        return 1.0f;
    }
    return zoom;
}

float BrushStrokeHost::dynamicsSmoothingWindowWorldPx() const
{
    // Floor in screen space: small brushes and fine detail keep the original
    // responsiveness (this is the legacy fixed window, converted to world px).
    constexpr float kMinScreenPx = 8.0f;
    const float zoom = std::max(viewportZoom(), 0.05f);
    float windowWorldPx = std::max(1.0f, kMinScreenPx / zoom);

    // Couple the window to the brush base radius (in world px, independent of
    // the current pressure so the window stays stable across the stroke). The
    // factor is a fraction of the radius: a pressure change is then spread over
    // roughly the gap between dabs (spacing ~ radius * spacingFactor), which is
    // the scale at which a step would otherwise be visible. On a large brush
    // this dwarfs the screen-space floor and dissolves the staircase; on a
    // small brush the floor dominates and behaviour is unchanged.
    if (const TileBrush* currentBrush = brush()) {
        constexpr float kRadiusWindowFactor = 0.5f;
        const float radiusWorld = std::max(0.0f, currentBrush->radius());
        windowWorldPx = std::max(windowWorldPx, radiusWorld * kRadiusWindowFactor);
    }
    return windowWorldPx;
}

float BrushStrokeHost::sampleSmoothedStrokeSpeed(float worldX, float worldY, double sampleTimeMs)
{
    if (!std::isfinite(worldX) || !std::isfinite(worldY) || !std::isfinite(sampleTimeMs)
        || m_strokeSpeedMeasurements.empty()
        || sampleTimeMs <= m_strokeSpeedMeasurements.back().sampleTimeMs) {
        return ruwa::core::brushes::normalizeBrushStrokeSpeed(
            m_strokeSpeedFilteredScreenPxPerSecond);
    }

    const float zoom = std::max(viewportZoom(), 0.001f);
    const float segmentScreenDistance
        = std::hypot(worldX - m_strokeSpeedSampleX, worldY - m_strokeSpeedSampleY) * zoom;
    m_strokeSpeedCumulativeScreenDistance += segmentScreenDistance;
    m_strokeSpeedMeasurements.push_back(
        { sampleTimeMs, m_strokeSpeedCumulativeScreenDistance });

    // A press and a stationary tablet packet are not motion samples. Remember
    // the first point that actually travelled, then wait for a second travelled
    // interval before declaring startup velocity observable. This prevents pen
    // dwell/report-rate packets from accidentally validating a zero-speed head.
    constexpr float kStartupMotionEpsilonScreenPx = 0.25f;
    bool startupEstimateBecameReliable = false;
    if (!m_initialStrokeSpeedSeeded && !m_strokeSpeedFirstMotionValid
        && segmentScreenDistance > kStartupMotionEpsilonScreenPx) {
        m_strokeSpeedFirstMotionSampleTimeMs = sampleTimeMs;
        m_strokeSpeedFirstMotionScreenDistance = m_strokeSpeedCumulativeScreenDistance;
        m_strokeSpeedFirstMotionValid = true;
    } else if (!m_initialStrokeSpeedSeeded && m_strokeSpeedFirstMotionValid
        && !m_strokeSpeedStartupEstimateReliable
        && m_strokeSpeedCumulativeScreenDistance - m_strokeSpeedFirstMotionScreenDistance
            > kStartupMotionEpsilonScreenPx
        && sampleTimeMs > m_strokeSpeedFirstMotionSampleTimeMs) {
        m_strokeSpeedStartupEstimateReliable = true;
        startupEstimateBecameReliable = true;
    }

    const double windowMs
        = static_cast<double>(ruwa::core::brushes::kBrushStrokeSpeedFilterTimeSeconds) * 1000.0;
    const double cutoffMs = sampleTimeMs - windowMs;
    // Keep exactly one sample before the cutoff so distance at the window edge
    // can be interpolated instead of jumping whenever an old packet expires.
    while (m_strokeSpeedMeasurements.size() > 2
        && m_strokeSpeedMeasurements[1].sampleTimeMs <= cutoffMs) {
        m_strokeSpeedMeasurements.pop_front();
    }

    double windowStartMs = m_strokeSpeedMeasurements.front().sampleTimeMs;
    double windowStartDistance
        = m_strokeSpeedMeasurements.front().cumulativeScreenDistance;
    if (m_strokeSpeedMeasurements.size() >= 2 && windowStartMs < cutoffMs) {
        const auto& afterCutoff = m_strokeSpeedMeasurements[1];
        const double intervalMs = afterCutoff.sampleTimeMs - windowStartMs;
        if (intervalMs > 0.0) {
            const double amount = std::clamp((cutoffMs - windowStartMs) / intervalMs, 0.0, 1.0);
            windowStartDistance += amount
                * (afterCutoff.cumulativeScreenDistance - windowStartDistance);
            windowStartMs = cutoffMs;
        }
    }

    const double measuredMs = sampleTimeMs - windowStartMs;
    float measuredScreenSpeed = 0.0f;
    if (measuredMs > 0.001) {
        measuredScreenSpeed = static_cast<float>(
            (m_strokeSpeedCumulativeScreenDistance - windowStartDistance) * 1000.0 / measuredMs);
    }

    if (!m_initialStrokeSpeedSeeded && m_strokeSpeedStartupEstimateReliable) {
        // Pen-down is not a motion sample. In particular on the mouse path the
        // user may hold the button briefly before moving; treating the complete
        // press -> first-move interval as uniform motion makes every early speed
        // estimate too low. Once a second movement sample exists, measure from
        // the first observed move onward. The geometry look-ahead keeps these
        // samples unpainted until this estimate is available.
        const double motionMs = sampleTimeMs - m_strokeSpeedFirstMotionSampleTimeMs;
        if (motionMs > 0.001) {
            measuredScreenSpeed = static_cast<float>(
                (m_strokeSpeedCumulativeScreenDistance - m_strokeSpeedFirstMotionScreenDistance)
                * 1000.0 / motionMs);
        }
    }

    // The measurement window removes per-packet variation. This second stage
    // controls the spatial rate of change: without it even a legitimate speed
    // change can alter radius too sharply between adjacent dabs on a large
    // brush. It is the same follower and response scale used by pressure.
    const float responseWindow = std::max(1.0f, dynamicsSmoothingWindowWorldPx());
    const double previousSampleTimeMs
        = m_strokeSpeedMeasurements[m_strokeSpeedMeasurements.size() - 2].sampleTimeMs;
    const float elapsedSeconds
        = static_cast<float>(std::max(0.0, sampleTimeMs - previousSampleTimeMs) / 1000.0);
    constexpr float kTimeFallbackResponseSeconds = 0.05f;
    constexpr float kTimeFallbackSpeedScreenPxPerSec = 24.0f;
    constexpr float kMaxTimeFallbackStepSeconds = 0.05f;
    const float timeStep = std::min(elapsedSeconds, kMaxTimeFallbackStepSeconds);
    const float normalizedSpeed
        = measuredScreenSpeed / kTimeFallbackSpeedScreenPxPerSec;
    const float timeWeight = 1.0f / (1.0f + normalizedSpeed * normalizedSpeed);
    const float virtualDistance
        = responseWindow * (timeStep / kTimeFallbackResponseSeconds) * timeWeight;
    const float filterStep = std::hypot(segmentScreenDistance / zoom, virtualDistance);
    if (!m_strokeSpeedFilterValid || startupEstimateBecameReliable
        || (!m_initialStrokeSpeedSeeded && !m_strokeSpeedStartupEstimateReliable)) {
        // Before velocity is observable, keep the provisional value revisable.
        // The moment a real motion interval appears, seed the follower exactly
        // at that estimate; subsequent deferred samples then use the same C1
        // smoothing as the rest of the stroke instead of bypassing the filter.
        m_strokeSpeedFilteredScreenPxPerSecond = measuredScreenSpeed;
        m_strokeSpeedFilterVelocity = 0.0f;
        m_strokeSpeedFilterValid = true;
    } else {
        advanceCriticallyDampedFollower(measuredScreenSpeed, responseWindow, filterStep,
            m_strokeSpeedFilteredScreenPxPerSecond, m_strokeSpeedFilterVelocity);
    }
    m_strokeSpeedFilteredScreenPxPerSecond
        = std::max(0.0f, m_strokeSpeedFilteredScreenPxPerSecond);

    m_strokeSpeedSampleX = worldX;
    m_strokeSpeedSampleY = worldY;
    return ruwa::core::brushes::normalizeBrushStrokeSpeed(
        m_strokeSpeedFilteredScreenPxPerSecond);
}

void BrushStrokeHost::backfillDeferredStrokeSpeed(const BrushInputDynamics& seedInputDynamics)
{
    TileBrush* currentBrush = brush();
    if (m_initialStrokeSpeedSeeded || !currentBrush || !seedInputDynamics.strokeSpeedAvailable
        || !currentBrush->hasActiveDynamicsBinding(
            ruwa::core::brushes::BrushInputSourceKey::StrokeSpeed)) {
        return;
    }

    // Pen-down has no velocity and the first move includes an arbitrary amount
    // of press latency, so only the prefix before the first reliable motion
    // interval needs correction. Later look-ahead points have already passed
    // through the normal follower and must retain their distinct values;
    // flattening all of them creates a long constant-radius startup capsule.
    float boundarySpeed = seedInputDynamics.strokeSpeed;
    size_t unreliablePointCount = m_liveStrokePoints.size();
    for (size_t i = 0; i < m_liveStrokePoints.size(); ++i) {
        if (m_liveStrokePoints[i].strokeSpeedReliable) {
            boundarySpeed = m_liveStrokePoints[i].inputDynamics.strokeSpeed;
            unreliablePointCount = i;
            break;
        }
    }

    const auto seedSpeed = [boundarySpeed](BrushInputDynamics& dynamics) {
        dynamics.strokeSpeed = boundarySpeed;
        dynamics.strokeSpeedAvailable = true;
        dynamics.strokeSpeedSpatialDerivative = 0.0f;
        dynamics.strokeSpeedSpatialDerivativeAvailable = false;
    };
    // Preserve a pen-down dab if a non-interactive caller already committed
    // one. Interactive speed-bound strokes defer that dab, so their still-empty
    // stroke seeds the anchor together with the pending motion prefix.
    const bool hasCommittedPenDownDab = !currentBrush->strokeDabs().empty();
    if (!hasCommittedPenDownDab) {
        seedSpeed(m_lastStrokeInputDynamics);
        seedSpeed(m_prevEmittedInputDynamics);
    }
    const size_t firstPendingPoint = hasCommittedPenDownDab ? 1 : 0;
    for (size_t i = firstPendingPoint; i < unreliablePointCount; ++i) {
        seedSpeed(m_liveStrokePoints[i].inputDynamics);
    }
}

bool BrushStrokeHost::isInitialized() const
{
    return m_callbacks.isInitialized ? m_callbacks.isInitialized() : false;
}

std::shared_ptr<ruwa::core::brushes::IEditableBrushStrokeReplayData>
BrushStrokeHost::activeStrokeReplayData() const
{
    return m_callbacks.getActiveStrokeReplayData ? m_callbacks.getActiveStrokeReplayData()
                                                 : nullptr;
}

void BrushStrokeHost::beginStroke(
    float worldX, float worldY, float pressure, StrokeInputDevice inputDevice, bool axisConstraint,
    const BrushInputDynamics& inputDynamics)
{
    flushPendingFinalization();
    if (auto* quickShape = quickShapeMorph()) {
        quickShape->stop();
    }
    m_quickLineStrokeModified = false;

    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    auto* layer = activeLayer();
    if (!currentBrush || !grid || !layer) {
        return;
    }

    if (m_callbacks.notifyCanvasInteraction) {
        m_callbacks.notifyCanvasInteraction(true);
    }

    m_isDrawing = true;
    m_strokeInputDevice = inputDevice;
    // The axis is decided from the first movement, not from the press, so the
    // origin is the raw press point and the axis itself stays Pending.
    m_strokeAxisConstraint
        = axisConstraint ? StrokeAxisConstraint::Pending : StrokeAxisConstraint::Off;
    m_strokeAxisOriginX = worldX;
    m_strokeAxisOriginY = worldY;
    m_strokeInputTimer.stop();
    m_processingQueuedStrokeInput = false;
    m_queuedSamplesSinceCompaction = 0;
    m_queuedStrokeSamples.clear();
    clearStrokeRuntimeState();

    m_selectionAtStrokeBegin = m_callbacks.captureSelectionState
        ? m_callbacks.captureSelectionState()
        : SelectionState {};
    BrushInputDynamics initialInputDynamics = inputDynamics;
    initialInputDynamics.strokeSpeed = 0.0f;
    initialInputDynamics.strokeSpeedAvailable = true;
    currentBrush->setPressure(pressure);
    currentBrush->setInputDynamics(initialInputDynamics);
    currentBrush->setStrokeElapsedSeconds(0.0f, true);
    const auto stabilizedStartPoint
        = ruwa::core::brushes::sampleStrokeStabilizer(m_stabilizationState, worldX, worldY,
            ruwa::core::brushes::stabilizationTauMs(currentBrush->stabilization()), 0.0, true);
    const Vector2 stabilizedStart { stabilizedStartPoint.x, stabilizedStartPoint.y };
    m_autoInputSmoothingValid = true;
    m_autoInputSmoothingPoint = { worldX, worldY };
    m_inputPressureSmoothValid = true;
    m_inputPressureSmoothed = pressure;
    m_inputPressureVel = 0.0f;
    m_inputPressureSmoothX = worldX;
    m_inputPressureSmoothY = worldY;
    m_inputPressureSmoothElapsedSeconds = 0.0f;
    m_stabPressureValid = true;
    m_stabPressure1 = pressure;
    m_stabPressure2 = pressure;
    m_stabPressureLastMs = 0.0;
    m_stabClockValid = false;
    m_stabClockLastWallMs = 0.0;
    m_stabClockEstimatedPeriodMs = kStabClockInitialPeriodMs;
    m_stabClockIdleAdvancedSinceRealInput = false;
    m_stabClockSourceOffsetMs = 0.0;
    m_stabRealWinCount = 0;
    m_strokeSpeedClockValid = false;
    m_strokeSpeedClockLastWallMs = 0.0;
    m_strokeSpeedClockIdleAdvancedSinceRealInput = false;
    m_strokeSpeedClockRealWinCount = 0;
    m_strokeSpeedClockEstimatedPeriodMs = kStrokeSpeedClockInitialPeriodMs;
    m_strokeSpeedClockUnreliableTimestampRun = false;
    m_dabClockValid = false;
    m_dabClockPrevSynthMs = 0.0;
    m_dabClockElapsedMs = 0.0;
    m_lastRealInputWallMs = 0.0;
    m_realInputWallArrivalSeen = false;
    m_realInputWallIntervalCount = 0;
    m_lastLiquifyMoveWallMs = 0.0;
    m_lastLiquifyMoveX = worldX;
    m_lastLiquifyMoveY = worldY;
    m_lastLiquifyMoveValid = true;
    m_lastStrokeX = stabilizedStart.x;
    m_lastStrokeY = stabilizedStart.y;
    m_prevEmittedPoint = stabilizedStart;
    m_prevEmittedInputDynamics = initialInputDynamics;
    m_lastStrokePressure = pressure;
    m_lastStrokeInputDynamics = initialInputDynamics;
    m_lastStrokeElapsedSeconds = 0.0f;
    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    m_lastStrokeTargetPressure = pressure;
    m_lastStrokeTargetInputDynamics = initialInputDynamics;
    m_lastStrokeTargetElapsedSeconds = 0.0f;
    m_lastStrokeInputX = stabilizedStart.x;
    m_lastStrokeInputY = stabilizedStart.y;
    m_lastStrokeInputPressure = pressure;
    m_lastRawStrokeInputDynamics = initialInputDynamics;
    m_lastStrokeInputElapsedSeconds = 0.0f;
    m_liveStrokePoints.clear();
    m_liveStrokePoints.push_back({ stabilizedStart, pressure, 0.0f, initialInputDynamics });

    // Stroke Speed is the speed of the cursor that actually lays down the
    // stroke, not the hardware cursor. They coincide at pen-down, but keeping
    // the measurement anchor explicitly in the stabilized coordinate stream is
    // important once idle catch-up starts advancing that cursor on its own.
    m_strokeSpeedSampleX = stabilizedStart.x;
    m_strokeSpeedSampleY = stabilizedStart.y;
    m_strokeSpeedCumulativeScreenDistance = 0.0;
    m_strokeSpeedMeasurements.clear();
    m_strokeSpeedMeasurements.push_back({ 0.0, 0.0 });
    m_strokeSpeedFirstMotionSampleTimeMs = 0.0;
    m_strokeSpeedFirstMotionScreenDistance = 0.0;
    m_strokeSpeedFilteredScreenPxPerSecond = 0.0f;
    m_strokeSpeedFilterVelocity = 0.0f;
    m_strokeSpeedFilterValid = false;
    m_strokeSpeedFirstMotionValid = false;
    m_strokeSpeedStartupEstimateReliable = false;
    m_initialStrokeSpeedSeeded = false;

    currentBrush->beginStroke();
    currentBrush->setPressure(pressure);
    currentBrush->setInputDynamics(initialInputDynamics);
    currentBrush->setStrokeElapsedSeconds(0.0f, true);
    m_strokeElapsedTimer.start();
    // Pen-down is the first wall-clock input arrival and anchors cadence
    // learning for the first few move packets of this stroke.
    m_realInputWallArrivalSeen = true;
    m_realtimePreviewEventCount = 0;
    m_lastRealtimeTaperTailStart = std::numeric_limits<size_t>::max();
    m_lastRealtimeTaperPreviewDabCount = 0;
    m_lastRealtimeTaperPreviewWasSampled = false;
    m_lastRealtimeTaperPreviewNs = std::numeric_limits<qint64>::min();
    m_realtimePreviewTimer.invalidate();

    auto* executionBackend = brushExecutionBackend();
    if (executionBackend) {
        executionBackend->setCanvasBounds(documentBoundsWidth(), documentBoundsHeight());
        if (currentBrush->isSmudgeMode() || currentBrush->isWetMode()) {
            executionBackend->resetSmudgeState();
        }
        if (currentBrush->isLiquifyMode()) {
            executionBackend->resetLiquifyState();
        }
    }

    // Time-based dabs for the held position-based liquify modes (not Push).
    m_liquifyDwellTimer.stop();
    if (currentBrush->isLiquifyMode() && currentBrush->liquifyToolMode() != 0) {
        m_liquifyDwellTimer.start();
    }

    const bool backendWantsGpu = executionBackend && executionBackend->shouldUseGpu();
    bool useGpu = isInitialized() && executionBackend
        && (currentBrush->isBlurMode() || currentBrush->isSmudgeMode() || currentBrush->isWetMode()
            || currentBrush->isLiquifyMode() || backendWantsGpu);
    m_useGPUBrush = (executionBackend && useGpu);

    // Settle the stroke buffer's storage format now, while it is still empty
    // (TileBrush::beginStroke above cleared it) and before any dab is stamped.
    // `grid` is the stroke's real target — the layer pixels, or the mask grid
    // when the layer's mask is the active edit target.
    if (executionBackend) {
        executionBackend->prepareStrokeBuffer(*currentBrush, *grid, m_useGPUBrush);
    }

    TileGrid* paintMask = effectivePaintMask(layer, grid);
    configureBrushSelectionMaskAlpha(*currentBrush, layer, paintMask);
    const bool realtimeRebuild = strokeNeedsRealtimeRebuild();
    const bool deferInitialDab = currentBrush->requiresMotionBeforeFirstDab();
    std::unordered_set<TileKey, TileKeyHash> rebuiltTiles;
    bool previewUpdated = false;
    const size_t changedDabStart = currentBrush->strokeDabs().size();
    // Direction and speed need a segment, so do not commit a dab evaluated with
    // fallback input at pen-down. Every other brush keeps immediate feedback.
    if (deferInitialDab) {
        previewUpdated = false;
    } else if (realtimeRebuild) {
        m_realtimePreviewTimer.start();
        m_realtimePreviewEventCount = 1;
        currentBrush->setStrokeElapsedSeconds(0.0f, true);
        currentBrush->recordDabPoint(stabilizedStart.x, stabilizedStart.y);
        previewUpdated
            = rebuildStrokePreviewFromDabs(grid, paintMask, executionBackend, true, &rebuiltTiles);
    } else if (executionBackend && useGpu) {
        if (m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, stabilizedStart.x,
            stabilizedStart.y, paintMask, true, 0.0f, true);
        if (m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }
    } else if (executionBackend) {
        m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, stabilizedStart.x,
            stabilizedStart.y, paintMask, false, 0.0f, true);
    } else {
        if (brushRequiresGpuEffect(currentBrush)) {
            m_useGPUBrush = false;
            return;
        }
        currentBrush->setStrokeElapsedSeconds(0.0f, true);
        currentBrush->stamp(*grid, stabilizedStart.x, stabilizedStart.y, paintMask);
        m_useGPUBrush = false;
    }

    if ((!realtimeRebuild && !deferInitialDab) || previewUpdated) {
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
    }

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
    // Only the plain-append branch below leaves the stroke buffer's tile set
    // monotonically growing; a rebuild re-stamps from the dab list and can drop
    // tiles, so it must not take the cheap delta.
    auto tileSetChange = StrokeTileSetChange::Arbitrary;
    if (realtimeRebuild) {
        if (previewUpdated) {
            // If range rebuild populated rebuiltTiles, use that precise set.
            // Otherwise (full rebuild), fall back to all stroke buffer tiles.
            if (!rebuiltTiles.empty()) {
                changedKeys = std::move(rebuiltTiles);
            } else {
                collectStrokeChangedKeys(changedKeys);
            }
        }
    } else if (deferInitialDab) {
        // The first segment will supply the missing direction or speed.
    } else if (currentBrush->hasPositionScatterEffect()) {
        collectStrokeChangedKeys(changedKeys);
    } else {
        const size_t changedDabCount = currentBrush->strokeDabs().size() - changedDabStart;
        currentBrush->collectStrokeDabRangeCoveredTiles(
            changedDabStart, changedDabCount, changedKeys);
        if (changedKeys.empty()) {
            appendStepTouchedTileKeys(stabilizedStart.x, stabilizedStart.y, stabilizedStart.x,
                stabilizedStart.y, currentBrush->effectiveRadius(), documentBoundsWidth(),
                documentBoundsHeight(), changedKeys);
        }
        tileSetChange = StrokeTileSetChange::GrowOnly;
    }
    if (!changedKeys.empty()) {
        markStrokeBufferDirtyDelta(changedKeys, tileSetChange);
    }

    if (auto* quickShape = quickShapeMorph(); quickShape && !currentBrush->isBlurMode()
        && !currentBrush->isSmudgeMode() && !currentBrush->isWetMode()
        && !currentBrush->isLiquifyMode()) {
        quickShape->restartHoldTimer();
    }

    if (m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

float BrushStrokeHost::strokeElapsedSecondsNow() const
{
    return elapsedSeconds(m_strokeElapsedTimer);
}

void BrushStrokeHost::continueStroke(
    float worldX, float worldY, float pressure, StrokeInputDevice inputDevice,
    const BrushInputDynamics& inputDynamics)
{
    continueStrokeAtElapsed(
        worldX, worldY, pressure, elapsedSeconds(m_strokeElapsedTimer), inputDevice, inputDynamics);
}

void BrushStrokeHost::continueStrokeAtElapsed(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, StrokeInputDevice inputDevice,
    const BrushInputDynamics& inputDynamics)
{
    addStrokeSampleAtElapsed(
        worldX, worldY, pressure, strokeElapsedSeconds, inputDevice, inputDynamics, true);
}

void BrushStrokeHost::queueStrokeAtElapsed(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, StrokeInputDevice inputDevice,
    const BrushInputDynamics& inputDynamics)
{
    addStrokeSampleAtElapsed(
        worldX, worldY, pressure, strokeElapsedSeconds, inputDevice, inputDynamics, false);
}

void BrushStrokeHost::applyStrokeAxisConstraint(float& worldX, float& worldY)
{
    if (m_strokeAxisConstraint == StrokeAxisConstraint::Off) {
        return;
    }

    const float dx = worldX - m_strokeAxisOriginX;
    const float dy = worldY - m_strokeAxisOriginY;

    if (m_strokeAxisConstraint == StrokeAxisConstraint::Pending) {
        // The axis is read from the displacement off the press point, which
        // integrates the movement and so is far steadier than any single
        // packet. Two gates keep a jittery hand from naming the wrong one:
        // the travel must be long enough to mean something, and one component
        // must clearly dominate the other. Thresholds are screen-space so the
        // gesture feels the same at every zoom level.
        constexpr float kAxisPickScreenPx = 10.0f;
        constexpr float kAxisForceScreenPx = 28.0f;
        constexpr float kAxisDominance = 1.6f;
        const float zoom = std::max(viewportZoom(), 0.05f);
        const float pickWorldPx = kAxisPickScreenPx / zoom;
        const float forceWorldPx = kAxisForceScreenPx / zoom;

        const float absDx = std::abs(dx);
        const float absDy = std::abs(dy);
        const float travel = std::sqrt(dx * dx + dy * dy);
        const bool longEnough = travel >= pickWorldPx;
        const bool decisive = absDx >= absDy * kAxisDominance || absDy >= absDx * kAxisDominance;
        // A diagonal drag never becomes decisive, so past the far gate the
        // dominant component wins outright rather than blocking the stroke.
        if (!longEnough || (!decisive && travel < forceWorldPx)) {
            // Still undecided: hold the sample on the origin so nothing is
            // painted off-axis before the axis exists. The segment from the
            // origin is drawn in one piece as soon as the axis resolves.
            worldX = m_strokeAxisOriginX;
            worldY = m_strokeAxisOriginY;
            return;
        }
        m_strokeAxisConstraint
            = absDx >= absDy ? StrokeAxisConstraint::Horizontal : StrokeAxisConstraint::Vertical;
    }

    if (m_strokeAxisConstraint == StrokeAxisConstraint::Horizontal) {
        worldY = m_strokeAxisOriginY;
    } else {
        worldX = m_strokeAxisOriginX;
    }
}

void BrushStrokeHost::addStrokeSampleAtElapsed(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, StrokeInputDevice inputDevice,
    const BrushInputDynamics& inputDynamics, bool processImmediately)
{
    if (!m_isDrawing) {
        return;
    }

    applyStrokeAxisConstraint(worldX, worldY);

    bool timestampReliable = true;
    if (!std::isfinite(strokeElapsedSeconds)) {
        strokeElapsedSeconds = elapsedSeconds(m_strokeElapsedTimer);
    }

    // Keep the stroke-time stream monotonic across both the queued samples
    // and already processed catch-up ticks. Recovered OS/WinTab history can
    // arrive after a catch-up timer has advanced the stabilizer; feeding those
    // older timestamps would freeze the filter while teleporting its target.
    float latestElapsed = m_lastStrokeTargetElapsedSeconds;
    if (!m_queuedStrokeSamples.empty()) {
        latestElapsed = std::max(latestElapsed, m_queuedStrokeSamples.back().strokeElapsedSeconds);
    }
    if (strokeElapsedSeconds <= latestElapsed) {
        strokeElapsedSeconds = latestElapsed + kStrokeInputMonotonicNudgeSec;
        timestampReliable = false;
    }

    // Wall-clock arrival time of the latest REAL pen input (not catch-up). The
    // catch-up gate below is evaluated by a QTimer against the same wall clock.
    // Do not use strokeElapsedSeconds here: recovered tablet packets carry an
    // acquisition timestamp which can lag behind (or be nudged ahead of) wall
    // time, making catch-up spuriously alternate with an active packet stream.
    // That alternation inserts collinear points which Catmull-Rom renders as
    // visible facets on curves.
    const double inputArrivalWallMs
        = static_cast<double>(elapsedSeconds(m_strokeElapsedTimer)) * 1000.0;
    if (m_realInputWallArrivalSeen) {
        // Measure distinct event-loop arrivals rather than packet timestamps.
        // Recovered tablet history is often delivered as several calls at the
        // same wall time; zero gaps within such a burst are intentionally
        // ignored, while the gap between bursts is what the catch-up gate must
        // learn. Very long gaps are UI stalls / real pauses and do not describe
        // the device's normal delivery cadence.
        constexpr double kMinArrivalIntervalMs = 0.25;
        constexpr double kMaxArrivalIntervalMs = 40.0;
        const double arrivalIntervalMs = inputArrivalWallMs - m_lastRealInputWallMs;
        if (arrivalIntervalMs >= kMinArrivalIntervalMs
            && arrivalIntervalMs <= kMaxArrivalIntervalMs) {
            if (m_realInputWallIntervalCount < kRealInputWallIntervalWindow) {
                m_realInputWallIntervals[m_realInputWallIntervalCount++] = arrivalIntervalMs;
            } else {
                for (int i = 1; i < kRealInputWallIntervalWindow; ++i) {
                    m_realInputWallIntervals[i - 1] = m_realInputWallIntervals[i];
                }
                m_realInputWallIntervals[kRealInputWallIntervalWindow - 1]
                    = arrivalIntervalMs;
            }
        }
    }
    m_lastRealInputWallMs = inputArrivalWallMs;
    m_realInputWallArrivalSeen = true;

    // Wall-clock time of the last meaningful pen movement, for the liquify dwell
    // gate. Unlike m_lastRealInputWallMs (bumped on every event), this only
    // advances when the raw input travels past a small screen-space threshold, so a
    // held-still stylus — which keeps streaming near-identical packets — reads as
    // idle and lets the dwell fire. Measured against m_strokeElapsedTimer (wall
    // clock) to stay in the same frame as emitLiquifyDwell, independent of the
    // event-timestamp clock the stylus path feeds into strokeElapsedSeconds.
    {
        constexpr float kLiquifyMoveScreenPx = 2.0f;
        const float zoom = std::max(viewportZoom(), 0.05f);
        const float moveThreshWorld = kLiquifyMoveScreenPx / zoom;
        const float dxMove = worldX - m_lastLiquifyMoveX;
        const float dyMove = worldY - m_lastLiquifyMoveY;
        if (!m_lastLiquifyMoveValid
            || (dxMove * dxMove + dyMove * dyMove) >= moveThreshWorld * moveThreshWorld) {
            m_lastLiquifyMoveX = worldX;
            m_lastLiquifyMoveY = worldY;
            m_lastLiquifyMoveWallMs
                = static_cast<double>(elapsedSeconds(m_strokeElapsedTimer)) * 1000.0;
            m_lastLiquifyMoveValid = true;
        }
    }

    // Critically-damped (2nd-order) smoothing of the raw input pressure over
    // world-space arc length with a continuous time-domain fallback at very
    // low pen speeds. A 1st-order EMA smooths the *value* but not its
    // *rate*: at every input sample its target jumps, so the smoothed output has
    // a slope discontinuity (a corner) at that sample. Sampled sparsely (Qt
    // drops tablet events under heavy-brush load) and mapped through the PCHIP
    // size curve, those corners read as visible facets / a staircase on a large
    // brush — and no amount of extra 1st-order smoothing removes them, it only
    // spreads them out. A critically-damped follower carries velocity, so it
    // eases through each target with a continuous slope (C1); the staircase
    // disappears without needing denser samples, stays realtime, and needs no
    // post-stroke rebuild. smoothTime is the size-coupled window so the easing
    // scale matches the dab spacing at any brush size (a fixed screen-space
    // scale is far too small on a large brush). The start is seeded exact in
    // beginStroke (value + zero velocity) so the head is not eased into
    // existence; end shape comes straight from the pressure signal (the post-hoc
    // velocity end taper was removed), pending a realtime taper.
    if (!m_inputPressureSmoothValid) {
        m_inputPressureSmoothed = pressure;
        m_inputPressureVel = 0.0f;
        m_inputPressureSmoothX = worldX;
        m_inputPressureSmoothY = worldY;
        m_inputPressureSmoothElapsedSeconds = strokeElapsedSeconds;
        m_inputPressureSmoothValid = true;
    } else {
        const float dpx = worldX - m_inputPressureSmoothX;
        const float dpy = worldY - m_inputPressureSmoothY;
        const float ds = std::sqrt(dpx * dpx + dpy * dpy);

        const float smoothTime = std::max(1.0f, dynamicsSmoothingWindowWorldPx());
        float filterStep = ds;
        if (inputDevice == StrokeInputDevice::Stylus) {
            // Pure arc-length smoothing freezes when ds reaches zero. Instead
            // of switching modes at a movement threshold, add a smoothly
            // weighted virtual distance that advances the same follower over
            // time. It dominates only near rest and fades continuously as pen
            // speed rises, so slowing down cannot cause a pressure snap.
            constexpr float kTimeFallbackResponseSeconds = 0.05f;
            constexpr float kTimeFallbackSpeedScreenPxPerSec = 24.0f;
            constexpr float kMaxTimeFallbackStepSeconds = 0.05f;
            const float elapsedDelta
                = std::max(0.0f, strokeElapsedSeconds - m_inputPressureSmoothElapsedSeconds);
            const float timeStep = std::min(elapsedDelta, kMaxTimeFallbackStepSeconds);
            float timeWeight = 0.0f;
            if (elapsedDelta > 0.000001f) {
                const float screenSpeed = (ds * viewportZoom()) / elapsedDelta;
                const float normalizedSpeed = screenSpeed / kTimeFallbackSpeedScreenPxPerSec;
                timeWeight = 1.0f / (1.0f + normalizedSpeed * normalizedSpeed);
            }
            const float virtualDistance
                = smoothTime * (timeStep / kTimeFallbackResponseSeconds) * timeWeight;
            filterStep = std::hypot(ds, virtualDistance);
        }

        advanceCriticallyDampedFollower(pressure, smoothTime, filterStep,
            m_inputPressureSmoothed, m_inputPressureVel);
        m_inputPressureSmoothX = worldX;
        m_inputPressureSmoothY = worldY;
        m_inputPressureSmoothElapsedSeconds = strokeElapsedSeconds;
    }
    pressure = std::clamp(m_inputPressureSmoothed, 0.0f, 1.0f);

    m_queuedStrokeSamples.push_back(
        { worldX, worldY, pressure, strokeElapsedSeconds, inputDevice, inputDynamics,
            timestampReliable });
    if (!processImmediately) {
        ++m_queuedSamplesSinceCompaction;
        const float queuedAge = stroke_input_queue::queuedAgeSeconds(m_queuedStrokeSamples);
        // Native WinTab is deliberately lossless while the renderer keeps up.
        // Once its deferred queue is more than one frame old, compact only
        // samples that the existing distance/pressure/time interpolation can
        // reconstruct within a sub-pixel error. The tolerance grows modestly
        // with queue age, preventing oversampled paths from accumulating latency
        // without turning this into a fixed-Hz throttle or changing healthy devices.
        if (queuedAge >= stroke_input_queue::kReductionActivationAgeSeconds
            && m_queuedSamplesSinceCompaction >= kStrokeInputCompactionCadenceSamples) {
            stroke_input_queue::compact(m_queuedStrokeSamples, viewportZoom(), queuedAge);
            m_queuedSamplesSinceCompaction = 0;
        }
    }
    if (processImmediately) {
        processQueuedStrokeInput();
    } else {
        scheduleQueuedStrokeInput();
    }
}

void BrushStrokeHost::scheduleQueuedStrokeInput()
{
    if (!m_isDrawing || m_processingQueuedStrokeInput) {
        return;
    }
    if (!m_strokeInputTimer.isActive()) {
        m_strokeInputTimer.start();
    }
}

void BrushStrokeHost::processQueuedStrokeInput()
{
    drainQueuedStrokeInput(StrokeInputDrainMode::BoundedLatency, true);
    // A drain returns early when the queue is already empty, so a completion
    // deferred by a frame-tick drain would never run if it relied on the drain
    // to notice. This slot is the one service point that always owns its GL
    // context, so the deferred completion is finished here.
    if (m_endStrokeRequested && m_isDrawing && m_queuedStrokeSamples.empty()
        && !m_processingQueuedStrokeInput) {
        completeEndStrokeAfterQueueDrain();
    }
}

void BrushStrokeHost::drainStrokeInputForFrame()
{
    if (!m_isDrawing || m_queuedStrokeSamples.empty()) {
        return;
    }
    // The queue is serviced before the frame's layer stack is built, so the
    // pixels this frame shows already include everything the pen produced since
    // the previous one. The GL context is current here, which also spares the
    // makeCurrent/doneCurrent pair a timer-driven drain needs.
    //
    // The drain still asks for a render afterwards, and must: nothing else keeps
    // the canvas repainting during a stroke, so suppressing it here would end the
    // frame loop and leave the fallback timer as the only service point. One
    // request per drain is not the old problem — that was one per 24-sample
    // batch, which made the queue's service rate equal to the frame rate.
    const bool wasDrainingInsideFrame = m_drainingInsideFrame;
    m_drainingInsideFrame = true;
    drainQueuedStrokeInput(StrokeInputDrainMode::BoundedLatency, true);
    // Restored rather than cleared: a nested frame would otherwise hand the
    // outer one permission to finalize the stroke mid-paint.
    m_drainingInsideFrame = wasDrainingInsideFrame;
}

void BrushStrokeHost::drainQueuedStrokeInput(
    StrokeInputDrainMode mode, bool requestRenderAfterDrain)
{
    if (!m_isDrawing || m_processingQueuedStrokeInput || m_queuedStrokeSamples.empty()) {
        return;
    }

    const bool boundedLatency = (mode == StrokeInputDrainMode::BoundedLatency);
    if (boundedLatency && m_queuedStrokeSamples.size() > m_strokeInputTickCapacity) {
        // Two stages, in order of how much they cost the stroke. The first is
        // lossless within the downstream interpolation's own tolerances but is
        // allowed to remove nothing (a driver with noisy pressure defeats it
        // completely), so it cannot be the mechanism that bounds latency. The
        // second one always meets the target: tolerances there decide the ORDER
        // points are dropped in, not whether dropping is permitted.
        //
        // Dropping input samples does not thin the stroke: dabs are placed along
        // arc length by the interpolator, so a point removed from a near-straight
        // span produces the same dabs. What it removes is the per-sample overhead
        // that scales with the DEVICE's packet rate rather than with the path.
        const float queuedAge = stroke_input_queue::queuedAgeSeconds(m_queuedStrokeSamples);
        stroke_input_queue::compact(m_queuedStrokeSamples, viewportZoom(), queuedAge);
        if (m_queuedStrokeSamples.size() > m_strokeInputTickCapacity) {
            stroke_input_queue::decimateToBudget(
                m_queuedStrokeSamples, m_strokeInputTickCapacity, viewportZoom());
        }
        m_queuedSamplesSinceCompaction = 0;
    }

    m_processingQueuedStrokeInput = true;
    size_t processedSamples = 0;
    QElapsedTimer budgetTimer;
    budgetTimer.start();

    bool ownsGpuContext = false;
    if (m_useGPUBrush && m_callbacks.makeCurrent) {
        const bool alreadyCurrent
            = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
        if (!alreadyCurrent) {
            m_callbacks.makeCurrent();
            ownsGpuContext = true;
        }
    }

    // Consume to empty. A partial drain is what let the backlog — and with it the
    // drawing lag — grow without bound: the queue was refilled by the device at
    // its own rate while the consumer was capped at a fixed slice per tick, and
    // that slice was itself paced by the repaint each tick asked for. The work
    // per tick is bounded by the decimation above instead.
    while (m_isDrawing && !m_queuedStrokeSamples.empty()) {
        const StrokeInputSample sample = m_queuedStrokeSamples.front();
        m_queuedStrokeSamples.pop_front();
        m_strokeInputDevice = sample.inputDevice;
        continueStrokeImmediate(
            sample.worldX, sample.worldY, sample.pressure, sample.strokeElapsedSeconds,
            sample.inputDynamics, false, true, sample.timestampReliable);
        ++processedSamples;
    }

    const double elapsedMs = static_cast<double>(budgetTimer.nsecsElapsed()) / 1000000.0;
    if (ownsGpuContext && m_callbacks.doneCurrent) {
        m_callbacks.doneCurrent();
    }
    m_processingQueuedStrokeInput = false;
    if (boundedLatency) {
        updateStrokeInputTickCapacity(processedSamples, elapsedMs);
    }

    if (m_isDrawing && processedSamples > 0 && requestRenderAfterDrain
        && m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
    if (m_isDrawing && !m_queuedStrokeSamples.empty()) {
        // Only reachable if the stroke was interrupted mid-drain; the queue is
        // empty on every normal path.
        scheduleQueuedStrokeInput();
    } else if (m_queuedStrokeSamples.empty()) {
        m_queuedSamplesSinceCompaction = 0;
    }
    // If endStroke was requested while a backlog existed, run the deferred
    // completion once the queue has drained. This is the async tail of the
    // "fast huge brush" flow — UI stays responsive while the backlog catches up.
    if (m_endStrokeRequested && m_isDrawing && m_queuedStrokeSamples.empty()) {
        if (m_drainingInsideFrame) {
            // Completion flattens the stroke, commits it and toggles the GL
            // context around that work. Running it from inside paintGL would
            // leave the rest of the frame drawing without a current context, so
            // it is handed to the fallback timer, which owns its context.
            if (!m_strokeInputTimer.isActive()) {
                m_strokeInputTimer.start();
            }
        } else {
            completeEndStrokeAfterQueueDrain();
        }
    }
}

void BrushStrokeHost::updateStrokeInputTickCapacity(size_t processedSamples, double elapsedMs)
{
    if (processedSamples < kStrokeInputCapacityMeasureMinSamples || !(elapsedMs >= 0.0)) {
        // A tick that found one or two samples is the healthy interactive case:
        // the queue never filled, so its duration says nothing about how much
        // this machine can afford. Measuring it anyway would let a single
        // expensive sample — a huge brush on a slow GPU, drawn with a mouse that
        // reports once per frame — pin the capacity at its floor and leave it
        // there for the tablet stroke that follows.
        return;
    }

    if (elapsedMs > kStrokeInputTickBudgetMs) {
        // Multiplicative decrease. Fast, because the cost of being wrong in this
        // direction is a stalled frame the user feels immediately.
        const size_t reduced = static_cast<size_t>(static_cast<double>(processedSamples) * 0.6);
        m_strokeInputTickCapacity
            = std::clamp(reduced, kStrokeInputMinTickCapacity, kStrokeInputMaxTickCapacity);
        return;
    }

    // Only grow when the tick actually ran into its capacity — otherwise the
    // number would drift upwards on idle ticks and stop describing anything.
    // Growth is additive-then-proportional so a device that briefly bursts does
    // not permanently cap a machine that can keep up.
    if (elapsedMs < kStrokeInputTickBudgetMs * 0.5
        && processedSamples * 10 >= m_strokeInputTickCapacity * 8) {
        const size_t grown
            = static_cast<size_t>(static_cast<double>(m_strokeInputTickCapacity) * 1.5) + 8;
        m_strokeInputTickCapacity
            = std::clamp(grown, kStrokeInputMinTickCapacity, kStrokeInputMaxTickCapacity);
    }
}

void BrushStrokeHost::flushQueuedStrokeInput()
{
    if (m_strokeInputTimer.isActive()) {
        m_strokeInputTimer.stop();
    }
    // Deliberately not a loop. A re-entrant call finds m_processingQueuedStrokeInput
    // already set and returns without consuming anything, which turned the old
    // "drain until empty" loop into a spin with no exit.
    drainQueuedStrokeInput(StrokeInputDrainMode::Complete, true);
}

double BrushStrokeHost::stepStabilizerClock(
    double realMs, double wallMs, bool isRealPenSample)
{
    constexpr double kPauseGapMs = 40.0; // gap that counts as a stop → resync
    constexpr double kMinPeriodMs = 0.2;
    constexpr double kMaxPeriodMs = 40.0;
    constexpr double kMaxDriftMs = 50.0;

    if (!std::isfinite(wallMs)) {
        wallMs = realMs;
    }

    if (!m_stabClockValid) {
        m_stabClockValid = true;
        m_stabSynthMs = realMs;
        m_stabLastRealPenMs = realMs;
        m_stabClockLastWallMs = wallMs;
        m_stabClockSourceOffsetMs = m_stabSynthMs - realMs;
        m_stabRealWin[0] = realMs;
        m_stabRealWinCount = 1;
        return m_stabSynthMs;
    }

    const double wallAdvanceMs = std::max(0.0, wallMs - m_stabClockLastWallMs);
    m_stabClockLastWallMs = wallMs;

    // Hand off from the packet clock to the catch-up wall clock by DELTAS. The
    // first idle callback arrives only after the idle gate, so assigning realMs
    // directly used to turn that whole wait into one EWMA step and visibly kick
    // the brush cursor forward. Continue with one nominal
    // catch-up timer period first; subsequent callbacks use their actual wall
    // duration. Absolute phase is irrelevant to the stabilizer — only monotonic
    // dt matters.
    if (!isRealPenSample) {
        double advanceMs = std::clamp(static_cast<double>(m_stabilizerCatchupTimer.interval()),
            kMinPeriodMs, kMaxPeriodMs);
        if (m_stabClockIdleAdvancedSinceRealInput) {
            advanceMs = wallAdvanceMs;
        }
        if (advanceMs > 0.0) {
            m_stabSynthMs += std::clamp(advanceMs, kMinPeriodMs, kMaxPeriodMs);
        }
        m_stabClockIdleAdvancedSinceRealInput = true;
        return m_stabSynthMs;
    }

    const double gap = realMs - m_stabLastRealPenMs;
    m_stabLastRealPenMs = realMs;

    if (m_stabClockIdleAdvancedSinceRealInput || !(gap >= 0.0)
        || gap > kPauseGapMs) {
        // Resume from catch-up (or from a pause too short to produce a catch-up
        // tick) with one normal packet step. Re-anchor the source-to-synthetic
        // phase instead of snapping to the packet timestamp; absolute clocks can
        // differ after an idle handoff, while their deltas remain continuous.
        m_stabSynthMs += m_stabClockEstimatedPeriodMs;
        m_stabClockSourceOffsetMs = m_stabSynthMs - realMs;
        m_stabClockIdleAdvancedSinceRealInput = false;
        m_stabRealWin[0] = realMs;
        m_stabRealWinCount = 1;
        return m_stabSynthMs;
    }

    // Slide the window of real-sample timestamps.
    if (m_stabRealWinCount < kStabClockWindow) {
        m_stabRealWin[m_stabRealWinCount++] = realMs;
    } else {
        for (int i = 1; i < kStabClockWindow; ++i) {
            m_stabRealWin[i - 1] = m_stabRealWin[i];
        }
        m_stabRealWin[kStabClockWindow - 1] = realMs;
    }

    // Windowed average = the real average sample period (unbiased, no seed), so
    // the synthetic dt matches the device's actual rate from the first stroke.
    double periodMs = gap;
    if (m_stabRealWinCount >= 2) {
        periodMs = (m_stabRealWin[m_stabRealWinCount - 1] - m_stabRealWin[0])
            / static_cast<double>(m_stabRealWinCount - 1);
    }
    periodMs = std::clamp(periodMs, kMinPeriodMs, kMaxPeriodMs);
    m_stabClockEstimatedPeriodMs = periodMs;

    m_stabSynthMs += periodMs;
    // Bound drift within the current input-clock phase. The offset is reset on
    // every idle/resume handoff, so this guard cannot undo the smooth transition
    // by snapping back to a different clock's absolute time.
    const double mappedRealMs = realMs + m_stabClockSourceOffsetMs;
    if (std::abs(m_stabSynthMs - mappedRealMs) > kMaxDriftMs) {
        m_stabSynthMs = mappedRealMs;
    }
    return m_stabSynthMs;
}

double BrushStrokeHost::stepStrokeSpeedClock(
    double realMs, double wallMs, bool isRealPenSample, bool inputTimestampReliable)
{
    constexpr double kPauseGapMs = 40.0;
    constexpr double kMinPeriodMs = 0.2;
    constexpr double kMaxPeriodMs = 40.0;
    constexpr double kMaxDriftMs = 50.0;

    if (!std::isfinite(wallMs)) {
        wallMs = realMs;
    }

    if (!m_strokeSpeedClockValid) {
        m_strokeSpeedClockValid = true;
        m_strokeSpeedClockSynthMs = realMs;
        m_strokeSpeedClockLastRealMs = realMs;
        m_strokeSpeedClockLastWallMs = wallMs;
        m_strokeSpeedClockIdleAdvancedSinceRealInput = false;
        m_strokeSpeedClockRealWin[0] = realMs;
        m_strokeSpeedClockRealWinCount = 1;
        m_strokeSpeedClockUnreliableTimestampRun = !inputTimestampReliable;
        return m_strokeSpeedClockSynthMs;
    }

    const double wallAdvanceMs = std::max(0.0, wallMs - m_strokeSpeedClockLastWallMs);
    m_strokeSpeedClockLastWallMs = wallMs;
    if (!isRealPenSample) {
        m_strokeSpeedClockSynthMs += wallAdvanceMs;
        m_strokeSpeedClockIdleAdvancedSinceRealInput |= wallAdvanceMs > 0.0;
        return m_strokeSpeedClockSynthMs;
    }

    const double gap = realMs - m_strokeSpeedClockLastRealMs;
    m_strokeSpeedClockLastRealMs = realMs;
    const bool resumedAfterIdle = m_strokeSpeedClockIdleAdvancedSinceRealInput;
    m_strokeSpeedClockIdleAdvancedSinceRealInput = false;
    if (resumedAfterIdle || !(gap >= 0.0) || gap > kPauseGapMs) {
        m_strokeSpeedClockSynthMs = std::max(
            m_strokeSpeedClockSynthMs + std::max(wallAdvanceMs, kMinPeriodMs), realMs);
        m_strokeSpeedClockRealWin[0] = realMs;
        m_strokeSpeedClockRealWinCount = 1;
        m_strokeSpeedClockUnreliableTimestampRun = !inputTimestampReliable;
        return m_strokeSpeedClockSynthMs;
    }

    if (!inputTimestampReliable) {
        m_strokeSpeedClockSynthMs += m_strokeSpeedClockEstimatedPeriodMs;
        m_strokeSpeedClockUnreliableTimestampRun = true;
        return m_strokeSpeedClockSynthMs;
    }

    if (m_strokeSpeedClockUnreliableTimestampRun) {
        m_strokeSpeedClockRealWin[0] = realMs;
        m_strokeSpeedClockRealWinCount = 1;
        m_strokeSpeedClockSynthMs += m_strokeSpeedClockEstimatedPeriodMs;
        m_strokeSpeedClockUnreliableTimestampRun = false;
        return m_strokeSpeedClockSynthMs;
    }

    if (m_strokeSpeedClockRealWinCount < kStabClockWindow) {
        m_strokeSpeedClockRealWin[m_strokeSpeedClockRealWinCount++] = realMs;
    } else {
        for (int i = 1; i < kStabClockWindow; ++i) {
            m_strokeSpeedClockRealWin[i - 1] = m_strokeSpeedClockRealWin[i];
        }
        m_strokeSpeedClockRealWin[kStabClockWindow - 1] = realMs;
    }

    double periodMs = gap;
    if (m_strokeSpeedClockRealWinCount >= 2) {
        periodMs = (m_strokeSpeedClockRealWin[m_strokeSpeedClockRealWinCount - 1]
                       - m_strokeSpeedClockRealWin[0])
            / static_cast<double>(m_strokeSpeedClockRealWinCount - 1);
    }
    m_strokeSpeedClockEstimatedPeriodMs = std::clamp(periodMs, kMinPeriodMs, kMaxPeriodMs);
    m_strokeSpeedClockSynthMs += m_strokeSpeedClockEstimatedPeriodMs;
    if (realMs - m_strokeSpeedClockSynthMs > kMaxDriftMs) {
        m_strokeSpeedClockSynthMs = realMs;
    }
    return m_strokeSpeedClockSynthMs;
}

float BrushStrokeHost::stepDabDynamicsClock(double synthNowMs, double realMs)
{
    if (!m_dabClockValid) {
        m_dabClockValid = true;
        m_dabClockPrevSynthMs = synthNowMs;
        // Seed from the raw clock so the ORIGIN is unchanged (0 == stroke
        // start, first sample lands where it always did); only the increments
        // from here on are de-jittered.
        m_dabClockElapsedMs = std::max(0.0, realMs);
        return static_cast<float>(m_dabClockElapsedMs / 1000.0);
    }

    // Forward motion only. A discontinuous input clock must never rewind hue,
    // size, or any other time-bound setting. A genuine pause moves real time
    // forward and is carried through in full, so a time-driven brush keeps
    // running while the pen rests exactly as it does on the mouse path.
    const double advanceMs = synthNowMs - m_dabClockPrevSynthMs;
    m_dabClockPrevSynthMs = synthNowMs;
    if (advanceMs > 0.0) {
        m_dabClockElapsedMs += advanceMs;
    }
    return static_cast<float>(m_dabClockElapsedMs / 1000.0);
}

float BrushStrokeHost::advanceDabDynamicsClockIdle(double realMs)
{
    if (!m_dabClockValid) {
        // Nothing has been drawn from a pen sample yet, so there is no clock to
        // carry: report raw stroke time and leave the seeding to the first real
        // sample.
        return static_cast<float>(std::max(0.0, realMs) / 1000.0);
    }
    // No new sample means no synthetic step to integrate — advance in real time
    // instead, which is the same rule stepStabilizerClock applies to its own
    // idle ticks, but without feeding the sample-period estimate.
    if (realMs > m_dabClockPrevSynthMs) {
        m_dabClockElapsedMs += realMs - m_dabClockPrevSynthMs;
        m_dabClockPrevSynthMs = realMs;
    }
    return static_cast<float>(m_dabClockElapsedMs / 1000.0);
}

void BrushStrokeHost::continueStrokeImmediate(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, const BrushInputDynamics& inputDynamics,
    bool requestRenderAfterStep, bool isRealPenSample, bool inputTimestampReliable)
{
    if (!m_isDrawing) {
        return;
    }

    if (!std::isfinite(strokeElapsedSeconds)) {
        strokeElapsedSeconds = elapsedSeconds(m_strokeElapsedTimer);
    }
    if (strokeElapsedSeconds <= m_lastStrokeTargetElapsedSeconds) {
        strokeElapsedSeconds = m_lastStrokeTargetElapsedSeconds + kStrokeInputMonotonicNudgeSec;
        inputTimestampReliable = false;
    }

    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    if (!currentBrush || !grid) {
        return;
    }

    if (m_callbacks.notifyCanvasInteraction) {
        m_callbacks.notifyCanvasInteraction(true);
    }

    // Separate clocks for ordering, geometry, and Stroke Speed.
    //
    //   strokeElapsedSeconds — the RAW input clock (pen event timestamps, or
    //     the wall clock on the mouse/catch-up paths). The input stream is
    //     ordered and de-duplicated against it, so the m_lastStrokeTarget*
    //     bookkeeping and the queue keep using it verbatim.
    //   stabilizerNowMs — the proven de-jittered geometry clock the stabilizer's
    //     time-domain EWMA runs on. The OS delivers moves in bursts at coarse
    //     (~15.6 ms) timer resolution, so the observable per-sample dt is
    //     bimodal (nudge floor / one big jump) and the EWMA turns that into a
    //     sawtooth (facets). See stepStabilizerClock.
    //   strokeSpeedNowMs — packet timing reconstructed independently for the
    //     Stroke Speed input. It never feeds position or pressure stabilization.
    //   dabElapsedSeconds — what the DABS carry, and therefore what the `Time`
    //     dynamics input reads. Same bimodality, different symptom: per dab it
    //     is a step function, so a hue-over-time brush bands. It integrates
    //     stabilizerNowMs rather than sampling it, which also keeps it monotonic across
    //     the snap-backs stepStabilizerClock performs. See stepDabDynamicsClock.
    //
    // Stepped here, above the quick-shape branch, so every consumer advances
    // exactly once per input/catch-up sample.
    const double realMs = static_cast<double>(strokeElapsedSeconds) * 1000.0;
    const double wallMs = static_cast<double>(elapsedSeconds(m_strokeElapsedTimer)) * 1000.0;
    const double stabilizerNowMs
        = stepStabilizerClock(realMs, wallMs, isRealPenSample);
    const double strokeSpeedNowMs
        = stepStrokeSpeedClock(realMs, wallMs, isRealPenSample, inputTimestampReliable);
    const float dabElapsedSeconds = stepDabDynamicsClock(stabilizerNowMs, realMs);

    // Stabilization may itself be driven by Stroke Speed. The current cursor
    // speed does not exist until the stabilizer has produced its output, so use
    // the previous resolved speed while evaluating this step's lag. This makes
    // the feedback causal (one output sample of delay) instead of measuring the
    // hardware cursor first or trying to solve a circular dependency.
    BrushInputDynamics stabilizationInputDynamics = inputDynamics;
    stabilizationInputDynamics.strokeSpeed = m_lastStrokeTargetInputDynamics.strokeSpeed;
    stabilizationInputDynamics.strokeSpeedAvailable
        = m_lastStrokeTargetInputDynamics.strokeSpeedAvailable;
    currentBrush->setPressure(pressure);
    currentBrush->setInputDynamics(stabilizationInputDynamics);
    currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);

    const float stabSlider = currentBrush->stabilization();
    const float stabLagMs = ruwa::core::brushes::stabilizationTauMs(stabSlider);
    const Vector2 filteredTarget
        = smoothInputTargetForViewport(worldX, worldY, stabLagMs > 0.0f);
    worldX = filteredTarget.x;
    worldY = filteredTarget.y;

    if (auto* quickShape = quickShapeMorph(); quickShape && quickShape->isActive()) {
        BrushInputDynamics sampledInputDynamics = inputDynamics;
        sampledInputDynamics.strokeSpeed
            = sampleSmoothedStrokeSpeed(worldX, worldY, strokeSpeedNowMs);
        sampledInputDynamics.strokeSpeedAvailable = true;
        currentBrush->setPressure(pressure);
        currentBrush->setInputDynamics(sampledInputDynamics);
        currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);
        m_lastStrokeTargetX = worldX;
        m_lastStrokeTargetY = worldY;
        m_lastStrokeTargetPressure = pressure;
        m_lastStrokeTargetInputDynamics = sampledInputDynamics;
        m_lastStrokeTargetElapsedSeconds = strokeElapsedSeconds;
        m_lastStrokeX = worldX;
        m_lastStrokeY = worldY;
        quickShape->updateCursorTarget(worldX, worldY);
        updateStabilizerCatchupTimer();
        return;
    }

    // Take ONE stabilized point per call and let the downstream Catmull-Rom
    // interpolate the curve between such points. Feeding the stabilizer's dense
    // 1 ms sub-point stream as geometry instead made smooth arcs FACETED:
    // between two pen events the 2-stage EWMA chases a straight target ramp, so
    // its output is nearly straight there with the curvature bunched at the
    // events; sampling that densely faithfully reproduced those straight
    // inter-event facets, and Catmull-Rom can't re-curve points that genuinely
    // lie on a line. Keeping only the per-event stabilized point (still
    // jitter-filtered by the EWMA) and curving BETWEEN them restores smooth
    // roundings without feeding dense sub-points into the rasterizer.
    //
    // Pressure is delayed in lockstep with position (2-stage EWMA, same alpha)
    // so each dab carries the pressure the pen had at that lagged point — see the
    // size-staircase fix. stabLagMs == 0 passes raw position + pressure through.
    Vector2 resolved { worldX, worldY };
    float emitPressure = pressure;
    if (stabLagMs > 0.0f) {
        const auto sp = ruwa::core::brushes::sampleStrokeStabilizer(
            m_stabilizationState, worldX, worldY, stabLagMs, stabilizerNowMs, false);
        resolved = { sp.x, sp.y };
        if (!m_stabPressureValid) {
            m_stabPressure1 = pressure;
            m_stabPressure2 = pressure;
            m_stabPressureLastMs = stabilizerNowMs;
            m_stabPressureValid = true;
        } else {
            const double dtMs = stabilizerNowMs - m_stabPressureLastMs;
            if (dtMs > 0.0) {
                const float a = ruwa::core::brushes::detail::stabilizerAlpha(stabLagMs, dtMs);
                m_stabPressure1 += a * (pressure - m_stabPressure1);
                m_stabPressure2 += a * (m_stabPressure1 - m_stabPressure2);
                m_stabPressureLastMs = stabilizerNowMs;
            }
        }
        emitPressure = m_stabPressure2;
    } else {
        // A dynamic stabilization binding can cross zero and later turn back
        // on. Synchronize both followers with the pass-through sample while it
        // is off, otherwise the catch-up timer would still see the old position
        // as pending and re-enabling would resurrect stale pressure state.
        ruwa::core::brushes::sampleStrokeStabilizer(
            m_stabilizationState, worldX, worldY, 0.0f, stabilizerNowMs, false);
        m_stabPressureValid = true;
        m_stabPressure1 = pressure;
        m_stabPressure2 = pressure;
        m_stabPressureLastMs = stabilizerNowMs;
    }

    // Measure the trajectory that is actually painted. Idle catch-up calls this
    // path too: although no new hardware event exists, the stabilized cursor is
    // still moving and therefore has a real Stroke Speed. Sampling only real pen
    // packets left startup speed forever "unreliable" after a short move + hold,
    // which kept the deferred stroke frozen until input resumed.
    BrushInputDynamics sampledInputDynamics = inputDynamics;
    sampledInputDynamics.strokeSpeed
        = sampleSmoothedStrokeSpeed(resolved.x, resolved.y, strokeSpeedNowMs);
    sampledInputDynamics.strokeSpeedAvailable = true;
    currentBrush->setPressure(emitPressure);
    currentBrush->setInputDynamics(sampledInputDynamics);
    currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);

    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    m_lastStrokeTargetPressure = pressure;
    m_lastStrokeTargetInputDynamics = sampledInputDynamics;
    m_lastStrokeTargetElapsedSeconds = strokeElapsedSeconds;

    const bool strokeSpeedBound = currentBrush->hasActiveDynamicsBinding(
        ruwa::core::brushes::BrushInputSourceKey::StrokeSpeed);
    // The Laplacian/Catmull-Rom path retains at least two future samples. That
    // look-ahead is appropriate for stabilized geometry and for the initial
    // Stroke Speed estimate, but a literal 0% setting must commit each resolved
    // input point in the current drain.
    const bool zeroLatencyGeometry = stabLagMs <= 0.0f && !strokeSpeedBound;
    continueStrokeWithResolvedPoint(worldX, worldY, emitPressure, dabElapsedSeconds,
        sampledInputDynamics, resolved, zeroLatencyGeometry, false, false);
    updateStabilizerCatchupTimer();
    if (requestRenderAfterStep && m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::continueStrokeWithResolvedPoint(float worldX, float worldY, float pressure,
    float dabElapsedSeconds, const BrushInputDynamics& inputDynamics,
    const Vector2& resolvedPoint, bool zeroLatencyGeometry, bool requestRenderAfterStep,
    bool updateCatchupTimer)
{
    if (!m_isDrawing) {
        return;
    }

    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    if (!currentBrush || !grid) {
        return;
    }

    currentBrush->setPressure(pressure);
    currentBrush->setInputDynamics(inputDynamics);
    currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);

    const Vector2 stabilizedPoint = resolvedPoint;

    float moveDx = stabilizedPoint.x - m_lastStrokeInputX;
    float moveDy = stabilizedPoint.y - m_lastStrokeInputY;
    const bool stabilizerCatchupPending
        = ruwa::core::brushes::hasPendingStrokeStabilizer(m_stabilizationState, worldX, worldY);
    // Pressure-only input must be compared with the previous input sample, not
    // m_lastStrokePressure. The latter is the rasterized path anchor and can lag
    // several samples behind while the live smoothing window is populated.
    const float pressureDelta = std::abs(pressure - m_lastStrokeInputPressure);
    const bool pressureChanged = pressureDelta > 0.001f;
    const bool strokeSpeedBound = currentBrush->hasActiveDynamicsBinding(
        ruwa::core::brushes::BrushInputSourceKey::StrokeSpeed);
    const auto normalizedAngleDistance = [](float first, float second) {
        const float direct = std::abs(first - second);
        return std::min(direct, 1.0f - direct);
    };
    const bool strokeSpeedChanged = strokeSpeedBound
        && (inputDynamics.strokeSpeedAvailable
                != m_lastRawStrokeInputDynamics.strokeSpeedAvailable
            || (inputDynamics.strokeSpeedAvailable
                && std::abs(inputDynamics.strokeSpeed - m_lastRawStrokeInputDynamics.strokeSpeed)
                    > 0.001f));
    const bool inputDynamicsChanged
        = inputDynamics.penTiltAvailable != m_lastRawStrokeInputDynamics.penTiltAvailable
        || (inputDynamics.penTiltAvailable
            && normalizedAngleDistance(
                   inputDynamics.penTilt, m_lastRawStrokeInputDynamics.penTilt)
                > (0.5f / 360.0f))
        || strokeSpeedChanged;
    const bool movementBelowThreshold = (moveDx * moveDx + moveDy * moveDy)
        < (kQuickLineMovementEpsilon * kQuickLineMovementEpsilon);
    const bool hasMeaningfulMovement = !movementBelowThreshold;
    if (!hasMeaningfulMovement && !pressureChanged && !inputDynamicsChanged) {
        if (updateCatchupTimer && stabilizerCatchupPending) {
            updateStabilizerCatchupTimer();
        }
        return;
    }

    const bool shouldResetQuickShapeHold = hasMeaningfulMovement;
    const bool quickShapeHoldAllowed = !currentBrush->isBlurMode() && !currentBrush->isSmudgeMode()
        && !currentBrush->isWetMode() && !currentBrush->isLiquifyMode();
    if (shouldResetQuickShapeHold) {
        if (auto* quickShape = quickShapeMorph(); quickShape && quickShapeHoldAllowed) {
            quickShape->stopHoldTimer();
        }
    }

    const float prevPressure = m_lastStrokePressure;
    const float prevX = m_lastStrokeX;
    const float prevY = m_lastStrokeY;

    auto* layer = activeLayer();
    TileGrid* paintMask = effectivePaintMask(layer, grid);
    configureBrushSelectionMaskAlpha(*currentBrush, layer, paintMask);
    auto* executionBackend = brushExecutionBackend();
    const bool realtimeRebuild = strokeNeedsRealtimeRebuild();
    const bool waitingForInitialMotion = currentBrush->strokeDabs().empty()
        && currentBrush->requiresMotionBeforeFirstDab();
    // Realtime replay normally bypasses the geometry look-ahead. Keep only its
    // first moving samples pending until speed is observable.
    const bool deferRealtimeStrokeSpeedMotion = realtimeRebuild && strokeSpeedBound
        && !m_initialStrokeSpeedSeeded && !m_strokeSpeedStartupEstimateReliable;
    std::unordered_set<TileKey, TileKeyHash> rebuiltTiles;
    bool previewUpdated = false;
    bool skippedDeferredMotion = false;
    const size_t changedDabStart = currentBrush->strokeDabs().size();

    if (!hasMeaningfulMovement && (pressureChanged || inputDynamicsChanged)) {
        if (!currentBrush->usesNonAccumulatingDabBlend()) {
            // A pressure-only packet revises the current input sample; it is not
            // distance travelled by the brush. Re-stamping an accumulating
            // (src-over or dynamic-color) dab here makes opacity depend on the
            // tablet report rate and creates dark stationary dots. Keep the
            // pending endpoint pressure current so the next real segment (or
            // the end-of-stroke drain) still interpolates the latest pressure.
            if (m_liveStrokePoints.size() > 1) {
                m_liveStrokePoints.back().pressure = pressure;
                m_liveStrokePoints.back().strokeElapsedSeconds = dabElapsedSeconds;
                m_liveStrokePoints.back().inputDynamics = inputDynamics;
                m_liveStrokePoints.back().strokeSpeedReliable
                    = m_strokeSpeedStartupEstimateReliable || m_initialStrokeSpeedSeeded;
            }
            m_lastStrokeInputPressure = pressure;
            m_lastRawStrokeInputDynamics = inputDynamics;
            m_lastStrokeInputElapsedSeconds = dabElapsedSeconds;
            if (updateCatchupTimer && stabilizerCatchupPending) {
                updateStabilizerCatchupTimer();
            }
            return;
        }

        if (waitingForInitialMotion) {
            skippedDeferredMotion = true;
            previewUpdated = false;
        } else if (realtimeRebuild) {
            ++m_realtimePreviewEventCount;
            currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);
            currentBrush->recordDabPoint(stabilizedPoint.x, stabilizedPoint.y);
            previewUpdated = rebuildStrokePreviewFromDabs(
                grid, paintMask, executionBackend, true, &rebuiltTiles);
        } else if (m_useGPUBrush && executionBackend) {
            const bool hasCurrentContext
                = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
            if (!hasCurrentContext && m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, stabilizedPoint.x,
                stabilizedPoint.y, paintMask, true, dabElapsedSeconds, true);
            if (!hasCurrentContext && m_callbacks.doneCurrent) {
                m_callbacks.doneCurrent();
            }
        } else if (executionBackend) {
            m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, stabilizedPoint.x,
                stabilizedPoint.y, paintMask, false, dabElapsedSeconds, true);
        } else {
            if (brushRequiresGpuEffect(currentBrush)) {
                m_useGPUBrush = false;
                return;
            }
            currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);
            currentBrush->stamp(*grid, stabilizedPoint.x, stabilizedPoint.y, paintMask);
            m_useGPUBrush = false;
        }

        if (!m_liveStrokePoints.empty()) {
            m_liveStrokePoints.back().pressure = pressure;
            m_liveStrokePoints.back().strokeElapsedSeconds = dabElapsedSeconds;
            m_liveStrokePoints.back().inputDynamics = inputDynamics;
        }
        if (realtimeRebuild || m_liveStrokePoints.size() <= 1) {
            m_lastStrokeX = stabilizedPoint.x;
            m_lastStrokeY = stabilizedPoint.y;
            m_lastStrokePressure = pressure;
            m_lastStrokeInputDynamics = inputDynamics;
            m_lastStrokeElapsedSeconds = dabElapsedSeconds;
        }
    } else if (deferRealtimeStrokeSpeedMotion) {
        m_liveStrokePoints.push_back({ stabilizedPoint, pressure, dabElapsedSeconds, inputDynamics,
            false });
        skippedDeferredMotion = true;
    } else if (realtimeRebuild) {
        ++m_realtimePreviewEventCount;
        currentBrush->setStrokeElapsedSeconds(dabElapsedSeconds, true);
        std::vector<TileBrush::DabPoint> segmentDabs;
        if (!m_initialStrokeSpeedSeeded && strokeSpeedBound && m_liveStrokePoints.size() > 1) {
            m_liveStrokePoints.push_back({ stabilizedPoint, pressure, dabElapsedSeconds,
                inputDynamics, m_strokeSpeedStartupEstimateReliable });
            backfillDeferredStrokeSpeed(m_liveStrokePoints.back().inputDynamics);
            for (size_t i = 1; i < m_liveStrokePoints.size(); ++i) {
                const LiveStrokePoint& from = m_liveStrokePoints[i - 1];
                const LiveStrokePoint& to = m_liveStrokePoints[i];
                currentBrush->appendInterpolatedStrokeDabs(from.point.x, from.point.y, to.point.x,
                    to.point.y, from.pressure, to.pressure, segmentDabs,
                    from.strokeElapsedSeconds, to.strokeElapsedSeconds, true, from.inputDynamics,
                    to.inputDynamics);
            }
            m_liveStrokePoints.clear();
        } else {
            if (!m_initialStrokeSpeedSeeded) {
                backfillDeferredStrokeSpeed(inputDynamics);
            }
            currentBrush->appendInterpolatedStrokeDabs(prevX, prevY, stabilizedPoint.x,
                stabilizedPoint.y, prevPressure, pressure, segmentDabs, m_lastStrokeElapsedSeconds,
                dabElapsedSeconds, true, m_lastStrokeInputDynamics, inputDynamics);
        }
        if (!m_initialStrokeSpeedSeeded
            && currentBrush->strokeDabs().size() > changedDabStart) {
            m_initialStrokeSpeedSeeded = true;
        }
        previewUpdated
            = rebuildStrokePreviewFromDabs(grid, paintMask, executionBackend, true, &rebuiltTiles);
    } else if (zeroLatencyGeometry) {
        // Reuse the established segment rasterizer, but commit the current input
        // point immediately instead of retaining the two-sample smoothing tail.
        rasterizeStrokeSegment(grid, paintMask, executionBackend, prevX, prevY,
            stabilizedPoint.x, stabilizedPoint.y, prevPressure, pressure,
            m_lastStrokeElapsedSeconds, dabElapsedSeconds, m_lastStrokeInputDynamics,
            inputDynamics);

        m_prevEmittedPoint = { prevX, prevY };
        m_prevEmittedInputDynamics = m_lastStrokeInputDynamics;
        m_lastStrokeX = stabilizedPoint.x;
        m_lastStrokeY = stabilizedPoint.y;
        m_lastStrokePressure = pressure;
        m_lastStrokeInputDynamics = inputDynamics;
        m_lastStrokeElapsedSeconds = dabElapsedSeconds;

        // Keep the same single anchor invariant beginStroke establishes. If a
        // dynamics binding turns stabilization on later in this stroke, the
        // existing look-ahead path can continue from this exact committed point.
        m_liveStrokePoints.clear();
        m_liveStrokePoints.push_back(
            { stabilizedPoint, pressure, dabElapsedSeconds, inputDynamics, true });
    } else {
        // Adaptive Laplacian smoothing of a sliding window over the live
        // polyline. Replaces the previous spline-based emission entirely.
        //
        //   m_liveStrokePoints[0]            = anchor (matches m_lastStroke*),
        //                                      i.e. the position the canvas
        //                                      was last drawn to.
        //   m_liveStrokePoints[1 .. N-2]     = pending interior points; their
        //                                      positions are iteratively
        //                                      averaged with their neighbors
        //                                      each time a new sample arrives.
        //   m_liveStrokePoints[N-1]          = the just-pushed raw sample;
        //                                      kept raw so the curve responds
        //                                      to direction changes promptly.
        //
        // Once enough future samples sit behind an interior point that
        // further smoothing would barely move it, we emit a straight chord
        // from the anchor to that point and slide the anchor forward.
        //
        // Both per-push iteration count and emission lag scale with 1/zoom:
        // on a zoomed-out canvas, one screen pixel of pointer jitter maps
        // to several world pixels of zig-zag, so we apply much stronger
        // filtering there. At zoom >= 1 the filter degenerates to near
        // pass-through (single iteration, lag of 2 samples).
        m_liveStrokePoints.push_back({ stabilizedPoint, pressure, dabElapsedSeconds, inputDynamics,
            m_strokeSpeedStartupEstimateReliable || m_initialStrokeSpeedSeeded });

        const float zoom = std::max(viewportZoom(), 0.05f);
        const float zoomInv = 1.0f / zoom;
        const int iterPerPush
            = std::clamp(static_cast<int>(std::lround((zoomInv - 1.0f) * 0.4f)) + 1, 1, 4);
        const int lagSamples
            = std::clamp(static_cast<int>(std::lround((zoomInv - 1.0f) * 0.6f)) + 2, 2, 8);

        const size_t bufN = m_liveStrokePoints.size();
        if (bufN >= 3) {
            for (int it = 0; it < iterPerPush; ++it) {
                Vector2 prev = m_liveStrokePoints[0].point;
                for (size_t i = 1; i + 1 < bufN; ++i) {
                    const Vector2 cur = m_liveStrokePoints[i].point;
                    const Vector2& next = m_liveStrokePoints[i + 1].point;
                    m_liveStrokePoints[i].point = { 0.5f * cur.x + 0.25f * (prev.x + next.x),
                        0.5f * cur.y + 0.25f * (prev.y + next.y) };
                    prev = cur;
                }
            }
        }

        while (m_liveStrokePoints.size() > static_cast<size_t>(lagSamples) + 1
            && (!strokeSpeedBound || m_strokeSpeedStartupEstimateReliable
                || m_initialStrokeSpeedSeeded)) {
            if (!m_initialStrokeSpeedSeeded) {
                // Use the newest point in the look-ahead, not the first movement
                // packet at the head of the buffer.
                backfillDeferredStrokeSpeed(m_liveStrokePoints.back().inputDynamics);
            }
            const LiveStrokePoint head = m_liveStrokePoints[1];
            // Emit a Catmull-Rom curve through the de-jittered vertices instead
            // of a straight chord, so the silhouette is curved rather than a
            // smoothed polygon. p1=anchor, p2=head; p0/p3 are the neighbouring
            // vertices that set the tangents (duplicated at the ends).
            const Vector2 p1 { m_lastStrokeX, m_lastStrokeY };
            const Vector2 p2 = head.point;
            const Vector2 p3 = (m_liveStrokePoints.size() > 2) ? m_liveStrokePoints[2].point : p2;
            const BrushInputDynamics& p3InputDynamics = (m_liveStrokePoints.size() > 2)
                ? m_liveStrokePoints[2].inputDynamics
                : head.inputDynamics;
            rasterizeCatmullRomStroke(grid, paintMask, executionBackend, m_prevEmittedPoint, p1, p2,
                p3, m_lastStrokePressure, head.pressure, m_lastStrokeElapsedSeconds,
                head.strokeElapsedSeconds, m_prevEmittedInputDynamics, m_lastStrokeInputDynamics,
                head.inputDynamics, p3InputDynamics);
            if (!m_initialStrokeSpeedSeeded
                && currentBrush->strokeDabs().size() > changedDabStart) {
                m_initialStrokeSpeedSeeded = true;
            }
            m_prevEmittedPoint = p1;
            m_prevEmittedInputDynamics = m_lastStrokeInputDynamics;
            m_lastStrokeX = head.point.x;
            m_lastStrokeY = head.point.y;
            m_lastStrokePressure = head.pressure;
            m_lastStrokeInputDynamics = head.inputDynamics;
            m_lastStrokeElapsedSeconds = head.strokeElapsedSeconds;
            m_liveStrokePoints.erase(m_liveStrokePoints.begin());
            // After erase, what was index [1] is now index [0] — the
            // just-emitted point. It already carries the smoothed position
            // we just rasterized to, so it serves as the new anchor with
            // no further fixup.
        }
    }

    if (realtimeRebuild && !skippedDeferredMotion) {
        m_lastStrokeX = stabilizedPoint.x;
        m_lastStrokeY = stabilizedPoint.y;
        m_lastStrokePressure = pressure;
        m_lastStrokeInputDynamics = inputDynamics;
        m_lastStrokeElapsedSeconds = dabElapsedSeconds;
    }
    m_lastStrokeInputX = stabilizedPoint.x;
    m_lastStrokeInputY = stabilizedPoint.y;
    m_lastStrokeInputPressure = pressure;
    m_lastRawStrokeInputDynamics = inputDynamics;
    m_lastStrokeInputElapsedSeconds = dabElapsedSeconds;

    if ((!realtimeRebuild && !skippedDeferredMotion) || previewUpdated) {
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
    }

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
    // See the note in beginStroke: only plain dab append keeps the stroke
    // buffer's tile set monotonically growing.
    auto tileSetChange = StrokeTileSetChange::Arbitrary;
    if (realtimeRebuild) {
        if (previewUpdated) {
            // If range rebuild populated rebuiltTiles, use that precise set.
            // Otherwise (full rebuild), fall back to all stroke buffer tiles.
            if (!rebuiltTiles.empty()) {
                changedKeys = std::move(rebuiltTiles);
            } else {
                collectStrokeChangedKeys(changedKeys);
            }
        }
    } else if (skippedDeferredMotion) {
        // No drawable segment has been observed yet.
    } else if (currentBrush->hasPositionScatterEffect()) {
        collectStrokeChangedKeys(changedKeys);
    } else {
        const size_t changedDabCount = currentBrush->strokeDabs().size() - changedDabStart;
        currentBrush->collectStrokeDabRangeCoveredTiles(
            changedDabStart, changedDabCount, changedKeys);
        if (changedKeys.empty()) {
            appendStepTouchedTileKeys(prevX, prevY, stabilizedPoint.x, stabilizedPoint.y,
                currentBrush->effectiveRadius(), documentBoundsWidth(), documentBoundsHeight(),
                changedKeys);
        }
        tileSetChange = StrokeTileSetChange::GrowOnly;
    }
    if (!changedKeys.empty()) {
        markStrokeBufferDirtyDelta(changedKeys, tileSetChange);
    }

    if (shouldResetQuickShapeHold) {
        if (auto* quickShape = quickShapeMorph(); quickShape && quickShapeHoldAllowed) {
            quickShape->restartHoldTimer();
        }
    }

    if (updateCatchupTimer) {
        updateStabilizerCatchupTimer();
    }
    if (requestRenderAfterStep
        && ((!realtimeRebuild && !skippedDeferredMotion) || previewUpdated)
        && m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::translateActiveStroke(float dx, float dy)
{
    if (!m_isDrawing) {
        return;
    }
    if (std::abs(dx) <= 0.0001f && std::abs(dy) <= 0.0001f) {
        return;
    }

    flushQueuedStrokeInput();

    // The whole stroke moves with the canvas grab, so the axis line it is
    // pinned to has to move with it.
    m_strokeAxisOriginX += dx;
    m_strokeAxisOriginY += dy;

    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    if (!currentBrush || !grid) {
        return;
    }

    if (auto* quickShape = quickShapeMorph(); quickShape && quickShape->isActive()) {
        quickShape->translate(dx, dy);
        m_lastStrokeX += dx;
        m_lastStrokeY += dy;
        m_prevEmittedPoint.x += dx;
        m_prevEmittedPoint.y += dy;
        m_strokeSpeedSampleX += dx;
        m_strokeSpeedSampleY += dy;
        return;
    }

    auto replayData = activeStrokeReplayData();
    if (!replayData || replayData->empty()) {
        return;
    }

    if (!replayData->translate(dx, dy)) {
        return;
    }

    for (auto& point : m_liveStrokePoints) {
        point.point.x += dx;
        point.point.y += dy;
    }

    m_lastStrokeX += dx;
    m_lastStrokeY += dy;
    m_prevEmittedPoint.x += dx;
    m_prevEmittedPoint.y += dy;
    m_lastStrokeTargetX += dx;
    m_lastStrokeTargetY += dy;
    m_lastStrokeInputX += dx;
    m_lastStrokeInputY += dy;
    m_strokeSpeedSampleX += dx;
    m_strokeSpeedSampleY += dy;
    ruwa::core::brushes::translateStrokeStabilizer(m_stabilizationState, dx, dy);

    auto* layer = activeLayer();
    TileGrid* paintMask = effectivePaintMask(layer, grid);
    configureBrushSelectionMaskAlpha(*currentBrush, layer, paintMask);
    auto* executionBackend = brushExecutionBackend();
    if (m_useGPUBrush && executionBackend) {
        if (m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, true);
        if (m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }
    } else if (executionBackend) {
        executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, false);
    } else {
        currentBrush->rebuildStrokeBufferFromDabs(paintMask, 0);
    }

    snapshotNewTiles(currentBrush->strokeBuffer(), grid);
    std::unordered_set<TileKey, TileKeyHash> changedKeys;
    changedKeys.reserve(currentBrush->strokeBuffer().tileCount());
    for (const auto& entry : currentBrush->strokeBuffer().tiles()) {
        changedKeys.insert(entry.first);
    }
    markStrokeBufferDirtyDelta(changedKeys);
    if (m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::endStroke()
{
    if (!m_isDrawing) {
        return;
    }
    // Stop dwell stamping the instant the user releases — no dwell dabs should
    // land during the queue drain / finalization.
    m_liquifyDwellTimer.stop();
    if (m_endStrokeRequested) {
        // Drain + completion already pending; m_strokeInputTimer will fire
        // completeEndStrokeAfterQueueDrain when the queue empties.
        return;
    }

    // Capture quick-shape state at the user-release instant; the queue drain
    // could change it before we finalize.
    m_endStrokeQuickShapeWasActive = quickShapeMorph() && quickShapeMorph()->isActive();

    // Samples may still be queued if the release lands between two drains. That
    // is at most one tick's worth now that every drain empties the queue, but the
    // deferral is kept: with a huge brush a single tick's worth of samples is
    // still enough work to be worth spreading, and this path also covers a drain
    // that was interrupted. drainQueuedStrokeInput / processQueuedStrokeInput
    // re-enter completeEndStrokeAfterQueueDrain once the queue empties.
    if (!m_queuedStrokeSamples.empty()) {
        m_endStrokeRequested = true;
        if (!m_strokeInputTimer.isActive()) {
            m_strokeInputTimer.start();
        }
        return;
    }

    completeEndStrokeAfterQueueDrain();
}

void BrushStrokeHost::completeEndStrokeAfterQueueDrain()
{
    if (!m_isDrawing) {
        return;
    }
    m_endStrokeRequested = false;

    const bool quickShapeWasActive = m_endStrokeQuickShapeWasActive;
    const bool stabilizerCatchupWasPending = !quickShapeWasActive && hasPendingStabilizerCatchup();
    m_strokeInputTimer.stop();
    m_queuedStrokeSamples.clear();
    m_processingQueuedStrokeInput = false;
    m_queuedSamplesSinceCompaction = 0;
    m_stabilizerCatchupTimer.stop();
    m_isDrawing = false;
    ruwa::core::brushes::clearStrokeStabilizer(m_stabilizationState);
    m_lastRealtimeTaperTailStart = std::numeric_limits<size_t>::max();
    m_lastRealtimeTaperPreviewDabCount = 0;
    m_lastRealtimeTaperPreviewWasSampled = false;
    m_lastRealtimeTaperPreviewNs = std::numeric_limits<qint64>::min();

    TileBrush* currentBrush = brush();
    if (auto* quickShape = quickShapeMorph()) {
        quickShape->stop();
    }
    if (!currentBrush) {
        clearStrokeRuntimeState();
        return;
    }

    TileGrid* grid = activeLayerTileGrid();
    auto* layer = activeLayer();
    if (!grid || !layer) {
        if (m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        if (m_callbacks.cleanupStrokeTextures) {
            m_callbacks.cleanupStrokeTextures();
        }
        if (m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }
        currentBrush->cancelStroke();
        clearStrokeRuntimeState();
        return;
    }

    auto* executionBackend = brushExecutionBackend();
    TileGrid* paintMask = effectivePaintMask(layer, grid);
    configureBrushSelectionMaskAlpha(*currentBrush, layer, paintMask);
    const bool hasPendingMotion = m_liveStrokePoints.size() > 1;
    const bool waitingForInitialMotion = currentBrush->strokeDabs().empty()
        && currentBrush->requiresMotionBeforeFirstDab() && !hasPendingMotion;
    if (!quickShapeWasActive && !waitingForInitialMotion
        && (!strokeNeedsRealtimeRebuild() || hasPendingMotion)) {
        // Flush the Laplacian smoothing buffer: apply a strong final
        // smoothing pass to converge any unemitted interior points, then
        // emit them all as chord segments. Without this, a long lag-window
        // worth of pending points would be collapsed into a single straight
        // tail by the rasterize-to-input-pos block below, producing a
        // visible "shortcut" at stroke end.
        if (m_liveStrokePoints.size() >= 3) {
            constexpr int kFinalSmoothingPasses = 8;
            for (int it = 0; it < kFinalSmoothingPasses; ++it) {
                Vector2 prev = m_liveStrokePoints[0].point;
                const size_t bufN = m_liveStrokePoints.size();
                for (size_t i = 1; i + 1 < bufN; ++i) {
                    const Vector2 cur = m_liveStrokePoints[i].point;
                    const Vector2& next = m_liveStrokePoints[i + 1].point;
                    m_liveStrokePoints[i].point = { 0.5f * cur.x + 0.25f * (prev.x + next.x),
                        0.5f * cur.y + 0.25f * (prev.y + next.y) };
                    prev = cur;
                }
            }
        }
        while (m_liveStrokePoints.size() > 1) {
            if (!m_initialStrokeSpeedSeeded) {
                backfillDeferredStrokeSpeed(m_liveStrokePoints.back().inputDynamics);
            }
            const size_t drainDabStart = currentBrush->strokeDabs().size();
            const LiveStrokePoint head = m_liveStrokePoints[1];
            // Same Catmull-Rom emission as the live loop, so the drained tail
            // keeps the curved silhouette instead of falling back to chords.
            const Vector2 p1 { m_lastStrokeX, m_lastStrokeY };
            const Vector2 p2 = head.point;
            const Vector2 p3 = (m_liveStrokePoints.size() > 2) ? m_liveStrokePoints[2].point : p2;
            const BrushInputDynamics& p3InputDynamics = (m_liveStrokePoints.size() > 2)
                ? m_liveStrokePoints[2].inputDynamics
                : head.inputDynamics;
            rasterizeCatmullRomStroke(grid, paintMask, executionBackend, m_prevEmittedPoint, p1, p2,
                p3, m_lastStrokePressure, head.pressure, m_lastStrokeElapsedSeconds,
                head.strokeElapsedSeconds, m_prevEmittedInputDynamics, m_lastStrokeInputDynamics,
                head.inputDynamics, p3InputDynamics);
            if (!m_initialStrokeSpeedSeeded
                && currentBrush->strokeDabs().size() > drainDabStart) {
                m_initialStrokeSpeedSeeded = true;
            }
            m_prevEmittedPoint = p1;
            m_prevEmittedInputDynamics = m_lastStrokeInputDynamics;
            m_lastStrokeX = head.point.x;
            m_lastStrokeY = head.point.y;
            m_lastStrokePressure = head.pressure;
            m_lastStrokeInputDynamics = head.inputDynamics;
            m_lastStrokeElapsedSeconds = head.strokeElapsedSeconds;
            m_liveStrokePoints.erase(m_liveStrokePoints.begin());
        }
        auto replayData = activeStrokeReplayData();
        if (!replayData || replayData->empty()) {
            currentBrush->setInputDynamics(m_lastRawStrokeInputDynamics);
            if (m_useGPUBrush && executionBackend) {
                if (m_callbacks.makeCurrent) {
                    m_callbacks.makeCurrent();
                }
                m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, m_lastStrokeInputX,
                    m_lastStrokeInputY, paintMask, true, m_lastStrokeInputElapsedSeconds, true);
                if (m_callbacks.doneCurrent) {
                    m_callbacks.doneCurrent();
                }
            } else if (executionBackend) {
                m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, m_lastStrokeInputX,
                    m_lastStrokeInputY, paintMask, false, m_lastStrokeInputElapsedSeconds, true);
            } else {
                if (brushRequiresGpuEffect(currentBrush)) {
                    m_useGPUBrush = false;
                } else {
                    currentBrush->setStrokeElapsedSeconds(m_lastStrokeInputElapsedSeconds, true);
                    currentBrush->stamp(*grid, m_lastStrokeInputX, m_lastStrokeInputY, paintMask);
                    m_useGPUBrush = false;
                }
            }
        }

        const float tailDx = m_lastStrokeInputX - m_lastStrokeX;
        const float tailDy = m_lastStrokeInputY - m_lastStrokeY;
        const bool hasUnrenderedTail = (tailDx * tailDx + tailDy * tailDy) > 0.0001f;
        if (!stabilizerCatchupWasPending && hasUnrenderedTail) {
            rasterizeStrokeSegment(grid, paintMask, executionBackend, m_lastStrokeX, m_lastStrokeY,
                m_lastStrokeInputX, m_lastStrokeInputY, m_lastStrokePressure,
                m_lastStrokeInputPressure, m_lastStrokeElapsedSeconds,
                m_lastStrokeInputElapsedSeconds, m_lastStrokeInputDynamics,
                m_lastRawStrokeInputDynamics);
            m_lastStrokeX = m_lastStrokeInputX;
            m_lastStrokeY = m_lastStrokeInputY;
            m_lastStrokePressure = m_lastStrokeInputPressure;
            m_lastStrokeInputDynamics = m_lastRawStrokeInputDynamics;
            m_lastStrokeElapsedSeconds = m_lastStrokeInputElapsedSeconds;
        }
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
    }

    m_liveStrokePoints.clear();
    const bool shouldPreserveAlpha = shouldPreserveAlphaForPaintMask(layer, paintMask);
    const bool useAlphaLockFlatten = shouldPreserveAlpha && !currentBrush->isEraseMode();
    TileGrid* finalSourceMask = currentBrush->selectionMaskAffectsAlpha()
            && !currentBrush->isBlurMode() && !currentBrush->isSmudgeMode()
            && !currentBrush->isWetMode() && !currentBrush->isLiquifyMode()
        ? paintMask
        : nullptr;
    // Soft-selection alpha cap at commit: applies on layers where the alpha-lock
    // emulation does NOT activate (i.e. non-source layers under a soft selection).
    // The cap forces the result alpha never to exceed the mask alpha, even
    // across multiple committed strokes.
    bool selectionAlphaCap = false;
    if (finalSourceMask && !useAlphaLockFlatten && !currentBrush->isEraseMode()
        && !currentBrush->isBlurMode() && !currentBrush->isSmudgeMode()
        && !currentBrush->isWetMode() && !currentBrush->isLiquifyMode()
        && m_callbacks.shouldPreserveAlphaForPaintMask) {
        // Reuse the soft-alpha detection that already powers the preview cap
        // (LayerCompositingBuilder reads this same flag via getSelectionMaskHasSoftAlpha).
        // Here we only need the boolean: if the mask has any soft alpha pixel,
        // cap is meaningful.
        selectionAlphaCap = LayerCompositingBuilder::hasSoftMaskAlpha(finalSourceMask);
    }

    auto markRebuiltPreviewDirty = [this]() {
        std::unordered_set<TileKey, TileKeyHash> changedKeys;
        collectStrokeChangedKeys(changedKeys);
        markStrokeBufferDirtyDelta(changedKeys);
    };

    if (m_quickLineStrokeModified) {
        if (m_useGPUBrush && executionBackend) {
            if (m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, true);
            if (m_callbacks.doneCurrent) {
                m_callbacks.doneCurrent();
            }
        } else if (executionBackend) {
            executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, false);
        } else {
            currentBrush->rebuildStrokeBufferFromDabs(paintMask);
        }
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
        markRebuiltPreviewDirty();
    }

    if (strokeNeedsRealtimeRebuild()) {
        rebuildStrokePreviewFromDabs(grid, paintMask, executionBackend, false);
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
        markRebuiltPreviewDirty();
    }

    if (currentBrush->hasStrokePostProcessingEffect()) {
        bool rebuildNeeded = false;
        // Quick shape replay dabs are already procedural target geometry.
        // Freehand post/end corrections distort the committed shape and can add a seam/tail.
        if (!quickShapeWasActive) {
            rebuildNeeded |= currentBrush->applyPostCorrectionToDabs();
            rebuildNeeded |= currentBrush->applyEndpointCorrectionToDabs();
        }
        if (rebuildNeeded) {
            if (currentBrush->hasTaperEffect()) {
                currentBrush->applyStrokeTaperToDabs();
            }

            if (m_useGPUBrush && executionBackend) {
                if (m_callbacks.makeCurrent) {
                    m_callbacks.makeCurrent();
                }
                executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, true);
                if (m_callbacks.doneCurrent) {
                    m_callbacks.doneCurrent();
                }
            } else if (executionBackend) {
                executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, false);
            } else {
                currentBrush->rebuildStrokeBufferFromDabs(paintMask);
            }
            snapshotNewTiles(currentBrush->strokeBuffer(), grid);
            markRebuiltPreviewDirty();
        }
    }

    const bool completedEraseMode = currentBrush->isEraseMode();
    const float completedStrokeOpacity = currentBrush->strokeOpacity();
    const QUuid completedLayerId = layer->id;
    // Erasing while the layer mask is the active paint target: the mask stores a
    // grayscale reveal (white = visible, black = hidden), so the eraser must
    // paint BLACK (src-over) instead of doing destination-out — destination-out
    // would clear the white toward transparent, which reads as reveal-all. The
    // eraser's opacity carries through the normal flatten unchanged.
    const bool maskErase
        = completedEraseMode && layer->maskEditActive && layer->maskGrid != nullptr;
    std::shared_ptr<TileGrid> strokeBlendBackdrop;
    Color strokeBlendBackdropColor = Color::transparent();
    const bool needsStrokeBlendBackdrop = !completedEraseMode && !currentBrush->isBlurMode()
        && !currentBrush->isSmudgeMode() && !currentBrush->isWetMode()
        && !currentBrush->isLiquifyMode()
        && currentBrush->strokeBlendMode() != ruwa::core::layers::BlendMode::Normal
        && m_callbacks.buildStrokeBlendBackdrop;
    if (needsStrokeBlendBackdrop) {
        if (m_callbacks.getStrokeBlendBackdropColor) {
            strokeBlendBackdropColor = m_callbacks.getStrokeBlendBackdropColor();
        }
        std::unordered_set<TileKey, TileKeyHash> strokeKeys;
        strokeKeys.reserve(currentBrush->strokeBuffer().tileCount());
        for (const auto& [key, tile] : currentBrush->strokeBuffer().tiles()) {
            strokeKeys.insert(key);
        }
        strokeBlendBackdrop = m_callbacks.buildStrokeBlendBackdrop(completedLayerId, strokeKeys);
    }

    if (m_useGPUBrush && executionBackend) {
        if (m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        auto flattenedKeys = executionBackend->flattenStroke(*currentBrush, *grid, true,
            useAlphaLockFlatten, strokeBlendBackdrop.get(), strokeBlendBackdropColor,
            finalSourceMask, selectionAlphaCap, maskErase);
        std::vector<TileKey> readbackKeys(flattenedKeys.begin(), flattenedKeys.end());
        if (m_callbacks.cleanupStrokeTextures) {
            m_callbacks.cleanupStrokeTextures();
        }
        if (m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }

        currentBrush->cancelStroke();
        // Wait for any in-flight async BEFORE-snapshot memcpys to settle
        // before we hand the map off to the pending finalization.
        m_snapshotSync.waitForFinished();
        m_pending.active = true;
        m_pending.layerId = layer->id;
        m_pending.maskTarget = layer->maskEditActive && layer->maskGrid != nullptr;
        m_pending.flattenedKeys = std::move(flattenedKeys);
        m_pending.finalizationKeysOrdered = std::move(readbackKeys);
        m_pending.readbackBatchKeys.clear();
        m_pending.beforeTiles = std::move(m_strokeBeforeSnapshots);
        m_pending.afterTiles.clear();
        m_pending.createdTiles = std::move(m_strokeCreatedTiles);
        m_pending.removedTiles.clear();
        m_pending.eraseMode = completedEraseMode;
        m_pending.removeEmptyTiles = completedEraseMode && !maskErase;
        m_pending.readbackActive = !m_pending.finalizationKeysOrdered.empty();
        m_pending.strokePaintedEmitted = false;
        m_pending.selectionRestoreCaptured = false;
        m_pending.nextKey = 0;
        m_pending.selectionRestore.reset();
        m_pending.fence = nullptr;

        if (m_callbacks.queueDeferredStrokeCommit) {
            m_callbacks.queueDeferredStrokeCommit(completedLayerId, m_pending.flattenedKeys,
                completedEraseMode, completedStrokeOpacity);
        }
        m_finalizeTimer.start();
    } else {
        if (m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        if (m_callbacks.cleanupStrokeTextures) {
            m_callbacks.cleanupStrokeTextures();
        }
        if (m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }

        auto flattenedKeys = executionBackend
            ? executionBackend->flattenStroke(*currentBrush, *grid, false, useAlphaLockFlatten,
                  strokeBlendBackdrop.get(), strokeBlendBackdropColor, finalSourceMask,
                  selectionAlphaCap, maskErase)
            : currentBrush->endStroke(*grid, useAlphaLockFlatten, strokeBlendBackdrop.get(),
                  strokeBlendBackdropColor, finalSourceMask, selectionAlphaCap, maskErase);

        // Wait for any in-flight async BEFORE-snapshot memcpys to settle
        // before we hand the map off to the pending finalization.
        m_snapshotSync.waitForFinished();
        m_pending.active = true;
        m_pending.layerId = layer->id;
        m_pending.maskTarget = layer->maskEditActive && layer->maskGrid != nullptr;
        m_pending.flattenedKeys = std::move(flattenedKeys);
        m_pending.finalizationKeysOrdered.assign(
            m_pending.flattenedKeys.begin(), m_pending.flattenedKeys.end());
        m_pending.readbackBatchKeys.clear();
        m_pending.beforeTiles = std::move(m_strokeBeforeSnapshots);
        m_pending.afterTiles.clear();
        m_pending.createdTiles = std::move(m_strokeCreatedTiles);
        m_pending.removedTiles.clear();
        m_pending.eraseMode = completedEraseMode;
        m_pending.removeEmptyTiles = completedEraseMode && !maskErase;
        m_pending.readbackActive = false;
        m_pending.strokePaintedEmitted = false;
        m_pending.selectionRestoreCaptured = false;
        m_pending.nextKey = 0;
        m_pending.selectionRestore.reset();
        m_pending.fence = nullptr;

        if (m_callbacks.queueDeferredStrokeCommit) {
            m_callbacks.queueDeferredStrokeCommit(completedLayerId, m_pending.flattenedKeys,
                completedEraseMode, completedStrokeOpacity);
        }
        m_finalizeTimer.start();
    }

    clearStrokeRuntimeState();
    if (m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::rebuildPreviewFromCurrentDabs()
{
    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    auto* layer = activeLayer();
    if (!currentBrush || !grid || !layer) {
        return;
    }

    TileGrid* paintMask = effectivePaintMask(layer, grid);
    configureBrushSelectionMaskAlpha(*currentBrush, layer, paintMask);
    auto* executionBackend = brushExecutionBackend();
    if (m_useGPUBrush && executionBackend) {
        if (m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, true);
        if (m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }
    } else if (executionBackend) {
        executionBackend->rebuildStrokeFromDabs(*currentBrush, paintMask, 0, false);
    } else {
        currentBrush->rebuildStrokeBufferFromDabs(paintMask, 0);
    }
}

void BrushStrokeHost::notifyQuickShapePreviewModified()
{
    TileBrush* currentBrush = brush();
    if (!currentBrush) {
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
    for (const auto& entry : currentBrush->strokeBuffer().tiles()) {
        changedKeys.insert(entry.first);
    }
    m_quickLineStrokeModified = true;
    markStrokeBufferDirtyDelta(changedKeys);
    if (m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::clearStrokeRuntimeState()
{
    // Make sure any in-flight async snapshot writes have finished before we
    // tear down the destination buffers they're writing into.
    m_snapshotSync.waitForFinished();
    decltype(m_strokeBeforeSnapshots) {}.swap(m_strokeBeforeSnapshots);
    decltype(m_strokeCreatedTiles) {}.swap(m_strokeCreatedTiles);
    decltype(m_strokeSnapshotted) {}.swap(m_strokeSnapshotted);
    decltype(m_prevStrokePreviewKeys) {}.swap(m_prevStrokePreviewKeys);
    ruwa::core::brushes::clearStrokeStabilizer(m_stabilizationState);
    m_quickLineStrokeModified = false;
    m_endStrokeRequested = false;
    m_endStrokeQuickShapeWasActive = false;
    m_useGPUBrush = false;
    m_lastStrokeElapsedSeconds = 0.0f;
    m_lastStrokeTargetElapsedSeconds = 0.0f;
    m_lastStrokeInputElapsedSeconds = 0.0f;
    m_dabClockValid = false;
    m_dabClockPrevSynthMs = 0.0;
    m_dabClockElapsedMs = 0.0;
    m_strokeSpeedSampleX = 0.0f;
    m_strokeSpeedSampleY = 0.0f;
    m_strokeSpeedCumulativeScreenDistance = 0.0;
    m_strokeSpeedMeasurements.clear();
    m_strokeSpeedFirstMotionSampleTimeMs = 0.0;
    m_strokeSpeedFirstMotionScreenDistance = 0.0;
    m_strokeSpeedFilteredScreenPxPerSecond = 0.0f;
    m_strokeSpeedFilterVelocity = 0.0f;
    m_strokeSpeedFilterValid = false;
    m_strokeSpeedFirstMotionValid = false;
    m_strokeSpeedStartupEstimateReliable = false;
    m_initialStrokeSpeedSeeded = false;
    m_strokeElapsedTimer.invalidate();
    m_stabilizerCatchupTimer.stop();
    m_liquifyDwellTimer.stop();
    m_autoInputSmoothingValid = false;
    m_autoInputSmoothingPoint = {};
}

void BrushStrokeHost::rasterizeStrokeSegment(TileGrid* grid, TileGrid* selectionMask,
    BrushExecutionBackend* executionBackend, float fromX, float fromY, float toX, float toY,
    float fromPressure, float toPressure, float fromStrokeElapsedSeconds,
    float toStrokeElapsedSeconds, const BrushInputDynamics& fromInputDynamics,
    const BrushInputDynamics& toInputDynamics)
{
    TileBrush* currentBrush = brush();
    if (!grid || !currentBrush) {
        return;
    }

    if (m_useGPUBrush && executionBackend) {
        const bool hasCurrentContext
            = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
        if (!hasCurrentContext && m_callbacks.makeCurrent) {
            m_callbacks.makeCurrent();
        }
        m_useGPUBrush = executionBackend->strokeTo(*currentBrush, *grid, fromX, fromY, toX, toY,
            fromPressure, toPressure, selectionMask, true, fromStrokeElapsedSeconds,
            toStrokeElapsedSeconds, true, fromInputDynamics, toInputDynamics);
        if (!hasCurrentContext && m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }
    } else if (executionBackend) {
        m_useGPUBrush = executionBackend->strokeTo(*currentBrush, *grid, fromX, fromY, toX, toY,
            fromPressure, toPressure, selectionMask, false, fromStrokeElapsedSeconds,
            toStrokeElapsedSeconds, true, fromInputDynamics, toInputDynamics);
    } else {
        if (brushRequiresGpuEffect(currentBrush)) {
            m_useGPUBrush = false;
            return;
        }
        currentBrush->strokeToInterpolatedSize(*grid, fromX, fromY, toX, toY, fromPressure,
            toPressure, selectionMask, fromStrokeElapsedSeconds, toStrokeElapsedSeconds, true,
            fromInputDynamics, toInputDynamics);
        m_useGPUBrush = false;
    }
}

void BrushStrokeHost::rasterizeQuadraticStroke(TileGrid* grid, TileGrid* selectionMask,
    BrushExecutionBackend* executionBackend, const Vector2& start, const Vector2& control,
    const Vector2& end, float startPressure, float controlPressure, float endPressure,
    float startStrokeElapsedSeconds, float controlStrokeElapsedSeconds,
    float endStrokeElapsedSeconds)
{
    TileBrush* currentBrush = brush();
    if (!currentBrush) {
        return;
    }

    const Vector2 incoming { control.x - start.x, control.y - start.y };
    const Vector2 outgoing { end.x - control.x, end.y - control.y };
    const float turnSharpness = (1.0f - normalizedDot(incoming, outgoing)) * 0.5f;
    const float approxLength = vectorLength(incoming) + vectorLength(outgoing);
    const float controlDeviation = pointLineDistance(control, start, end);
    const float targetStep
        = std::max(0.35f, currentBrush->radius() * (0.24f - turnSharpness * 0.10f));
    const int segmentsByLength = static_cast<int>(std::ceil(approxLength / targetStep));
    const float curvatureTolerance = std::max(0.2f, currentBrush->radius() * 0.10f);
    const int segmentsByCurvature = static_cast<int>(
        std::ceil(std::sqrt(std::max(0.0f, controlDeviation) / curvatureTolerance) * 3.0f));
    const int segments
        = std::clamp(std::max(2, std::max(segmentsByLength, segmentsByCurvature)), 2, 64);

    const bool useGpuContext = (m_useGPUBrush && executionBackend);
    const bool hasCurrentContext
        = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
    if (useGpuContext && !hasCurrentContext && m_callbacks.makeCurrent) {
        m_callbacks.makeCurrent();
    }
    // Collect every micro-segment's dabs and stamp the span in one go.
    if (useGpuContext) {
        executionBackend->beginDabBatch(*currentBrush);
    }

    Vector2 prevPoint = start;
    float prevPressure = startPressure;
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const Vector2 nextPoint = quadraticPoint(start, control, end, t);
        const float midPressureA = lerpScalar(startPressure, controlPressure, t);
        const float midPressureB = lerpScalar(controlPressure, endPressure, t);
        const float nextPressure = lerpScalar(midPressureA, midPressureB, t);
        const float midElapsedA
            = lerpScalar(startStrokeElapsedSeconds, controlStrokeElapsedSeconds, t);
        const float midElapsedB
            = lerpScalar(controlStrokeElapsedSeconds, endStrokeElapsedSeconds, t);
        const float nextElapsedSeconds = lerpScalar(midElapsedA, midElapsedB, t);
        rasterizeStrokeSegment(grid, selectionMask, executionBackend, prevPoint.x, prevPoint.y,
            nextPoint.x, nextPoint.y, prevPressure, nextPressure, startStrokeElapsedSeconds,
            nextElapsedSeconds);
        prevPoint = nextPoint;
        prevPressure = nextPressure;
        startStrokeElapsedSeconds = nextElapsedSeconds;
    }

    // Must land before the caller touches the stroke buffer (snapshot, dirty
    // delta, rebuild, flatten) and while the GL context is still current.
    if (useGpuContext) {
        executionBackend->endDabBatch(*currentBrush, selectionMask);
    }
    if (useGpuContext && !hasCurrentContext && m_callbacks.doneCurrent) {
        m_callbacks.doneCurrent();
    }
}

void BrushStrokeHost::rasterizeCatmullRomStroke(TileGrid* grid, TileGrid* selectionMask,
    BrushExecutionBackend* executionBackend, const Vector2& p0, const Vector2& p1,
    const Vector2& p2, const Vector2& p3, float p1Pressure, float p2Pressure,
    float p1StrokeElapsedSeconds, float p2StrokeElapsedSeconds,
    const BrushInputDynamics& p0InputDynamics, const BrushInputDynamics& p1InputDynamics,
    const BrushInputDynamics& p2InputDynamics, const BrushInputDynamics& p3InputDynamics)
{
    TileBrush* currentBrush = brush();
    if (!currentBrush) {
        return;
    }

    // Interpolating uniform Catmull-Rom: the curve passes THROUGH p1 and p2
    // (b0 == p1, b3 == p2), so it drops into the vertex-anchored Laplacian
    // emission without shifting the path or breaking the anchor chain. We can
    // interpolate safely here (unlike a raw-sample spline, which zig-zags on
    // pixel-quantised jitter) because the caller feeds points that the Laplacian
    // has ALREADY de-jittered — so this only removes the chord faceting between
    // those smoothed vertices, the last visible trace of sample sparsity.
    // Tangents: m1 = (p2-p0)/2 at p1, m2 = (p3-p1)/2 at p2; Hermite→Bezier puts
    // the inner control points at p1 + m1/3 and p2 - m2/3 (the 1/6 factors).
    // kTension scales the tangents (0.5 = standard CR); lower it if any segment
    // overshoots into a wiggle.
    constexpr float kTension = 0.5f;
    constexpr float kHandle = kTension / 3.0f;
    const Vector2 b0 = p1;
    const Vector2 b1 { p1.x + (p2.x - p0.x) * kHandle, p1.y + (p2.y - p0.y) * kHandle };
    const Vector2 b2 { p2.x - (p3.x - p1.x) * kHandle, p2.y - (p3.y - p1.y) * kHandle };
    const Vector2 b3 = p2;

    // Subdivision count: blend chord length and curvature, similar to the
    // quadratic path. Use the convex hull (b0..b3 polyline length) as an
    // upper bound on arc length, and the max distance from b1/b2 to the
    // straight chord b0→b3 as a curvature proxy.
    const float chord01 = vectorLength({ b1.x - b0.x, b1.y - b0.y });
    const float chord12 = vectorLength({ b2.x - b1.x, b2.y - b1.y });
    const float chord23 = vectorLength({ b3.x - b2.x, b3.y - b2.y });
    const float approxLength = chord01 + chord12 + chord23;
    const float dev1 = pointLineDistance(b1, b0, b3);
    const float dev2 = pointLineDistance(b2, b0, b3);
    const float controlDeviation = std::max(dev1, dev2);
    // Smoothness is driven by CURVATURE, not length: hold the curve's deviation
    // from its chord under ~0.35 SCREEN px so the polyline is visually indistinct
    // from the true curve at any brush size/zoom. The previous tolerance was
    // radius-scaled (~0.10R), so a big brush approximated each curved segment
    // with as few as 2 straight pieces → faintly faceted silhouette. targetStep
    // is only a loose chord-length cap (kept radius-generous) so straight runs
    // are NOT over-subdivided — dab placement walks fine regardless, since the
    // spacing accumulator carries across micro-segments.
    const float zoom = std::max(viewportZoom(), 0.05f);
    const float targetStep = std::max(8.0f, currentBrush->radius());
    const int segmentsByLength = static_cast<int>(std::ceil(approxLength / targetStep));
    const float curvatureTolerance = std::max(0.05f, 0.35f / zoom);
    const int segmentsByCurvature = static_cast<int>(
        std::ceil(std::sqrt(std::max(0.0f, controlDeviation) / curvatureTolerance) * 3.0f));
    const int segments
        = std::clamp(std::max(2, std::max(segmentsByLength, segmentsByCurvature)), 2, 128);

    const bool useGpuContext = (m_useGPUBrush && executionBackend);
    const bool hasCurrentContext
        = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
    if (useGpuContext && !hasCurrentContext && m_callbacks.makeCurrent) {
        m_callbacks.makeCurrent();
    }
    // The hot one: this span is subdivided into up to 128 micro-segments, and
    // without coalescing each of them is a separate GPU stamp with full setup.
    if (useGpuContext) {
        executionBackend->beginDabBatch(*currentBrush);
    }

    BrushInputDynamics p1SplineInputDynamics = p1InputDynamics;
    BrushInputDynamics p2SplineInputDynamics = p2InputDynamics;
    const float segmentDistance = vectorLength({ p2.x - p1.x, p2.y - p1.y });
    if (p0InputDynamics.strokeSpeedAvailable && p1InputDynamics.strokeSpeedAvailable
        && p2InputDynamics.strokeSpeedAvailable && p3InputDynamics.strokeSpeedAvailable) {
        const float incomingDistance = vectorLength({ p1.x - p0.x, p1.y - p0.y });
        const float outgoingDistance = vectorLength({ p3.x - p2.x, p3.y - p2.y });
        p1SplineInputDynamics.strokeSpeedSpatialDerivative = monotonePathDerivative(
            p0InputDynamics.strokeSpeed, p1InputDynamics.strokeSpeed,
            p2InputDynamics.strokeSpeed, incomingDistance, segmentDistance);
        p2SplineInputDynamics.strokeSpeedSpatialDerivative = monotonePathDerivative(
            p1InputDynamics.strokeSpeed, p2InputDynamics.strokeSpeed,
            p3InputDynamics.strokeSpeed, segmentDistance, outgoingDistance);
        p1SplineInputDynamics.strokeSpeedSpatialDerivativeAvailable = true;
        p2SplineInputDynamics.strokeSpeedSpatialDerivativeAvailable = true;
    }

    Vector2 prevPoint = b0;
    float prevPressure = p1Pressure;
    float prevElapsed = p1StrokeElapsedSeconds;
    BrushInputDynamics prevInputDynamics = p1SplineInputDynamics;
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const Vector2 nextPoint = cubicPoint(b0, b1, b2, b3, t);
        const float nextPressure = lerpScalar(p1Pressure, p2Pressure, t);
        const float nextElapsed = lerpScalar(p1StrokeElapsedSeconds, p2StrokeElapsedSeconds, t);
        const BrushInputDynamics nextInputDynamics
            = ruwa::core::brushes::interpolateBrushInputDynamics(
                p1SplineInputDynamics, p2SplineInputDynamics, t, segmentDistance);
        rasterizeStrokeSegment(grid, selectionMask, executionBackend, prevPoint.x, prevPoint.y,
            nextPoint.x, nextPoint.y, prevPressure, nextPressure, prevElapsed, nextElapsed,
            prevInputDynamics, nextInputDynamics);
        prevPoint = nextPoint;
        prevPressure = nextPressure;
        prevElapsed = nextElapsed;
        prevInputDynamics = nextInputDynamics;
    }

    // Must land before the caller touches the stroke buffer (snapshot, dirty
    // delta, rebuild, flatten) and while the GL context is still current.
    if (useGpuContext) {
        executionBackend->endDabBatch(*currentBrush, selectionMask);
    }
    if (useGpuContext && !hasCurrentContext && m_callbacks.doneCurrent) {
        m_callbacks.doneCurrent();
    }
}

bool BrushStrokeHost::rebuildStrokePreviewFromDabs(TileGrid* grid, TileGrid* selectionMask,
    BrushExecutionBackend* executionBackend, bool allowPreviewSampling,
    std::unordered_set<TileKey, TileKeyHash>* outRebuiltTiles)
{
    TileBrush* currentBrush = brush();
    if (!grid || !currentBrush) {
        return false;
    }

    const bool usesTaperReplay = currentBrush->requiresRealtimeTaperReplay();
    constexpr qint64 kTaperPreviewFrameIntervalNs = 1000000000ll / 60ll;

    const auto rebuildFullPreview = [&](size_t maxPreviewDabs) {
        if (m_useGPUBrush && executionBackend) {
            const bool hasCurrentContext
                = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
            if (!hasCurrentContext && m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            executionBackend->rebuildStrokeFromDabs(
                *currentBrush, selectionMask, maxPreviewDabs, true);
            if (!hasCurrentContext && m_callbacks.doneCurrent) {
                m_callbacks.doneCurrent();
            }
        } else if (executionBackend) {
            executionBackend->rebuildStrokeFromDabs(
                *currentBrush, selectionMask, maxPreviewDabs, false);
        } else {
            currentBrush->rebuildStrokeBufferFromDabs(selectionMask, maxPreviewDabs);
        }
        // Full rebuild: all tiles in the stroke buffer were touched.
        // outRebuiltTiles = nullptr signals caller to use collectStrokeChangedKeys.
        return true;
    };

    if (usesTaperReplay) {
        const auto replayData = activeStrokeReplayData();
        const size_t strokeDabCount = replayData ? replayData->size() : 0;
        if (strokeDabCount == 0) {
            return false;
        }

        // Throttle before choosing full versus range replay. Previously the
        // whole-stroke/overlap branch returned above this gate, so the most
        // expensive path ran once per tablet packet instead of once per frame.
        if (allowPreviewSampling && m_realtimePreviewTimer.isValid()) {
            const qint64 previewNowNs = m_realtimePreviewTimer.nsecsElapsed();
            if (m_lastRealtimeTaperPreviewNs != std::numeric_limits<qint64>::min()
                && (previewNowNs - m_lastRealtimeTaperPreviewNs) < kTaperPreviewFrameIntervalNs) {
                return false;
            }
            m_lastRealtimeTaperPreviewNs = previewNowNs;
        }

        const TileBrush::StrokeTaperState taperState = currentBrush->strokeTaperState();
        if (taperState.dabCount != strokeDabCount) {
            // Replay data and TileBrush dabs are expected to be the same store.
            // Do not risk a partial clear against mismatched indices.
            currentBrush->applyStrokeTaperToDabs(taperState);
            m_lastRealtimeTaperTailStart = taperState.endRangeStart();
            m_lastRealtimeTaperPreviewDabCount = taperState.dabCount;
            const size_t previewDabLimit
                = allowPreviewSampling && taperState.dabCount > TileBrush::kMaxTaperAffectedDabs
                ? TileBrush::kMaxTaperAffectedDabs
                : 0;
            m_lastRealtimeTaperPreviewWasSampled = previewDabLimit > 0;
            return rebuildFullPreview(previewDabLimit);
        }

        if (taperState.touchesWholeStroke() || !allowPreviewSampling) {
            currentBrush->applyStrokeTaperToDabs(taperState);
            m_lastRealtimeTaperTailStart = taperState.endRangeStart();
            m_lastRealtimeTaperPreviewDabCount = strokeDabCount;

            // The existing rebuild API already supports a sampled interactive
            // replay. Cap only the live preview; endStroke passes
            // allowPreviewSampling=false and always commits every dab.
            const size_t previewDabLimit
                = allowPreviewSampling && strokeDabCount > TileBrush::kMaxTaperAffectedDabs
                ? TileBrush::kMaxTaperAffectedDabs
                : 0;
            m_lastRealtimeTaperPreviewWasSampled = previewDabLimit > 0;
            return rebuildFullPreview(previewDabLimit);
        }

        const size_t tailStart = taperState.endRangeStart();
        if (m_lastRealtimeTaperPreviewWasSampled) {
            // A range rebuild cannot fill dabs omitted by the preceding sampled
            // whole-stroke preview. Materialize one exact frame when leaving
            // the overlap phase, then subsequent frames can stay on ranges.
            currentBrush->applyStrokeTaperToDabs(taperState);
            m_lastRealtimeTaperTailStart = tailStart;
            m_lastRealtimeTaperPreviewDabCount = strokeDabCount;
            m_lastRealtimeTaperPreviewWasSampled = false;
            return rebuildFullPreview(0);
        }

        const size_t updateStart = stroke_taper::updateRangeStart(
            taperState, m_lastRealtimeTaperPreviewDabCount, m_lastRealtimeTaperTailStart);
        const size_t updateCount = strokeDabCount - updateStart;
        m_lastRealtimeTaperTailStart = tailStart;
        m_lastRealtimeTaperPreviewDabCount = strokeDabCount;
        m_lastRealtimeTaperPreviewWasSampled = false;
        currentBrush->applyStrokeTaperToDabRange(updateStart, updateCount, taperState);

        // Collect tiles covered by the rebuild dab range BEFORE the rebuild.
        // This gives us the precise set of tiles that will be cleared/rewritten,
        // independent of whether the GPU path clears dirty flags after rendering.
        if (outRebuiltTiles) {
            currentBrush->collectStrokeDabRangeCoveredTiles(
                updateStart, updateCount, *outRebuiltTiles, true);
        }

        if (m_useGPUBrush && executionBackend) {
            const bool hasCurrentContext
                = m_callbacks.hasCurrentGlContext ? m_callbacks.hasCurrentGlContext() : false;
            if (!hasCurrentContext && m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            executionBackend->rebuildStrokeRangeFromDabs(
                *currentBrush, updateStart, updateCount, selectionMask, true);
            if (!hasCurrentContext && m_callbacks.doneCurrent) {
                m_callbacks.doneCurrent();
            }
        } else if (executionBackend) {
            executionBackend->rebuildStrokeRangeFromDabs(
                *currentBrush, updateStart, updateCount, selectionMask, false);
        } else {
            currentBrush->rebuildStrokeBufferRangeFromDabs(updateStart, updateCount, selectionMask);
        }
        return true;
    }
    return rebuildFullPreview(0);
}

void BrushStrokeHost::collectStrokeChangedKeys(
    std::unordered_set<TileKey, TileKeyHash>& changedKeys) const
{
    TileBrush* currentBrush = brush();
    if (!currentBrush) {
        return;
    }
    changedKeys.reserve(currentBrush->strokeBuffer().tileCount());
    for (const auto& entry : currentBrush->strokeBuffer().tiles()) {
        changedKeys.insert(entry.first);
    }
}

void BrushStrokeHost::markStrokeBufferDirtyDelta(
    const std::unordered_set<TileKey, TileKeyHash>& changedKeys, StrokeTileSetChange tileSetChange)
{
    if (!m_callbacks.markCompositionTilesDirty) {
        return;
    }

    TileBrush* currentBrush = brush();
    if (!currentBrush) {
        return;
    }

    // Fast path for plain dab append. The caller guarantees no tile LEFT the
    // stroke buffer, so an unchanged tile count proves the key set is exactly
    // the one recorded last time — nothing was added either. Both the
    // "appeared" and "disappeared" halves of the delta are then empty, and the
    // only dirty tiles are the changed keys that live in the buffer, which
    // m_prevStrokePreviewKeys can answer without materializing a fresh set.
    //
    // That is the whole point: the rescan below builds two hash sets spanning
    // the entire stroke footprint, on every input event, so its cost grew with
    // the stroke — a long sweep across a large canvas ended up paying it 3-10
    // times per frame over hundreds of tiles. Tiles are only created every
    // ~TILE_SIZE px of travel, so the rescan still runs, just on the handful of
    // events that actually grow the buffer instead of all of them.
    if (tileSetChange == StrokeTileSetChange::GrowOnly
        && currentBrush->strokeBuffer().tileCount() == m_prevStrokePreviewKeys.size()) {
        std::vector<TileKey> dirtyVec;
        dirtyVec.reserve(changedKeys.size());
        for (const auto& key : changedKeys) {
            if (m_prevStrokePreviewKeys.find(key) != m_prevStrokePreviewKeys.end()) {
                dirtyVec.push_back(key);
            }
        }
        if (!dirtyVec.empty()) {
            m_callbacks.markCompositionTilesDirty(dirtyVec);
        }
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> currentPreviewKeys;
    currentPreviewKeys.reserve(currentBrush->strokeBuffer().tileCount());
    for (const auto& [key, tile] : currentBrush->strokeBuffer().tiles()) {
        currentPreviewKeys.insert(key);
    }

    std::unordered_set<TileKey, TileKeyHash> dirtyKeys;
    for (const auto& key : changedKeys) {
        if (currentPreviewKeys.find(key) != currentPreviewKeys.end()) {
            dirtyKeys.insert(key);
        }
    }
    for (const auto& key : currentPreviewKeys) {
        if (m_prevStrokePreviewKeys.find(key) == m_prevStrokePreviewKeys.end()) {
            dirtyKeys.insert(key);
        }
    }
    for (const auto& key : m_prevStrokePreviewKeys) {
        if (currentPreviewKeys.find(key) == currentPreviewKeys.end()) {
            dirtyKeys.insert(key);
        }
    }

    if (!dirtyKeys.empty()) {
        std::vector<TileKey> dirtyVec(dirtyKeys.begin(), dirtyKeys.end());
        m_callbacks.markCompositionTilesDirty(dirtyVec);
    }

    m_prevStrokePreviewKeys = std::move(currentPreviewKeys);
}

void BrushStrokeHost::snapshotNewTiles(const TileGrid& strokeBuffer, TileGrid* layerGrid)
{
    if (!layerGrid) {
        return;
    }
    // Format-sized opaque transport: RGBA8 == TILE_BYTE_SIZE; wider formats copy
    // the full per-pixel payload. All tiles in a grid share its format.
    const size_t bytesPerTile = tileByteSize(layerGrid->format());
    struct SnapshotJob {
        const uint8_t* src;
        uint8_t* dst;
    };
    std::vector<SnapshotJob> jobs;
    jobs.reserve(strokeBuffer.tileCount());
    for (const auto& [key, tile] : strokeBuffer.tiles()) {
        if (m_strokeSnapshotted.count(key)) {
            continue;
        }
        m_strokeSnapshotted.insert(key);

        const TileData* existing = layerGrid->getTile(key);
        if (existing) {
            auto& buf = m_strokeBeforeSnapshots[key];
            buf.resize(bytesPerTile);
            jobs.push_back({ existing->pixels(), buf.data() });
        } else {
            m_strokeCreatedTiles.insert(key);
        }
    }
    if (jobs.empty()) {
        return;
    }
    // Heavy per-tile memcpy (TILE_BYTE_SIZE = 256 KB each) — punt to a worker so
    // a huge brush doesn't stall the UI at stroke begin/end. The layer grid is
    // not mutated during the stroke (flattenStroke runs only in endStroke), so
    // reading from existing->pixels() is safe. We wait on m_snapshotSync before
    // moving m_strokeBeforeSnapshots into pending finalization or clearing it.
    m_snapshotSync.addFuture(QtConcurrent::run([jobs = std::move(jobs), bytesPerTile]() {
        for (const auto& job : jobs) {
            std::memcpy(job.dst, job.src, bytesPerTile);
        }
    }));
}

bool BrushStrokeHost::strokeNeedsRealtimeRebuild() const
{
    TileBrush* currentBrush = brush();
    if (!currentBrush || currentBrush->isBlurMode() || currentBrush->isSmudgeMode()
        || currentBrush->isWetMode() || currentBrush->isLiquifyMode()) {
        return false;
    }
    return currentBrush->requiresRealtimeTaperReplay();
}

bool BrushStrokeHost::hasPendingStabilizerCatchup() const
{
    TileBrush* currentBrush = brush();
    if (!m_isDrawing || !currentBrush) {
        return false;
    }

    // Do not gate pending geometry on the brush's currently evaluated slider.
    // In particular Stroke Speed can drive Stabilization to zero while an older
    // non-zero-lag step still has a tail. The timer must get one more chance to
    // process that tail; sampleStrokeStabilizer will then either keep converging
    // or deliberately snap it through the zero-lag path.
    return ruwa::core::brushes::hasPendingStrokeStabilizer(
        m_stabilizationState, m_lastStrokeTargetX, m_lastStrokeTargetY);
}

Vector2 BrushStrokeHost::smoothInputTargetForViewport(float worldX, float worldY, bool enabled)
{
    const Vector2 raw { worldX, worldY };
    if (m_strokeInputDevice != StrokeInputDevice::Stylus || !enabled) {
        // Do not apply the independent zoom-out follower when stabilization is
        // explicitly disabled. Otherwise the later zero-latency raster path
        // would still receive an already-lagged target.
        m_autoInputSmoothingValid = false;
        return raw;
    }

    const float zoom = viewportZoom();
    if (zoom >= 1.0f) {
        m_autoInputSmoothingValid = true;
        m_autoInputSmoothingPoint = raw;
        return raw;
    }

    const float screenRadiusPx = std::clamp(
        (1.0f - zoom) * kAutoInputSmoothingMaxScreenPx, 0.0f, kAutoInputSmoothingMaxScreenPx);
    const float worldRadius
        = std::min(screenRadiusPx / std::max(zoom, 0.001f), kAutoInputSmoothingMaxWorldRadius);
    if (worldRadius <= 0.001f) {
        m_autoInputSmoothingValid = true;
        m_autoInputSmoothingPoint = raw;
        return raw;
    }

    if (!m_autoInputSmoothingValid) {
        m_autoInputSmoothingValid = true;
        m_autoInputSmoothingPoint = raw;
        return raw;
    }

    const Vector2 delta { raw.x - m_autoInputSmoothingPoint.x,
        raw.y - m_autoInputSmoothingPoint.y };
    const float dist = vectorLength(delta);
    if (dist <= 0.0001f) {
        return m_autoInputSmoothingPoint;
    }

    const float alpha
        = std::clamp((dist - worldRadius * 0.05f) / (worldRadius * 4.00f), 0.08f, 0.65f);
    m_autoInputSmoothingPoint.x += delta.x * alpha;
    m_autoInputSmoothingPoint.y += delta.y * alpha;
    return m_autoInputSmoothingPoint;
}

void BrushStrokeHost::updateStabilizerCatchupTimer()
{
    const bool catchupActive
        = hasPendingStabilizerCatchup() && !(quickShapeMorph() && quickShapeMorph()->isActive());
    if (catchupActive) {
        if (!m_stabilizerCatchupTimer.isActive()) {
            m_stabilizerCatchupTimer.start();
        }
        return;
    }
    m_stabilizerCatchupTimer.stop();
}

double BrushStrokeHost::stabilizerCatchupIdleThresholdMs() const
{
    constexpr double kMaximumIdleThresholdMs = 24.0;
    constexpr double kArrivalSafetyFactor = 1.35;
    constexpr double kSchedulingMarginMs = 1.0;

    // Until a cadence is observable, retain the conservative historical gate.
    // Once enough distinct arrivals exist, use their upper quartile: this reacts
    // to the actual device/event-loop rate without treating the zero-time packet
    // spacing inside a recovered burst as a 1000+ Hz device.
    if (m_realInputWallIntervalCount < 3) {
        return kMaximumIdleThresholdMs;
    }

    std::array<double, kRealInputWallIntervalWindow> sortedIntervals
        = m_realInputWallIntervals;
    std::sort(sortedIntervals.begin(),
        sortedIntervals.begin() + m_realInputWallIntervalCount);
    const int upperQuartileIndex = (m_realInputWallIntervalCount - 1) * 3 / 4;
    const double expectedArrivalMs = sortedIntervals[upperQuartileIndex];
    const double minimumIdleThresholdMs
        = std::max(1.0, static_cast<double>(m_stabilizerCatchupTimer.interval()));
    return std::clamp(expectedArrivalMs * kArrivalSafetyFactor + kSchedulingMarginMs,
        minimumIdleThresholdMs, kMaximumIdleThresholdMs);
}

void BrushStrokeHost::processStabilizerCatchup()
{
    if (!m_queuedStrokeSamples.empty()) {
        // Real input is still pending. It drives the geometry on its own tick,
        // and this timer must not double as a queue-service policy: while it did,
        // input scheduling silently depended on the stabilization slider — at
        // exactly 0% no catch-up timer exists, so the queue lost its only
        // aggressive drain and a fast device accumulated seconds of lag, while at
        // 1% the same queue was flushed synchronously 125 times a second.
        return;
    }
    if (!hasPendingStabilizerCatchup()) {
        m_stabilizerCatchupTimer.stop();
        return;
    }
    // Only advance the lag toward the pen once real input has gone IDLE. While
    // the pen is actively feeding events (normal ~120-200 Hz), those events
    // already drive the geometry; a catch-up tick in between would inject a
    // stabilized point on the straight inter-event approach — collinear samples
    // that Catmull-Rom renders as a faceted rounding. The timer keeps running
    // and re-checks; it fires for real only after the next expected input batch
    // has actually been missed. A fixed 24 ms gate was safe but created the
    // visible stop before catch-up on fast devices.
    const double catchupIdleMs = stabilizerCatchupIdleThresholdMs();
    const double nowMs = static_cast<double>(elapsedSeconds(m_strokeElapsedTimer)) * 1000.0;
    if (nowMs - m_lastRealInputWallMs < catchupIdleMs) {
        return;
    }
    continueStrokeImmediate(m_lastStrokeTargetX, m_lastStrokeTargetY, m_lastStrokeTargetPressure,
        elapsedSeconds(m_strokeElapsedTimer), m_lastStrokeTargetInputDynamics, true,
        /*isRealPenSample=*/false);
}

void BrushStrokeHost::emitLiquifyDwell()
{
    if (!m_isDrawing || m_endStrokeRequested) {
        m_liquifyDwellTimer.stop();
        return;
    }
    TileBrush* currentBrush = brush();
    if (!currentBrush || !currentBrush->isLiquifyMode() || currentBrush->liquifyToolMode() == 0) {
        m_liquifyDwellTimer.stop();
        return;
    }
    // Don't race the input queue / an in-progress flush.
    if (!m_queuedStrokeSamples.empty() || m_processingQueuedStrokeInput) {
        return;
    }

    // Only dwell once the pen has stopped MOVING. While the cursor moves the
    // movement-driven dabs already apply the warp; a dwell tick on top would
    // double-apply. Gate on m_lastLiquifyMoveWallMs (advanced only on real
    // displacement) rather than m_lastRealInputWallMs (every event) — a stylus held
    // still keeps streaming packets, so the latter would never let this fire.
    // Both this nowMs and m_lastLiquifyMoveWallMs come from m_strokeElapsedTimer,
    // so the comparison stays inside one wall-clock frame.
    constexpr double kLiquifyDwellIdleMs = 24.0;
    const double nowMs = static_cast<double>(elapsedSeconds(m_strokeElapsedTimer)) * 1000.0;
    if (m_lastLiquifyMoveValid && nowMs - m_lastLiquifyMoveWallMs < kLiquifyDwellIdleMs) {
        return;
    }

    TileGrid* grid = activeLayerTileGrid();
    auto* layer = activeLayer();
    auto* executionBackend = brushExecutionBackend();
    if (!grid || !layer || !executionBackend) {
        return;
    }

    TileGrid* paintMask = effectivePaintMask(layer, grid);
    const size_t changedDabStart = currentBrush->strokeDabs().size();

    // One zero-movement segment at the current position → exactly one dwell dab
    // (see TileBrush::appendInterpolatedStrokeDabs). rasterizeStrokeSegment makes
    // the GL context current itself.
    //
    // The dwell dab reads the same DAB clock as every other dab instead of the
    // wall clock, so a time-bound dynamic doesn't jump when the pen stops and
    // the dwell takes over. No pen sample arrived, so this is the idle entry
    // point: it advances in real time and leaves the stabilizer's period
    // estimate alone.
    const float elapsed
        = advanceDabDynamicsClockIdle(static_cast<double>(strokeElapsedSecondsNow()) * 1000.0);
    BrushInputDynamics dwellInputDynamics = m_lastRawStrokeInputDynamics;
    dwellInputDynamics.strokeSpeed = 0.0f;
    dwellInputDynamics.strokeSpeedAvailable = true;
    rasterizeStrokeSegment(grid, paintMask, executionBackend, m_lastStrokeInputX,
        m_lastStrokeInputY, m_lastStrokeInputX, m_lastStrokeInputY, m_lastStrokeInputPressure,
        m_lastStrokeInputPressure, elapsed, elapsed, dwellInputDynamics, dwellInputDynamics);

    snapshotNewTiles(currentBrush->strokeBuffer(), grid);

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
    const size_t total = currentBrush->strokeDabs().size();
    if (total > changedDabStart) {
        currentBrush->collectStrokeDabRangeCoveredTiles(
            changedDabStart, total - changedDabStart, changedKeys);
    }
    if (!changedKeys.empty()) {
        // Dwell only appends dabs, never rebuilds.
        markStrokeBufferDirtyDelta(changedKeys, StrokeTileSetChange::GrowOnly);
    }
    if (m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::finalizeStroke()
{
    if (!tryFinalizeStroke(false)) {
        m_finalizeTimer.start(1);
    }
}

bool BrushStrokeHost::tryFinalizeStroke(bool forceWait)
{
    if (m_pending.active && m_pending.fence && !forceWait) {
        auto* executionBackend = brushExecutionBackend();
        bool readbackComplete = true;
        if (executionBackend) {
            if (m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            readbackComplete = executionBackend->isReadbackComplete(m_pending.fence);
            if (m_callbacks.doneCurrent) {
                m_callbacks.doneCurrent();
            }
        }
        if (!readbackComplete) {
            return false;
        }
    }

    if (m_callbacks.finalizePendingStroke) {
        const bool emitStrokePainted = !m_pending.strokePaintedEmitted && !m_pending.eraseMode
            && !m_pending.flattenedKeys.empty();
        if (emitStrokePainted) {
            m_pending.strokePaintedEmitted = true;
        }
        m_callbacks.finalizePendingStroke(m_pending, m_selectionAtStrokeBegin, emitStrokePainted);
    }
    return !m_pending.active;
}

void BrushStrokeHost::flushPendingFinalization()
{
    m_finalizeTimer.stop();
    // If an async end-of-stroke drain is in flight (queue not yet empty),
    // force-finish it synchronously here so a freshly-starting stroke sees
    // a consistent committed state.
    if (m_endStrokeRequested && m_isDrawing) {
        if (!m_queuedStrokeSamples.empty() && !m_processingQueuedStrokeInput) {
            flushQueuedStrokeInput();
        }
        if (m_endStrokeRequested && m_isDrawing) {
            // Completion clears whatever is still queued. That is deliberate and
            // only reachable if the flush above was skipped because a drain is
            // already on the stack: the previous stroke has to be committed
            // before the new one starts, so a truncated tail beats an
            // inconsistent document. The guard is what keeps that from turning
            // into a re-entrant drain instead.
            completeEndStrokeAfterQueueDrain();
        }
    }
    if (m_pending.active) {
        while (m_pending.active) {
            if (!tryFinalizeStroke(true) && !m_callbacks.finalizePendingStroke) {
                break;
            }
        }
    }
}

} // namespace aether
