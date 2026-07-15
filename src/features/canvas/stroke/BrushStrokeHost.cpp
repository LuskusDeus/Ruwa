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
constexpr size_t kStrokeInputBatchMaxSamples = 24;
constexpr qint64 kStrokeInputBatchBudgetMs = 4;
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

} // namespace

namespace aether {

BrushStrokeHost::BrushStrokeHost(QObject* parent, Callbacks callbacks)
    : QObject(parent)
    , m_callbacks(std::move(callbacks))
{
    m_finalizeTimer.setSingleShot(true);
    m_finalizeTimer.setInterval(0);
    connect(&m_finalizeTimer, &QTimer::timeout, this, &BrushStrokeHost::finalizeStroke);

    m_strokeInputTimer.setSingleShot(true);
    m_strokeInputTimer.setInterval(0);
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

float BrushStrokeHost::pressureSmoothingWindowWorldPx() const
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
    float worldX, float worldY, float pressure, StrokeInputDevice inputDevice)
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
    m_strokeInputTimer.stop();
    m_processingQueuedStrokeInput = false;
    m_queuedStrokeSamples.clear();
    clearStrokeRuntimeState();

    m_selectionAtStrokeBegin = m_callbacks.captureSelectionState
        ? m_callbacks.captureSelectionState()
        : SelectionState {};
    currentBrush->setPressure(pressure);
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
    m_stabRealWinCount = 0;
    m_lastRealInputMs = 0.0;
    m_lastLiquifyMoveWallMs = 0.0;
    m_lastLiquifyMoveX = worldX;
    m_lastLiquifyMoveY = worldY;
    m_lastLiquifyMoveValid = true;
    m_lastStrokeX = stabilizedStart.x;
    m_lastStrokeY = stabilizedStart.y;
    m_prevEmittedPoint = stabilizedStart;
    m_lastStrokePressure = pressure;
    m_lastStrokeElapsedSeconds = 0.0f;
    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    m_lastStrokeTargetPressure = pressure;
    m_lastStrokeTargetElapsedSeconds = 0.0f;
    m_lastStrokeInputX = stabilizedStart.x;
    m_lastStrokeInputY = stabilizedStart.y;
    m_lastStrokeInputPressure = pressure;
    m_lastStrokeInputElapsedSeconds = 0.0f;
    m_liveStrokePoints.clear();
    m_liveStrokePoints.push_back({ stabilizedStart, pressure, 0.0f });

    currentBrush->beginStroke();
    currentBrush->setPressure(pressure);
    currentBrush->setStrokeElapsedSeconds(0.0f, true);
    m_strokeElapsedTimer.start();
    m_realtimePreviewEventCount = 0;
    m_lastRealtimeTaperTailStart = std::numeric_limits<size_t>::max();
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

    TileGrid* paintMask = effectivePaintMask(layer, grid);
    configureBrushSelectionMaskAlpha(*currentBrush, layer, paintMask);
    const bool realtimeRebuild = strokeNeedsRealtimeRebuild();
    const bool deferInitialDabForDirection
        = currentBrush->hasActiveStrokeDirectionDynamicsBinding();
    std::unordered_set<TileKey, TileKeyHash> rebuiltTiles;
    bool previewUpdated = false;
    const size_t changedDabStart = currentBrush->strokeDabs().size();
    if (deferInitialDabForDirection) {
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

    if ((!realtimeRebuild && !deferInitialDabForDirection) || previewUpdated) {
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
    }

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
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
    } else if (deferInitialDabForDirection) {
        // The first dab will be generated from the first actual segment, once direction is known.
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
    }
    if (!changedKeys.empty()) {
        markStrokeBufferDirtyDelta(changedKeys);
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
    float worldX, float worldY, float pressure, StrokeInputDevice inputDevice)
{
    continueStrokeAtElapsed(
        worldX, worldY, pressure, elapsedSeconds(m_strokeElapsedTimer), inputDevice);
}

void BrushStrokeHost::continueStrokeAtElapsed(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, StrokeInputDevice inputDevice)
{
    if (!m_isDrawing) {
        return;
    }

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
    }
    // Timestamp of the latest REAL pen input (not catch-up). The stabilizer
    // catch-up only injects geometry once input has gone idle past this — during
    // continuous drawing the real events drive the curve and injecting catch-up
    // points would lay collinear samples along the inter-event approach, which
    // Catmull-Rom can't re-curve (faceted roundings).
    m_lastRealInputMs = static_cast<double>(strokeElapsedSeconds) * 1000.0;

