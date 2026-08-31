// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/engine/aether/AetherCanvasEngineQtBinding.h"

#include "features/canvas/document/CanvasDocumentFacade.h"
#include "features/canvas/document/CanvasHistoryFacade.h"
#include "features/canvas/engine/CanvasEngineQtEvents.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/scene/Viewport.h"
#include "shared/undo/UndoManager.h"
#include "shared/types/Types.h"

#include <QCoreApplication>
#include <QCursor>
#include <QEventLoop>
#include <QImage>
#include <QOpenGLWidget>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QSizeF>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "services/input/StylusInputManager.h"

namespace ruwa::ui::workspace {

namespace {

QPointF toQt(const aether::Vector2& v)
{
    return QPointF(static_cast<qreal>(v.x), static_cast<qreal>(v.y));
}

aether::Vector2 toAether(const QPointF& p)
{
    return aether::Vector2(static_cast<float>(p.x()), static_cast<float>(p.y()));
}

QSizeF toQt(const aether::Extent2D& extent)
{
    return QSizeF(static_cast<qreal>(extent.width), static_cast<qreal>(extent.height));
}

QColor toQColor(const CanvasUiColor& color)
{
    return QColor::fromRgbF(color.r, color.g, color.b, color.a);
}

CanvasUiColor toUiColor(const QColor& color)
{
    CanvasUiColor out;
    out.r = static_cast<float>(color.redF());
    out.g = static_cast<float>(color.greenF());
    out.b = static_cast<float>(color.blueF());
    out.a = static_cast<float>(color.alphaF());
    return out;
}

aether::Color toAetherColor(const CanvasUiColor& color)
{
    return aether::Color(color.r, color.g, color.b, color.a);
}

/// QRC asset path of a tool's cursor badge. The mapping is an Aether
/// integration detail (plan 7.12.6); the boundary carries the semantic tool.
QString badgeIconResourceForTool(ToolId tool)
{
    switch (tool) {
    case ToolId::Fill:
        return QStringLiteral(":/icons/SmartFillColor");
    case ToolId::ClassicFill:
        return QStringLiteral(":/icons/FillColor");
    case ToolId::Move:
        return QStringLiteral(":/icons/Move");
    case ToolId::MagicWand:
        return QStringLiteral(":/icons/MagicWand");
    case ToolId::Lasso:
        return QStringLiteral(":/icons/Lasso");
    case ToolId::LassoFill:
        return QStringLiteral(":/icons/LassoFill");
    default:
        return QString();
    }
}

/// Effective view snapshot of the widget's live presentation (plan 7.14.2).
/// The mirror flags report the stored display toggles; callers that capture a
/// presentation with capture-local overrides re-state the flags themselves so
/// the snapshot describes exactly the captured frame.
CanvasViewSnapshot buildCurrentViewSnapshot(const aether::OpenGLCanvasWidget* widget)
{
    auto& cam = widget->viewport().camera();
    CanvasViewSnapshot snapshot;
    snapshot.state.cameraCenter = toQt(cam.position());
    snapshot.state.zoom = static_cast<qreal>(cam.zoom());
    snapshot.state.rotationRadians = static_cast<qreal>(cam.rotation());
    snapshot.state.minZoom = static_cast<qreal>(cam.minZoom());
    snapshot.state.maxZoom = static_cast<qreal>(cam.maxZoom());
    snapshot.state.mirrorHorizontal = widget->canvasContentFlipHorizontal();
    snapshot.state.mirrorVertical = widget->canvasContentFlipVertical();
    snapshot.state.animating = cam.isAnimating();
    snapshot.state.fitAnimationActive = cam.isFitToViewAnimating();
    snapshot.metrics.logicalSize = QSizeF(static_cast<qreal>(widget->viewport().width()),
        static_cast<qreal>(widget->viewport().height()));
    const qreal surfaceScale = widget->devicePixelRatioF();
    snapshot.metrics.surfaceWidth
        = static_cast<uint32_t>(std::llround(static_cast<double>(widget->width()) * surfaceScale));
    snapshot.metrics.surfaceHeight
        = static_cast<uint32_t>(std::llround(static_cast<double>(widget->height()) * surfaceScale));
    snapshot.metrics.logicalToSurfaceScaleX = surfaceScale;
    snapshot.metrics.logicalToSurfaceScaleY = surfaceScale;
    return snapshot;
}

/// Wrap the framebuffer as a CPU surface, labelling the alpha semantics
/// exactly as the source image reports them — the bytes are never touched.
ruwa::shared::imaging::PixelSurface pixelSurfaceFromImage(const QImage& image)
{
    using ruwa::shared::imaging::PixelAlpha;
    using ruwa::shared::imaging::PixelStorage;
    using ruwa::shared::imaging::PixelSurface;

    if (image.isNull()) {
        return {};
    }

    QImage source = image;
    if (source.format() != QImage::Format_RGBA8888
        && source.format() != QImage::Format_RGBA8888_Premultiplied) {
        source = std::move(source).convertToFormat(QImage::Format_RGBA8888);
    }
    auto surface = PixelSurface::create(source.width(), source.height(), PixelStorage::UInt8,
        source.format() == QImage::Format_RGBA8888_Premultiplied ? PixelAlpha::Premultiplied
                                                                 : PixelAlpha::Straight);
    if (surface.isNull()) {
        return {};
    }
    for (int y = 0; y < source.height(); ++y) {
        std::memcpy(
            surface.scanLine(y), source.constScanLine(y), static_cast<size_t>(source.width()) * 4);
    }
    return surface;
}

} // namespace

// ==========================================================================
//   A E T H E R   S E S S I O N   ( a d a p t e r )
// ==========================================================================

/// Forwards the renderer-neutral session contract onto the legacy Aether
/// widget. Pure translation: no behavior lives here.
///
/// Named at namespace scope on purpose: the binding header forward-declares
/// this exact type as the concrete session behind CanvasEngineSession, so the
/// unique_ptr in the binding must see one and the same class.
class AetherCanvasSession final : public CanvasEngineSession {
public:
    explicit AetherCanvasSession(aether::OpenGLCanvasWidget* widget)
        : m_widget(widget)
    {
    }

