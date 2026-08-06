// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   O P E N G L   C A N V A S   W I D G E T
// ==========================================================================

#ifndef AETHER_ENGINE_QT_OPENGL_CANVAS_WIDGET_H
#define AETHER_ENGINE_QT_OPENGL_CANVAS_WIDGET_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "features/canvas/CanvasBoundsMode.h"
#include "features/canvas/scene/Canvas.h"
#include "features/canvas/scene/Viewport.h"
#include "features/selection/LassoSelectionManager.h"
#include "shared/tiles/TileBrush.h"
#include "features/canvas/undo/DrawCommand.h"
#include "features/canvas/undo/LayerContentSwapCommand.h"
#include "features/transform/TransformController.h"
#include "features/transform/TransformTargetSet.h"
#include "features/canvas/quick-shape/QuickShapeMorph.h"
#include "features/canvas/stroke/BrushStrokeHost.h"
#include "features/canvas/overlays/CursorOverlayState.h"
#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/canvas/rendering/CanvasBackdropRenderer.h"
#include "features/canvas/rendering/SceneFboManager.h"
#include "features/canvas/selection/CanvasSelectionController.h"
#include "shared/undo/SelectionState.h"
#include "features/fill/FillTypes.h"
#include "features/fill/FloodFill.h"
#include "features/fill/GLFillRenderer.h"
#include "shared/rendering/CanvasBackdropSource.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
#include <QByteArray>
#include <QElapsedTimer>
#include <QTimer>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <QRectF>
#include <QSet>

#include <atomic>
#include <condition_variable>
#include <limits>
#include <chrono>
#include <array>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class QShowEvent;
class QOffscreenSurface;
class QThread;
class QEvent;

namespace aether {
class UndoManager;
class FillProgressPopupWidget;
class FillWorker;
} // namespace aether

namespace ruwa::core::layers {
class LayerModel;
struct LayerData;
struct SmartContent;
struct SmartDocument;
} // namespace ruwa::core::layers
namespace ruwa::core::brushes {
class IBrushEngineSession;
}
namespace ruwa::ui::widgets {
class CanvasMetricLabelOverlay;
}

namespace aether {

class GLRenderer;
class BrushCursorContourBuilder;
class BrushExecutionBackend;
class CanvasOverlayManager;
class GLSelectionRenderer;
class LayerScreenSourceCache;
struct CompositeLayerInfo;
struct TextEditOverlayState;

class OpenGLCanvasWidget : public QOpenGLWidget,
                           protected QOpenGLFunctions_4_5_Core,
                           public ruwa::shared::rendering::ICanvasBackdropSource {
    Q_OBJECT

public:
    enum class FillAlgorithm { Smart, Classic };

    explicit OpenGLCanvasWidget(QWidget* parent = nullptr);
    ~OpenGLCanvasWidget() override;

    // Canvas access
    Canvas& canvas() { return m_canvas; }
    const Canvas& canvas() const { return m_canvas; }
    void setCanvas(uint32_t width, uint32_t height);
    void setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode)
    {
        m_canvas.setBoundsMode(mode);
        if (m_fillPreview.active) {
            ++m_fillPreview.viewportRevision;
            m_fillPreview.finalCompositeDirty = true;
        }
    }
    ruwa::core::canvas::CanvasBoundsMode canvasBoundsMode() const { return m_canvas.boundsMode(); }
    bool hasFiniteDocumentBounds() const { return m_canvas.hasFiniteDocumentBounds(); }
    bool isInfiniteCanvas() const { return m_canvas.isInfiniteCanvas(); }

    // Viewport access
    Viewport& viewport() { return m_viewport; }
    const Viewport& viewport() const { return m_viewport; }

    // Mirror canvas pixels in the view only (camera pan/zoom/rotate unchanged).
    bool canvasContentFlipHorizontal() const { return m_canvasContentFlipHorizontal; }
    bool canvasContentFlipVertical() const { return m_canvasContentFlipVertical; }
    void setCanvasContentFlipHorizontal(bool flip);
    void setCanvasContentFlipVertical(bool flip);
    void toggleCanvasContentFlipHorizontal();
    void toggleCanvasContentFlipVertical();

    /// Export preview: draw as unmirrored without changing stored flip toggles.
    void setExportPreviewSuppressContentMirror(bool suppress);
    void setExportPreviewHideBoardLayers(bool hide);

    /// Screen → document/world (for painting, sampling, selection).
    Vector2 documentWorldFromScreen(const Vector2& screenPx) const;
    /// Document/world → on-screen position of that pixel.
    Vector2 screenFromDocumentWorld(const Vector2& documentWorld) const;
    /// VP × mirror — same matrix used to draw tiles and document overlays.
    std::array<float, 16> canvasContentViewProjectionMatrix() const;

    // Layer model integration
    void setLayerModel(ruwa::core::layers::LayerModel* model);
    ruwa::core::layers::LayerModel* layerModel() const { return m_layerModel; }

    /// Optional callback for rasterization confirmation. If set, used instead of QMessageBox.
    /// Signature: (title, message) -> true if user confirms, false if cancels.
    void setRasterizationConfirmCallback(std::function<bool(const QString&, const QString&)> fn);

    // Undo manager integration
    void setUndoManager(UndoManager* manager) { m_undoManager = manager; }
    UndoManager* undoManager() const { return m_undoManager; }
    UndoManager* activeUndoManager();
    const UndoManager* activeUndoManager() const;

    // Background color (area outside canvas)
    void setBackgroundColor(const Color& color);

    // Checker pattern colors (on canvas — transparency indicator)
    void setCheckerColors(const Color& color1, const Color& color2);
    void setCheckerSize(float size);

    // Brush access
    TileBrush& brush() { return *m_brush; }
    const TileBrush& brush() const { return *m_brush; }
    const ruwa::core::brushes::IBrushEngineSession* brushSession() const
    {
        return m_brushSession.get();
    }