    // Wall-clock time of the last meaningful pen movement, for the liquify dwell
    // gate. Unlike m_lastRealInputMs (bumped on every event), this only advances
    // when the raw input travels past a small screen-space threshold, so a
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

        const float smoothTime = std::max(1.0f, pressureSmoothingWindowWorldPx());
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

        const float omega = 2.0f / smoothTime;
        const float x = omega * filterStep;
        // Padé approximation of exp(-x): cheap and unconditionally stable for
        // any step (semi-implicit critically-damped integration).
        const float expTerm = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        const float change = m_inputPressureSmoothed - pressure;
        const float temp = (m_inputPressureVel + omega * change) * filterStep;
        m_inputPressureVel = (m_inputPressureVel - omega * temp) * expTerm;
        m_inputPressureSmoothed = pressure + (change + temp) * expTerm;
        m_inputPressureSmoothX = worldX;
        m_inputPressureSmoothY = worldY;
        m_inputPressureSmoothElapsedSeconds = strokeElapsedSeconds;
    }
    pressure = std::clamp(m_inputPressureSmoothed, 0.0f, 1.0f);

    m_queuedStrokeSamples.push_back(
        { worldX, worldY, pressure, strokeElapsedSeconds, inputDevice });
    processQueuedStrokeInput();
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
    processQueuedStrokeInputImpl(false);
}