    CanvasEngineStatus status() const override
    {
        if (m_widget->isInitialized()) {
            return CanvasEngineStatus::Ready;
        }
        return m_widget->lastFailure() ? CanvasEngineStatus::Failed
                                       : CanvasEngineStatus::Initializing;
    }
    std::optional<CanvasEngineDiagnostic> diagnostic() const override
    {
        return m_widget->lastFailure();
    }
    void requestFrame() override { m_widget->requestRender(); }

    CanvasViewCapability& view() override { return m_view; }
    CanvasPaintingCapability& painting() override { return m_painting; }
    CanvasEditingCapability& editing() override { return m_editing; }
    CanvasTransformCapability& transform() override { return m_transform; }
    CanvasHitTesting& hitTesting() override { return m_hitTesting; }
    CanvasPresentationCapability& presentation() override { return m_presentation; }
    CanvasCaptureCapability& capture() override { return m_capture; }

    // capability implementations ------------------------------------------

    class View final : public CanvasViewCapability {
    public:
        explicit View(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        QSizeF viewportExtent() const override { return toQt(m_widget->viewport().extent()); }
        qreal zoom() const override
        {
            return static_cast<qreal>(m_widget->viewport().camera().zoom());
        }
        qreal minZoom() const override
        {
            return static_cast<qreal>(m_widget->viewport().camera().minZoom());
        }
        qreal maxZoom() const override
        {
            return static_cast<qreal>(m_widget->viewport().camera().maxZoom());
        }
        QPointF cameraPosition() const override
        {
            return toQt(m_widget->viewport().camera().position());
        }
        qreal rotationRadians() const override
        {
            return static_cast<qreal>(m_widget->viewport().camera().rotation());
        }
        bool isCameraAnimating() const override
        {
            return m_widget->viewport().camera().isAnimating();
        }
        bool isFitToViewAnimating() const override
        {
            return m_widget->viewport().camera().isFitToViewAnimating();
        }
        bool contentFlipHorizontal() const override
        {
            return m_widget->canvasContentFlipHorizontal();
        }
        bool contentFlipVertical() const override { return m_widget->canvasContentFlipVertical(); }

        QPointF documentFromViewport(const QPointF& viewportPos) const override
        {
            return toQt(m_widget->documentWorldFromScreen(toAether(viewportPos)));
        }
        QPointF viewportFromDocument(const QPointF& documentPos) const override
        {
            return toQt(m_widget->screenFromDocumentWorld(toAether(documentPos)));
        }
        QPolygonF visibleDocumentPolygon() const override
        {
            const QSizeF extent = viewportExtent();
            QPolygonF polygon;
            polygon.reserve(4);
            polygon.append(documentFromViewport(QPointF(0.0, 0.0)));
            polygon.append(documentFromViewport(QPointF(extent.width(), 0.0)));
            polygon.append(documentFromViewport(QPointF(extent.width(), extent.height())));
            polygon.append(documentFromViewport(QPointF(0.0, extent.height())));
            return polygon;
        }

        void setZoom(qreal zoom) override
        {
            m_widget->viewport().camera().setZoom(static_cast<float>(zoom));
        }
        void setZoomSmooth(qreal zoom) override
        {
            m_widget->viewport().camera().setZoomSmooth(
                static_cast<float>(zoom), m_widget->viewport().size());
        }
        void zoomAtViewportPoint(qreal factor, const QPointF& viewportPos) override
        {
            m_widget->viewport().camera().zoomAt(
                static_cast<float>(factor), toAether(viewportPos), m_widget->viewport().size());
        }
        void zoomAtViewportPointSmooth(qreal factor, const QPointF& viewportPos) override
        {
            m_widget->viewport().camera().zoomAtSmooth(
                static_cast<float>(factor), toAether(viewportPos), m_widget->viewport().size());
        }
        void setZoomLimits(qreal minZoom, qreal maxZoom) override
        {
            m_widget->viewport().camera().setZoomLimits(
                static_cast<float>(minZoom), static_cast<float>(maxZoom));
        }
        void setCameraPosition(const QPointF& documentPos) override
        {
            m_widget->viewport().camera().setPosition(toAether(documentPos));
        }
        void moveCameraBy(const QPointF& documentDelta) override
        {
            m_widget->viewport().camera().move(toAether(documentDelta));
        }
        void setRotationRadians(qreal radians) override
        {
            m_widget->viewport().camera().setRotation(static_cast<float>(radians));
        }
        void addRotationRadians(qreal deltaRadians) override
        {
            m_widget->viewport().camera().addRotation(static_cast<float>(deltaRadians));
        }
        void setRotationSmoothRadians(qreal radians) override
        {
            m_widget->viewport().camera().setRotationSmooth(static_cast<float>(radians));
        }
        bool snapRotationRadiansSmooth(
            qreal incrementRadians, qreal captureDistanceRadians) override
        {
            return m_widget->viewport().camera().snapRotationSmooth(
                static_cast<float>(incrementRadians), static_cast<float>(captureDistanceRadians));
        }
        void centerCameraOn(const QPointF& documentPoint) override
        {
            m_widget->viewport().camera().centerOn(toAether(documentPoint));
        }
        void stopCameraAnimation() override { m_widget->viewport().camera().stopAnimation(); }
        void setContentFlipHorizontal(bool flip) override
        {
            m_widget->setCanvasContentFlipHorizontal(flip);
        }
        void setContentFlipVertical(bool flip) override
        {
            m_widget->setCanvasContentFlipVertical(flip);
        }
        void setExportPreviewSuppressContentMirror(bool suppress) override
        {
            m_widget->setExportPreviewSuppressContentMirror(suppress);
        }
        void setExportPreviewHideBoardLayers(bool hide) override
        {
            m_widget->setExportPreviewHideBoardLayers(hide);
        }

        void beginPanSampling() override { m_widget->beginPanSampling(); }
        void endPanSampling() override { m_widget->endPanSampling(); }
        bool isPanSampling() const override { return m_widget->isPanSampling(); }

    private:
        aether::OpenGLCanvasWidget* m_widget = nullptr;
    };

