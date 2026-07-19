// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_STROKE_BRUSHSTROKEHOST_H
#define RUWA_FEATURES_CANVAS_STROKE_BRUSHSTROKEHOST_H

#include "features/brush/engine/StrokeStabilizer.h"
#include "features/brush/engine/BrushStrokeReplay.h"
#include "features/canvas/stroke/StrokeFinalizationController.h"
#include "shared/tiles/TileBrush.h"
#include "shared/types/Types.h"
#include "shared/undo/SelectionState.h"

#include <QElapsedTimer>
#include <QFutureSynchronizer>
#include <QObject>
#include <QTimer>

#include <array>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aether {

class BrushExecutionBackend;
class QuickShapeMorph;
class TileGrid;
struct TileKeyHash;

} // namespace aether
namespace ruwa::core::layers {
struct LayerData;
}
namespace aether {

class BrushStrokeHost final : public QObject {
    Q_OBJECT

public:
    enum class StrokeInputDevice { Stylus, Mouse };

    struct SyncCommit {
        StrokeSnapshot snapshot;
        std::unordered_set<TileKey, TileKeyHash> flattenedKeys;
        SelectionState selectionBefore;
        QUuid layerId;
        bool eraseMode = false;
        float strokeOpacity = 1.0f;
    };

    struct Callbacks {
        std::function<TileBrush*()> getBrush;
        std::function<TileGrid*()> getActiveLayerTileGrid;
        std::function<ruwa::core::layers::LayerData*()> getActiveLayer;
        std::function<TileGrid*(ruwa::core::layers::LayerData*, TileGrid*)> getEffectivePaintMask;
        std::function<bool(const ruwa::core::layers::LayerData*, const TileGrid*)>
            shouldPreserveAlphaForPaintMask;
        std::function<BrushExecutionBackend*()> getBrushExecutionBackend;
        std::function<QuickShapeMorph*()> getQuickShapeMorph;
        std::function<uint32_t()> getDocumentBoundsWidth;
        std::function<uint32_t()> getDocumentBoundsHeight;
        std::function<bool()> isInitialized;
        std::function<void(bool)> notifyCanvasInteraction;
        std::function<void()> requestRender;
        std::function<void(const std::vector<TileKey>&)> markCompositionTilesDirty;
        std::function<void()> cleanupStrokeTextures;
        std::function<void()> makeCurrent;
        std::function<void()> doneCurrent;
        std::function<bool()> hasCurrentGlContext;
        std::function<SelectionState()> captureSelectionState;
        std::function<std::shared_ptr<ruwa::core::brushes::IEditableBrushStrokeReplayData>()>
            getActiveStrokeReplayData;
        std::function<void(
            const QUuid&, const std::unordered_set<TileKey, TileKeyHash>&, bool, float)>
            queueDeferredStrokeCommit;
        std::function<std::shared_ptr<TileGrid>(
            const QUuid&, const std::unordered_set<TileKey, TileKeyHash>&)>
            buildStrokeBlendBackdrop;
        std::function<Color()> getStrokeBlendBackdropColor;
        std::function<void(SyncCommit&&)> commitSynchronousStroke;
        std::function<void(PendingStrokeFinalization&, const SelectionState&, bool)>
            finalizePendingStroke;
        std::function<float()> getViewportZoom;
    };

    explicit BrushStrokeHost(QObject* parent, Callbacks callbacks);

    bool isDrawing() const { return m_isDrawing; }
    /// Elapsed seconds since stroke begin, as measured by the host's
    /// internal timer. Callers feeding history-recovered intermediate
    /// samples use this as an anchor to back-date them via WM_MOUSEMOVE
    /// timestamps.
    float strokeElapsedSecondsNow() const;
    bool hasPendingFinalization() const { return m_pending.active; }
    std::pair<float, float> lastStrokePosition() const { return { m_lastStrokeX, m_lastStrokeY }; }