    // Drawing state — brush writes to active layer's TileGrid
    bool isDrawing() const { return m_strokeHost && m_strokeHost->isDrawing(); }
    bool hasPendingStrokeFinalization() const
    {
        return m_strokeHost && m_strokeHost->hasPendingFinalization();
    }
    bool isQuickShapeTransformActive() const
    {
        return m_quickShapeMorph && m_quickShapeMorph->isActive();
    }
    void beginStroke(float worldX, float worldY, float pressure = 1.0f,
        BrushStrokeHost::StrokeInputDevice inputDevice
        = BrushStrokeHost::StrokeInputDevice::Stylus);
    void continueStroke(float worldX, float worldY, float pressure = 1.0f,
        BrushStrokeHost::StrokeInputDevice inputDevice
        = BrushStrokeHost::StrokeInputDevice::Stylus);
    void continueStrokeAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds,
        BrushStrokeHost::StrokeInputDevice inputDevice
        = BrushStrokeHost::StrokeInputDevice::Stylus);
    void queueStrokeAtElapsed(float worldX, float worldY, float pressure,
        float strokeElapsedSeconds,
        BrushStrokeHost::StrokeInputDevice inputDevice
        = BrushStrokeHost::StrokeInputDevice::Stylus);
    float strokeElapsedSecondsNow() const;
    void translateActiveStroke(float dx, float dy);
    void endStroke();
    /// Complete a released stroke's queued input, commit, and deferred readback
    /// before callers mutate the shared brush configuration.
    void flushPendingStrokeFinalization();

    // Lasso selection (UI plumbing, selection logic TBD)
    void beginLasso(
        float worldX, float worldY, bool addSelection = false, bool subtractSelection = false);
    void updateLasso(float worldX, float worldY);
    void endLasso(bool addSelection, bool subtractSelection);
    bool isLassoActive() const
    {
        return m_selectionController && m_selectionController->isLassoActive();
    }
    void setLassoStabilization(float stabilization);
    float lassoStabilization() const { return m_lassoStabilization; }

    // Lasso Fill (draw polygon, real-time fill preview, commit on release)
    void beginLassoFill(float worldX, float worldY);
    void updateLassoFill(float worldX, float worldY);
    void endLassoFill();
    void cancelLassoFill();
    bool isLassoFillActive() const { return m_lassoFillActive; }
    void setLassoFillStabilization(float stabilization);
    float lassoFillStabilization() const { return m_lassoFillStabilization; }

    // Rectangular (square) selection
    void beginRectSelection(
        float worldX, float worldY, bool addSelection = false, bool subtractSelection = false);
    void updateRectSelection(float worldX, float worldY);
    void endRectSelection(bool addSelection, bool subtractSelection);
    bool isRectSelectionActive() const
    {
        return m_selectionController && m_selectionController->isRectSelectionActive();
    }
    /// Live (normalized) rect-selection bounds in world/document coords during a drag.
    bool liveRectSelectionBoundsWorld(QRectF& out) const
    {
        return m_selectionController && m_selectionController->liveRectBoundsWorld(out);
    }

    // Circular selection
    void beginCircleSelection(
        float worldX, float worldY, bool addSelection = false, bool subtractSelection = false);
    void updateCircleSelection(float worldX, float worldY);
    void endCircleSelection(bool addSelection, bool subtractSelection);
    bool isCircleSelectionActive() const
    {
        return m_selectionController && m_selectionController->isCircleSelectionActive();
    }
    /// Queue a Magic Wand selection. Returns true when the request was accepted;
    /// the selection and its undo command are applied asynchronously.
    bool performMagicWandSelection(
        int worldX, int worldY, bool addSelection = false, bool subtractSelection = false);
    void translateActiveSelection(float dx, float dy);
    void clearSelectionMask();
    void selectActiveLayerContent();
    void selectActiveLayerMask();
    bool hasSelectionMask() const;
    bool selectionBoundsWorld(QRectF& outBounds) const;
    bool fillSelectionWithColor(const QColor& color);
    bool clearSelectionContent();
    /// Lift the pixels under the active selection out of the active raster layer
    /// into the edit clipboard; the layer itself is left untouched. Fills
    /// @p outFlattenedImage (when non-null) with the same region as an image for
    /// the system clipboard. False when there is no selection, the active layer
    /// is not a plain raster layer, or the region is empty.
    bool copySelectionPixelsToClipboard(QImage* outFlattenedImage = nullptr);
    /// Clear all pixels in a raster layer (undoable). Layer must be visible, unlocked, raster.
    bool clearLayerPixelContent(const QUuid& layerId);
    /// Bake generated pixel layer to raster (transform applied). Returns false if layer is missing
    /// or not rasterizable.
    bool rasterizeSmartLayerById(const QUuid& layerId);
    /// Wrap a layer's pixels into a smart object (undoable, the inverse of
    /// rasterizing). Returns false if the layer is missing or not convertible.
    bool convertLayerToSmartObjectById(const QUuid& layerId);
    /// Give a smart object new pixels, keeping its placement (undoable).
    ///
    /// This writes THROUGH the shared content, so every instance of the object
    /// shows the new pixels — replacing an object's contents is a statement
    /// about the object, not about one layer that shows it. @p sourcePath and
    /// @p sourceHash record where the content can later be REFRESHED from; the
    /// pixels themselves always stay in the document.
    bool replaceSmartLayerContents(const QUuid& layerId, std::unique_ptr<TileGrid> contentGrid,
        const QString& sourcePath, const QByteArray& sourceHash);
    /// Re-flatten every smart object whose nested document changed, then
    /// re-project the layers that show it.
    ///
    /// Cheap and idempotent: a content whose composite already matches its
    /// document costs one integer comparison, and a document-less (flat) content
    /// costs nothing at all. Call it after committing edits to a smart object's
    /// contents, or whenever a nested document may have changed. Needs a GL
    /// context, which it makes current itself — so never call it from paintGL;
    /// it defers instead when the renderer is not up yet.
    void refreshSmartContentComposites();
    /// Flatten ONE smart content now (and re-project the layers showing it).
    /// False when nothing had to be done, or when the composite could not run —
    /// see refreshSmartContentComposites for the context rules.
    bool recompositeSmartContent(
        const std::shared_ptr<ruwa::core::layers::SmartContent>& content);
    /// Install edited contents for the smart object identified by @p contentId
    /// and flatten them (undoable, content-level: every instance follows).
    ///
    /// This is what a smart object's contents tab commits through. The placement
    /// of each instance is deliberately preserved — editing an object's contents
    /// does not move the object. Returns false when no layer here shows those
    /// contents, or when the flattening could not run: nothing is changed then,
    /// so the caller can keep its edits and retry.
    bool applySmartContentDocument(
        const QUuid& contentId, std::shared_ptr<ruwa::core::layers::SmartDocument> document);
    /// Bake a layer's mask into its pixels and remove the mask (undoable).
    /// Layer must be an editable raster layer carrying a mask.
    bool applyLayerMask(const QUuid& layerId);
    /// Invert a layer mask (reveal -> 1 - reveal) across every tile and its
    /// infinite background. Undoable. Raster layer with a mask required.
    bool invertLayerMask(const QUuid& layerId);
    /// Bake a raster layer's effect chain into its pixels and clear the chain
    /// (undoable). No-op (returns false) without a raster layer carrying at
    /// least one effect.
    bool applyLayerEffects(const QUuid& layerId);
    /// Bake the active selection into the layer's (already created) mask: inside
    /// the selection becomes white/visible, outside becomes black/hidden, soft
    /// selection edges preserved. No-op (returns false) without an active
    /// selection. Dirties the affected region; the caller owns undo + notify.
    bool fillLayerMaskFromActiveSelection(const QUuid& layerId);
    bool flipSelectionHorizontally();
    bool flipSelectionVertically();

    /// Fill tool: flood fill at (worldX, worldY) with brush color. Returns true if pixels were
    /// filled.
    bool performFill(int worldX, int worldY);
    bool performClassicFill(int worldX, int worldY);
    void cancelFillPreview();
    bool isFillPreviewActive() const
    {
        return m_fillPreview.active || m_activeFillWorkerRequest != 0;
    }

    // State
    bool isInitialized() const { return m_initialized; }
    uint32_t surfaceWidth() const { return static_cast<uint32_t>(width()); }
    uint32_t surfaceHeight() const { return static_cast<uint32_t>(height()); }

    // Request redraw
    void requestRender();
    void notifyCanvasInteraction(bool canvasEdited = false);

    // VSync-synchronous camera panning. Reads cursor position in paintGL
    // (directly from the OS via QCursor::pos) instead of reacting to each
    // QMouseEvent — avoids beat-pattern judder when mouse poll rate and
    // display refresh rate are not evenly matched (e.g. 125Hz vs 200Hz).
    void beginPanSampling();
    void endPanSampling();
    bool isPanSampling() const { return m_panSamplingActive; }

    // Transform mode
    bool isTransformActive() const { return m_transformController.isActive(); }
    bool transformUsesSelectionMask() const
    {
        return m_transformController.isActive() && m_transformController.usesSelectionMask();
    }
    bool isMoveOnlyTransform() const { return m_moveOnlyTransform; }
    bool isAutoApplyingTransform() const { return m_autoApplyingTransform; }
    aether::TransformInteractionMode transformInteractionMode() const
    {
        return m_transformController.interactionMode();
    }
    void setTransformInteractionMode(aether::TransformInteractionMode mode);
    void enterTransformMode();
    /// Enter transform for Move tool: translation only, no overlay. Returns true if entered.
    bool enterMoveOnlyTransformMode();
    /// Topmost move-tool content rectangle at world position. Locked/unsupported hits block lower
    /// layers.
    QUuid moveToolContentLayerAt(const Vector2& worldPos) const;
    void confirmTransform();
    /// @param moveOnlyStateForOverlay When set (e.g. confirmTransform after clearing the flag),
    /// used for overlay exit animation.
    void cancelTransform(std::optional<bool> moveOnlyStateForOverlay = std::nullopt);
    void beginTransformUndoStep();
    /// Freeze canvas/layer snap targets and current editor settings for this drag.
    void beginTransformSnapSession();
    /// Refresh every transform capsule: the snap spacing labels and the live
    /// move/rotate/scale readout for the handle currently being dragged.
    void syncTransformMetricOverlays();
    void commitTransformUndoStep();
    void discardTransformUndoStep();
    TransformController& transformController() { return m_transformController; }
    const TransformController& transformController() const { return m_transformController; }
    bool isSelectionCopyMoveTransform() const { return m_selectionCopyMoveTransform; }
    bool latchSelectionCopyMoveTransformIfNeeded(const Vector2& worldPos);

    /// Rebuild the transform source atlas from the active layer's current tile grid.
    /// Used to update the preview after text re-rasterization at a different scale.
    void rebuildTransformAtlas();

    // Force-complete pending transform finalization (call before destruction)
    void flushPendingTransformFinalization();

    /// Brush cursor (GL-rendered with inversion). Position in widget coords, radius in screen
    /// pixels.
    void setBrushCursorState(bool visible, float centerX, float centerY, float radiusPx);
    bool isBrushCursorVisible() const { return m_cursorOverlayState.brushVisible; }

    /// Eyedropper cursor (GL-rendered sampled/selected swatches with an invert border).
    void setEyedropperCursorState(bool visible, float centerX, float centerY,
        const QColor& selectedColor = QColor(0, 0, 0, 255));

    /// Sample color from the rendered scene texture at world position (what the user sees).
    /// Returns true if sampling succeeded and out is filled; false if scene FBO unavailable.
    bool sampleColorFromScene(float worldX, float worldY, QColor& out);

    /// Render the full canvas to an image (for export). Returns null QImage if GL not ready.
    QImage grabCanvasImage();
    QImage grabCanvasImage(const QRect& worldRect);
    QImage renderCompositedRegion(const QRect& worldRect, const QSize& targetSize);
    bool computeExportContentBounds(QRect& outBounds);
    bool computeNavigatorContentBounds(QRect& outBounds);

    /// Update brush cursor stamp contour from current brush (call when brush changes).
    void updateBrushCursorStamp();

    /// When true, paintGL skips brush/eyedropper cursor overlays (for thumbnail capture).
    void setSkipCursorOverlays(bool skip) { m_skipCursorOverlays = skip; }

    // Canvas resize overlay (GL-rendered)
    void setCanvasResizeOverlayState(
        bool active, const QRectF& selectionWorldRect, bool selectingOrMoving);
    void setTextEditOverlayState(const TextEditOverlayState& state);

    using BackdropRegionProvider = std::function<std::vector<CanvasBackdropRegion>()>;
    void setBackdropRegionProvider(BackdropRegionProvider provider);

    // --- ICanvasBackdropSource: coordinates QWidget chrome with GPU backdrop ---
    bool backdropAvailable() const override;
    void requestBackdropUpdate() override;