    class Painting final : public CanvasPaintingCapability {
    public:
        explicit Painting(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        bool isDrawing() const override { return m_widget->isDrawing(); }
        bool hasPendingStrokeFinalization() const override
        {
            return m_widget->hasPendingStrokeFinalization();
        }
        void flushPendingStrokeFinalization() override
        {
            m_widget->flushPendingStrokeFinalization();
        }

        void setBrushColor(const CanvasColorValue& color) override
        {
            // Quantize to the legacy brush implementation; the contract stays
            // value-based (plan 7.21.4).
            m_widget->brush().setColor(
                quantize(color.r), quantize(color.g), quantize(color.b), quantize(color.a));
        }
        void setBrushRadius(float radiusPx) override { m_widget->brush().setRadius(radiusPx); }
        float brushRadius() const override { return m_widget->brush().radius(); }
        void updateBrushCursorStamp() override { m_widget->updateBrushCursorStamp(); }

        void translateActiveStroke(float dxDocument, float dyDocument) override
        {
            m_widget->translateActiveStroke(dxDocument, dyDocument);
        }

        void beginStroke(float documentX, float documentY, float pressure,
            StrokeInputDevice inputDevice, bool axisConstraint,
            const ruwa::core::brushes::BrushInputDynamics& inputDynamics) override
        {
            m_widget->beginStroke(
                documentX, documentY, pressure, inputDevice, axisConstraint, inputDynamics);
        }
        void continueStroke(float documentX, float documentY, float pressure,
            StrokeInputDevice inputDevice,
            const ruwa::core::brushes::BrushInputDynamics& inputDynamics) override
        {
            m_widget->continueStroke(documentX, documentY, pressure, inputDevice, inputDynamics);
        }
        void continueStrokeAtElapsed(float documentX, float documentY, float pressure,
            float strokeElapsedSeconds, StrokeInputDevice inputDevice,
            const ruwa::core::brushes::BrushInputDynamics& inputDynamics) override
        {
            m_widget->continueStrokeAtElapsed(
                documentX, documentY, pressure, strokeElapsedSeconds, inputDevice, inputDynamics);
        }
        void queueStrokeAtElapsed(float documentX, float documentY, float pressure,
            float strokeElapsedSeconds, StrokeInputDevice inputDevice,
            const ruwa::core::brushes::BrushInputDynamics& inputDynamics) override
        {
            m_widget->queueStrokeAtElapsed(
                documentX, documentY, pressure, strokeElapsedSeconds, inputDevice, inputDynamics);
        }
        void endStroke() override { m_widget->endStroke(); }
        float strokeElapsedSecondsNow() const override
        {
            return m_widget->strokeElapsedSecondsNow();
        }

        void setLiquifyToolMode(int mode) override { m_widget->brush().setLiquifyToolMode(mode); }

    private:
        static uint8_t quantize(float channel)
        {
            return static_cast<uint8_t>(qBound(0.0f, channel, 1.0f) * 255.0f + 0.5f);
        }
        aether::OpenGLCanvasWidget* m_widget = nullptr;
    };

    class Editing final : public CanvasEditingCapability {
    public:
        explicit Editing(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        void clearSelectionMask() override { m_widget->clearSelectionMask(); }
        bool selectAll() override { return m_widget->selectAll(); }
        bool invertSelection() override { return m_widget->invertSelection(); }
        bool canReselect() const override { return m_widget->canReselect(); }
        bool reselect() override { return m_widget->reselect(); }
        void selectActiveLayerContent() override { m_widget->selectActiveLayerContent(); }
        void selectActiveLayerMask() override { m_widget->selectActiveLayerMask(); }
        bool hasSelectionMask() const override { return m_widget->hasSelectionMask(); }
        bool selectionBoundsDocument(QRectF& outBounds) const override
        {
            return m_widget->selectionBoundsWorld(outBounds);
        }
        void translateActiveSelection(float dxDocument, float dyDocument) override
        {
            m_widget->translateActiveSelection(dxDocument, dyDocument);
        }
        bool fillSelectionWithColor(const QColor& color) override
        {
            return m_widget->fillSelectionWithColor(color);
        }
        bool canClearSelectionContent() const override
        {
            return m_widget->canClearSelectionContent();
        }
        bool clearSelectionContent() override { return m_widget->clearSelectionContent(); }

        void beginLasso(float documentX, float documentY, bool add, bool subtract) override
        {
            m_widget->beginLasso(documentX, documentY, add, subtract);
        }
        void updateLasso(float documentX, float documentY) override
        {
            m_widget->updateLasso(documentX, documentY);
        }
        void endLasso(bool add, bool subtract) override { m_widget->endLasso(add, subtract); }
        void beginLassoFill(float documentX, float documentY) override
        {
            m_widget->beginLassoFill(documentX, documentY);
        }
        void updateLassoFill(float documentX, float documentY) override
        {
            m_widget->updateLassoFill(documentX, documentY);
        }
        void endLassoFill() override { m_widget->endLassoFill(); }
        void beginRectSelection(float documentX, float documentY, bool add, bool subtract) override
        {
            m_widget->beginRectSelection(documentX, documentY, add, subtract);
        }
        void updateRectSelection(float documentX, float documentY) override
        {
            m_widget->updateRectSelection(documentX, documentY);
        }
        void endRectSelection(bool add, bool subtract) override
        {
            m_widget->endRectSelection(add, subtract);
        }
        void beginCircleSelection(
            float documentX, float documentY, bool add, bool subtract) override
        {
            m_widget->beginCircleSelection(documentX, documentY, add, subtract);
        }
        void updateCircleSelection(float documentX, float documentY) override
        {
            m_widget->updateCircleSelection(documentX, documentY);
        }
        void endCircleSelection(bool add, bool subtract) override
        {
            m_widget->endCircleSelection(add, subtract);
        }
        void performMagicWandSelection(
            int documentX, int documentY, bool add, bool subtract) override
        {
            m_widget->performMagicWandSelection(documentX, documentY, add, subtract);
        }

        ruwa::core::canvas::CanvasFillRequestResult performFill(
            int documentX, int documentY) override
        {
            return m_widget->performFill(documentX, documentY);
        }
        ruwa::core::canvas::CanvasFillRequestResult performClassicFill(
            int documentX, int documentY) override
        {
            return m_widget->performClassicFill(documentX, documentY);
        }
        void cancelFillPreview() override { m_widget->cancelFillPreview(); }
        bool isFillPreviewActive() const override { return m_widget->isFillPreviewActive(); }

        bool sampleSceneColor(const QPointF& documentPos, QColor& out) override
        {
            return m_widget->sampleColorFromScene(
                static_cast<float>(documentPos.x()), static_cast<float>(documentPos.y()), out);
        }

        bool flipContentHorizontally() override { return m_widget->flipContentHorizontally(); }
        bool flipContentVertically() override { return m_widget->flipContentVertically(); }
        bool rotateContent90Clockwise() override { return m_widget->rotateContent90Clockwise(); }
        bool rotateContent90Counterclockwise() override
        {
            return m_widget->rotateContent90Counterclockwise();
        }
        bool rotateContent180() override { return m_widget->rotateContent180(); }

    private:
        aether::OpenGLCanvasWidget* m_widget = nullptr;
    };

