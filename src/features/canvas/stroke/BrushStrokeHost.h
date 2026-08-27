// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_STROKE_BRUSHSTROKEHOST_H
#define RUWA_FEATURES_CANVAS_STROKE_BRUSHSTROKEHOST_H

#include "features/brush/engine/StrokeStabilizer.h"
#include "features/brush/engine/BrushStrokeReplay.h"
#include "features/canvas/stroke/StrokeFinalizationController.h"
#include "features/canvas/stroke/StrokeInputQueue.h"
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
    using StrokeInputDevice = aether::StrokeInputDevice;
    using BrushInputDynamics = ruwa::core::brushes::BrushInputDynamics;

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

    /// axisConstraint mirrors the Shift hold: the stroke picks a single axis
    /// (horizontal or vertical) from its first real movement and stays on it
    /// for the rest of the stroke, even after Shift is released.
    void beginStroke(float worldX, float worldY, float pressure = 1.0f,
        StrokeInputDevice inputDevice = StrokeInputDevice::Stylus, bool axisConstraint = false,
        const BrushInputDynamics& inputDynamics = {});
    void continueStroke(float worldX, float worldY, float pressure = 1.0f,
        StrokeInputDevice inputDevice = StrokeInputDevice::Stylus,
        const BrushInputDynamics& inputDynamics = {});
    /// Variant that records the sample with an explicit elapsed time (in
    /// seconds since stroke begin) instead of the wall-clock instant of
    /// the call. Used to feed history-recovered intermediate positions
    /// at their real WM_MOUSEMOVE timestamps so the stabilizer doesn't
    /// see them as a Δt≈0 burst.
    void continueStrokeAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice = StrokeInputDevice::Stylus,
        const BrushInputDynamics& inputDynamics = {});
    /// Adds a timestamped sample to the existing time-budgeted input queue.
    /// Used for recovered WinTab bursts so native event dispatch can finish
    /// before expensive brush rasterization begins.
    void queueStrokeAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice = StrokeInputDevice::Stylus,
        const BrushInputDynamics& inputDynamics = {});
    void translateActiveStroke(float dx, float dy);
    /// Frame tick for the input pump. Called once per rendered frame, before the
    /// layer stack is built, so everything the pen produced since the previous
    /// frame is rasterized into the frame that is about to be drawn. This is the
    /// primary service point for the queue; the fallback timer only covers the
    /// case where frames are not being produced at all.
    void drainStrokeInputForFrame();
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
    struct LiveStrokePoint {
        Vector2 point {};
        float pressure = 1.0f;
        float strokeElapsedSeconds = 0.0f;
        BrushInputDynamics inputDynamics {};
        bool strokeSpeedReliable = false;
    };

    struct StrokeSpeedMeasurement {
        double sampleTimeMs = 0.0;
        double cumulativeScreenDistance = 0.0;
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

    // Starting point for the adaptive per-tick capacity. High enough that a
    // healthy device never notices the pump exists, low enough that the first
    // oversized burst of a session cannot stall a frame outright.
    static constexpr std::size_t kStrokeInputInitialTickCapacity = 128;

    // How a drain treats the samples it finds queued.
    enum class StrokeInputDrainMode {
        // Bound LATENCY, not work: the queue is emptied on every tick, and if the
        // tick cannot afford the samples in it they are decimated down to what it
        // can. Anything else lets a device that produces faster than the brush
        // rasterizes accumulate an unbounded backlog — the drawing lag is then
        // proportional to how long the user has been drawing, not to how much
        // work one sample costs.
        BoundedLatency,
        // Consume every queued sample verbatim. Used for stroke completion, where
        // correctness outranks the frame budget and there is no next frame to
        // spread the work over.
        Complete,
    };

    void scheduleQueuedStrokeInput();
    void drainQueuedStrokeInput(StrokeInputDrainMode mode, bool requestRenderAfterDrain);
    // AIMD controller for the per-tick sample capacity. Cost per sample is not a
    // usable divisor on its own: decimating makes each surviving sample span more
    // arc length, so its cost RISES while total work stays flat — a straight
    // "budget / cost" would spiral downwards. Reacting to the measured tick
    // duration instead is stable, because total work is bounded by path length.
    void updateStrokeInputTickCapacity(std::size_t processedSamples, double elapsedMs);
    void flushQueuedStrokeInput();
    void addStrokeSampleAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice,
        const BrushInputDynamics& inputDynamics, bool processImmediately);
    void continueStrokeImmediate(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds, const BrushInputDynamics& inputDynamics,
        bool requestRenderAfterStep, bool isRealPenSample = true,
        bool inputTimestampReliable = true);
    // Geometry clock used by stabilization and dab time. This deliberately
    // retains the pre-Stroke-Speed cadence: dynamics timestamp reconstruction
    // must not alter the trajectory of the brush cursor.
    double stepStabilizerClock(double realMs, double wallMs, bool isRealPenSample);
    // Separate clock for Stroke Speed measurement. Repeated packet timestamps
    // are reconstructed here without feeding their cadence back into geometry.
    double stepStrokeSpeedClock(double realMs, double wallMs, bool isRealPenSample,
        bool inputTimestampReliable = true);
    // Stroke time carried by DABS (the `Time` dynamics input). Integrates the
    // forward motion of the synthetic clock above onto the stroke's own origin.
    // synthNowMs is a stepStabilizerClock result; realMs is the raw input clock,
    // used only to seed the origin. See the member block for why dabs must not
    // read the raw clock directly.
    float stepDabDynamicsClock(double synthNowMs, double realMs);
    // Same clock, entry point for ticks that carry no pen sample (the liquify
    // dwell): advances in real time without touching the period estimate.
    float advanceDabDynamicsClockIdle(double realMs);
    // dabElapsedSeconds is the DAB clock (stepDabDynamicsClock), not the raw
    // input clock — everything this writes ends up on a dab.
    void continueStrokeWithResolvedPoint(float worldX, float worldY, float pressure,
        float dabElapsedSeconds, const BrushInputDynamics& inputDynamics,
        const Vector2& stabilizedPoint, bool zeroLatencyGeometry, bool requestRenderAfterStep,
        bool updateCatchupTimer);
    void rasterizeStrokeSegment(TileGrid* grid, TileGrid* selectionMask,
        BrushExecutionBackend* brushExecutionBackend, float fromX, float fromY, float toX,
        float toY, float fromPressure, float toPressure, float fromStrokeElapsedSeconds,
        float toStrokeElapsedSeconds, const BrushInputDynamics& fromInputDynamics = {},
        const BrushInputDynamics& toInputDynamics = {});
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
        float p1StrokeElapsedSeconds, float p2StrokeElapsedSeconds,
        const BrushInputDynamics& p0InputDynamics = {},
        const BrushInputDynamics& p1InputDynamics = {},
        const BrushInputDynamics& p2InputDynamics = {},
        const BrushInputDynamics& p3InputDynamics = {});
    bool rebuildStrokePreviewFromDabs(TileGrid* grid, TileGrid* selectionMask,
        BrushExecutionBackend* brushExecutionBackend, bool allowPreviewSampling,
        std::unordered_set<TileKey, TileKeyHash>* outRebuiltTiles = nullptr);
    void collectStrokeChangedKeys(std::unordered_set<TileKey, TileKeyHash>& changedKeys) const;
    // How far the stroke buffer's TILE SET may have moved since the previous
    // markStrokeBufferDirtyDelta call. GrowOnly is the plain dab-append case,
    // where tiles can only be added — that is what lets the delta cost
    // O(changed keys) instead of O(stroke tiles). Rebuild paths re-stamp from
    // the whole dab list and can drop tiles, so they stay on Arbitrary.
    enum class StrokeTileSetChange { Arbitrary, GrowOnly };
    void markStrokeBufferDirtyDelta(const std::unordered_set<TileKey, TileKeyHash>& changedKeys,
        StrokeTileSetChange tileSetChange = StrokeTileSetChange::Arbitrary);
    void snapshotNewTiles(const TileGrid& strokeBuffer, TileGrid* layerGrid);
    void completeEndStrokeAfterQueueDrain();
    bool strokeNeedsRealtimeRebuild() const;
    bool hasPendingStabilizerCatchup() const;
    void updateStabilizerCatchupTimer();
    double stabilizerCatchupIdleThresholdMs() const;
    // Liquify "dwell": time-based dab while the brush is held (twirl/bloat/pucker
    // keep applying even with no cursor movement). Push is movement-only.
    void emitLiquifyDwell();
    Vector2 smoothInputTargetForViewport(float worldX, float worldY, bool enabled);
    // World-space window (px) for continuous dynamics signals that can affect
    // dab geometry. Coupled to the brush base radius so their rate of change
    // stays smooth relative to dab spacing at any brush size / zoom.
    float dynamicsSmoothingWindowWorldPx() const;
    float sampleSmoothedStrokeSpeed(float worldX, float worldY, double sampleTimeMs);
    void backfillDeferredStrokeSpeed(const BrushInputDynamics& seedInputDynamics);
    bool tryFinalizeStroke(bool forceWait);
    void clearStrokeRuntimeState();
    // Projects an incoming raw sample onto the locked axis, choosing the axis
    // on the first sample that has moved far enough from the stroke origin.
    // Runs before the stabilizer so every downstream consumer (dabs, liquify
    // deltas, catch-up ticks) already sees constrained coordinates.
    void applyStrokeAxisConstraint(float& worldX, float& worldY);

    Callbacks m_callbacks;

    bool m_isDrawing = false;
    bool m_useGPUBrush = false;
    float m_lastStrokeX = 0.0f;
    float m_lastStrokeY = 0.0f;
    float m_lastStrokePressure = 1.0f;
    BrushInputDynamics m_lastStrokeInputDynamics {};
    // Anchors of the rasterized path. Their *ElapsedSeconds are in the DAB clock
    // domain (m_dabClock*), because that is what they are handed to the brush
    // as; only m_lastStrokeTargetElapsedSeconds tracks the raw input clock, which
    // is what the incoming sample stream is ordered against.
    float m_lastStrokeElapsedSeconds = 0.0f;
    float m_lastStrokeTargetX = 0.0f;
    float m_lastStrokeTargetY = 0.0f;
    float m_lastStrokeTargetPressure = 1.0f;
    BrushInputDynamics m_lastStrokeTargetInputDynamics {};
    float m_lastStrokeTargetElapsedSeconds = 0.0f;
    float m_lastStrokeInputX = 0.0f;
    float m_lastStrokeInputY = 0.0f;
    float m_lastStrokeInputPressure = 1.0f;
    BrushInputDynamics m_lastRawStrokeInputDynamics {};
    float m_lastStrokeInputElapsedSeconds = 0.0f;
    QElapsedTimer m_strokeElapsedTimer;
    StrokeInputDevice m_strokeInputDevice = StrokeInputDevice::Stylus;
    // Stroke speed follows the resolved stroke cursor (the stabilizer output),
    // using an independent uniform sample clock so packet timing repair cannot
    // affect geometry. Idle catch-up ticks are samples too because that cursor
    // keeps moving after hardware input has stopped. Repeated input timestamps
    // advance by the learned device period rather than their artificial ordering
    // nudge. A trailing arc-length window rejects packet quantization; the shared
    // radius-coupled dynamics follower makes it C1.
    float m_strokeSpeedSampleX = 0.0f;
    float m_strokeSpeedSampleY = 0.0f;
    double m_strokeSpeedCumulativeScreenDistance = 0.0;
    std::deque<StrokeSpeedMeasurement> m_strokeSpeedMeasurements;
    double m_strokeSpeedFirstMotionSampleTimeMs = 0.0;
    double m_strokeSpeedFirstMotionScreenDistance = 0.0;
    float m_strokeSpeedFilteredScreenPxPerSecond = 0.0f;
    float m_strokeSpeedFilterVelocity = 0.0f;
    bool m_strokeSpeedFilterValid = false;
    bool m_strokeSpeedFirstMotionValid = false;
    bool m_strokeSpeedStartupEstimateReliable = false;
    bool m_initialStrokeSpeedSeeded = false;

    // Shift axis constraint. Armed at beginStroke and resolved once, on the
    // first sample far enough from the origin to have an unambiguous dominant
    // direction; releasing Shift mid-stroke does not clear it.
    enum class StrokeAxisConstraint { Off, Pending, Horizontal, Vertical };
    StrokeAxisConstraint m_strokeAxisConstraint = StrokeAxisConstraint::Off;
    float m_strokeAxisOriginX = 0.0f;
    float m_strokeAxisOriginY = 0.0f;
    bool m_autoInputSmoothingValid = false;
    Vector2 m_autoInputSmoothingPoint {};

    // Critically-damped 2nd-order smoothing of the raw input pressure, run in
    // world-space arc length with a continuous time-domain fallback near rest
    // (see addStrokeSampleAtElapsed). A 1st-order EMA only
    // smooths the value, leaving a slope corner at each input sample that shows
    // up as a staircase on large size-pressure brushes; carrying a velocity
    // makes the output C1 and removes it. The smoothTime is coupled to the brush
    // base radius (dynamicsSmoothingWindowWorldPx) so the easing scale matches
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

    std::deque<StrokeInputSample> m_queuedStrokeSamples;
    QTimer m_strokeInputTimer;
    bool m_processingQueuedStrokeInput = false;
    std::size_t m_queuedSamplesSinceCompaction = 0;
    // Samples one tick is currently believed to afford. Adapted by
    // updateStrokeInputTickCapacity and deliberately NOT reset per stroke: it is
    // a property of this machine (GPU, canvas size, device packet rate), so
    // carrying it across strokes means only the very first burst of a session
    // can overshoot the budget.
    std::size_t m_strokeInputTickCapacity = kStrokeInputInitialTickCapacity;
    // True while a drain runs from inside paintGL. Stroke completion toggles the
    // GL context around its flatten/commit work, which would leave the rest of
    // the frame drawing without one, so a completion that comes due during such
    // a drain is handed to the fallback timer instead.
    bool m_drainingInsideFrame = false;
    std::vector<LiveStrokePoint> m_liveStrokePoints;
    // The vertex emitted just before the current anchor (m_lastStroke*). Gives
    // the incoming tangent for the Catmull-Rom curve emission so the silhouette
    // is curved between the de-jittered vertices rather than a polygon.
    Vector2 m_prevEmittedPoint {};
    BrushInputDynamics m_prevEmittedInputDynamics {};

    ruwa::core::brushes::StrokeStabilizerState m_stabilizationState;
    // Pressure delayed in lockstep with the position stabilizer (2-stage EWMA,
    // same alpha) so dabs drawn at the lagged position carry the pressure the
    // pen had there — see continueStrokeImmediate. Without this the stabilizer
    // (position-only) decoupled pressure from position and produced size steps.
    bool m_stabPressureValid = false;
    float m_stabPressure1 = 0.0f;
    float m_stabPressure2 = 0.0f;
    double m_stabPressureLastMs = 0.0;
    // Uniform geometry clock fed to the stabilizer. The OS delivers moves in
    // bursts at coarse (~15.6 ms) timer resolution, so the
    // observable per-sample dt is bimodal (nudge floor / one big jump) — the
    // time-domain EWMA turns that into a sawtooth (facets). We estimate the
    // real average sample period over a sliding window of REAL pen samples and
    // advance a synthetic clock by it each sample, so dt is even. The EWMA/τ
    // are unchanged; only the time base is de-jittered, so the lag stays τ in
    // real time. Catch-up (idle) continues this clock from its current value by
    // wall-clock deltas; the first tick uses one nominal timer period rather than
    // absorbing the whole idle gate. Returning to packet input rebases phase but
    // preserves dt continuity. See continueStrokeImmediate / stepStabilizerClock.
    static constexpr int kStabClockWindow = 8;
    static constexpr double kStabClockInitialPeriodMs = 4.0;
    bool m_stabClockValid = false;
    double m_stabSynthMs = 0.0;
    double m_stabLastRealPenMs = 0.0;
    double m_stabClockLastWallMs = 0.0;
    double m_stabClockEstimatedPeriodMs = kStabClockInitialPeriodMs;
    bool m_stabClockIdleAdvancedSinceRealInput = false;
    // Maps the current raw packet timeline onto the monotonic synthetic one.
    // Rebased after idle so returning from wall-clock catch-up never snaps time.
    double m_stabClockSourceOffsetMs = 0.0;
    std::array<double, kStabClockWindow> m_stabRealWin {};
    int m_stabRealWinCount = 0;

    // Stroke Speed has its own reconstructed clock. Keeping these fields out of
    // the stabilizer clock is intentional: packet timestamp repair may change
    // dynamics sampling, but can no longer introduce geometry cadence changes.
    static constexpr double kStrokeSpeedClockInitialPeriodMs = 4.0;
    bool m_strokeSpeedClockValid = false;
    double m_strokeSpeedClockSynthMs = 0.0;
    double m_strokeSpeedClockLastRealMs = 0.0;
    double m_strokeSpeedClockLastWallMs = 0.0;
    double m_strokeSpeedClockEstimatedPeriodMs = kStrokeSpeedClockInitialPeriodMs;
    bool m_strokeSpeedClockIdleAdvancedSinceRealInput = false;
    bool m_strokeSpeedClockUnreliableTimestampRun = false;
    std::array<double, kStabClockWindow> m_strokeSpeedClockRealWin {};
    int m_strokeSpeedClockRealWinCount = 0;
    // Stroke time handed to the DABS, i.e. what the `Time` dynamics input reads
    // (BrushSettings.h normalizedBrushStrokeTime). It must NOT be the raw input
    // clock: with a stylus the OS stamps a whole burst of samples with one
    // coarse (~15.6 ms) time, so the raw stream advances by the 0.5 ms ordering
    // nudge for three or four samples and then jumps a full tick. Per-dab that
    // is a step function — a run of dabs gets a near-constant time, then one
    // short segment absorbs the whole tick — which a hue-over-time brush paints
    // as flat colour bands with hard edges. A mouse never shows it because its
    // clock is read from QElapsedTimer at processing time and always advances.
    // So dabs integrate the FORWARD motion of the de-jittered synthetic clock
    // instead: even increments, real-time rate, monotonic across the snap-backs
    // stepStabilizerClock performs on a pause or a drift correction.
    bool m_dabClockValid = false;
    double m_dabClockPrevSynthMs = 0.0;
    double m_dabClockElapsedMs = 0.0;
    // Wall-clock ms of the latest REAL pen input; gates stabilizer catch-up so
    // it only fires when input is idle (see processStabilizerCatchup). This must
    // stay in the same QElapsedTimer domain as the timer callback: tablet packet
    // timestamps are acquisition time and can arrive later in recovered bursts.
    double m_lastRealInputWallMs = 0.0;
    // Recent non-zero wall-clock intervals between distinct input arrivals.
    // Catch-up starts after one expected arrival is missed, instead of imposing
    // the old fixed 24 ms pause on every device. Intra-burst zero gaps are not
    // samples of this cadence.
    static constexpr int kRealInputWallIntervalWindow = 8;
    bool m_realInputWallArrivalSeen = false;
    std::array<double, kRealInputWallIntervalWindow> m_realInputWallIntervals {};
    int m_realInputWallIntervalCount = 0;
    QTimer m_stabilizerCatchupTimer;
    QTimer m_liquifyDwellTimer;
    // Liquify dwell must fire once the pen physically stops MOVING, not once the
    // last input event arrived. A stylus in contact streams packets continuously
    // even while held perfectly still (a mouse goes silent when idle), so gating
    // the dwell on m_lastRealInputWallMs — refreshed on every event — would keep the
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
    size_t m_lastRealtimeTaperPreviewDabCount = 0;
    bool m_lastRealtimeTaperPreviewWasSampled = false;
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