signals:
    /// Emitted once the GPU backdrop pipeline becomes ready.
    void backdropAvailabilityChanged();
    /// Emitted when a paint stroke (not erase) is completed. Use to add color to recent palette.
    void strokePainted();
    void contentRegionChanged(const QRect& worldRect);
    void contentTilesChanged(const QList<QPoint>& tilePositions);
    void fillProcessingLayerChanged(const QUuid& layerId);
    void surfaceResized(uint32_t width, uint32_t height);
    void initialized();
    void transformModeEntered();
    void transformModeExited(bool applied);
    void cameraZoomChanged(qreal zoom);
    void cameraRotationChanged(qreal rotationRadians);

protected:
    bool event(QEvent* event) override;
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    friend class FillWorker;

    void rebuildSmartProjectionCacheForLayer(const QUuid& layerId);
    void rebuildLayerProjectionCaches();
    void purgeStaleCompositionCacheTiles();
    std::shared_ptr<TileGrid> buildSmartProjectedGrid(const ruwa::core::layers::LayerData* layer);

    /// Bakes a smart layer's CONTENT-SPACE filters into a throwaway clone of its
    /// content, which then becomes the source of the projection. Null when there
    /// is nothing to bake (no such filters, empty content, no renderer) — the
    /// projection then reads the content grid itself, as it always did.
    std::shared_ptr<TileGrid> buildContentSpaceEffectedGrid(
        const ruwa::core::layers::LayerData* layer);

    // Bakes the layer's effect chain into a throwaway clone of its raw content
    // and returns it, so a content selection can trace the effect-processed
    // silhouette. Returns nullptr for non-raster layers or layers with no
    // effects (caller then selects the raw content).
    std::shared_ptr<TileGrid> buildEffectShapedSelectionGrid(
        const ruwa::core::layers::LayerData* layer);

    std::unique_ptr<LayerCompositingBuilder> m_layerCompositingBuilder;

    // Get the active (selected) layer's TileGrid for brush drawing
    bool ensurePaintableActiveLayer();

    /// Rasterize generated pixel layer in-place. Caller must ensure layer is rasterizable.
    void rasterizeSmartLayer(ruwa::core::layers::LayerData* layer);

    /// Wrap a layer's pixels into a smart object in-place. False when the layer
    /// is not convertible (see LayerData::canConvertToSmartObject).
    bool convertLayerToSmartObject(ruwa::core::layers::LayerData* layer);

    /// Move a group's children into a smart object's nested document, in place.
    ///
    /// The group layer itself becomes the smart layer: its children leave the
    /// model and become the contents, so the object can be re-opened and edited
    /// with its stack intact. Structural removal and the content swap are one
    /// undo transaction — a redo that replayed only half of it would leave the
    /// children duplicated. Called by convertLayerToSmartObject for groups.
    bool convertGroupToSmartObject(ruwa::core::layers::LayerData* layer);

    /// Wrap a text layer into a smart object whose nested document holds the
    /// TEXT, still editable, in place.
    ///
    /// Two things separate this from the raster path. The glyphs are not baked —
    /// the contents tab opens on a real text layer, the way Photoshop keeps text
    /// editable inside a smart object. And the nested canvas is the text's own
    /// box rather than everything from the document origin out to it: the text is
    /// moved to the content-space origin and the layer's placement carries the
    /// offset back, so the object sits exactly where the text did while its
    /// frame hugs the glyphs. Called by convertLayerToSmartObject for text.
    bool convertTextToSmartObject(ruwa::core::layers::LayerData* layer);

    /// Shared tail of both conversions: refresh the projection caches, notify
    /// the model and push the undo command that swaps the displaced state back.
    void finishLayerContentSwap(
        ruwa::core::layers::LayerData* layer, LayerContentState replacedState, const QString& text);

    /// Re-place and re-project every layer showing @p contentId after its pixels
    /// were replaced. Each layer keeps its own placement, so this is per-layer
    /// work driven by a content-level change.
    ///
    /// @p refitPlacement fits each instance's placement box to the new content
    /// bounds — right when the object became a DIFFERENT image (Replace
    /// Contents), wrong when its own contents were merely edited: refitting
    /// there would shift and rescale every instance on each brush stroke inside
    /// the object.
    void refreshLayersForSmartContent(const QUuid& contentId, bool refitPlacement = true);

    /// If generated pixel layer has selection: show rasterize dialog. Returns false if user
    /// cancels.
    bool confirmRasterizeForSelectionTransform(
        ruwa::core::layers::LayerData* layer, bool hasSelection);
    /// Confirm and rasterize one generated pixel layer when selection transform requires raster
    /// pixels.
    bool offerRasterizeForSelectionTransform(
        ruwa::core::layers::LayerData* layer, bool hasSelection);
    /// Confirm all multi-target generated pixel layers first, then rasterize them together.
    bool offerRasterizeForSelectionTransformTargets(bool hasSelection);
    TileGrid* activeLayerTileGrid() const;
    ruwa::core::layers::LayerData* activeLayer() const;
    bool startAnimatedSelectionFlip(bool flipHorizontal, bool flipVertical);

    // Update tile position index after brush stroke
    void updateTileIndex(const ruwa::core::layers::LayerData* layer,
        const std::unordered_set<TileKey, TileKeyHash>& dirtyKeys);

    // Destroy GPU textures in stroke buffer (must be called in GL context)
    void cleanupStrokeTextures();
    uint32_t effectiveDocumentBoundsWidth() const;
    uint32_t effectiveDocumentBoundsHeight() const;

    void ensureFillProgressPopup();
    QPoint fillProgressPopupAnchorPoint() const;
    QPoint fillProgressPopupTopLeft() const;
    void updateFillProgressPopupPosition();
    void showClassicFillWaitPopup();
    void showFillProgressPopupDone(const QPoint& anchorPoint);
    void hideFillProgressPopupImmediate();

    bool selectionPathNeedsCatchup(float targetX, float targetY, float stabilization) const;
    void resetSelectionPathStabilizer();
    void updateStabilizerCatchupTimer();
    void processStabilizerCatchup();

    // --- Deferred transform finalization ---
    void finalizeTransform();
    bool tryFinalizeTransform(bool forceWait);
    bool hasPendingSelectionTransform() const;
    const TransformState* selectionDisplayTransformState() const;
    bool transformViewportPreviewSupportsViewportPath(const TransformState& state) const;
    void refreshTransformViewportPreviewCapabilities();
    void activateTransformViewportPreview(const QUuid& targetLayerId, const QUuid& sourceLayerId);
    void clearTransformViewportPreview();
    void clearTransformPreviewCacheTiles(const Rect& currentBounds);
    void invalidateTransformViewportPreviewTransform();
    void invalidateTransformViewportPreviewSource();
    void invalidateTransformViewportPreviewSelectionMask();
    bool enterSelectedTransformMode(bool moveOnly);
    void createTransformUndoStack();
    void destroyTransformUndoStack();
    void onTransformUndoStateRestored();
    bool latchLayerCopyMoveTransform();
    void commitLayerCopyMoveAddUndo();
    void discardLayerCopyMoveDuplicates();
    void clearLayerCopyMoveState();

    // paintGL helpers
    void paintGL_updateCameraAndEmitSignals();
    void paintGL_markTransformDirty();
    void paintGL_runComposite(const std::vector<CompositeLayerInfo>& layerStack);
    void paintGL_renderSceneAndBlit(GLuint& outSceneTarget, GLint defaultFbo,
        bool needSceneForOverlay, const std::vector<CompositeLayerInfo>& boardLayerStack);
    void paintGL_renderOverlays(GLuint sceneTarget);
    /// Downsample, blur and composite the current visible QWidget regions.
    /// Samples the default framebuffer, so it must run after every pass that
    /// writes canvas pixels (including the screen-space previews).
    void paintGL_renderBackdrop(GLint defaultFbo);
    void paintGL_processSelectionReadback();
    void paintGL_renderLassoOverlay();
    void paintGL_renderTransformViewportPreview(const std::vector<CompositeLayerInfo>& layerStack,
        const std::vector<CompositeLayerInfo>& boardLayerStack, GLint defaultFbo);
    void paintGL_renderFillPreviewOverlay(
        const std::vector<CompositeLayerInfo>& layerStack, GLuint sceneTarget, GLint defaultFbo);
    void paintGL_renderLassoFillOverlay(const std::vector<CompositeLayerInfo>& layerStack,
        const std::vector<CompositeLayerInfo>& boardLayerStack, GLint defaultFbo);
    // Resolves a painted layer mask to the standard viewport. GLViewportCompositor
    // samples this texture in the layer's final blend, after the effect chain.
    GLuint acquireLayerMaskTextureForPreview(
        const CompositeLayerInfo& layer, bool flipH, bool flipV, uint64_t viewportRevision);
    void renderBoardLayers(const std::vector<CompositeLayerInfo>& boardLayerStack);
    void invalidateCachedLayerStacks();
    void invalidateBoardCompositionCache();
    void clearBoardCompositionCache();
    bool layerAffectsBoardComposition(const ruwa::core::layers::LayerData* layer) const;
    bool isBoardCompositionLayerId(const QUuid& id) const;
    void markBoardCompositionTilesDirty(const QUuid& layerId, const std::vector<TileKey>& keys);
    void markBoardCompositionTilesDirty(
        const QUuid& layerId, const std::unordered_set<TileKey, TileKeyHash>& keys);
    void updateBoardCompositionTransientDirty();
    bool updateCanvasCornerEffectState();
    bool isCanvasFullyVisible(float marginPx = 0.0f) const;
    void scheduleCanvasCornerEffectUpdate(qint64 delayMs);
    float canvasCornerRadiusCanvasPx() const;

    bool performLassoFill(const std::vector<Vector2>& polygon);
    FloodFillResult::RawTileMap buildLassoFillScreenMask(const std::vector<Vector2>& polygon) const;
    void cancelPendingLassoFillCommit(const QUuid& layerId = QUuid());
    void handlePendingLassoFillResult(uint64_t sequence, const QUuid& layerId,
        SelectionRestoreContext selectionRestore, FloodFillResult result);
    void scheduleLassoFillPreviewRefresh();
    void refreshLassoFillPreview();
    void clearLassoFillPreview(bool markDirtyTiles = true);
    using FillPreviewRawTileMap = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;
    void startAsyncFillSession(const QUuid& layerId, FillAlgorithm algorithm,
        FillPreviewRawTileMap layerSnapshotTiles, FillPreviewRawTileMap selectionMaskTiles,
        FillPreviewRawTileMap initialPreviewTiles, FillPreviewRawTileMap initialMaskTiles,
        FloodFillResult initialPendingResult, SelectionRestoreContext selectionRestore,
        FillOrigin origin, FillColor color, FillCanvasBounds canvasBounds, bool maskTarget = false,
        bool forceFinalResultOnly = false, bool waitForExternalResultOnly = false);
    void scheduleDeferredFillKickoff(const QUuid& layerId, FillAlgorithm algorithm,
        SelectionRestoreContext selectionRestore, FillOrigin origin, FillColor color,
        FillCanvasBounds canvasBounds, bool maskTarget = false, bool forceFinalResultOnly = false);
    void executeDeferredFillKickoff(uint64_t sequence);
    void initializeFillWorker();
    void shutdownFillWorker();
    void prewarmOneTimeGpuPaths();
    void handleFillWorkerResult(uint64_t requestSequence, const QUuid& layerId,
        SelectionRestoreContext selectionRestore, FloodFillResult result, FillOrigin origin,
        FillColor color, FillCanvasBounds canvasBounds);
    void enqueueFillPreviewBatches(const FillPreviewRawTileMap& previewTiles,
        const FillPreviewRawTileMap& maskTiles, int originX, int originY);
    bool applyPendingFillPreviewBatches();
    void adoptCompletedFillResult();
    void beginFillPreviewAnimation(FloodFillResult&& result);
    void retargetFillPreviewReveal(float newReadyRadius);
    bool applyFloodFillResult(const QUuid& layerId, FloodFillResult&& result,
        std::optional<SelectionRestoreContext> selectionRestore, bool maskTarget = false);
    bool commitFillPreviewResult();
    void stopFillPreview(bool cancelWorker = true, bool hidePopup = true);
    bool updateFillPreviewAnimationState();
    void failFillPreviewGpuPipeline();
    void releaseFillPreviewGpuResources();
    void flushPendingFillPreviewTextureDeletes();
    QUuid currentFillProcessingLayerId() const;
    void syncFillProcessingLayerSignal();

    // Called by CanvasSelectionController for fill/clear operations
    bool doFillSelectionWithColor(const QColor& color);
    bool doClearSelectionContent();