    class Transform final : public CanvasTransformCapability {
    public:
        explicit Transform(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        void setSnapPolicy(const CanvasTransformSnapPolicy& policy) override
        {
            m_snapPolicy = policy;
            m_widget->setTransformSnapSettings(policy.snapCanvas, policy.snapLayers,
                policy.equalSpacing, policy.pixelAlignRasterMoves);
        }

        bool isActive() const override { return m_widget->isTransformActive(); }
        bool isMoveOnly() const override { return m_widget->isMoveOnlyTransform(); }
        bool isAutoApplying() const override { return m_widget->isAutoApplyingTransform(); }
        bool usesSelectionMask() const override { return m_widget->transformUsesSelectionMask(); }

        void enter() override { m_widget->enterTransformMode(); }
        bool enterMoveOnly() override { return m_widget->enterMoveOnlyTransformMode(); }
        void confirm() override { m_widget->confirmTransform(); }
        void cancel(std::optional<bool> moveOnlyStateForOverlay = std::nullopt) override
        {
            m_widget->cancelTransform(moveOnlyStateForOverlay);
        }

        bool moveSelectedContentBy(const QPointF& documentDelta) override
        {
            return m_widget->moveSelectedContentBy(toAether(documentDelta));
        }
        void beginInteractiveContentMove() override { m_widget->beginInteractiveContentMove(); }
        void endInteractiveContentMove() override { m_widget->endInteractiveContentMove(); }
        bool isInteractiveContentMoveActive() const override
        {
            return m_widget->isInteractiveContentMoveActive();
        }

        void beginUndoStep() override { m_widget->beginTransformUndoStep(); }
        void commitUndoStep() override { m_widget->commitTransformUndoStep(); }
        void discardUndoStep() override { m_widget->discardTransformUndoStep(); }
        void beginSnapSession() override { m_widget->beginTransformSnapSession(); }
        void syncMetricOverlays() override { m_widget->syncTransformMetricOverlays(); }

        TransformHitResult hitTest(const QPointF& documentPos, qreal viewportZoom) const override
        {
            return m_widget->transformController().hitTestDetailed(
                toAether(documentPos), static_cast<float>(viewportZoom));
        }
        bool pointerPress(const QPointF& documentPos, qreal viewportZoom,
            Qt::KeyboardModifiers modifiers) override
        {
            return m_widget->transformController().mousePress(
                toAether(documentPos), static_cast<float>(viewportZoom), modifiers);
        }
        bool pointerMove(const QPointF& documentPos, qreal viewportZoom,
            Qt::KeyboardModifiers modifiers) override
        {
            return m_widget->transformController().mouseMove(toAether(documentPos),
                static_cast<float>(viewportZoom), modifiers, &m_widget->viewport());
        }
        void pointerRelease() override { m_widget->transformController().mouseRelease(); }
        bool isDragging() const override { return m_widget->transformController().isDragging(); }
        bool hasPendingDiscreteActionAnimation() const override
        {
            return m_widget->transformController().hasPendingDiscreteActionAnimation();
        }
        bool isMoveAxisGuideActive() const override
        {
            return m_widget->transformController().moveAxisGuideActive();
        }
        bool isSnapGuideActive() const override
        {
            return m_widget->transformController().snapVisualState().active();
        }
        bool cornersActAsRotationHandles() const override
        {
            return m_widget->transformController().cornersActAsRotationHandles();
        }
        bool isScaleMirroredHorizontally() const override
        {
            return std::signbit(m_widget->transformController().state().scale.x);
        }
        bool isScaleMirroredVertically() const override
        {
            return std::signbit(m_widget->transformController().state().scale.y);
        }
        bool containsDocumentPoint(const QPointF& documentPos) const override
        {
            return m_widget->transformController().state().pointInTransformedRect(
                toAether(documentPos));
        }
        TransformInteractionMode interactionMode() const override
        {
            return m_widget->transformInteractionMode();
        }
        bool canFlipContent() const override { return m_widget->canFlipActiveTransform(); }
        void latchSelectionCopyMove(const QPointF& documentPos) override
        {
            m_widget->latchSelectionCopyMoveTransformIfNeeded(toAether(documentPos));
        }

    private:
        aether::OpenGLCanvasWidget* m_widget = nullptr;
        CanvasTransformSnapPolicy m_snapPolicy {};
    };