    void beginStroke(float worldX, float worldY, float pressure = 1.0f,
        StrokeInputDevice inputDevice = StrokeInputDevice::Stylus);
    void continueStroke(float worldX, float worldY, float pressure = 1.0f,
        StrokeInputDevice inputDevice = StrokeInputDevice::Stylus);
    /// Variant that records the sample with an explicit elapsed time (in
    /// seconds since stroke begin) instead of the wall-clock instant of
    /// the call. Used to feed history-recovered intermediate positions
    /// at their real WM_MOUSEMOVE timestamps so the stabilizer doesn't
    /// see them as a Δt≈0 burst.
    void continueStrokeAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice = StrokeInputDevice::Stylus);
    /// Adds a timestamped sample to the existing time-budgeted input queue.
    /// Used for recovered WinTab bursts so native event dispatch can finish
    /// before expensive brush rasterization begins.
    void queueStrokeAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice = StrokeInputDevice::Stylus);
    void translateActiveStroke(float dx, float dy);
    void endStroke();
    bool isEndStrokeDraining() const { return m_endStrokeRequested; }
    void flushPendingFinalization();
    void rebuildPreviewFromCurrentDabs();
    void notifyQuickShapePreviewModified();

private slots:
    void finalizeStroke();
    void processQueuedStrokeInput();
    void processStabilizerCatchup();

