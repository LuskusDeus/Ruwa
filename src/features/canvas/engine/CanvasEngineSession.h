// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   E N G I N E   S E S S I O N
// ==========================================================================
// The application-facing rendering contract (Stage 1 decoupling).
//
// CanvasEngineSession is the renderer-neutral session an interactive canvas
// document talks to. It is capability-oriented on purpose: callers depend on
// the narrow capability they need (view, painting, editing, ...) instead of on
// one concrete widget that implements everything.
//
// HARD RULES for everything in this header:
//   - no QOpenGLWidget / QOpenGLFunctions_* / GL handles;
//   - no concrete rendering-engine types (no aether::OpenGLCanvasWidget,
//     aether::TileBrush, aether::TransformController, aether::Viewport);
//   - no Qt widget types that encode a backend (no QCursor selection here).
//
// Coordinate domains crossing this boundary are explicit:
//   - "document"  — document/world space, the canvas pixel grid;
//   - "viewport"  — the interactive host's logical space (Qt logical units);
//   - "surface"   — physical device pixels of the backing GPU surface.
// A raw QPointF is never silently allowed to mean all three.
//
// Document/history operations are NOT session capabilities (plan 7.12/7.30):
// they live behind the transitional CanvasHistoryFacade / CanvasDocumentFacade
// exposed by CanvasEngineQtBinding. The shared value types crossing this
// boundary (stroke input device, transform handle/hit/mode, snap visual
// state, cursor/text overlay state models) are defined in this neutral
// workspace namespace — the legacy engine keeps internal aliases.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINESESSION_H
#define RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINESESSION_H