    class HitTestingCapability final : public CanvasHitTesting {
    public:
        explicit HitTestingCapability(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        QUuid movableContentLayerAt(const QPointF& documentPos) const override
        {
            return m_widget->moveToolContentLayerAt(toAether(documentPos));
        }

    private:
        aether::OpenGLCanvasWidget* m_widget = nullptr;
    };

    class Presentation final : public CanvasPresentationCapability {
    public:
        explicit Presentation(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        void setBrushCursorState(
            bool visible, float centerX, float centerY, float radiusPx) override
        {
            m_widget->setBrushCursorState(visible, centerX, centerY, radiusPx);
        }
        bool isBrushCursorVisible() const override { return m_widget->isBrushCursorVisible(); }
        void setEyedropperCursorState(bool visible, float centerX, float centerY,
            const std::optional<CanvasColorValue>& sampledColor) override
        {
            QColor selectedColor(0, 0, 0, 255);
            if (sampledColor) {
                selectedColor = QColor::fromRgbF(
                    sampledColor->r, sampledColor->g, sampledColor->b, sampledColor->a);
            }
            m_widget->setEyedropperCursorState(visible, centerX, centerY, selectedColor);
        }
        void setToolCursorState(
            bool visible, float centerX, float centerY, ToolCursorStyle style, ToolId tool) override
        {
            // The QRC asset mapping is an Aether integration detail; the
            // boundary carries the semantic tool (plan 7.12.6).
            m_widget->setToolCursorState(
                visible, centerX, centerY, style, badgeIconResourceForTool(tool));
        }
        void setDisplayStyle(const CanvasDisplayStyle& style) override
        {
            m_widget->setBackgroundColor(toAetherColor(style.background));
            m_widget->setCheckerColors(
                toAetherColor(style.checkerColorA), toAetherColor(style.checkerColorB));
            m_widget->setCheckerSize(static_cast<float>(style.checkerSizeViewportUnits));
        }
        void setTransformPresentationStyle(const TransformPresentationStyle& style) override
        {
            m_widget->setTransformPresentationStyle(style);
        }
        void setMotionPolicy(const CanvasMotionPolicy& policy) override
        {
            m_widget->setMotionPolicy(policy);
        }
        void setParameterCircleOverlayState(
            std::vector<ParameterCircleOverlayState> circles) override
        {
            m_widget->setParameterCircleOverlayState(std::move(circles));
        }

        void setCanvasResizeOverlayState(bool active, const QRectF& selectionDocumentRect,
            bool selectingOrMoving, bool suppressCanvasCornerRounding) override
        {
            m_widget->setCanvasResizeOverlayState(
                active, selectionDocumentRect, selectingOrMoving, suppressCanvasCornerRounding);
        }
        void setCanvasResizeSnapVisualState(const TransformSnapVisualState& state) override
        {
            m_widget->setCanvasResizeSnapVisualState(state);
        }
        void setTextEditOverlayState(const TextEditOverlayState& state) override
        {
            m_widget->setTextEditOverlayState(state);
        }

        void setBackdropRegionProvider(BackdropRegionProvider provider) override
        {
            m_widget->setBackdropRegionProvider(std::move(provider));
        }
        bool backdropAvailable() const override { return m_widget->backdropAvailable(); }
        void requestBackdropUpdate() override { m_widget->requestBackdropUpdate(); }

        void setCursorPositionPinned(bool pinned) override
        {
            m_widget->setCursorPositionPinned(pinned);
        }

    private:
        aether::OpenGLCanvasWidget* m_widget = nullptr;
    };

    class Capture final : public CanvasCaptureCapability {
    public:
        explicit Capture(aether::OpenGLCanvasWidget* widget)
            : m_widget(widget)
        {
        }

        CanvasResult<ruwa::shared::imaging::PixelSurface> captureDocumentRegion(
            const CanvasDocumentCaptureRequest& request) override
        {
            using SurfaceResult = CanvasResult<ruwa::shared::imaging::PixelSurface>;
            if (!ready()) {
                return SurfaceResult::fail(
                    error(CanvasErrorCode::NotReady, "the engine session is not ready"));
            }
            const QRect region = request.region.normalized();
            if (!region.isValid() || region.isEmpty()) {
                return SurfaceResult::fail(
                    error(CanvasErrorCode::InvalidRequest, "the capture region is empty"));
            }

            auto surface = m_widget->captureCanvasSurface(request);
            if (surface.isNull()) {
                return SurfaceResult::fail(error(CanvasErrorCode::OutOfMemory,
                    "could not allocate the document capture surface"));
            }
            return SurfaceResult::ok(std::move(surface));
        }

        CanvasResult<ruwa::shared::imaging::PixelSurface> renderDocumentRegion(
            const CanvasResampledCaptureRequest& request) override
        {
            using SurfaceResult = CanvasResult<ruwa::shared::imaging::PixelSurface>;
            if (!ready()) {
                return SurfaceResult::fail(
                    error(CanvasErrorCode::NotReady, "the engine session is not ready"));
            }
            const QRect region = request.region.normalized();
            if (!region.isValid() || region.isEmpty()) {
                return SurfaceResult::fail(
                    error(CanvasErrorCode::InvalidRequest, "the capture region is empty"));
            }
            if (!request.outputSize.isValid() || request.outputSize.isEmpty()) {
                return SurfaceResult::fail(
                    error(CanvasErrorCode::InvalidRequest, "the output extent is empty"));
            }

            const QImage image = m_widget->renderCompositedRegion(
                region, request.outputSize, request.includeCanvasBackground);
            if (image.isNull()) {
                return SurfaceResult::fail(error(CanvasErrorCode::BackendFailure,
                    "the offscreen overview render produced no image"));
            }
            auto surface = pixelSurfaceFromImage(image);
            if (surface.isNull()) {
                return SurfaceResult::fail(error(CanvasErrorCode::OutOfMemory,
                    "could not allocate the overview capture surface"));
            }
            return SurfaceResult::ok(std::move(surface));
        }