private:
    struct QueuedStrokeSample {
        float worldX = 0.0f;
        float worldY = 0.0f;
        float pressure = 1.0f;
        float strokeElapsedSeconds = 0.0f;
        StrokeInputDevice inputDevice = StrokeInputDevice::Stylus;
    };

    struct LiveStrokePoint {
        Vector2 point {};
        float pressure = 1.0f;
        float strokeElapsedSeconds = 0.0f;
    };

    TileBrush* brush() const;
    TileGrid* activeLayerTileGrid() const;
    ruwa::core::layers::LayerData* activeLayer() const;
    TileGrid* effectivePaintMask(ruwa::core::layers::LayerData* layer, TileGrid* grid) const;
    bool shouldPreserveAlphaForPaintMask(
        const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) const;
    void configureBrushSelectionMaskAlpha(TileBrush& brush,
        const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) const;
    BrushExecutionBackend* brushExecutionBackend() const;
    QuickShapeMorph* quickShapeMorph() const;
    uint32_t documentBoundsWidth() const;
    uint32_t documentBoundsHeight() const;
    float viewportZoom() const;
    bool isInitialized() const;
    std::shared_ptr<ruwa::core::brushes::IEditableBrushStrokeReplayData>
    activeStrokeReplayData() const;

    void scheduleQueuedStrokeInput();
    void processQueuedStrokeInputImpl(bool drainAll);
    void flushQueuedStrokeInput();
    void addStrokeSampleAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice, bool processImmediately);
    void continueStrokeImmediate(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, bool requestRenderAfterStep, bool isRealPenSample = true);
    // Advances the de-jittered synthetic clock and returns the nowMs to feed the
    // stabilizer. Real pen samples drive a windowed period estimate; catch-up
    // (idle) ticks pass through real time and reset-on-pause keeps it honest.
    double stepStabilizerClock(double realMs, bool isRealPenSample);
    void continueStrokeWithResolvedPoint(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, const Vector2& stabilizedPoint, bool requestRenderAfterStep,
        bool updateCatchupTimer);
    void rasterizeStrokeSegment(TileGrid* grid, TileGrid* selectionMask,
        BrushExecutionBackend* brushExecutionBackend, float fromX, float fromY, float toX,
        float toY, float fromPressure, float toPressure, float fromStrokeElapsedSeconds,
        float toStrokeElapsedSeconds);
    void rasterizeQuadraticStroke(TileGrid* grid, TileGrid* selectionMask,
        BrushExecutionBackend* brushExecutionBackend, const Vector2& start, const Vector2& control,
        const Vector2& end, float startPressure, float controlPressure, float endPressure,
        float startStrokeElapsedSeconds, float controlStrokeElapsedSeconds,
        float endStrokeElapsedSeconds);
    // Catmull-Rom segment from p1 to p2 with tangents derived from p0 and p3.
    // Internally converts to a cubic Bezier and subdivides into short straight
    // pieces fed to rasterizeStrokeSegment.
    void rasterizeCatmullRomStroke(TileGrid* grid, TileGrid* selectionMask,
        BrushExecutionBackend* brushExecutionBackend, const Vector2& p0, const Vector2& p1,
        const Vector2& p2, const Vector2& p3, float p1Pressure, float p2Pressure,
        float p1StrokeElapsedSeconds, float p2StrokeElapsedSeconds);
    bool rebuildStrokePreviewFromDabs(TileGrid* grid, TileGrid* selectionMask,
        BrushExecutionBackend* brushExecutionBackend, bool allowPreviewSampling,
        std::unordered_set<TileKey, TileKeyHash>* outRebuiltTiles = nullptr);
    void collectStrokeChangedKeys(std::unordered_set<TileKey, TileKeyHash>& changedKeys) const;
    void markStrokeBufferDirtyDelta(const std::unordered_set<TileKey, TileKeyHash>& changedKeys);
    void snapshotNewTiles(const TileGrid& strokeBuffer, TileGrid* layerGrid);
    void completeEndStrokeAfterQueueDrain();
    bool strokeNeedsRealtimeRebuild() const;
    bool hasPendingStabilizerCatchup() const;
    void updateStabilizerCatchupTimer();
    // Liquify "dwell": time-based dab while the brush is held (twirl/bloat/pucker
    // keep applying even with no cursor movement). Push is movement-only.
    void emitLiquifyDwell();
    Vector2 smoothInputTargetForViewport(float worldX, float worldY);
    // World-space window (px) for the input-pressure EMA, coupled to the brush
    // base radius so the smoothing scale tracks dab spacing at any brush size /
    // zoom. See continueStrokeAtElapsed for the rationale (huge-brush stepping).
    float pressureSmoothingWindowWorldPx() const;
    bool tryFinalizeStroke(bool forceWait);
    void clearStrokeRuntimeState();

    Callbacks m_callbacks;

    bool m_isDrawing = false;
    bool m_useGPUBrush = false;
    float m_lastStrokeX = 0.0f;
    float m_lastStrokeY = 0.0f;
    float m_lastStrokePressure = 1.0f;
    float m_lastStrokeElapsedSeconds = 0.0f;
    float m_lastStrokeTargetX = 0.0f;
    float m_lastStrokeTargetY = 0.0f;
    float m_lastStrokeTargetPressure = 1.0f;
    float m_lastStrokeTargetElapsedSeconds = 0.0f;
    float m_lastStrokeInputX = 0.0f;
    float m_lastStrokeInputY = 0.0f;
    float m_lastStrokeInputPressure = 1.0f;
    float m_lastStrokeInputElapsedSeconds = 0.0f;
    QElapsedTimer m_strokeElapsedTimer;
    StrokeInputDevice m_strokeInputDevice = StrokeInputDevice::Stylus;
    bool m_autoInputSmoothingValid = false;
    Vector2 m_autoInputSmoothingPoint {};

    // Critically-damped 2nd-order smoothing of the raw input pressure, run in
    // world-space arc length with a continuous time-domain fallback near rest
    // (see continueStrokeAtElapsed). A 1st-order EMA only
    // smooths the value, leaving a slope corner at each input sample that shows
    // up as a staircase on large size-pressure brushes; carrying a velocity
    // makes the output C1 and removes it. The smoothTime is coupled to the brush
    // base radius (pressureSmoothingWindowWorldPx) so the easing scale matches
    // the dab spacing at any brush size. The time fallback advances the same
    // follower rather than switching or snapping when the pen slows down.
    // Stroke ends are no longer post-processed (the velocity end taper was
    // removed); their shape now comes straight from the pressure signal,
    // pending a realtime taper.
    bool m_inputPressureSmoothValid = false;
    float m_inputPressureSmoothed = 1.0f;
    // Smoothing velocity (pressure per world px) for the critically-damped
    // 2nd-order follower: carrying it makes the smoothed pressure C1 (continuous
    // slope), which is what removes the staircase a 1st-order EMA leaves behind.
    float m_inputPressureVel = 0.0f;
    float m_inputPressureSmoothX = 0.0f;
    float m_inputPressureSmoothY = 0.0f;
    float m_inputPressureSmoothElapsedSeconds = 0.0f;

    std::deque<QueuedStrokeSample> m_queuedStrokeSamples;
    QTimer m_strokeInputTimer;
    bool m_processingQueuedStrokeInput = false;
    std::vector<LiveStrokePoint> m_liveStrokePoints;
    // The vertex emitted just before the current anchor (m_lastStroke*). Gives
    // the incoming tangent for the Catmull-Rom curve emission so the silhouette
    // is curved between the de-jittered vertices rather than a polygon.
    Vector2 m_prevEmittedPoint {};

    ruwa::core::brushes::StrokeStabilizerState m_stabilizationState;
    // Pressure delayed in lockstep with the position stabilizer (2-stage EWMA,
    // same alpha) so dabs drawn at the lagged position carry the pressure the
    // pen had there — see continueStrokeImmediate. Without this the stabilizer
    // (position-only) decoupled pressure from position and produced size steps.
    bool m_stabPressureValid = false;
    float m_stabPressure1 = 0.0f;
    float m_stabPressure2 = 0.0f;
    double m_stabPressureLastMs = 0.0;
    // Reconstructed UNIFORM sample clock fed to the stabilizer as nowMs. The OS
    // delivers moves in bursts at coarse (~15.6 ms) timer resolution, so the
    // observable per-sample dt is bimodal (nudge floor / one big jump) — the
    // time-domain EWMA turns that into a sawtooth (facets). We estimate the
    // real average sample period over a sliding window of REAL pen samples and
    // advance a synthetic clock by it each sample, so dt is even. The EWMA/τ
    // are unchanged; only the time base is de-jittered, so the lag stays τ in
    // real time. Catch-up (idle) ticks use real time directly and do NOT feed
    // the estimate; a pause resets it so resume is clean. See
    // continueStrokeImmediate / stepStabilizerClock.
    static constexpr int kStabClockWindow = 8;
    bool m_stabClockValid = false;
    double m_stabSynthMs = 0.0;
    double m_stabLastRealPenMs = 0.0;
    std::array<double, kStabClockWindow> m_stabRealWin {};
    int m_stabRealWinCount = 0;
    // Stroke-elapsed ms of the latest REAL pen input; gates stabilizer catch-up
    // so it only fires when input is idle (see processStabilizerCatchup).
    double m_lastRealInputMs = 0.0;
    QTimer m_stabilizerCatchupTimer;
    QTimer m_liquifyDwellTimer;
    // Liquify dwell must fire once the pen physically stops MOVING, not once the
    // last input event arrived. A stylus in contact streams packets continuously
    // even while held perfectly still (a mouse goes silent when idle), so gating
    // the dwell on m_lastRealInputMs — refreshed on every event — would keep the
    // gate shut forever with a stylus. Track the wall-clock time of the last
    // meaningful movement instead. The threshold is in screen px (zoom-divided to
    // world px at use), so a held pen's sub-pixel jitter still reads as idle at
    // any zoom. See addStrokeInput / emitLiquifyDwell.
    double m_lastLiquifyMoveWallMs = 0.0;
    float m_lastLiquifyMoveX = 0.0f;
    float m_lastLiquifyMoveY = 0.0f;
    bool m_lastLiquifyMoveValid = false;

    QElapsedTimer m_realtimePreviewTimer;
    size_t m_realtimePreviewEventCount = 0;
    size_t m_lastRealtimeTaperTailStart = std::numeric_limits<size_t>::max();
    qint64 m_lastRealtimeTaperPreviewNs = std::numeric_limits<qint64>::min();

    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> m_strokeBeforeSnapshots;
    std::unordered_set<TileKey, TileKeyHash> m_strokeCreatedTiles;
    std::unordered_set<TileKey, TileKeyHash> m_strokeSnapshotted;
    std::unordered_set<TileKey, TileKeyHash> m_prevStrokePreviewKeys;
    bool m_quickLineStrokeModified = false;

    SelectionState m_selectionAtStrokeBegin;
    PendingStrokeFinalization m_pending;
    QTimer m_finalizeTimer;
    QFutureSynchronizer<void> m_snapshotSync;
    // End-of-stroke async drain state. When the input queue still has samples
    // at release time (fast huge brush), endStroke() returns early and lets
    // m_strokeInputTimer continue draining in time-budgeted chunks. The
    // completion (smoothing/flatten/commit) runs once the queue is empty.
    bool m_endStrokeRequested = false;
    bool m_endStrokeQuickShapeWasActive = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_STROKE_BRUSHSTROKEHOST_H