void BrushStrokeHost::processQueuedStrokeInputImpl(bool drainAll)
{
    if (!m_isDrawing || m_processingQueuedStrokeInput || m_queuedStrokeSamples.empty()) {
        return;
    }

    m_processingQueuedStrokeInput = true;
    size_t processedSamples = 0;
    QElapsedTimer budgetTimer;
    if (!drainAll) {
        budgetTimer.start();
    }

    while (m_isDrawing && !m_queuedStrokeSamples.empty()) {
        const QueuedStrokeSample sample = m_queuedStrokeSamples.front();
        m_queuedStrokeSamples.pop_front();
        m_strokeInputDevice = sample.inputDevice;
        continueStrokeImmediate(
            sample.worldX, sample.worldY, sample.pressure, sample.strokeElapsedSeconds, false);
        ++processedSamples;

        if (!drainAll
            && (processedSamples >= kStrokeInputBatchMaxSamples
                || budgetTimer.elapsed() >= kStrokeInputBatchBudgetMs)) {
            break;
        }
    }

    m_processingQueuedStrokeInput = false;

    if (m_isDrawing && processedSamples > 0 && m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
    if (!drainAll && m_isDrawing && !m_queuedStrokeSamples.empty()) {
        scheduleQueuedStrokeInput();
    }
    // If endStroke was requested while a backlog existed, run the deferred
    // completion once the queue has drained. This is the async tail of the
    // "fast huge brush" flow — UI stays responsive while the backlog catches up.
    if (m_endStrokeRequested && m_isDrawing && m_queuedStrokeSamples.empty()) {
        completeEndStrokeAfterQueueDrain();
    }
}

void BrushStrokeHost::flushQueuedStrokeInput()
{
    if (m_strokeInputTimer.isActive()) {
        m_strokeInputTimer.stop();
    }
    while (m_isDrawing && !m_queuedStrokeSamples.empty()) {
        processQueuedStrokeInputImpl(true);
    }
}

double BrushStrokeHost::stepStabilizerClock(double realMs, bool isRealPenSample)
{
    constexpr double kPauseGapMs = 40.0; // gap that counts as a stop → resync
    constexpr double kMinPeriodMs = 0.2;
    constexpr double kMaxPeriodMs = 40.0;
    constexpr double kMaxDriftMs = 50.0;

    if (!m_stabClockValid) {
        m_stabClockValid = true;
        m_stabSynthMs = realMs;
        m_stabLastRealPenMs = realMs;
        m_stabRealWin[0] = realMs;
        m_stabRealWinCount = 1;
        return m_stabSynthMs;
    }

    // Catch-up / idle tick: converge in REAL time and do NOT feed the rate
    // estimate. Leaving m_stabLastRealPenMs untouched means the next real sample
    // sees the whole pause as a gap → reset below → clean resume.
    if (!isRealPenSample) {
        m_stabSynthMs = std::max(m_stabSynthMs, realMs);
        return m_stabSynthMs;
    }

    const double gap = realMs - m_stabLastRealPenMs;
    m_stabLastRealPenMs = realMs;

    if (!(gap >= 0.0) || gap > kPauseGapMs) {
        // Pause/resume or non-monotonic input: snap and start a fresh window.
        m_stabSynthMs = realMs;
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

    m_stabSynthMs += periodMs;
    // The synthetic clock may lead/lag within a burst but must not drift away
    // from real time over the long run, or the lag would stop being τ.
    if (std::abs(m_stabSynthMs - realMs) > kMaxDriftMs) {
        m_stabSynthMs = realMs;
    }
    return m_stabSynthMs;
}

void BrushStrokeHost::continueStrokeImmediate(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, bool requestRenderAfterStep, bool isRealPenSample)
{
    if (!m_isDrawing) {
        return;
    }

    if (!std::isfinite(strokeElapsedSeconds)) {
        strokeElapsedSeconds = elapsedSeconds(m_strokeElapsedTimer);
    }
    if (strokeElapsedSeconds <= m_lastStrokeTargetElapsedSeconds) {
        strokeElapsedSeconds = m_lastStrokeTargetElapsedSeconds + kStrokeInputMonotonicNudgeSec;
    }

    const Vector2 smoothedTarget = smoothInputTargetForViewport(worldX, worldY);
    worldX = smoothedTarget.x;
    worldY = smoothedTarget.y;

    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    if (!currentBrush || !grid) {
        return;
    }

    if (m_callbacks.notifyCanvasInteraction) {
        m_callbacks.notifyCanvasInteraction(true);
    }

    currentBrush->setPressure(pressure);
    currentBrush->setStrokeElapsedSeconds(strokeElapsedSeconds, true);

    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    m_lastStrokeTargetPressure = pressure;
    m_lastStrokeTargetElapsedSeconds = strokeElapsedSeconds;

    if (auto* quickShape = quickShapeMorph(); quickShape && quickShape->isActive()) {
        m_lastStrokeX = worldX;
        m_lastStrokeY = worldY;
        quickShape->updateCursorTarget(worldX, worldY);
        updateStabilizerCatchupTimer();
        return;
    }

    const float stabSlider = currentBrush->stabilization();
    const float stabLagMs = ruwa::core::brushes::stabilizationTauMs(stabSlider);

    // De-jittered sample clock (see m_stabClock*/stepStabilizerClock). The OS
    // delivers bursts stamped at coarse (~15.6 ms) resolution, so the raw
    // per-sample dt is bimodal and the time-domain EWMA turns it into the facet
    // sawtooth. We feed the
    // EWMA an even time base estimated from the real pen rate. The EWMA and τ
    // are untouched (same visual logic); lag stays τ in real time.
    const double realMs = static_cast<double>(strokeElapsedSeconds) * 1000.0;
    const double nowMs = stepStabilizerClock(realMs, isRealPenSample);

    // Take ONE stabilized point per call and let the downstream Catmull-Rom
    // interpolate the curve between such points. Feeding the stabilizer's dense
    // 1 ms sub-point stream as geometry instead made smooth arcs FACETED:
    // between two pen events the 2-stage EWMA chases a straight target ramp, so
    // its output is nearly straight there with the curvature bunched at the
    // events; sampling that densely faithfully reproduced those straight
    // inter-event facets, and Catmull-Rom can't re-curve points that genuinely
    // lie on a line. Keeping only the per-event stabilized point (still
    // jitter-filtered by the EWMA) and curving BETWEEN them restores smooth
    // roundings — exactly how the no-stabilizer path already works.
    //
    // Pressure is delayed in lockstep with position (2-stage EWMA, same alpha)
    // so each dab carries the pressure the pen had at that lagged point — see the
    // size-staircase fix. stabLagMs == 0 passes raw position + pressure through.
    Vector2 resolved { worldX, worldY };
    float emitPressure = pressure;
    if (stabLagMs > 0.0f) {
        const auto sp = ruwa::core::brushes::sampleStrokeStabilizer(
            m_stabilizationState, worldX, worldY, stabLagMs, nowMs, false);
        resolved = { sp.x, sp.y };
        if (!m_stabPressureValid) {
            m_stabPressure1 = pressure;
            m_stabPressure2 = pressure;
            m_stabPressureLastMs = nowMs;
            m_stabPressureValid = true;
        } else {
            const double dtMs = nowMs - m_stabPressureLastMs;
            if (dtMs > 0.0) {
                const float a = ruwa::core::brushes::detail::stabilizerAlpha(stabLagMs, dtMs);
                m_stabPressure1 += a * (pressure - m_stabPressure1);
                m_stabPressure2 += a * (m_stabPressure1 - m_stabPressure2);
                m_stabPressureLastMs = nowMs;
            }
        }
        emitPressure = m_stabPressure2;
    }
    continueStrokeWithResolvedPoint(
        worldX, worldY, emitPressure, strokeElapsedSeconds, resolved, false, false);
    updateStabilizerCatchupTimer();
    if (requestRenderAfterStep && m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

void BrushStrokeHost::continueStrokeWithResolvedPoint(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, const Vector2& resolvedPoint, bool requestRenderAfterStep,
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
    currentBrush->setStrokeElapsedSeconds(strokeElapsedSeconds, true);

    const Vector2 stabilizedPoint = resolvedPoint;

    float moveDx = stabilizedPoint.x - m_lastStrokeInputX;
    float moveDy = stabilizedPoint.y - m_lastStrokeInputY;
    const bool stabilizerCatchupPending = (currentBrush->stabilization() > 0.0001f)
        && ruwa::core::brushes::hasPendingStrokeStabilizer(m_stabilizationState, worldX, worldY);
    const float pressureDelta = std::abs(pressure - m_lastStrokePressure);
    const bool pressureChanged = pressureDelta > 0.001f;
    const bool movementBelowThreshold = (moveDx * moveDx + moveDy * moveDy)
        < (kQuickLineMovementEpsilon * kQuickLineMovementEpsilon);
    const bool hasMeaningfulMovement = !movementBelowThreshold;
    if (!hasMeaningfulMovement && !pressureChanged) {
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
    const bool deferInitialDabForDirection = currentBrush->hasActiveStrokeDirectionDynamicsBinding()
        && currentBrush->strokeDabs().empty();
    std::unordered_set<TileKey, TileKeyHash> rebuiltTiles;
    bool previewUpdated = false;
    bool skippedDabForDirection = false;
    const size_t changedDabStart = currentBrush->strokeDabs().size();

    if (!hasMeaningfulMovement && pressureChanged) {
        if (deferInitialDabForDirection) {
            skippedDabForDirection = true;
            previewUpdated = false;
        } else if (realtimeRebuild) {
            ++m_realtimePreviewEventCount;
            currentBrush->setStrokeElapsedSeconds(strokeElapsedSeconds, true);
            currentBrush->recordDabPoint(stabilizedPoint.x, stabilizedPoint.y);
            previewUpdated = rebuildStrokePreviewFromDabs(
                grid, paintMask, executionBackend, true, &rebuiltTiles);
        } else if (m_useGPUBrush && executionBackend) {
            if (m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, stabilizedPoint.x,
                stabilizedPoint.y, paintMask, true, strokeElapsedSeconds, true);
            if (m_callbacks.doneCurrent) {
                m_callbacks.doneCurrent();
            }
        } else if (executionBackend) {
            m_useGPUBrush = executionBackend->stamp(*currentBrush, *grid, stabilizedPoint.x,
                stabilizedPoint.y, paintMask, false, strokeElapsedSeconds, true);
        } else {
            if (brushRequiresGpuEffect(currentBrush)) {
                m_useGPUBrush = false;
                return;
            }
            currentBrush->setStrokeElapsedSeconds(strokeElapsedSeconds, true);
            currentBrush->stamp(*grid, stabilizedPoint.x, stabilizedPoint.y, paintMask);
            m_useGPUBrush = false;
        }

        if (!m_liveStrokePoints.empty()) {
            m_liveStrokePoints.back().pressure = pressure;
            m_liveStrokePoints.back().strokeElapsedSeconds = strokeElapsedSeconds;
        }
        if (realtimeRebuild || m_liveStrokePoints.size() <= 1) {
            m_lastStrokeX = stabilizedPoint.x;
            m_lastStrokeY = stabilizedPoint.y;
            m_lastStrokePressure = pressure;
            m_lastStrokeElapsedSeconds = strokeElapsedSeconds;
        }
    } else if (realtimeRebuild) {
        ++m_realtimePreviewEventCount;
        currentBrush->setStrokeElapsedSeconds(strokeElapsedSeconds, true);
        std::vector<TileBrush::DabPoint> segmentDabs;
        currentBrush->appendInterpolatedStrokeDabs(prevX, prevY, stabilizedPoint.x,
            stabilizedPoint.y, prevPressure, pressure, segmentDabs, m_lastStrokeElapsedSeconds,
            strokeElapsedSeconds, true);
        previewUpdated
            = rebuildStrokePreviewFromDabs(grid, paintMask, executionBackend, true, &rebuiltTiles);
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
        m_liveStrokePoints.push_back({ stabilizedPoint, pressure, strokeElapsedSeconds });

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

        while (m_liveStrokePoints.size() > static_cast<size_t>(lagSamples) + 1) {
            const LiveStrokePoint head = m_liveStrokePoints[1];
            // Emit a Catmull-Rom curve through the de-jittered vertices instead
            // of a straight chord, so the silhouette is curved rather than a
            // smoothed polygon. p1=anchor, p2=head; p0/p3 are the neighbouring
            // vertices that set the tangents (duplicated at the ends).
            const Vector2 p1 { m_lastStrokeX, m_lastStrokeY };
            const Vector2 p2 = head.point;
            const Vector2 p3 = (m_liveStrokePoints.size() > 2) ? m_liveStrokePoints[2].point : p2;
            rasterizeCatmullRomStroke(grid, paintMask, executionBackend, m_prevEmittedPoint, p1, p2,
                p3, m_lastStrokePressure, head.pressure, m_lastStrokeElapsedSeconds,
                head.strokeElapsedSeconds);
            m_prevEmittedPoint = p1;
            m_lastStrokeX = head.point.x;
            m_lastStrokeY = head.point.y;
            m_lastStrokePressure = head.pressure;
            m_lastStrokeElapsedSeconds = head.strokeElapsedSeconds;
            m_liveStrokePoints.erase(m_liveStrokePoints.begin());
            // After erase, what was index [1] is now index [0] — the
            // just-emitted point. It already carries the smoothed position
            // we just rasterized to, so it serves as the new anchor with
            // no further fixup.
        }
    }

    if (realtimeRebuild) {
        m_lastStrokeX = stabilizedPoint.x;
        m_lastStrokeY = stabilizedPoint.y;
        m_lastStrokePressure = pressure;
        m_lastStrokeElapsedSeconds = strokeElapsedSeconds;
    }
    m_lastStrokeInputX = stabilizedPoint.x;
    m_lastStrokeInputY = stabilizedPoint.y;
    m_lastStrokeInputPressure = pressure;
    m_lastStrokeInputElapsedSeconds = strokeElapsedSeconds;

    if ((!realtimeRebuild && !skippedDabForDirection) || previewUpdated) {
        snapshotNewTiles(currentBrush->strokeBuffer(), grid);
    }

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
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
    } else if (skippedDabForDirection) {
        // The first dab will be generated from the first actual segment, once direction is known.
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
    }
    if (!changedKeys.empty()) {
        markStrokeBufferDirtyDelta(changedKeys);
    }

    if (shouldResetQuickShapeHold) {
        if (auto* quickShape = quickShapeMorph(); quickShape && quickShapeHoldAllowed) {
            quickShape->restartHoldTimer();
        }
    }

    if (updateCatchupTimer) {
        updateStabilizerCatchupTimer();
    }
    if (requestRenderAfterStep && ((!realtimeRebuild && !skippedDabForDirection) || previewUpdated)
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

    TileBrush* currentBrush = brush();
    TileGrid* grid = activeLayerTileGrid();
    if (!currentBrush || !grid) {
        return;
    }

    if (auto* quickShape = quickShapeMorph(); quickShape && quickShape->isActive()) {
        quickShape->translate(dx, dy);
        m_lastStrokeX += dx;
        m_lastStrokeY += dy;
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
    m_lastStrokeTargetX += dx;
    m_lastStrokeTargetY += dy;
    m_lastStrokeInputX += dx;
    m_lastStrokeInputY += dy;
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

    // If samples are still queued (typical for huge brushes moved fast — input
    // arrives faster than the per-tick budget can rasterize), defer the heavy
    // catch-up work. Let m_strokeInputTimer continue draining in time-budgeted
    // chunks so the UI stays responsive. processQueuedStrokeInputImpl will
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
    m_stabilizerCatchupTimer.stop();
    m_isDrawing = false;
    ruwa::core::brushes::clearStrokeStabilizer(m_stabilizationState);
    m_lastRealtimeTaperTailStart = std::numeric_limits<size_t>::max();
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
    const bool forceDeferredDirectionStamp = !quickShapeWasActive
        && currentBrush->strokeDabs().empty()
        && currentBrush->hasActiveStrokeDirectionDynamicsBinding();
    if (!quickShapeWasActive && (!strokeNeedsRealtimeRebuild() || forceDeferredDirectionStamp)) {
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
            const LiveStrokePoint head = m_liveStrokePoints[1];
            // Same Catmull-Rom emission as the live loop, so the drained tail
            // keeps the curved silhouette instead of falling back to chords.
            const Vector2 p1 { m_lastStrokeX, m_lastStrokeY };
            const Vector2 p2 = head.point;
            const Vector2 p3 = (m_liveStrokePoints.size() > 2) ? m_liveStrokePoints[2].point : p2;
            rasterizeCatmullRomStroke(grid, paintMask, executionBackend, m_prevEmittedPoint, p1, p2,
                p3, m_lastStrokePressure, head.pressure, m_lastStrokeElapsedSeconds,
                head.strokeElapsedSeconds);
            m_prevEmittedPoint = p1;
            m_lastStrokeX = head.point.x;
            m_lastStrokeY = head.point.y;
            m_lastStrokePressure = head.pressure;
            m_lastStrokeElapsedSeconds = head.strokeElapsedSeconds;
            m_liveStrokePoints.erase(m_liveStrokePoints.begin());
        }
        auto replayData = activeStrokeReplayData();
        if (!replayData || replayData->empty()) {
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
                m_lastStrokeInputElapsedSeconds);
            m_lastStrokeX = m_lastStrokeInputX;
            m_lastStrokeY = m_lastStrokeInputY;
            m_lastStrokePressure = m_lastStrokeInputPressure;
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
        GLsync fence = executionBackend->startAsyncReadback(*grid, readbackKeys, true);
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
        m_pending.readbackKeysOrdered = std::move(readbackKeys);
        m_pending.beforeTiles = std::move(m_strokeBeforeSnapshots);
        m_pending.createdTiles = std::move(m_strokeCreatedTiles);
        m_pending.removedTiles.clear();
        m_pending.eraseMode = currentBrush->isEraseMode();
        m_pending.fence = fence;

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
        std::unordered_set<TileKey, TileKeyHash> removedTiles;
        if (currentBrush->isEraseMode() && !maskErase) {
            for (const auto& key : flattenedKeys) {
                TileData* tile = grid->getTile(key);
                if (tile && tile->isEmpty()) {
                    removedTiles.insert(key);
                }
            }
            grid->pruneEmpty();
        }

        // Wait for any in-flight async BEFORE-snapshot memcpys to settle
        // before we hand the map off to the pending finalization.
        m_snapshotSync.waitForFinished();
        m_pending.active = true;
        m_pending.layerId = layer->id;
        m_pending.maskTarget = layer->maskEditActive && layer->maskGrid != nullptr;
        m_pending.flattenedKeys = std::move(flattenedKeys);
        m_pending.readbackKeysOrdered.clear();
        m_pending.beforeTiles = std::move(m_strokeBeforeSnapshots);
        m_pending.createdTiles = std::move(m_strokeCreatedTiles);
        m_pending.removedTiles = std::move(removedTiles);
        m_pending.eraseMode = currentBrush->isEraseMode();
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
    m_strokeElapsedTimer.invalidate();
    m_stabilizerCatchupTimer.stop();
    m_liquifyDwellTimer.stop();
    m_autoInputSmoothingValid = false;
    m_autoInputSmoothingPoint = {};
}

void BrushStrokeHost::rasterizeStrokeSegment(TileGrid* grid, TileGrid* selectionMask,
    BrushExecutionBackend* executionBackend, float fromX, float fromY, float toX, float toY,
    float fromPressure, float toPressure, float fromStrokeElapsedSeconds,
    float toStrokeElapsedSeconds)
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
            toStrokeElapsedSeconds, true);
        if (!hasCurrentContext && m_callbacks.doneCurrent) {
            m_callbacks.doneCurrent();
        }
    } else if (executionBackend) {
        m_useGPUBrush = executionBackend->strokeTo(*currentBrush, *grid, fromX, fromY, toX, toY,
            fromPressure, toPressure, selectionMask, false, fromStrokeElapsedSeconds,
            toStrokeElapsedSeconds, true);
    } else {
        if (brushRequiresGpuEffect(currentBrush)) {
            m_useGPUBrush = false;
            return;
        }
        currentBrush->strokeToInterpolatedSize(*grid, fromX, fromY, toX, toY, fromPressure,
            toPressure, selectionMask, fromStrokeElapsedSeconds, toStrokeElapsedSeconds, true);
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
    if (useGpuContext && m_callbacks.makeCurrent) {
        m_callbacks.makeCurrent();
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

    if (useGpuContext && m_callbacks.doneCurrent) {
        m_callbacks.doneCurrent();
    }
}

void BrushStrokeHost::rasterizeCatmullRomStroke(TileGrid* grid, TileGrid* selectionMask,
    BrushExecutionBackend* executionBackend, const Vector2& p0, const Vector2& p1,
    const Vector2& p2, const Vector2& p3, float p1Pressure, float p2Pressure,
    float p1StrokeElapsedSeconds, float p2StrokeElapsedSeconds)
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
    if (useGpuContext && m_callbacks.makeCurrent) {
        m_callbacks.makeCurrent();
    }

    Vector2 prevPoint = b0;
    float prevPressure = p1Pressure;
    float prevElapsed = p1StrokeElapsedSeconds;
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const Vector2 nextPoint = cubicPoint(b0, b1, b2, b3, t);
        const float nextPressure = lerpScalar(p1Pressure, p2Pressure, t);
        const float nextElapsed = lerpScalar(p1StrokeElapsedSeconds, p2StrokeElapsedSeconds, t);
        rasterizeStrokeSegment(grid, selectionMask, executionBackend, prevPoint.x, prevPoint.y,
            nextPoint.x, nextPoint.y, prevPressure, nextPressure, prevElapsed, nextElapsed);
        prevPoint = nextPoint;
        prevPressure = nextPressure;
        prevElapsed = nextElapsed;
    }

    if (useGpuContext && m_callbacks.doneCurrent) {
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

    const bool hasTaper = currentBrush->hasTaperEffect();
    constexpr qint64 kTaperPreviewFrameIntervalNs = 1000000000ll / 60ll;

    const auto rebuildFullPreview = [&](size_t maxPreviewDabs) {
        if (m_useGPUBrush && executionBackend) {
            if (m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            executionBackend->rebuildStrokeFromDabs(
                *currentBrush, selectionMask, maxPreviewDabs, true);
            if (m_callbacks.doneCurrent) {
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

    if (hasTaper) {
        const auto replayData = activeStrokeReplayData();
        const size_t strokeDabCount = replayData ? replayData->size() : 0;
        if (strokeDabCount == 0) {
            return false;
        }

        const size_t startCount = currentBrush->startTaperDabCount();
        const size_t endCount = currentBrush->endTaperDabCount();
        const bool startTouchesWholeStroke = startCount >= strokeDabCount;
        const bool endTouchesWholeStroke = endCount >= strokeDabCount;
        const bool taperRangesOverlap
            = (startCount > 0 && endCount > 0 && (startCount + endCount) >= strokeDabCount);
        if (startTouchesWholeStroke || endTouchesWholeStroke || taperRangesOverlap
            || !allowPreviewSampling) {
            m_lastRealtimeTaperTailStart = std::numeric_limits<size_t>::max();
            m_lastRealtimeTaperPreviewNs = std::numeric_limits<qint64>::min();
            currentBrush->applyStrokeTaperToDabs();
            return rebuildFullPreview(0);
        }

        if (m_realtimePreviewTimer.isValid()) {
            const qint64 previewNowNs = m_realtimePreviewTimer.nsecsElapsed();
            if (m_lastRealtimeTaperPreviewNs != std::numeric_limits<qint64>::min()
                && (previewNowNs - m_lastRealtimeTaperPreviewNs) < kTaperPreviewFrameIntervalNs) {
                return false;
            }
            m_lastRealtimeTaperPreviewNs = previewNowNs;
        }

        const size_t tailStart
            = (endCount > 0 && strokeDabCount > endCount) ? (strokeDabCount - endCount) : 0;
        size_t updateStart = tailStart;
        if (m_lastRealtimeTaperTailStart != std::numeric_limits<size_t>::max()) {
            updateStart = std::min(updateStart, m_lastRealtimeTaperTailStart);
        }
        const size_t updateCount = strokeDabCount - updateStart;
        m_lastRealtimeTaperTailStart = tailStart;
        currentBrush->applyStrokeTaperToDabRange(updateStart, updateCount);

        // Collect tiles covered by the rebuild dab range BEFORE the rebuild.
        // This gives us the precise set of tiles that will be cleared/rewritten,
        // independent of whether the GPU path clears dirty flags after rendering.
        if (outRebuiltTiles) {
            currentBrush->collectStrokeDabRangeCoveredTiles(
                updateStart, updateCount, *outRebuiltTiles, true);
        }

        if (m_useGPUBrush && executionBackend) {
            if (m_callbacks.makeCurrent) {
                m_callbacks.makeCurrent();
            }
            executionBackend->rebuildStrokeRangeFromDabs(
                *currentBrush, updateStart, updateCount, selectionMask, true);
            if (m_callbacks.doneCurrent) {
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
    const std::unordered_set<TileKey, TileKeyHash>& changedKeys)
{
    if (!m_callbacks.markCompositionTilesDirty) {
        return;
    }

    TileBrush* currentBrush = brush();
    if (!currentBrush) {
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
        if (dirtyKeys.size() > 20 || changedKeys.size() > 20) { }
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
    return currentBrush->hasTaperEffect();
}

bool BrushStrokeHost::hasPendingStabilizerCatchup() const
{
    TileBrush* currentBrush = brush();
    if (!m_isDrawing || !currentBrush) {
        return false;
    }

    const float tauMs = ruwa::core::brushes::stabilizationTauMs(currentBrush->stabilization());
    if (tauMs <= 0.0f) {
        return false;
    }

    return ruwa::core::brushes::hasPendingStrokeStabilizer(
        m_stabilizationState, m_lastStrokeTargetX, m_lastStrokeTargetY);
}

Vector2 BrushStrokeHost::smoothInputTargetForViewport(float worldX, float worldY)
{
    const Vector2 raw { worldX, worldY };
    if (m_strokeInputDevice != StrokeInputDevice::Stylus) {
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

void BrushStrokeHost::processStabilizerCatchup()
{
    if (!m_queuedStrokeSamples.empty()) {
        flushQueuedStrokeInput();
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
    // and re-checks; it fires for real only on a pause / just before lift.
    constexpr double kCatchupIdleMs = 24.0;
    const double nowMs = static_cast<double>(elapsedSeconds(m_strokeElapsedTimer)) * 1000.0;
    if (nowMs - m_lastRealInputMs < kCatchupIdleMs) {
        return;
    }
    continueStrokeImmediate(m_lastStrokeTargetX, m_lastStrokeTargetY, m_lastStrokeTargetPressure,
        elapsedSeconds(m_strokeElapsedTimer), true,
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
    // displacement) rather than m_lastRealInputMs (every event) — a stylus held
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
    const float elapsed = strokeElapsedSecondsNow();
    rasterizeStrokeSegment(grid, paintMask, executionBackend, m_lastStrokeInputX,
        m_lastStrokeInputY, m_lastStrokeInputX, m_lastStrokeInputY, m_lastStrokeInputPressure,
        m_lastStrokeInputPressure, elapsed, elapsed);

    snapshotNewTiles(currentBrush->strokeBuffer(), grid);

    std::unordered_set<TileKey, TileKeyHash> changedKeys;
    const size_t total = currentBrush->strokeDabs().size();
    if (total > changedDabStart) {
        currentBrush->collectStrokeDabRangeCoveredTiles(
            changedDabStart, total - changedDabStart, changedKeys);
    }
    if (!changedKeys.empty()) {
        markStrokeBufferDirtyDelta(changedKeys);
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
        if (executionBackend && !executionBackend->isReadbackComplete(m_pending.fence)) {
            return false;
        }
    }

    if (m_callbacks.finalizePendingStroke) {
        const bool emitStrokePainted = !m_pending.eraseMode && !m_pending.flattenedKeys.empty();
        m_callbacks.finalizePendingStroke(m_pending, m_selectionAtStrokeBegin, emitStrokePainted);
    }
    return true;
}

void BrushStrokeHost::flushPendingFinalization()
{
    m_finalizeTimer.stop();
    // If an async end-of-stroke drain is in flight (queue not yet empty),
    // force-finish it synchronously here so a freshly-starting stroke sees
    // a consistent committed state.
    if (m_endStrokeRequested && m_isDrawing) {
        if (!m_queuedStrokeSamples.empty()) {
            flushQueuedStrokeInput();
        }
        if (m_endStrokeRequested && m_isDrawing) {
            completeEndStrokeAfterQueueDrain();
        }
    }
    if (m_pending.active) {
        tryFinalizeStroke(true);
    }
}

} // namespace aether