        CanvasResult<CanvasPresentedViewCaptureResult> capturePresentedView(
            const CanvasPresentedViewCaptureRequest& request) override
        {
            using CaptureResult = CanvasResult<CanvasPresentedViewCaptureResult>;
            if (!ready()) {
                return CaptureResult::fail(
                    error(CanvasErrorCode::NotReady, "the engine session is not ready"));
            }

            // Atomic capture transaction (plan 7.29.4): save the presentation
            // state, apply capture-local overrides, produce and read back one
            // frame, restore on every exit, then request the normal
            // presentation again. The application never toggles presentation
            // flags and never pumps the event loop to make temporary state
            // visible — that is this transaction's job.
            const bool prevFlipH = m_widget->canvasContentFlipHorizontal();
            const bool prevFlipV = m_widget->canvasContentFlipVertical();
            const bool prevSkipCursors = m_widget->skipCursorOverlays();

            if (request.suppressViewMirror) {
                m_widget->setCanvasContentFlipHorizontal(false);
                m_widget->setCanvasContentFlipVertical(false);
            }
            m_widget->setSkipCursorOverlays(!request.includeRenderedPointer);

            m_widget->update();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QImage frame = m_widget->grabFramebuffer();

            // The snapshot must describe the captured presentation — the
            // stored mirror toggles may have been suppressed for the frame
            // above, and the export-preview mirror suppression also shapes
            // what the frame actually shows.
            CanvasPresentedViewCaptureResult result;
            result.view = buildCurrentViewSnapshot(m_widget);
            result.view.state.mirrorHorizontal = m_widget->effectiveContentFlipH();
            result.view.state.mirrorVertical = m_widget->effectiveContentFlipV();

            m_widget->setCanvasContentFlipHorizontal(prevFlipH);
            m_widget->setCanvasContentFlipVertical(prevFlipV);
            m_widget->setSkipCursorOverlays(prevSkipCursors);
            m_widget->update();

            if (frame.isNull()) {
                return CaptureResult::fail(error(CanvasErrorCode::BackendFailure,
                    "the presented-frame readback produced no image"));
            }
            result.surface = pixelSurfaceFromImage(frame);
            if (result.surface.isNull()) {
                return CaptureResult::fail(error(CanvasErrorCode::OutOfMemory,
                    "could not allocate the presented-frame surface"));
            }
            return CaptureResult::ok(std::move(result));
        }

        CanvasResult<std::optional<QRect>> exportContentBounds() override
        {
            using BoundsResult = CanvasResult<std::optional<QRect>>;
            if (!ready()) {
                return BoundsResult::fail(
                    error(CanvasErrorCode::NotReady, "the engine session is not ready"));
            }
            QRect bounds;
            if (m_widget->computeExportContentBounds(bounds)) {
                return BoundsResult::ok(std::optional<QRect>(bounds));
            }
            // No exportable content is a fact, not a failure (plan 7.29.5).
            return BoundsResult::ok(std::nullopt);
        }

        CanvasResult<std::optional<QRect>> navigatorContentBounds() override
        {
            using BoundsResult = CanvasResult<std::optional<QRect>>;
            if (!ready()) {
                return BoundsResult::fail(
                    error(CanvasErrorCode::NotReady, "the engine session is not ready"));
            }
            QRect bounds;
            if (m_widget->computeNavigatorContentBounds(bounds)) {
                return BoundsResult::ok(std::optional<QRect>(bounds));
            }
            return BoundsResult::ok(std::nullopt);
        }

    private:
        bool ready() const { return m_widget && m_widget->isInitialized(); }

        static CanvasOperationError error(CanvasErrorCode code, const char* message)
        {
            CanvasOperationError out;
            out.code = code;
            out.message = QString::fromUtf8(message);
            out.diagnosticCode = QStringLiteral("AetherCapture");
            return out;
        }

        aether::OpenGLCanvasWidget* m_widget = nullptr;
    };

private:
    aether::OpenGLCanvasWidget* m_widget = nullptr;
    View m_view { m_widget };
    Painting m_painting { m_widget };
    Editing m_editing { m_widget };
    Transform m_transform { m_widget };
    HitTestingCapability m_hitTesting { m_widget };
    Presentation m_presentation { m_widget };
    Capture m_capture { m_widget };
};

// ==========================================================================
//   T R A N S I T I O N A L   F A C A D E S
// ==========================================================================

/// Forwards the history facade onto the legacy document undo manager
/// (plan 7.30.1).
class AetherHistoryFacade final : public CanvasHistoryFacade {
public:
    explicit AetherHistoryFacade(aether::OpenGLCanvasWidget* widget)
        : m_widget(widget)
    {
    }

    bool canUndo() const override { return m_widget->canvas().undoManager().canUndo(); }
    bool canRedo() const override { return m_widget->canvas().undoManager().canRedo(); }
    void undo() override { m_widget->canvas().undoManager().undo(); }
    void redo() override { m_widget->canvas().undoManager().redo(); }
    void setMemoryLimit(qint64 bytes) override
    {
        m_widget->canvas().undoManager().setMemoryLimit(bytes);
    }
    void beginTransaction(const QString& text) override
    {
        m_widget->canvas().undoManager().beginTransaction(text);
    }
    void endTransaction() override { m_widget->canvas().undoManager().endTransaction(); }
    void pushLegacyCommand(std::unique_ptr<aether::IUndoCommand> command) override
    {
        m_widget->canvas().undoManager().push(std::move(command));
    }
    aether::UndoManager* legacyUndoManager() override { return &m_widget->canvas().undoManager(); }

private:
    aether::OpenGLCanvasWidget* m_widget = nullptr;
};

/// Forwards the document-action facade onto the legacy widget's editing code
/// (plan 7.30.2).
class AetherDocumentFacade final : public CanvasDocumentFacade {
public:
    explicit AetherDocumentFacade(aether::OpenGLCanvasWidget* widget)
        : m_widget(widget)
    {
    }

