// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   E N G I N E   T Y P E S
// ==========================================================================
// Renderer-neutral value types crossing the Ruwa <-> rendering-engine
// boundary (Stage 1 decoupling, plan v0.6). Nothing here may name a concrete
// rendering backend: no QOpenGLWidget, no GL handles, no Aether
// implementation classes. Qt value types are permitted while the application
// is Qt-hosted (plan 7.21.5).
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINETYPES_H
#define RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINETYPES_H

#include "features/fill/FillAlgorithm.h"
#include "shared/imaging/PixelSurface.h"

#include <QMetaType>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QUuid>
#include <QtGlobal>

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace ruwa::ui::workspace {

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

/// Lifecycle state of the canvas rendering engine session. Callers must be
/// able to distinguish "not ready yet" from "failed": the engine reports
/// failure through status plus a diagnostic, and the UI decides how to
/// present it. The engine never shows a dialog itself.
enum class CanvasEngineStatus {
    Initializing,
    Ready,
    Failed,
    ShuttingDown,
};

/// Owned diagnostic information for a Failed status (plan 7.22.1).
struct CanvasEngineDiagnostic {
    QString code;
    QString message;
};

// ==========================================================================
//   C O L O U R   ( p l a n   7 . 2 1 . 4 )
// ==========================================================================

/// Document/engine-facing colour value. Deliberately NOT 8-bit-quantized:
/// the future colour system must be able to carry working-space identity.
/// Straight-alpha semantic at the application boundary. Adapters may quantize
/// internally to legacy implementations without changing this contract.
struct CanvasColorValue {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    /// Document/application colour-space identity (empty = application default).
    QString colorSpaceId;
};

/// UI-chrome colour for renderer-side presentation (plan 7.21.4 / 7.28).
/// Deliberately distinct from CanvasColorValue: this is interface colour the
/// renderer draws chrome with, never document pixel data. Straight alpha,
/// display sRGB.
struct CanvasUiColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

// ==========================================================================
//   P R E S E N T A T I O N   S T Y L E S   ( p l a n   7 . 2 8 )
// ==========================================================================

/// How the renderer draws the canvas display surface itself (plan 7.28.1).
/// Theme lookup happens in the application; the engine only consumes values.
struct CanvasDisplayStyle {
    CanvasUiColor background;
    CanvasUiColor checkerColorA;
    CanvasUiColor checkerColorB;
    double checkerSizeViewportUnits = 8.0;
};

/// Colours and assets the transform overlay renders with (plan 7.28.4). The
/// application resolves theme colours and the rotation-corner icon; the
/// engine uploads/renders what it is given and never asks ThemeManager or
/// IconProvider for anything.
struct TransformPresentationStyle {
    CanvasUiColor primary;
    CanvasUiColor accent;
    /// Straight-alpha RGBA8, row 0 at the top (PixelSurface layout). Empty
    /// when the theme has no icon; the overlay then draws corners as lines.
    std::optional<ruwa::shared::imaging::PixelSurface> rotationCornerIcon;
};

/// Camera/transform motion interpolation policy (plan 7.15.10). Pushed by the
/// application; renderer code never reads the shared UI animation policy.
struct CanvasMotionPolicy {
    bool enabled = true;
    double speed = 1.0;
};

/// Live pointer sampling injected by the platform binding (plan 7.15.6). Both
/// accessors return positions in viewport-logical coordinates; renderer code
/// never reads QCursor, widget-global mapping or the stylus service itself.
struct CanvasPointerSource {
    /// System pointer position, always resolvable while a window exists
    /// (frame-sampled pan and transform metric anchors track it even when it
    /// is off the canvas). Nullopt only when there is no pointer at all.
    std::function<std::optional<QPointF>()> systemPointerViewportLocal;
    /// Pointer the rendered cursor follows: native stylus position while
    /// native routing owns it, otherwise the system pointer. Nullopt while
    /// the pointer is not ours (another active window) or off the canvas.
    std::function<std::optional<QPointF>()> renderedPointerViewportLocal;
};

// ==========================================================================
//   C O O R D I N A T E   D O M A I N S   ( p l a n   7 . 1 3 / 7 . 2 1 . 1 )
// ==========================================================================
// "Document"  = document/world space (the canvas pixel grid).
// "Viewport"  = the interactive host's logical space (Qt logical units).
// "Surface"   = physical device pixels of the backing GPU render target.
// A raw QPointF is never silently allowed to mean all three; during Stage 1
// the view capability carries the domains as explicit snapshot fields.

/// Effective view state snapshot (plan 7.23.1).
struct CanvasViewState {
    QPointF cameraCenter; ///< document coordinates
    double zoom = 1.0;
    double rotationRadians = 0.0;
    double minZoom = 0.0;
    double maxZoom = 0.0;
    bool mirrorHorizontal = false;
    bool mirrorVertical = false;
    bool animating = false;
    bool fitAnimationActive = false;
};

/// Render-target/viewport metrics snapshot (plan 7.13.2 / 7.21.1). X/Y scales
/// are separate on purpose even though DPR is normally uniform.
struct ViewportMetrics {
    QSizeF logicalSize; ///< viewport-logical units
    uint32_t surfaceWidth = 0; ///< backing surface pixels
    uint32_t surfaceHeight = 0;
    double logicalToSurfaceScaleX = 1.0;
    double logicalToSurfaceScaleY = 1.0;
};

/// Which parts of the effective view changed (plan 7.14.2).
enum class CanvasViewChange : uint32_t {
    None = 0,
    Center = 1u << 0,
    Zoom = 1u << 1,
    Rotation = 1u << 2,
    Mirror = 1u << 3,
    Animation = 1u << 4,
    Metrics = 1u << 5,
};

// Named helpers on purpose, NOT operator|/operator&: a namespace-scope
// operator| in ruwa::ui::workspace takes over unqualified operator lookup for
// EVERY enum in this namespace (ordinary operator lookup stops at the first
// scope containing the name, hiding QFlags' global operators — seen live with
// CanvasOverlayLayout::Caps).
inline constexpr CanvasViewChange canvasViewChangeJoin(CanvasViewChange a, CanvasViewChange b)
{
    return static_cast<CanvasViewChange>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr bool canvasViewChangeContains(CanvasViewChange flags, CanvasViewChange test)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

/// Complete effective view snapshot published with view-change events and
/// presentation synchronization (plan 7.14.2). The revision increments on
/// every effective change, including frame-sampled pan and camera animation.
struct CanvasViewSnapshot {
    CanvasViewState state;
    ViewportMetrics metrics;
    uint64_t revision = 0;
};

// ==========================================================================
//   F I L L   A C T I V I T Y   ( p l a n   7 . 1 4 . 5 )
// ==========================================================================

/// Phase of the fill pipeline the engine is currently executing.
enum class CanvasFillPhase {
    Idle,
    Queued,
    Computing,
    Previewing,
    Committing,
};

/// Semantic snapshot of what the fill machinery is doing right now. Published
/// as the source of truth for fill UI (plan 7.14.5): wait-popup delays,
/// wording, theming and animations are application policy built on top of
/// these facts, never engine behavior.
struct CanvasFillActivityState {
    CanvasFillPhase phase = CanvasFillPhase::Idle;
    /// Layer the fill is computing/previewing for, when one is owned.
    std::optional<QUuid> layer;
    /// Fill origin in document coordinates; UI derives the popup anchor from
    /// it through the current view snapshot.
    std::optional<QPointF> origin;
    ruwa::core::canvas::CanvasFillAlgorithm algorithm
        = ruwa::core::canvas::CanvasFillAlgorithm::Smart;
    /// Progressive preview is being presented live right now.
    bool livePreviewAvailable = false;
    /// Classic fill waiting for the final result (no live preview); the UI
    /// applies its own wait delay before surfacing this.
    bool waitingForFinalResult = false;

    friend bool operator==(const CanvasFillActivityState& a, const CanvasFillActivityState& b)
    {
        return a.phase == b.phase && a.layer == b.layer && a.origin == b.origin
            && a.algorithm == b.algorithm && a.livePreviewAvailable == b.livePreviewAvailable
            && a.waitingForFinalResult == b.waitingForFinalResult;
    }
    friend bool operator!=(const CanvasFillActivityState& a, const CanvasFillActivityState& b)
    {
        return !(a == b);
    }
};

// ==========================================================================
//   T R A N S F O R M   M E T R I C   P R E S E N T A T I O N
//   ( p l a n   7 . 2 7 . 6 / 7 . 1 5 . 1 )
// ==========================================================================

/// What one live transform drag readout measures. The application maps the
/// kind to an icon and presentation; the engine only reports facts.
enum class TransformMetricKind {
    MoveX,
    MoveY,
    Rotation,
    Scale,
};

/// Snap-spacing label anchored at a document position.
struct TransformMetricPointLabel {
    QPointF anchorDocument;
    QString text;
};

/// One live drag readout value. @p negativeDirection is direction relative to
/// the kind's positive reading axis (content moving left/up for MoveX/MoveY,
/// area shrinking for Scale); the presenter decides how each kind renders it.
struct TransformMetricSegment {
    TransformMetricKind kind = TransformMetricKind::MoveX;
    QString text;
    bool negativeDirection = false;
};

/// Transform metric data for QWidget presentation owned by the application
/// (plan 7.15.1): the engine publishes facts, the Qt side draws capsules.
/// @p dragAnchor is already viewport-local — the engine samples it from the
/// injected system pointer source, so the presenter never needs the pointer.
struct TransformPresentationState {
    std::vector<TransformMetricPointLabel> snapLabels;
    std::vector<TransformMetricSegment> dragSegments;
    std::optional<QPointF> dragAnchor;
};

// ==========================================================================
//   N E U T R A L   O P E R A T I O N   R E S U L T   ( p l a n   7 . 2 1 . 3 )
// ==========================================================================

/// Error taxonomy for engine operations that can fail semantically. Ruwa-owned
/// on purpose: the legacy engine's result type carries backend-era error
/// categories and must not become the application-facing contract.
enum class CanvasErrorCode {
    /// The session exists but cannot serve the request yet (still
    /// initializing, already shutting down).
    NotReady,
    /// The request itself is malformed (empty region, invalid extent, ...).
    InvalidRequest,
    /// The selected engine cannot serve this operation at all.
    Unsupported,
    /// The operation was aborted before completing.
    Cancelled,
    /// An allocation the request needed could not be made.
    OutOfMemory,
    /// The rendering backend reported a failure.
    BackendFailure,
    /// The graphics device/context was lost.
    DeviceLost,
    Unknown,
};

/// Description of a failed engine operation. @p message is human-readable for
/// logs/diagnostics and may be empty; @p diagnosticCode is an engine-specific
/// category for reports and may be empty.
struct CanvasOperationError {
    CanvasErrorCode code = CanvasErrorCode::Unknown;
    QString message;
    QString diagnosticCode;
};

/// Ruwa-owned value-or-error result (plan 7.21.3). The ownership and error
/// taxonomy are normative; the container is deliberately a small explicit
/// type — no exceptions, no backend result types.
template <typename T> class CanvasResult {
public:
    static CanvasResult ok(T value) { return CanvasResult(std::move(value), {}); }
    static CanvasResult fail(CanvasOperationError error)
    {
        return CanvasResult({}, std::move(error));
    }

    bool isOk() const { return m_value.has_value(); }
    explicit operator bool() const { return isOk(); }

    /// The success value. Only valid when isOk().
    const T& value() const
    {
        Q_ASSERT(isOk());
        return *m_value;
    }
    T& value()
    {
        Q_ASSERT(isOk());
        return *m_value;
    }
    /// Move the success value out. Only valid when isOk().
    T takeValue()
    {
        Q_ASSERT(isOk());
        return std::move(*m_value);
    }

    /// The failure description. Only valid when !isOk().
    const CanvasOperationError& error() const
    {
        Q_ASSERT(!isOk());
        return m_error;
    }

private:
    CanvasResult(std::optional<T> value, CanvasOperationError error)
        : m_value(std::move(value))
        , m_error(std::move(error))
    {
    }

    std::optional<T> m_value;
    CanvasOperationError m_error;
};

/// Void specialization for operations that only report success/failure.
template <> class CanvasResult<void> {
public:
    static CanvasResult ok() { return CanvasResult(std::nullopt); }
    static CanvasResult fail(CanvasOperationError error)
    {
        return CanvasResult(std::optional<CanvasOperationError>(std::move(error)));
    }

    bool isOk() const { return !m_error.has_value(); }
    explicit operator bool() const { return isOk(); }

    /// The failure description. Only valid when !isOk().
    const CanvasOperationError& error() const
    {
        Q_ASSERT(!isOk());
        return *m_error;
    }

private:
    explicit CanvasResult(std::optional<CanvasOperationError> error)
        : m_error(std::move(error))
    {
    }

    std::optional<CanvasOperationError> m_error;
};

// ==========================================================================
//   C A P T U R E   ( p l a n   7 . 1 2 . 7 / 7 . 2 9 )
// ==========================================================================

/// 1:1 offscreen capture of a document region (plan 7.29.1). The engine
/// renders the region independently of what is currently presented — this is
/// not a screenshot of the viewport.
struct CanvasDocumentCaptureRequest {
    /// Document-space region to render. Normalized by the engine.
    QRect region;
    /// Draw the document background layer. When false the area behind the
    /// content stays fully transparent regardless of the layer's own state.
    bool includeCanvasBackground = true;
    /// Render and read back in 32-bit float instead of 8-bit unsigned.
    /// Costs 4x the memory and is only worth asking for when the result is
    /// headed somewhere that can carry the extra precision — a 16-bit file,
    /// or a resampling pass ahead of one.
    bool highPrecision = false;
    /// Alpha semantics of the returned surface. Straight is file-ready
    /// (thumbnails, clipboard images); Premultiplied is the filter-correct
    /// GPU-native readback that a resampling pipeline should consume and
    /// convert exactly once at its end (plan 7.21.5).
    ruwa::shared::imaging::PixelAlpha alphaMode = ruwa::shared::imaging::PixelAlpha::Straight;
};

/// Document region rendered resampled to an explicit output extent
/// (plan 7.29.2) — the offscreen overview path. Distinct from both the 1:1
/// document capture and the presented-view capture.
struct CanvasResampledCaptureRequest {
    /// Document-space region to render. Normalized by the engine.
    QRect region;
    /// Output pixel extent of the rendered result.
    QSize outputSize;
    /// Draw the document background layer (see CanvasDocumentCaptureRequest).
    bool includeCanvasBackground = true;
};

/// Capture of what the engine is currently PRESENTING (plan 7.29.3) — the
/// presented-viewport path used by the current-view thumbnail. Any temporary
/// presentation-state changes the engine needs (mirror suppression, rendered
/// pointer suppression) happen inside an atomic engine transaction; the
/// caller never toggles presentation flags itself.
struct CanvasPresentedViewCaptureRequest {
    /// Capture the document un-mirrored regardless of the display mirror
    /// toggles, without changing them.
    bool suppressViewMirror = false;
    /// Keep the rendered pointer cursors in the captured frame. False for
    /// thumbnails; the live pointer itself is never part of the frame.
    bool includeRenderedPointer = true;
};

/// Result of a presented-view capture. @p surface is the full presented
/// frame; @p view is the exact view snapshot corresponding to that captured
/// presentation (its mirror flags and metrics describe the frame in
/// @p surface, not the live view the caller may observe afterwards) — so
/// crop/geometry work derives from the returned snapshot instead of
/// reconstructing it from an independently queried camera state. The revision
/// is not part of the live view-change stream and stays 0.
struct CanvasPresentedViewCaptureResult {
    ruwa::shared::imaging::PixelSurface surface;
    CanvasViewSnapshot view;
};

/// Document point -> presented-surface pixel position for @p snapshot
/// (the normative mapping a CanvasViewSnapshot defines). Document content is
/// mirrored within the document rect when the snapshot's mirror flags are
/// set; camera math is translate-to-center, rotate, scale; the logical
/// result maps onto backing-surface pixels through the snapshot's
/// logical-to-surface scales. With the mirror flags unset, @p documentSize
/// is irrelevant.
inline QPointF canvasSurfacePointFromDocument(
    const CanvasViewSnapshot& snapshot, const QPointF& documentPos, const QSizeF& documentSize)
{
    qreal x = documentPos.x();
    qreal y = documentPos.y();
    if (snapshot.state.mirrorHorizontal) {
        x = documentSize.width() - x;
    }
    if (snapshot.state.mirrorVertical) {
        y = documentSize.height() - y;
    }
    const qreal dx = x - snapshot.state.cameraCenter.x();
    const qreal dy = y - snapshot.state.cameraCenter.y();
    const qreal c = std::cos(snapshot.state.rotationRadians);
    const qreal s = std::sin(snapshot.state.rotationRadians);
    const qreal logicalX
        = (dx * c - dy * s) * snapshot.state.zoom + snapshot.metrics.logicalSize.width() / 2.0;
    const qreal logicalY
        = (dx * s + dy * c) * snapshot.state.zoom + snapshot.metrics.logicalSize.height() / 2.0;
    return QPointF(logicalX * snapshot.metrics.logicalToSurfaceScaleX,
        logicalY * snapshot.metrics.logicalToSurfaceScaleY);
}

} // namespace ruwa::ui::workspace

Q_DECLARE_METATYPE(ruwa::ui::workspace::CanvasViewSnapshot)
Q_DECLARE_METATYPE(ruwa::ui::workspace::CanvasFillActivityState)
Q_DECLARE_METATYPE(ruwa::ui::workspace::TransformPresentationState)

#endif // RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINETYPES_H