private slots:
    // LayerModel signal handlers
    void onLayersChanged();
    void onLayerDataChanged(const QUuid& id);
    void onLayerEffectResultChanged(const QUuid& id, quint64 revision);
    void onLayerRemoved(const QUuid& id);

private:
    bool updateBoundsEffectInvalidationState(
        const QUuid& id, const ruwa::core::layers::LayerData* layer);
    void dirtyClippedLayerDependents(const QUuid& id);
    void synchronizeCompositionForReadback();

    std::unique_ptr<GLRenderer> m_renderer;
    std::unique_ptr<LayerScreenSourceCache> m_layerScreenSourceCache;

    // Scene
    Canvas m_canvas;
    Viewport m_viewport;

    // Layer model (non-owning)
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;

    std::function<bool(const QString&, const QString&)> m_rasterizationConfirmCallback;
    QHash<QUuid, std::shared_ptr<TileGrid>> m_smartProjectedGrids;
    /// Layers whose cached projection has content-space filters baked into it.
    /// Without this the removal of the last such filter would leave the baked
    /// projection in place: the compositor's chain changed, but the pixels it
    /// runs on did not.
    QSet<QUuid> m_smartContentEffectProjections;
    /// A composite refresh was asked for before the GL context existed. The
    /// content still holds its last valid pixels, so nothing is broken in the
    /// meantime — the sweep is simply re-run once there is a renderer.
    bool m_smartCompositeRefreshPending = false;
    CompositionCache m_boardCompositionCache;
    bool m_boardCompositionCacheDirty = true;
    std::unordered_set<TileKey, TileKeyHash> m_boardCompositionKeys;
    QSet<QUuid> m_boardCompositionLayerIds;
    // Per-layer flag: did the layer have an enabled bounds-expanding effect (blur)
    // in its last observed effect state? Toggling/removing such an effect drops its
    // pad to 0, so effect invalidation would otherwise only dirty the layer's own
    // tiles and leave the now-stale bleed ("ghost") on the expanded neighbour tiles.
    // The flag forces a full (viewport-culled) cache invalidation across the transition.
    QHash<QUuid, bool> m_layerHadBoundsEffect;

    // Undo manager (non-owning)
    UndoManager* m_undoManager = nullptr;

    // Brush
    std::unique_ptr<ruwa::core::brushes::IBrushEngineSession> m_brushSession;
    TileBrush* m_brush = nullptr;
    float m_lastStrokeTargetX = 0.0f;
    float m_lastStrokeTargetY = 0.0f;
    ruwa::core::brushes::StrokeStabilizerState m_lassoStabilizerState;
    QElapsedTimer m_stabilizerElapsedTimer;
    QTimer m_stabilizerCatchupTimer;
    float m_lassoStabilization = 0.0f;
    float m_lassoFillStabilization = 0.0f;
    std::unique_ptr<BrushStrokeHost> m_strokeHost;

    // Brush cursor contour builder (debounced, off-thread; matches dab transform).
    std::unique_ptr<aether::BrushCursorContourBuilder> m_brushCursorContourBuilder;

    // Selection (lasso, rect, circle) — delegated to controller
    std::unique_ptr<CanvasSelectionController> m_selectionController;

    /// Alpha-lock mask: layer alpha (like select content), combined with selection when both active
    mutable TileGrid m_alphaLockMaskGrid;

    /// Returns the effective paint mask: selection only, or layer alpha (alpha lock), or
    /// min(selection, layer alpha)
    TileGrid* getEffectivePaintMask(ruwa::core::layers::LayerData* layer, TileGrid* grid) const;
    bool shouldPreserveAlphaForPaintMask(
        const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) const;
    void buildAlphaLockCombinedMask(
        const TileGrid* selectionMask, const TileGrid* layerAlphaGrid) const;

    std::unique_ptr<QuickShapeMorph> m_quickShapeMorph;

    // --- Selection undo: capture before lasso/rect/circle, push on end ---
    SelectionState m_selectionAtLassoBegin;
    SelectionState m_lastSelectionState;
    bool m_ignoreSelectionChange = false;
    quint64 m_magicWandRequestSequence = 0;

    SelectionRestoreContext buildCurrentSelectionRestore();
    void pushSelectionCommand(const SelectionState& before, const SelectionState& after);
    void onLayerSelectionChanged(const ruwa::core::layers::LayerId&);

    QTimer m_selectionTick;

    // Background color (dark, outside canvas)
    Color m_backgroundColor { 0.102f, 0.102f, 0.102f, 1.0f }; // ~rgb(26,26,26)

    // Checkerboard colors (on canvas, from theme: canvas/canvasGrid)
    Color m_checkerColor1 { 0.235f, 0.235f, 0.235f, 1.0f };
    Color m_checkerColor2 { 0.314f, 0.314f, 0.314f, 1.0f };
    float m_checkerSize = 8.0f;

    bool m_canvasContentFlipHorizontal = false;
    bool m_canvasContentFlipVertical = false;
    bool m_exportPreviewSuppressContentMirror = false;
    bool m_exportPreviewHideBoardLayers = false;

    // True while a replace-mode stroke (blur/smudge/liquify/wet) on a layer with
    // a preview-disabled effect is suppressing that effect across the visible
    // region. Tracked to detect the suppress-state transition and invalidate the
    // visible composite tiles exactly once when it flips. See paintGL_runComposite.
    bool m_strokeEffectSuppressed = false;

    bool effectiveContentFlipH() const;
    bool effectiveContentFlipV() const;

    bool m_initialized = false;

    // Transform mode
    TransformController m_transformController;
    TransformTargetSet m_transformTargetSet;
    std::vector<ruwa::ui::widgets::CanvasMetricLabelOverlay*> m_transformSnapMetricLabels;
    /// Live readout of the drag in progress (offset / angle / scale percentage).
    ruwa::ui::widgets::CanvasMetricLabelOverlay* m_transformDragMetricLabel = nullptr;
    /// Transformed corners latched when the drag started; the readout above is
    /// measured against them, so it works for quad/mesh modes too.
    std::optional<std::array<Vector2, 4>> m_transformDragStartCorners;
    void syncTransformSnapMetricLabels();
    void syncTransformDragMetricLabel();
    /// Keep the drag readout glued to the cursor while the drag is in progress.
    void refreshTransformDragMetricAnchor();
    std::unique_ptr<UndoManager> m_transformUndoManager;
    std::optional<TransformState> m_transformUndoStepBefore;
    std::optional<TransformInteractionMode> m_transformUndoStepBeforeMode;
    bool m_moveOnlyTransform = false; // Move tool: transform without overlay
    // Active transform session is editing the selected layer's mask grid (the
    // mask is warped, the layer pixels stay fixed) rather than its content.
    // Set on entry when the target layer has maskEditActive; consumed by the
    // confirm/finalize grid selection and the screen-space preview.
    bool m_transformEditingMask = false;
    bool m_selectionCopyMoveTransform = false;
    bool m_layerCopyMoveTransform = false;
    QList<ruwa::core::layers::LayerId> m_layerCopyMoveAddedIds;
    QList<std::shared_ptr<ruwa::core::layers::LayerData>> m_layerCopyMoveAddedLayers;
    QList<std::pair<ruwa::core::layers::LayerId, int>> m_layerCopyMoveAddedPositions;
    bool m_autoApplyingTransform = false;
    uint64_t m_autoApplyTransformSequence = 0;

    // --- Previous-frame transform dirty region (for trail cleanup) ---
    bool m_prevTransformDirtyValid = false;
    int32_t m_prevTransformMinTX = 0, m_prevTransformMinTY = 0;
    int32_t m_prevTransformMaxTX = 0, m_prevTransformMaxTY = 0;

    // --- Deferred transform finalization ---
    struct PendingTransformFin {
        bool active = false;
        QUuid layerId;
        std::unordered_set<TileKey, TileKeyHash> resultKeys;
        std::vector<TileKey> readbackKeysOrdered;
        std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> beforeTiles;
        std::unordered_set<TileKey, TileKeyHash> beforeKeys;
        std::unordered_set<TileKey, TileKeyHash> createdTiles;
        std::unordered_set<TileKey, TileKeyHash> removedTiles;
        GLsync fence = nullptr;
        bool maskTarget = false; // readback/undo apply into layer maskGrid, not pixels
        bool applySelectionMask = false;
        GLsync selectionFence = nullptr;
        std::vector<TileKey> selectionReadbackKeysOrdered;
        TransformState selectionTransformState {};
        LassoSelectionState selectionBefore {};
    };
    PendingTransformFin m_pendingTransform;
    QTimer m_transformFinalizeTimer;

    struct TransformViewportPreviewState {
        bool active = false;
        bool viewportPathEnabled = false;
        QUuid targetLayerId;
        QUuid sourceLayerId;
        uint64_t viewportRevision = 0;
        bool viewportDirty = true;
        bool transformDirty = true;
        bool sourceDirty = true;
        bool selectionMaskDirty = true;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        Vector2 cameraPosition {};
        float cameraZoom = 0.0f;
        float cameraRotation = 0.0f;
        bool flipH = false;
        bool flipV = false;
        // Source-overscan pad latched for this preview session (screen px, already
        // quantised). The pad a transform needs grows with the drag offset, and the
        // screen-source cache keys on texture size — letting the pad follow the
        // offset px-for-px reallocated and fully re-rendered every transform target
        // on every drag frame. Latching the running maximum (reset whenever the
        // camera/viewport changes, which invalidates the cache anyway) keeps the
        // source size stable so those renders are reused across frames.
        uint32_t sourceOverscanPadX = 0;
        uint32_t sourceOverscanPadY = 0;
    };
    TransformViewportPreviewState m_transformViewportPreview;

    // Lasso Fill (separate from selection lasso — no overlay, real-time fill preview)
    bool m_lassoFillActive = false;
    std::vector<Vector2> m_lassoFillPoints;
    bool m_lassoFillPreviewRefreshQueued = false;
    struct LassoFillPreviewState {
        bool active = false;
        QUuid targetLayerId;
        uint64_t revision = 0;
        Rect bounds {};
        std::vector<Vector2> polygon;
        Color color {};
    };
    mutable LassoFillPreviewState m_lassoFillPreview;
    struct PreviewViewportSessionState {
        bool active = false;
        QUuid targetLayerId;
        uint64_t viewportRevision = 0;
        std::vector<Vector2> polygonWorld;
        std::vector<Vector2> polygonScreen;
        QRect clippedScreenBounds;
        bool screenPolygonDirty = true;
        bool screenMaskDirty = true;
        GLuint screenMaskTexture = 0;
        QRect screenMaskBounds;
        bool screenSourcesDirty = true;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        Vector2 cameraPosition {};
        float cameraZoom = 0.0f;
        float cameraRotation = 0.0f;
        bool flipH = false;
        bool flipV = false;
    };
    PreviewViewportSessionState m_lassoFillViewportPreview;
    struct LassoFillCommitState {
        struct AsyncJob {
            std::atomic<bool> cancelled { false };
        };

        uint64_t sequence = 0;
        QUuid targetLayerId;
        std::shared_ptr<AsyncJob> job;
    };
    LassoFillCommitState m_lassoFillCommit;

    struct FillPreviewState {
        struct ProgressBatch {
            int minTileX = 0;
            int minTileY = 0;
            int maxTileX = -1;
            int maxTileY = -1;
            float minRadius = 0.0f;
            float maxRadius = 0.0f;
            float tileStartRadius = 0.0f;
            float contentMinRadius = 0.0f;
            float contentMaxRadius = 0.0f;
            int pixelCount = 0;
            std::vector<TileKey> keys;
            FillPreviewRawTileMap previewTiles;
            FillPreviewRawTileMap maskTiles;
        };

        bool active = false;
        FillAlgorithm algorithm = FillAlgorithm::Smart;
        bool finalResultOnly = false;
        bool maskTarget = false;
        // Pixel format of the CONTENT (source/result) tiles for this fill: the
        // format of the grid being filled (document layer vs RGBA8 mask).
        aether::TilePixelFormat contentFormat = aether::kDefaultTileFormat;
        bool previewActive = false;
        bool awaitingResult = false;
        bool easeActive = false;
        QUuid targetLayerId;
        std::shared_ptr<TileGrid> previewContentGrid;
        std::shared_ptr<TileGrid> fillMaskGrid;
        std::unordered_set<TileKey, TileKeyHash> affectedKeys;
        std::deque<ProgressBatch> queuedBatches;
        QUuid sourceCacheId;
        uint64_t contentRevision = 0;
        uint64_t viewportRevision = 0;
        bool finalCompositeDirty = true;
        bool gpuPipelineFailed = false;
        GLuint finalCompositeTexture = 0;
        uint32_t finalCompositeWidth = 0;
        uint32_t finalCompositeHeight = 0;
        QRect affectedDocumentBounds;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        Vector2 cameraPosition {};
        float cameraZoom = 0.0f;
        float cameraRotation = 0.0f;
        bool flipH = false;
        bool flipV = false;
        Vector2 origin {};
        float minRevealRadius = 0.0f;
        float readyRadius = 0.0f;
        float displayRadius = 0.0f;
        float revealSpeedPxPerMs = 0.0f;
        float easeStartRadius = 0.0f;
        float easeTargetRadius = 0.0f;
        float feather = 0.0f;
        int durationMs = 160;
        int appliedPixelCount = 0;
        float appliedMinRadius = 0.0f;
        float appliedMaxRadius = 0.0f;
        bool metricsDirty = false;
        QElapsedTimer timer;
        qint64 lastAnimationMs = 0;
        qint64 easeStartMs = 0;
        FloodFillResult pendingResult;
        SelectionRestoreContext selectionRestore {};
        struct AsyncJob {
            std::atomic<bool> cancelled { false };
            std::atomic<bool> done { false };
            std::mutex resultMutex;
            std::deque<ProgressBatch> pendingBatches;
            FloodFillResult result;
        };
        std::shared_ptr<AsyncJob> job;
    };
    FillPreviewState m_fillPreview;
    std::vector<GLuint> m_pendingFillPreviewTextureDeletes;
    void resetFillPreviewMetrics();
    void rebuildFillPreviewMetricsFromGrid();
    bool updateFillPreviewMetricsFromBatch(const FillPreviewState::ProgressBatch& batch);
    size_t applyFillPreviewBatchBudget(size_t maxBatchCount, qint64 maxElapsedMs);
    static std::deque<FillPreviewState::ProgressBatch> buildFillPreviewBatches(
        const FillPreviewRawTileMap& previewTiles, const FillPreviewRawTileMap& maskTiles,
        int originX, int originY, float readyRadius);

    struct PendingFillKickoff {
        bool pending = false;
        uint64_t sequence = 0;
        QUuid layerId;
        FillAlgorithm algorithm = FillAlgorithm::Smart;
        SelectionRestoreContext selectionRestore {};
        FillOrigin origin;
        FillColor color;
        FillCanvasBounds canvasBounds;
        bool maskTarget = false;
        bool forceFinalResultOnly = false;
    };
    PendingFillKickoff m_pendingFillKickoff;
    QString m_fillShaderDir;
    std::unique_ptr<QThread> m_fillWorkerThread;
    std::unique_ptr<QOffscreenSurface> m_fillWorkerSurface;
    FillWorker* m_fillWorker = nullptr;
    std::shared_ptr<std::atomic<bool>> m_fillWorkerCancelState;
    uint64_t m_fillWorkerRequestSequence = 0;
    uint64_t m_activeFillWorkerRequest = 0;
    QUuid m_signaledFillProcessingLayerId;

    // Lasso selection
    QRectF m_canvasResizeSelectionWorld;
    bool m_canvasResizeOverlayActive = false;
    bool m_canvasResizeOverlaySelecting = false;
    std::unique_ptr<GLSelectionRenderer> m_selectionRenderer;

    // Canvas overlay manager (owns all GL overlays)
    std::unique_ptr<CanvasOverlayManager> m_overlayManager;
    FillProgressPopupWidget* m_fillProgressPopup = nullptr;

    // Brush and eyedropper cursor overlay state
    CursorOverlayState m_cursorOverlayState;
    bool m_skipCursorOverlays = false;

    // Scene FBO for cursor overlay rendering (inversion needs scene texture)
    SceneFboManager m_sceneFboManager;

    // Same-frame GPU backdrop-blur regions for on-canvas overlays.
    std::unique_ptr<CanvasBackdropRenderer> m_backdropRenderer;
    BackdropRegionProvider m_backdropRegionProvider;
    bool m_backdropRendererAvailable = false;
    QTimer m_canvasCornerEffectTimer;
    QElapsedTimer m_canvasCornerEffectClock;
    float m_canvasCornerRadiusScreenPx = 0.0f;
    float m_canvasCornerTargetScreenPx = 0.0f;
    qint64 m_canvasCornerLastTickMs = 0;
    qint64 m_lastCanvasInteractionMs = 0;
    qint64 m_lastCanvasEditMs = 0;
    qreal m_lastEmittedZoom = -1.0;
    qreal m_lastEmittedRotation = std::numeric_limits<qreal>::quiet_NaN();

    struct PendingSelectionReadback {
        bool active = false;
        std::vector<TileKey> keys;
        GLsync fence = nullptr;
    };

    QElapsedTimer m_cameraFrameTimer;
    QTimer m_cameraAnimationFrameTimer;
    bool m_cameraWasAnimatingLastFrame { false };

    // VSync-driven pan: cursor is sampled in paintGL and one delta is applied
    // per frame, so pan advances in lock-step with display refresh rather than
    // mouse event rate.
    bool m_panSamplingActive = false;
    QPointF m_panSamplingLastGlobalPos;
};

} // namespace aether

#endif // AETHER_ENGINE_QT_OPENGL_CANVAS_WIDGET_H