    bool clearLayerPixelContent(const QUuid& layerId) override
    {
        return m_widget->clearLayerPixelContent(layerId);
    }
    bool rasterizeSmartLayer(const QUuid& layerId) override
    {
        return m_widget->rasterizeSmartLayerById(layerId);
    }
    bool convertLayerToSmartObject(const QUuid& layerId) override
    {
        return m_widget->convertLayerToSmartObjectById(layerId);
    }
    bool replaceSmartLayerContents(const QUuid& layerId,
        std::unique_ptr<aether::TileGrid> contentGrid, const QString& sourcePath,
        const QByteArray& sourceHash) override
    {
        return m_widget->replaceSmartLayerContents(
            layerId, std::move(contentGrid), sourcePath, sourceHash);
    }
    bool applySmartContentDocument(const QUuid& contentId,
        std::shared_ptr<ruwa::core::layers::SmartDocument> document) override
    {
        return m_widget->applySmartContentDocument(contentId, std::move(document));
    }
    bool applyLayerMask(const QUuid& layerId) override { return m_widget->applyLayerMask(layerId); }
    bool invertLayerMask(const QUuid& layerId) override
    {
        return m_widget->invertLayerMask(layerId);
    }
    bool applyLayerEffects(const QUuid& layerId) override
    {
        return m_widget->applyLayerEffects(layerId);
    }
    bool fillLayerMaskFromActiveSelection(const QUuid& layerId) override
    {
        return m_widget->fillLayerMaskFromActiveSelection(layerId);
    }
    bool copySelectionPixelsToClipboard(QImage* outFlattenedImage) override
    {
        return m_widget->copySelectionPixelsToClipboard(outFlattenedImage);
    }
    bool copyMergedSelectionPixelsToClipboard(QImage* outFlattenedImage) override
    {
        return m_widget->copyMergedSelectionPixelsToClipboard(outFlattenedImage);
    }

private:
    aether::OpenGLCanvasWidget* m_widget = nullptr;
};

// ==========================================================================
//   B I N D I N G
// ==========================================================================

AetherCanvasEngineQtBinding::AetherCanvasEngineQtBinding(
    const CanvasEngineCreateInfo& createInfo, QWidget* hostParent)
    : m_events(std::make_unique<CanvasEngineQtEvents>())
{
    // Construction and host-widget configuration live here and only here: the
    // rest of Ruwa sees a generic QWidget host plus the neutral session.
    m_widget = new aether::OpenGLCanvasWidget(hostParent);
    m_widget->setTabletTracking(true);
    m_widget->setAttribute(Qt::WA_AcceptTouchEvents, true);
    m_widget->setAcceptDrops(true);
    m_widget->setMinimumSize(200, 200);
    m_widget->setCanvasBoundsMode(createInfo.boundsMode);
    m_widget->setCanvas(static_cast<uint32_t>(createInfo.initialCanvasSize.width()),
        static_cast<uint32_t>(createInfo.initialCanvasSize.height()));
    m_widget->setLassoStabilization(createInfo.lassoStabilization);
    m_widget->setLassoFillStabilization(createInfo.lassoFillStabilization);

    // Rasterization confirmation is an application decision (plan 7.15.4):
    // forward the injected provider; the renderer has no dialog fallback.
    if (createInfo.rasterizationDecisionProvider) {
        m_widget->setRasterizationConfirmCallback(createInfo.rasterizationDecisionProvider);
    }

    // Live pointer sampling injection (plan 7.15.6): this binding maps the
    // system/native pointer into viewport-local coordinates so the renderer
    // never reads QCursor or the stylus service itself.
    aether::OpenGLCanvasWidget* widget = m_widget;
    ruwa::ui::workspace::CanvasPointerSource pointerSource;
    pointerSource.systemPointerViewportLocal = [widget]() -> std::optional<QPointF> {
        return QPointF(widget->mapFromGlobal(QCursor::pos()));
    };
    pointerSource.renderedPointerViewportLocal = [widget]() -> std::optional<QPointF> {
        if (!widget->isActiveWindow()) {
            // The rendered cursor belongs to this canvas interaction; while
            // another window is in front, the pointer is not ours to follow.
            return std::nullopt;
        }
        // The direct WinTab position while native routing owns the stylus,
        // the system pointer otherwise — the same source of truth the
        // application cursor manager uses.
        const auto nativePos
            = ruwa::services::input::StylusInputManager::instance().nativeCursorPosition();
        const QPoint globalPos = nativePos.value_or(QCursor::pos());
        const QPoint localPos = widget->mapFromGlobal(globalPos);
        if (!widget->rect().contains(localPos)) {
            return std::nullopt;
        }
        return QPointF(localPos);
    };
    m_widget->setPointerSource(pointerSource);

    m_session = std::make_unique<AetherCanvasSession>(m_widget);
    m_history = std::make_unique<AetherHistoryFacade>(m_widget);
    m_document = std::make_unique<AetherDocumentFacade>(m_widget);

    // Translate implementation events into renderer-neutral application
    // events. QOpenGLWidget's own signals stay behind this line. The
    // integration itself is not a QObject, so the static form is used.
    auto* events = m_events.get();
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::initialized, events,
        &CanvasEngineQtEvents::engineReady);
    QObject::connect(
        m_widget, &QOpenGLWidget::frameSwapped, events, &CanvasEngineQtEvents::framePresented);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::surfaceResized, events,
        &CanvasEngineQtEvents::viewportMetricsChanged);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::cameraZoomChanged, events,
        &CanvasEngineQtEvents::viewZoomChanged);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::cameraRotationChanged, events,
        &CanvasEngineQtEvents::viewRotationChanged);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::strokePainted, events,
        &CanvasEngineQtEvents::strokePainted);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::contentRegionChanged, events,
        &CanvasEngineQtEvents::contentRegionChanged);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::contentTilesChanged, events,
        &CanvasEngineQtEvents::contentTilesChanged);
    // Fill activity is the source of truth (plan 7.14.5); the transitional
    // per-layer event is derived here so existing Layers UI keeps working.
    // The sender is the host widget, so these connections die with it in
    // shutdown() before the captured adapter pointers can dangle.
    auto lastDerivedFillLayer = std::make_shared<QUuid>();
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::fillActivityChanged, events,
        [events, lastDerivedFillLayer](const CanvasFillActivityState& state) {
            events->publishFillActivity(state);
            const QUuid layerId = state.layer ? *state.layer : QUuid();
            if (layerId != *lastDerivedFillLayer) {
                *lastDerivedFillLayer = layerId;
                emit events->fillProcessingLayerChanged(layerId);
            }
        });
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::transformPresentationChanged, events,
        [events](const TransformPresentationState& state) {
            events->publishTransformPresentation(state);
        });
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::rendererFailed, events,
        [events](const QString& code, const QString& message) {
            events->publishEngineFailure(CanvasEngineDiagnostic { code, message });
        });
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::transformModeEntered, events,
        &CanvasEngineQtEvents::transformModeEntered);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::transformModeExited, events,
        &CanvasEngineQtEvents::transformModeExited);
    QObject::connect(m_widget, &aether::OpenGLCanvasWidget::backdropAvailabilityChanged, events,
        &CanvasEngineQtEvents::backdropAvailabilityChanged);

    // History events belong to the binding layer (plan 7.30.1): translate the
    // document undo manager signals here so feature code never connects to it.
    auto* undoManager = &m_widget->canvas().undoManager();
    QObject::connect(undoManager, &aether::UndoManager::canUndoChanged, events,
        &CanvasEngineQtEvents::historyCanUndoChanged);
    QObject::connect(undoManager, &aether::UndoManager::canRedoChanged, events,
        &CanvasEngineQtEvents::historyCanRedoChanged);
    QObject::connect(undoManager, &aether::UndoManager::indexChanged, events,
        &CanvasEngineQtEvents::historyIndexChanged);
    QObject::connect(undoManager, &aether::UndoManager::commandApplied, events,
        &CanvasEngineQtEvents::historyCommandApplied);

    // Effective view-state tracking (plan 7.14.2 / 7.32.2): compare complete
    // snapshots at the presentation synchronization point, so frame-sampled
    // pan and camera animation publish view changes too — not just the legacy
    // zoom/rotation signals. The snapshot builder reads the widget through the
    // adapter only; the implementation never escapes.
    auto lastSnapshot = std::make_shared<CanvasViewSnapshot>();
    auto revision = std::make_shared<uint64_t>(0);
    auto widgetSnapshot
        = [this]() -> CanvasViewSnapshot { return buildCurrentViewSnapshot(m_widget); };
    // Sender is the host widget, so these connections die with it in
    // shutdown() before the captured adapter pointer can dangle.
    QObject::connect(m_widget, &QOpenGLWidget::aboutToCompose, events,
        [this, events, widgetSnapshot, lastSnapshot, revision]() {
            CanvasViewSnapshot snapshot = widgetSnapshot();
            CanvasViewChange changes = CanvasViewChange::None;
            const CanvasViewState& prev = lastSnapshot->state;
            const CanvasViewState& next = snapshot.state;
            if (prev.cameraCenter != next.cameraCenter) {
                changes = canvasViewChangeJoin(changes, CanvasViewChange::Center);
            }
            if (prev.zoom != next.zoom) {
                changes = canvasViewChangeJoin(changes, CanvasViewChange::Zoom);
            }
            if (prev.rotationRadians != next.rotationRadians) {
                changes = canvasViewChangeJoin(changes, CanvasViewChange::Rotation);
            }
            if (prev.mirrorHorizontal != next.mirrorHorizontal
                || prev.mirrorVertical != next.mirrorVertical) {
                changes = canvasViewChangeJoin(changes, CanvasViewChange::Mirror);
            }
            if (prev.animating != next.animating
                || prev.fitAnimationActive != next.fitAnimationActive) {
                changes = canvasViewChangeJoin(changes, CanvasViewChange::Animation);
            }
            if (lastSnapshot->metrics.logicalSize != snapshot.metrics.logicalSize
                || lastSnapshot->metrics.surfaceWidth != snapshot.metrics.surfaceWidth
                || lastSnapshot->metrics.surfaceHeight != snapshot.metrics.surfaceHeight) {
                changes = canvasViewChangeJoin(changes, CanvasViewChange::Metrics);
            }
            if (changes != CanvasViewChange::None) {
                snapshot.revision = ++(*revision);
                *lastSnapshot = snapshot;
                events->publishViewState(snapshot, changes);
            } else {
                snapshot = *lastSnapshot;
            }
            events->publishPresentationSync(snapshot);
        });
}