#include "features/canvas/overlays/CursorOverlayState.h"
#include "features/canvas/overlays/TextEditOverlayState.h"
#include "features/canvas/engine/CanvasEngineTypes.h"
#include "features/canvas/stroke/StrokeInputQueue.h"
#include "features/fill/FillAlgorithm.h"
#include "features/transform/TransformSnapTypes.h"
#include "features/transform/TransformState.h"
#include "shared/imaging/PixelSurface.h"
#include "shared/rendering/CanvasBackdropRegion.h"
#include "shared/rendering/CanvasBackdropSource.h"
#include "shared/types/ToolId.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QPolygonF>
#include <QPoint>
#include <QUuid>

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace ruwa::ui::workspace {

// ==========================================================================
//   V I E W   C A P A B I L I T Y
// ==========================================================================

/// Semantic camera/view operations. Replaces direct public access to the
/// engine's viewport and camera objects.
class CanvasViewCapability {
public:
    virtual ~CanvasViewCapability() = default;

    // --- state ---
    /// Interactive host extent in viewport-logical units.
    virtual QSizeF viewportExtent() const = 0;
    virtual qreal zoom() const = 0;
    virtual qreal minZoom() const = 0;
    virtual qreal maxZoom() const = 0;
    /// Camera centre in document coordinates.
    virtual QPointF cameraPosition() const = 0;
    virtual qreal rotationRadians() const = 0;
    virtual bool isCameraAnimating() const = 0;
    /// True only during an animated zoom-to-fit (camera is moving on its own).
    virtual bool isFitToViewAnimating() const = 0;
    /// Display-only mirror of the canvas content (document stays unmirrored).
    virtual bool contentFlipHorizontal() const = 0;
    virtual bool contentFlipVertical() const = 0;

    // --- mapping ---
    /// Document point under a viewport-logical position.
    virtual QPointF documentFromViewport(const QPointF& viewportPos) const = 0;
    /// Viewport-logical position of a document point.
    virtual QPointF viewportFromDocument(const QPointF& documentPos) const = 0;
    /// The part of the document currently visible in the viewport.
    virtual QPolygonF visibleDocumentPolygon() const = 0;

    // --- mutations ---
    virtual void setZoom(qreal zoom) = 0;
    virtual void setZoomSmooth(qreal zoom) = 0;
    /// Zoom by @p factor keeping @p viewportPos (viewport-logical) stationary.
    virtual void zoomAtViewportPoint(qreal factor, const QPointF& viewportPos) = 0;
    /// Animated variant of zoomAtViewportPoint().
    virtual void zoomAtViewportPointSmooth(qreal factor, const QPointF& viewportPos) = 0;
    virtual void setZoomLimits(qreal minZoom, qreal maxZoom) = 0;
    virtual void setCameraPosition(const QPointF& documentPos) = 0;
    /// Move the camera by a document-space delta.
    virtual void moveCameraBy(const QPointF& documentDelta) = 0;
    virtual void setRotationRadians(qreal radians) = 0;
    virtual void addRotationRadians(qreal deltaRadians) = 0;
    /// Smooth (animated) rotation toward @p radians.
    virtual void setRotationSmoothRadians(qreal radians) = 0;
    /// Snap the rotation to the nearest multiple of @p incrementRadians when
    /// the current angle is within @p captureDistanceRadians. True when snapped.
    virtual bool snapRotationRadiansSmooth(qreal incrementRadians, qreal captureDistanceRadians)
        = 0;
    virtual void centerCameraOn(const QPointF& documentPoint) = 0;
    virtual void stopCameraAnimation() = 0;
    virtual void setContentFlipHorizontal(bool flip) = 0;
    virtual void setContentFlipVertical(bool flip) = 0;
    /// Export preview: draw as unmirrored without changing stored flip toggles.
    virtual void setExportPreviewSuppressContentMirror(bool suppress) = 0;
    virtual void setExportPreviewHideBoardLayers(bool hide) = 0;

    // --- frame-sampled panning ---
    // While active the engine samples the live pointer once per presented
    // frame and pans by that delta (VSync-synchronous panning). The pointer
    // source itself is injected by the platform binding, never reached into
    // from engine logic.
    virtual void beginPanSampling() = 0;
    virtual void endPanSampling() = 0;
    virtual bool isPanSampling() const = 0;
};

// ==========================================================================
//   P A I N T I N G   C A P A B I L I T Y
// ==========================================================================

/// Stroke execution and brush configuration. Hides the engine's brush
/// implementation: callers pass application-level configuration, never a
/// concrete brush object.
class CanvasPaintingCapability {
public:
    virtual ~CanvasPaintingCapability() = default;

    virtual bool isDrawing() const = 0;
    virtual bool hasPendingStrokeFinalization() const = 0;
    /// Complete a released stroke's queued input, commit, and deferred readback
    /// before callers mutate the shared brush configuration.
    virtual void flushPendingStrokeFinalization() = 0;

    // --- brush configuration (application-level state) ---
    /// Commit the brush colour as a neutral value. Adapters quantize to their
    /// legacy brush implementation internally; the boundary never freezes an
    /// 8-bit-only colour contract (plan 7.21.4).
    virtual void setBrushColor(const CanvasColorValue& color) = 0;
    /// Brush tip radius in document pixels.
    virtual void setBrushRadius(float radiusPx) = 0;
    virtual float brushRadius() const = 0;
    /// Re-sync the rendered brush cursor contour after a configuration change.
    /// Engines keep this up to date themselves once configuration is committed;
    /// the explicit call remains while adapters forward to the legacy engine.
    virtual void updateBrushCursorStamp() = 0;

    // --- stroke translation (space-drag stroke move) ---
    /// Move the active stroke by a document-space delta.
    virtual void translateActiveStroke(float dxDocument, float dyDocument) = 0;

    // --- stroke ingestion (plan 7.6.3C / 7.6.22) ---
    // Timing and device classification stay application input behavior; the
    // engine only receives samples anchored in document space.
    /// Start a stroke at a document position. @p axisConstraint is the Shift
    /// axis lock held at stroke start; the stroke stays locked for its life.
    virtual void beginStroke(float documentX, float documentY, float pressure,
        StrokeInputDevice inputDevice, bool axisConstraint,
        const ruwa::core::brushes::BrushInputDynamics& inputDynamics)
        = 0;
    virtual void continueStroke(float documentX, float documentY, float pressure,
        StrokeInputDevice inputDevice, const ruwa::core::brushes::BrushInputDynamics& inputDynamics)
        = 0;
    /// Continue with an explicit stroke-relative timestamp (real device sample
    /// time) instead of the engine clock — tablet path.
    virtual void continueStrokeAtElapsed(float documentX, float documentY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice,
        const ruwa::core::brushes::BrushInputDynamics& inputDynamics)
        = 0;
    /// Queue a recovered/coalesced sample without rasterizing it inline; the
    /// engine drains its queue under its frame budget.
    virtual void queueStrokeAtElapsed(float documentX, float documentY, float pressure,
        float strokeElapsedSeconds, StrokeInputDevice inputDevice,
        const ruwa::core::brushes::BrushInputDynamics& inputDynamics)
        = 0;
    virtual void endStroke() = 0;
    /// Stroke-relative clock reading at the current instant; input paths use it
    /// to anchor recovered sample timestamps.
    virtual float strokeElapsedSecondsNow() const = 0;

    // --- paint modes ---
    /// Liquify tool mode (semantic paint-mode state; engines translate it into
    /// their brush implementation). Only safe between strokes.
    virtual void setLiquifyToolMode(int mode) = 0;
};

// ==========================================================================
//   E D I T I N G   C A P A B I L I T Y
// ==========================================================================

/// Selection, fill and scene-sampling interaction, plus the layer/document
/// mutations the legacy engine currently hosts. The selection and fill parts
/// are stable contract; the layer/document mutation block is a transitional
/// quarantine (its final owner is a later-stage document subsystem).
class CanvasEditingCapability {
public:
    virtual ~CanvasEditingCapability() = default;

    // --- selection ---
    virtual void clearSelectionMask() = 0;
    /// Select the whole document. False on a document without finite bounds.
    virtual bool selectAll() = 0;
    /// Swap selected for unselected. False without a selection to invert.
    virtual bool invertSelection() = 0;
    virtual bool canReselect() const = 0;
    /// Bring back the selection that was last deselected.
    virtual bool reselect() = 0;
    virtual void selectActiveLayerContent() = 0;
    virtual void selectActiveLayerMask() = 0;
    virtual bool hasSelectionMask() const = 0;
    /// Live selection bounds in document coordinates. False without a mask.
    virtual bool selectionBoundsDocument(QRectF& outBounds) const = 0;
    virtual void translateActiveSelection(float dxDocument, float dyDocument) = 0;
    virtual bool fillSelectionWithColor(const QColor& color) = 0;
    virtual bool canClearSelectionContent() const = 0;
    virtual bool clearSelectionContent() = 0;

    // --- interactive selection dragging (document space) ---
    /// @p add / @p subtract are the Shift/Alt combine modes held at drag start.
    virtual void beginLasso(float documentX, float documentY, bool add, bool subtract) = 0;
    virtual void updateLasso(float documentX, float documentY) = 0;
    virtual void endLasso(bool add, bool subtract) = 0;
    virtual void beginLassoFill(float documentX, float documentY) = 0;
    virtual void updateLassoFill(float documentX, float documentY) = 0;
    virtual void endLassoFill() = 0;
    virtual void beginRectSelection(float documentX, float documentY, bool add, bool subtract) = 0;
    virtual void updateRectSelection(float documentX, float documentY) = 0;
    virtual void endRectSelection(bool add, bool subtract) = 0;
    virtual void beginCircleSelection(float documentX, float documentY, bool add, bool subtract)
        = 0;
    virtual void updateCircleSelection(float documentX, float documentY) = 0;
    virtual void endCircleSelection(bool add, bool subtract) = 0;
    virtual void performMagicWandSelection(int documentX, int documentY, bool add, bool subtract)
        = 0;

    // --- fill tool ---
    /// Start a fill at a document position. Returns a semantic preflight
    /// result (plan 7.6.41): Accepted means the fill was started;
    /// RejectedRegionTooLarge carries radius facts in result.limit. The engine
    /// reports facts — the application owns any user messaging.
    virtual ruwa::core::canvas::CanvasFillRequestResult performFill(int documentX, int documentY)
        = 0;
    virtual ruwa::core::canvas::CanvasFillRequestResult performClassicFill(
        int documentX, int documentY)
        = 0;
    virtual void cancelFillPreview() = 0;
    virtual bool isFillPreviewActive() const = 0;

    // --- scene sampling ---
    /// Sample the color the user sees at a document position.
    virtual bool sampleSceneColor(const QPointF& documentPos, QColor& out) = 0;

    // --- one-shot content transforms ---
    virtual bool flipContentHorizontally() = 0;
    virtual bool flipContentVertically() = 0;
    virtual bool rotateContent90Clockwise() = 0;
    virtual bool rotateContent90Counterclockwise() = 0;
    virtual bool rotateContent180() = 0;
};

// ==========================================================================
//   T R A N S F O R M   C A P A B I L I T Y
// ==========================================================================

/// User snap preferences for interactive transforms (plan 7.27.2). The
/// application pushes a snapshot at engine-session creation and whenever the
/// preferences change; the engine caches it and consumes it when a snap
/// session begins — the renderer never queries SettingsManager. Capture/
/// release thresholds are engine-owned constants, not user settings.
struct CanvasTransformSnapPolicy {
    bool snapCanvas = true;
    bool snapLayers = true;
    bool equalSpacing = true;
    bool pixelAlignRasterMoves = true;
};

/// Interactive transform-mode lifecycle. The engine's transform controller
/// object itself must never cross this boundary: pointer-driven interaction is
/// routed through semantic operations.
class CanvasTransformCapability {
public:
    virtual ~CanvasTransformCapability() = default;

    /// Push the current snap preferences (plan 7.27.2). Call again whenever
    /// they change; the engine keeps the latest snapshot for the next drag.
    virtual void setSnapPolicy(const CanvasTransformSnapPolicy& policy) = 0;

    virtual bool isActive() const = 0;
    virtual bool isMoveOnly() const = 0;
    virtual bool isAutoApplying() const = 0;
    virtual bool usesSelectionMask() const = 0;

    virtual void enter() = 0;
    /// Enter transform for Move tool: translation only, no overlay.
    /// Returns false when nothing is transformable right now.
    virtual bool enterMoveOnly() = 0;
    virtual void confirm() = 0;
    virtual void cancel(std::optional<bool> moveOnlyStateForOverlay = std::nullopt) = 0;

    // --- programmatic moves (Move tool / arrow-key nudges) ---
    virtual bool moveSelectedContentBy(const QPointF& documentDelta) = 0;
    /// Opens a live move session: while it runs, moveSelectedContentBy() only
    /// nudges the transform preview. Pair with endInteractiveContentMove().
    virtual void beginInteractiveContentMove() = 0;
    virtual void endInteractiveContentMove() = 0;
    virtual bool isInteractiveContentMoveActive() const = 0;

    // --- undo/snap sequencing owned by drag interactions ---
    virtual void beginUndoStep() = 0;
    virtual void commitUndoStep() = 0;
    virtual void discardUndoStep() = 0;
    /// Freeze canvas/layer snap targets and editor settings for one drag.
    virtual void beginSnapSession() = 0;
    /// Refresh snap-spacing labels and the live move/rotate/scale readout.
    virtual void syncMetricOverlays() = 0;

    // --- pointer-driven interaction (plan 7.6.22) ---
    // The engine's transform controller object never crosses this boundary;
    // pointer drags go through these semantic operations. All positions are
    // document-space; @p viewportZoom is the current view zoom the engine
    // needs for handle hit ranges and snap math.
    /// Which part of the transform frame @p documentPos would grab. Pure
    /// query; used for cursor choice before a press.
    virtual TransformHitResult hitTest(const QPointF& documentPos, qreal viewportZoom) const = 0;
    /// Start a pointer drag. False when nothing was grabbed (caller discards
    /// the undo step it opened for the attempt).
    virtual bool pointerPress(
        const QPointF& documentPos, qreal viewportZoom, Qt::KeyboardModifiers modifiers)
        = 0;
    /// Continue the drag. True when the transform state changed.
    virtual bool pointerMove(
        const QPointF& documentPos, qreal viewportZoom, Qt::KeyboardModifiers modifiers)
        = 0;
    /// End the active drag.
    virtual void pointerRelease() = 0;
    virtual bool isDragging() const = 0;
    /// Discrete scale/rotation feedback animations still in flight; presses
    /// are swallowed while these run.
    virtual bool hasPendingDiscreteActionAnimation() const = 0;
    /// Shift-move axis guides are visible or still fading out.
    virtual bool isMoveAxisGuideActive() const = 0;
    /// The active snap session currently shows a snap guide.
    virtual bool isSnapGuideActive() const = 0;
    /// Classic mode: corners scale, offset glyphs rotate.
    virtual bool cornersActAsRotationHandles() const = 0;
    /// Sign of the live transform's scale per axis. Diagonal resize cursors
    /// flip when exactly one axis is mirrored relative to the view mirror.
    virtual bool isScaleMirroredHorizontally() const = 0;
    virtual bool isScaleMirroredVertically() const = 0;
    /// Whether @p documentPos lands inside the transformed content quad.
    virtual bool containsDocumentPoint(const QPointF& documentPos) const = 0;
    virtual TransformInteractionMode interactionMode() const = 0;
    /// Whether the transform content can still be flipped in place (similarity
    /// state; a free quad or deform mesh cannot).
    virtual bool canFlipContent() const = 0;
    /// One-shot Move-tool copy/move latch evaluated at the drag position
    /// before the next pointer move applies it.
    virtual void latchSelectionCopyMove(const QPointF& documentPos) = 0;
};

// ==========================================================================
//   H I T   T E S T I N G   C A P A B I L I T Y
// ==========================================================================

/// Content/layer hit testing against the presented scene (plan 7.26.1).
/// Methods are added only for demonstrated content queries; the application
/// stays responsible for what it does with the result (e.g. selection
/// changes). A neutral CanvasResult wrapper arrives with the capture/result
/// type work (plan 7.29); a null id means "nothing hit" for now.
class CanvasHitTesting {
public:
    virtual ~CanvasHitTesting() = default;

    /// Layer whose visible content contains @p documentPos (Move tool pick).
    virtual QUuid movableContentLayerAt(const QPointF& documentPos) const = 0;
};

// ==========================================================================
//   P R E S E N T A T I O N   C A P A B I L I T Y
// ==========================================================================

/// Renderer-side interactive viewport presentation: rendered pointer cursors,
/// shape/resize overlays, the text-edit overlay, and backdrop coordination.
/// State in, semantics out — no GL objects, textures or overlay renderers.
class CanvasPresentationCapability {
public:
    virtual ~CanvasPresentationCapability() = default;

    // --- rendered pointer cursors ---
    /// Brush cursor ring. Center in viewport-logical units, radius in the
    /// same logical space after zoom.
    virtual void setBrushCursorState(bool visible, float centerX, float centerY, float radiusPx)
        = 0;
    virtual bool isBrushCursorVisible() const = 0;
    /// Eyedropper ring with the sampled document colour it shows. The colour
    /// is document data crossing as a neutral value (plan 7.21.4); nullopt
    /// before a sample exists.
    virtual void setEyedropperCursorState(bool visible, float centerX, float centerY,
        const std::optional<CanvasColorValue>& sampledColor = std::nullopt)
        = 0;
    /// Tool cursor (pointer arrow plus a tool badge, or a crosshair). The
    /// badge is identified by the semantic tool, never by a resource path —
    /// the engine integration maps the identifier to its own assets.
    virtual void setToolCursorState(bool visible, float centerX, float centerY,
        ToolCursorStyle style = ToolCursorStyle::PointerBadge, ToolId tool = ToolId::Brush)
        = 0;
    /// Canvas parameter controls rendered through the same scene-inverting
    /// path as brush and tool cursors.
    virtual void setParameterCircleOverlayState(std::vector<ParameterCircleOverlayState> circles)
        = 0;

    // --- display/style/motion state (plan 7.28) ---
    /// How the canvas surface itself is presented (background, checkerboard).
    virtual void setDisplayStyle(const CanvasDisplayStyle& style) = 0;
    /// Colours/assets the transform overlay renders with (plan 7.28.4).
    virtual void setTransformPresentationStyle(const TransformPresentationStyle& style) = 0;
    /// Camera/transform interpolation policy (plan 7.15.10). The engine caches
    /// the latest snapshot; renderer code never reads the UI animation policy.
    virtual void setMotionPolicy(const CanvasMotionPolicy& policy) = 0;

    // --- interaction overlays ---
    virtual void setCanvasResizeOverlayState(bool active, const QRectF& selectionDocumentRect,
        bool selectingOrMoving, bool suppressCanvasCornerRounding = false)
        = 0;
    virtual void setCanvasResizeSnapVisualState(const TransformSnapVisualState& state) = 0;
    virtual void setTextEditOverlayState(const TextEditOverlayState& state) = 0;

    // --- same-frame GPU backdrop coordination ---
    /// Region geometry provider, sampled immediately before each frame.
    /// Regions are viewport-logical rectangles (see CanvasBackdropRegion).
    using BackdropRegionProvider
        = std::function<std::vector<ruwa::shared::rendering::CanvasBackdropRegion>()>;
    virtual void setBackdropRegionProvider(BackdropRegionProvider provider) = 0;
    virtual bool backdropAvailable() const = 0;
    virtual void requestBackdropUpdate() = 0;

    // --- cursor presentation policy used by capture paths ---
    /// Pin the rendered cursor to the position it was given instead of
    /// re-sampling the live pointer each frame.
    virtual void setCursorPositionPinned(bool pinned) = 0;
};

// ==========================================================================
//   C A P T U R E   C A P A B I L I T Y   ( p l a n   7 . 1 2 . 7 / 7 . 2 9 )
// ==========================================================================

/// Image output. The operations are deliberately distinct — a presented-view
/// capture, a 1:1 document-region render and a resampled overview render have
/// different semantics and must not be collapsed into one "give me an image"
/// call. Every operation reports failure through CanvasResult (plan 7.21.3):
/// a null/optional result is "no content", never "the engine failed".
class CanvasCaptureCapability {
public:
    virtual ~CanvasCaptureCapability() = default;

    /// Render a document region at 1:1 into a CPU surface (plan 7.29.1). The
    /// region is rendered independently of what is currently presented.
    virtual CanvasResult<ruwa::shared::imaging::PixelSurface> captureDocumentRegion(
        const CanvasDocumentCaptureRequest& request)
        = 0;

    /// Render a document region resampled to the requested output extent
    /// (plan 7.29.2) — the offscreen overview path (Navigator tiles, export
    /// size-estimate samples).
    virtual CanvasResult<ruwa::shared::imaging::PixelSurface> renderDocumentRegion(
        const CanvasResampledCaptureRequest& request)
        = 0;

    /// Capture the currently presented viewport (plan 7.29.3). Transactional:
    /// any capture-local presentation overrides (mirror suppression, rendered
    /// pointer suppression) are an atomic engine transaction — applied,
    /// captured, and restored on every exit — and the result carries the exact
    /// view snapshot of the captured presentation so crop/geometry work never
    /// reconstructs it from an independently queried camera state.
    virtual CanvasResult<CanvasPresentedViewCaptureResult> capturePresentedView(
        const CanvasPresentedViewCaptureRequest& request)
        = 0;

    /// Document-space bounds of the exportable content. nullopt when there is
    /// nothing exportable (not an error).
    virtual CanvasResult<std::optional<QRect>> exportContentBounds() = 0;

    /// Document-space bounds the Navigator should display. Separately named
    /// from exportContentBounds() on purpose (plan 7.29.5): the two policies
    /// differ (layer bounds vs. composited tile coverage) even though both
    /// currently answer with a rectangle.
    virtual CanvasResult<std::optional<QRect>> navigatorContentBounds() = 0;
};

// ==========================================================================
//   S E S S I O N
// ==========================================================================

/// The renderer-neutral session for one interactive canvas/document session.
/// Implemented today by the Aether legacy adapter; the concrete rendering
/// backend never leaks through this interface.
class CanvasEngineSession {
public:
    virtual ~CanvasEngineSession() = default;

    virtual CanvasEngineStatus status() const = 0;
    /// Owned diagnostic for a Failed status; empty when there is nothing to
    /// report (plan 7.22.1). Must not reference engine-owned storage.
    virtual std::optional<CanvasEngineDiagnostic> diagnostic() const = 0;
    /// Request a new presented frame.
    virtual void requestFrame() = 0;

    virtual CanvasViewCapability& view() = 0;
    virtual CanvasPaintingCapability& painting() = 0;
    virtual CanvasEditingCapability& editing() = 0;
    virtual CanvasTransformCapability& transform() = 0;
    virtual CanvasHitTesting& hitTesting() = 0;
    virtual CanvasPresentationCapability& presentation() = 0;
    virtual CanvasCaptureCapability& capture() = 0;
};

// ==========================================================================
//   B I N D I N G
// ==========================================================================

/// Owns one engine integration for a canvas panel: the generic host widget the
/// Qt UI places in a layout, the renderer-neutral session, and the Qt event
/// relay that translates engine-implementation signals into application
/// events. Ruwa code never receives the concrete engine object.
class CanvasEngineQtEvents;

class CanvasHistoryFacade;
class CanvasDocumentFacade;

/// Qt-only per-canvas host binding (plan 7.31.2): the generic host widget the
/// Qt UI places in a layout, the renderer-neutral session, the Qt event relay,
/// and the transitional document/history facades. Owns teardown authority for
/// one canvas.
class CanvasEngineQtBinding {
public:
    virtual ~CanvasEngineQtBinding() = default;

    /// The widget the UI hosts. UI/platform concerns only (layout, focus,
    /// hit-testing, cursor ownership, event filters) — never renderer API.
    virtual QWidget* viewportHostWidget() const = 0;
    virtual CanvasEngineSession& session() = 0;
    virtual CanvasEngineQtEvents& events() = 0;
    /// TRANSITIONAL (plan 7.30.1): application-owned history facade.
    virtual CanvasHistoryFacade& history() = 0;
    /// TRANSITIONAL (plan 7.30.2): application-owned document-action facade.
    virtual CanvasDocumentFacade& document() = 0;
    /// Neutral backdrop source while the QWidget glass system requires one.
    virtual ruwa::shared::rendering::ICanvasBackdropSource* backdropSource() = 0;

    /// True once shutdown() began; operations reject afterwards.
    virtual bool isShuttingDown() const = 0;
    /// Explicit, idempotent teardown authority (plan 7.17): quiesces engine
    /// work while the host/context is still valid. The QWidget host itself
    /// remains under normal Qt parent ownership.
    virtual void shutdown() = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINESESSION_H