AetherCanvasEngineQtBinding::~AetherCanvasEngineQtBinding()
{
    shutdown();
}

QWidget* AetherCanvasEngineQtBinding::viewportHostWidget() const
{
    return m_widget;
}

CanvasEngineSession& AetherCanvasEngineQtBinding::session()
{
    return *m_session;
}

CanvasEngineQtEvents& AetherCanvasEngineQtBinding::events()
{
    return *m_events;
}

CanvasHistoryFacade& AetherCanvasEngineQtBinding::history()
{
    return *m_history;
}

CanvasDocumentFacade& AetherCanvasEngineQtBinding::document()
{
    return *m_document;
}

ruwa::shared::rendering::ICanvasBackdropSource* AetherCanvasEngineQtBinding::backdropSource()
{
    return m_widget;
}

bool AetherCanvasEngineQtBinding::isShuttingDown() const
{
    return m_shuttingDown;
}

void AetherCanvasEngineQtBinding::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;

    // Deterministic teardown while the GL context is still valid: disconnect
    // everything, stop presenting, then destroy the host widget. The panel has
    // already detached it from its layout by this point.
    if (m_widget) {
        QObject::disconnect(m_widget, nullptr, nullptr, nullptr);
        m_widget->hide();
        delete m_widget;
        m_widget = nullptr;
    }
}

aether::OpenGLCanvasWidget* aetherLegacyRenderer(CanvasEngineQtBinding& binding)
{
    return static_cast<AetherCanvasEngineQtBinding&>(binding).m_widget;
}

} // namespace ruwa::ui::workspace
