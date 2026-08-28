// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASPANEL_H

#include "features/canvas/CanvasBoundsMode.h"
#include "features/canvas/ui/CanvasInputHost.h"
#include "features/canvas/ui/CanvasPanelTypes.h"
#include "features/canvas/ui/CanvasToolStateController.h"
#include "features/fill/FillAlgorithm.h"
#include "shell/docking/widgets/DockPanel.h"

#include "features/canvas-resize/CanvasResizeController.h"
#include "features/canvas/scene/Canvas.h"
#include "features/canvas/scene/Viewport.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/TextLayerEdit.h"
#include "shared/types/CanvasWidgets.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QRect>
#include <QRectF>
#include <QPolygonF>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QVariant>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QGraphicsOpacityEffect;
class QKeyEvent;
class QLabel;
class QPropertyAnimation;
class QTimer;
class QVariantAnimation;
class QTabletEvent;
class QResizeEvent;
class QShowEvent;
class QHideEvent;
class QMimeData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QPainter;

namespace aether {
class Canvas;
class IUndoCommand;
class OpenGLCanvasWidget;
class TileGrid;
class Viewport;
} // namespace aether

namespace ruwa::core::layers {
class LayerModel;
struct SmartDocument;
} // namespace ruwa::core::layers

namespace ruwa::ui::widgets {
class RadialMenuWidget;
class BrushControlOverlay;
class BrushPackPanel;
class CanvasToolStateOverlay;
class CanvasZoomInfoOverlay;
class CanvasSelectionSizeOverlay;
class CanvasStylusJoystickContainerWidget;
class ConfirmationPopup;
class SelectionActionPopup;
class ColorPickerOverlay;
class DotGridLoadingIndicator;
class CanvasPositionPickerOverlay;
} // namespace ruwa::ui::widgets

namespace ruwa::core::exporting {
struct ExportSettings;
struct ExportResult;
class ExportService;
} // namespace ruwa::core::exporting

namespace ruwa::ui::workspace {

class CanvasCursorManager;
class RadialMenuController;
class CanvasTabletHandler;
class CanvasMouseInputHandler;
class CanvasSelectionPopupManager;
class CanvasKeyEventHandler;
class CanvasSpaceMoveHandler;
class CanvasImageImportHelper;
class CanvasOverlayLayoutManager;
class CanvasViewController;
class TextEditingController;
class ExportSettingsPanel;
class ExportModeController;
class ExportAreaController;
class ImageImportSelectionOverlay;
class CanvasParameterOverlayWidget;
class CanvasEngineQtBinding;

class CanvasPanel : public ruwa::ui::docking::DockPanel, public CanvasInputHost {
    friend class CanvasMouseInputHandler;
    friend class RadialMenuController;
    friend class CanvasSelectionPopupManager;
    friend class CanvasSpaceMoveHandler;
    friend class CanvasImageImportHelper;
    friend class CanvasOverlayLayoutManager;
    friend class CanvasViewController;
    friend class TextEditingController;

    Q_OBJECT

public:
    using PersistedToolState = CanvasPersistedToolState;

    enum TransformContextActionId {
        TransformActionModeClassic = 1,
        TransformActionModeDeform = 2,
        TransformActionFlipHorizontal = 3,
        TransformActionFlipVertical = 4,
    };

    explicit CanvasPanel(
        const QSize& canvasSize, const QRect& exportFrame = QRect(), QWidget* parent = nullptr);
    ~CanvasPanel() override;

    // Canvas properties
    QSize canvasSize() const { return m_canvasSize; }
    void setCanvasSize(const QSize& size);
    QRect documentBoundsRect() const
    {
        return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
    }
    bool hasFiniteDocumentBounds() const
    {
        return ruwa::core::canvas::hasFiniteDocumentBounds(m_canvasBoundsMode);
    }
    QRect exportFrame() const { return m_exportFrame; }
    bool hasExportFrame() const { return m_exportFrame.width() > 0 && m_exportFrame.height() > 0; }
    QSize exportFrameSize() const { return m_exportFrame.size(); }
    QRect effectiveDisplayFrame() const;
    QRect navigatorDisplayFrame() const;
    QRect exportPreviewCameraFrame() const;
    void setExportFrame(const QRect& frame);

    /// Resize the export frame from a numeric edit, keeping its top-left corner
    /// where it is. The result is clamped like any other frame change, so the
    /// size that lands may be smaller than the one asked for.
    void resizeExportFrame(const QSize& size);

    /// The frame the export panel's Reset button restores: the document bounds
    /// on a bounded canvas, the current content bounds on an infinite one.
    QRect defaultExportFrame() const;
    void resetExportFrameToDefault();

    void setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode);
    ruwa::core::canvas::CanvasBoundsMode canvasBoundsMode() const { return m_canvasBoundsMode; }
    bool isInfiniteCanvas() const
    {
        return ruwa::core::canvas::isInfiniteCanvas(m_canvasBoundsMode);
    }
    void setInfiniteCanvasEnabled(bool enabled)
    {
        setCanvasBoundsMode(enabled ? ruwa::core::canvas::CanvasBoundsMode::Infinite
                                    : ruwa::core::canvas::CanvasBoundsMode::Bounded);
    }
    bool infiniteCanvasEnabled() const { return isInfiniteCanvas(); }

    // === Renderer-neutral view access ===
    //
    // Semantic camera/view operations. The engine's viewport/camera objects do
    // not cross the application boundary (accessViewport()/accessCanvas() below
    // are quarantined implementation access for CanvasPanel's own internals and
    // its friend classes).

    /// Camera/view state queries. Return defaults until the render content
    /// exists; most callers additionally guard with isRenderContentReady().
    qreal currentZoom() const;
    qreal minZoom() const;
    qreal maxZoom() const;
    /// Camera centre in document coordinates.
    QPointF cameraPosition() const;
    qreal cameraRotationRadians() const;
    bool isCameraAnimating() const;
    /// Interactive viewport extent in viewport-logical units.
    QSizeF viewportExtent() const;
    /// The generic widget hosting the render viewport (UI host role only:
    /// layout, hit-testing, focus — never renderer API).
    QWidget* viewportHostWidget() const { return m_viewportHostWidget; }
    /// The part of the document currently visible, in document coordinates.
    QPolygonF visibleDocumentPolygon() const;
    /// Document point under a viewport-logical position.
    QPointF documentFromViewport(const QPointF& viewportPos) const;
    /// Viewport-logical position of a document point.
    QPointF viewportFromDocument(const QPointF& documentPos) const;

    // Camera mutations
    void setCameraZoom(qreal zoom);
    /// Zoom at a viewport-logical point (keeps that point stationary).
    void zoomAtViewportPoint(qreal factor, const QPointF& viewportPos);
    void setCameraZoomLimits(qreal minZoom, qreal maxZoom);
    void setCameraPosition(const QPointF& documentPos);
    /// Move the camera by a document-space delta.
    void moveCameraBy(const QPointF& documentDelta);
    void setCameraRotationRadians(qreal radians);
    void addCameraRotationRadians(qreal deltaRadians);
    void centerCameraOn(const QPointF& documentPoint);
    void stopCameraAnimation();

    // Display-only content mirror (document and export stay unmirrored).
    bool canvasContentFlipHorizontal() const;
    bool canvasContentFlipVertical() const;
    void setCanvasContentFlipHorizontal(bool flip);
    void setCanvasContentFlipVertical(bool flip);

    // === History (plan 7.30.1) ===
    // Semantic panel wrappers over the binding's transitional history facade.
    // Raw UndoManager access below is quarantined for call sites that still
    // need the concrete manager (see docs/renderer-boundary-quarantine.md).
    bool canHistoryUndo() const;
    bool canHistoryRedo() const;
    void historyUndo();
    void historyRedo();
    void pushHistoryCommand(std::unique_ptr<aether::IUndoCommand> command);
    void beginHistoryTransaction(const QString& text);
    void endHistoryTransaction();

    /// TRANSITIONAL QUARANTINE: raw legacy manager access. Do not use in new
    /// code; ownership moves to an application document subsystem.
    aether::UndoManager* undoManagerOrNull();
    /// TRANSITIONAL QUARANTINE: active-manager routing (transform override).
    aether::UndoManager* activeUndoManagerOrNull();

    // Camera controls
    void setZoom(float zoom);
    void setZoomSmooth(float zoom);
    void zoomBy(float factor);
    /// Zoom at a world-space point (e.g. from Navigator widget). Zooms toward that point.
    void zoomAtWorldPoint(float factor, const QPointF& worldPos);
    void zoomToFit();
    void centerCanvas();

    /// Toggle horizontal/vertical view mirror (display only; document and export stay unmirrored).
    void toggleCanvasViewFlipHorizontal();
    void toggleCanvasViewFlipVertical();

    // Brush controls
    void setBrushColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void applyCurrentBrushColor(const QColor& color);
    void applyCurrentBrushColorPreservingOpacity(const QColor& color);
    void setBrushRadius(float radius);
    qreal brushSizeNormalized() const;
    qreal brushOpacityNormalized() const;
    void setBrushSizeNormalized(qreal size);
    void setBrushOpacityNormalized(qreal opacity);
    void adjustBrushSizeNormalized(qreal delta);
    void adjustBrushOpacityNormalized(qreal delta);
    uint8_t brushAlpha() const
    {
        return m_toolStateController ? m_toolStateController->currentAlpha() : 255;
    }
    QColor currentBrushColor() const;
    QPoint brushOverlayPosition() const;
    void setBrushOverlayPosition(const QPoint& pos);
    void setPendingBrushOverlayPosition(const QPoint& pos); ///< Store for apply when content ready
    /// Normalized (0-1) for size-independent persistence. Returns invalid QPointF if content has no
    /// size.
    QPointF brushOverlayPositionNormalized() const;
    /// Store normalized position for later apply (e.g. when content is ready). Does not apply
    /// immediately.
    void setPendingBrushOverlayPositionNormalized(const QPointF& norm);
    void setBrushOverlayPositionFromNormalized(const QPointF& norm);
    QPoint toolStateOverlayPosition() const;
    void setToolStateOverlayPosition(const QPoint& pos);
    void setPendingToolStateOverlayPosition(const QPoint& pos);
    QPointF toolStateOverlayPositionNormalized() const;
    void setPendingToolStateOverlayPositionNormalized(const QPointF& norm);
    void setToolStateOverlayPositionFromNormalized(const QPointF& norm);
    QPoint stylusJoystickPosition() const;
    void setStylusJoystickPosition(const QPoint& pos);
    void setPendingStylusJoystickPosition(const QPoint& pos);
    QPointF stylusJoystickPositionNormalized() const;
    void setPendingStylusJoystickPositionNormalized(const QPointF& norm);
    void setStylusJoystickPositionFromNormalized(const QPointF& norm);
    bool stylusJoystickAbovePanel() const;
    void setPendingStylusJoystickAbovePanel(bool above);
    PersistedToolState persistedToolState(ToolId tool) const;
    void setPersistedToolState(ToolId tool, const PersistedToolState& state);
    void reapplyCurrentToolState();
    ToolId brushSelectionToolMode() const;
    bool selectBrushForCurrentContext(const QString& brushId);
    QString selectedBrushIdForCurrentContext() const;
    /// Open the configurable radial menu centred on @p globalPos (canvas right-click).
    /// @p armReleaseSelect keeps the opening button's release as the pick.
    void showRadialMenu(const QPoint& globalPos, bool armReleaseSelect = false);
    void hideRadialMenu();
    bool isRadialMenuVisible() const;

    // Layer model integration
    void setLayerModel(ruwa::core::layers::LayerModel* model);
    /// Show the declarative on-canvas controls belonging to one transiently
    /// selected effect. Null ids clear them immediately.
    void setEffectParameterOverlaySelection(
        const ruwa::core::layers::LayerId& layerId, const QUuid& effectId);
    void selectLayerContent(const ruwa::core::layers::LayerId& id);
    /// Load a layer's mask into the pixel selection (grays stay partially selected).
    void selectLayerMaskContent(const ruwa::core::layers::LayerId& id);
    bool startTextLayerEditing(const ruwa::core::layers::LayerId& id);
    /// True while a text layer is open for editing on the canvas.
    bool isTextEditingActive() const;
    /// Layer the open editing session belongs to, null when none is open.
    ruwa::core::layers::LayerId textEditingLayerId() const;
    /// Character range selected in the open editing session, as [from, to).
    /// Empty (from == to) for a bare caret, nullopt when nothing is open — the
    /// difference matters: a caret edits the defaults, no session edits the
    /// whole layer.
    std::optional<std::pair<int, int>> textEditingSelection() const;

    /// A colour picker is up over the text colour of an open session: it owns
    /// the preview until it closes, so the selection highlight stands down and
    /// the focus it takes does not end the session.
    void setTextColorPickerActive(bool active);
    /// Perform one Character / Paragraph panel edit on @p id as a single undo
    /// step. Honours the open editing session's selection when there is one.
    bool applyTextLayerEdit(
        const ruwa::core::layers::LayerId& id, const ruwa::core::layers::TextLayerEdit& edit);
    /// Ends any open live text interaction, landing it as one undo step. Called
    /// when the run closes, and whenever something else is about to take over
    /// the document.
    void flushTextEditInteraction();
    /// Abandons an open live run on @p id, restoring the state it began at.
    bool cancelTextEditInteraction(const ruwa::core::layers::LayerId& id);
    /// Widgets that may take focus without ending an open text session — the
    /// Layer Properties panel's Character and Paragraph controls, which stand
    /// in for the formatting popup that used to live on the canvas.
    void addTextEditingFocusExclusion(QWidget* widget);
    bool isTextEditingFocusExclusion(const QWidget* widget) const;
    /// Clear raster layer pixels (GL). No-op if GL not ready or layer not editable.
    bool clearLayerPixelContent(const ruwa::core::layers::LayerId& id);
    bool rasterizeSmartLayer(const ruwa::core::layers::LayerId& id);
    /// Wrap a layer's pixels into a smart object (GL, undoable) — the inverse of
    /// rasterizing.
    bool convertLayerToSmartObject(const ruwa::core::layers::LayerId& id);
    /// Ask for a file and give the smart object new contents, keeping its
    /// placement (GL, undoable). Every instance of the object follows. Returns
    /// false when the layer is not a smart object or no file was chosen; the
    /// replacement itself completes asynchronously once the file is decoded.
    bool replaceSmartLayerContents(const ruwa::core::layers::LayerId& id);
    /// Commit edited contents into the smart object @p contentId of THIS
    /// document and re-flatten them (undoable; every instance follows, none of
    /// them moves). False when no layer here shows those contents or the
    /// flattening could not run — nothing is changed in that case.
    bool applySmartContentDocument(
        const QUuid& contentId, std::shared_ptr<ruwa::core::layers::SmartDocument> document);
    /// Bake a layer's mask into its pixels and remove the mask (GL, undoable).
    bool applyLayerMask(const ruwa::core::layers::LayerId& id);
    /// Invert a layer mask (reveal -> 1 - reveal) across all tiles + background (GL, undoable).
    bool invertLayerMask(const ruwa::core::layers::LayerId& id);
    /// Bake a raster layer's effect chain into its pixels and clear the chain (GL, undoable).
    bool applyLayerEffects(const ruwa::core::layers::LayerId& id);
    /// Bake the active selection into the layer's freshly created mask (inside =
    /// visible, outside = hidden). Returns false if there is no active selection.
    bool fillLayerMaskFromActiveSelection(const ruwa::core::layers::LayerId& id);
    void clearSelectionMask();
    bool fillSelectionWithCurrentColor();
    /// True when Delete can clear the selected pixels on the active layer.
    bool canDeleteSelectionContent() const;
    /// Erase the content under the active selection. Returns false when there is
    /// no selection (or a transform is in flight).
    bool deleteSelectionContent();
    /// Select the whole document. False on a document without finite bounds.
    bool selectAllCanvas();
    /// Swap selected for unselected, partial coverage preserved.
    bool invertSelection();
    /// Whether a deselected selection is still available to bring back.
    bool canReselectSelection() const;
    /// Bring back the selection that was last deselected.
    bool reselectSelection();
    /// True while the canvas holds an active selection mask.
    bool hasActiveSelection() const;
    /// True when the layer the selection commands act on carries a mask.
    bool selectedLayerHasMask() const;
    /// Replace the selection with the selected layer's content silhouette.
    void selectActiveLayerContent();
    /// Replace the selection with the selected layer's mask coverage.
    void selectActiveLayerMaskContent();
    /// Apply an animated, undoable one-shot transform to the active selection,
    /// or to the selected layer(s) when there is no active selection.
    bool flipContentHorizontally();
    bool flipContentVertically();
    bool rotateContent90Clockwise();
    bool rotateContent90Counterclockwise();
    bool rotateContent180();
    bool canApplyContentTransformAction() const;
    /// Copy the pixels under the active selection (edit clipboard + system
    /// clipboard image). False when there is nothing copyable.
    bool copySelectionPixels();
    /// Copy the merged visible result under the active selection.
    bool copyMergedSelectionPixels();
    /// Copy the pixels under the active selection, then erase them.
    bool cutSelectionPixels();
    /// Create a raster layer from the pixels under the active selection. When
    /// @p cutFromSource is true, remove those pixels from the source layer in
    /// the same undo step. The selection and edit clipboard are preserved.
    bool canCreateLayerFromSelection(bool cutFromSource) const;
    bool createLayerFromSelection(bool cutFromSource);
    /// Whether the edit clipboard holds pixels this canvas could paste.
    bool canPasteClipboardPixels() const;
    /// Paste the copied pixels as a new layer above the current one, in place,
    /// and enter transform mode so they can be placed before being committed.
    bool pasteClipboardPixelsAsLayer();
    void importImageFilesBelowSelectedKeepingSelection(const QStringList& filePaths);
    void importImageBelowSelectedKeepingSelection(const QImage& image, const QString& layerName);
    void promptImportImageFiles(const QStringList& filePaths);
    bool importImageFromClipboard();

    // Color panel integration
    void connectColorPanel(QObject* colorPanel);

    // Rendering control
    void requestRender();
    void notifyContentChanged();
    void refreshZoomLimits();

    /// Capture current canvas view as thumbnail (for recent projects). Returns null if GL not
    /// ready.
    QPixmap grabCanvasThumbnail(int maxSize = 256) const;

    /// Full canvas image scaled to maxSize (for Navigator panel). Returns null if GL not ready.
    QImage getFullCanvasThumbnail(int maxSize = 512) const;
    QImage getCanvasRegionThumbnail(const QRect& worldRect, const QSize& targetSize) const;
    QImage renderNavigatorOverviewTile(const QRect& worldRect, const QSize& targetSize) const;

    /// Get full-resolution canvas image for export. Returns null QImage if GL not ready.
    QImage exportCanvasImage();
    bool copyCanvasToClipboard();

    /// Fast export: render the canvas and save straight to a PNG file (single save
    /// dialog, no export-mode UI). @p suggestedBaseName seeds the file name.
    /// Returns true if a file was written.
    bool fastExportPng(const QString& suggestedBaseName = QString());

    /// Seeds the export panel's file-name field. Ignored once the user has
    /// typed a name of their own.
    void setExportBaseName(const QString& baseName);

    /// Capture the export frame and hand it to the export service.
    ///
    /// The capture is synchronous (it needs the GL context, which lives here);
    /// everything after it — resampling, depth conversion, encoding, writing —
    /// runs off the GUI thread and reports back through the service's signals.
    /// Returns false when the request was rejected outright, having already
    /// shown the reason; a true return means a job started, not that it
    /// succeeded.
    ///
    /// @p settings is validated and clamped in place, so the caller can read
    /// back what will actually be written.
    bool startExport(ruwa::core::exporting::ExportSettings& settings);

    /// Re-renders the export frame's content at reduced resolution straight on
    /// the GPU and hands it to the export panel, whose size estimate is
    /// measured from it. Cheap enough to run debounced on every frame drag.
    void refreshExportPanelSample();

    /// Lazily created; owned by the panel so each document exports independently.
    ruwa::core::exporting::ExportService* exportService();

    // Tools
    void setEraseMode(bool erase) override;
    /// Eraser-brush state of the Brush tool: erase with the brush's own tip.
    bool isBrushEraserActive() const
    {
        return m_toolStateController && m_toolStateController->brushEraserActive();
    }
    void toggleBrushEraserMode() { setBrushEraserActive(!isBrushEraserActive()); }
    void setBlurMode(bool blur);
    void setSmudgeMode(bool smudge);
    void setLiquifyMode(bool liquify);
    void setToolMode(ToolId tool) override;
    ToolId toolMode() const
    {
        return m_toolStateController ? m_toolStateController->currentTool() : ToolId::Brush;
    }
    qreal lassoStabilization() const
    {
        return m_toolStateController ? m_toolStateController->lassoStabilization() : 0.0;
    }
    qreal lassoFillStabilization() const
    {
        return m_toolStateController ? m_toolStateController->lassoFillStabilization() : 0.0;
    }
    void setLassoStabilization(qreal stabilization);
    void setLassoFillStabilization(qreal stabilization);

    // Transform mode
    bool canToggleTransformMode() const;
    void enterTransformMode();
    /// Enter the existing transform session directly in Warp mode.
    bool canEnterWarpTransformMode() const;
    bool enterWarpTransformMode();
    void confirmTransform();
    /// Translate the selected layers' content by @p delta document pixels as a
    /// single undoable step (the Move tool's path, driven programmatically).
    /// Returns false when nothing could move right now.
    bool moveSelectedContentBy(const QPointF& delta);
    /// Nudge an open transform with the arrow keys: one document pixel per
    /// press, ten with Shift, or a jump to the next cell of the Position
    /// group's anchor grid with Ctrl. Returns false when the key is not a plain
    /// arrow or no transform is open, so the event keeps travelling.
    bool handleTransformArrowNudge(QKeyEvent* event);
    /// Distance from the transform's current place to the next anchor-grid cell
    /// along @p direction — the same nine cells the Position group aligns to.
    /// False when there is no cell left that way, or no document box to align
    /// against.
    bool transformAnchorSnapDelta(const QPointF& direction, QPointF& delta) const;
    /// Brackets a run of moveSelectedContentBy() calls that belong to one
    /// gesture (dragging a coordinate field): the content follows every delta
    /// on screen, and the whole run lands in history as a single move.
    void setContentMovePreviewActive(bool active);
    /// Commit transform and synchronously finish deferred GPU readback before
    /// an operation mutates layer content, structure, selection, or canvas geometry.
    void commitTransformBeforeDocumentMutation();
    void cancelTransform();
    bool isDrawingActive() const override;
    /// True while a lasso, shape-selection or canvas-resize drag owns the panel's
    /// mouse grab. Public alongside isDrawingActive() because TabletToMouseEventFilter
    /// routes stylus packets by whether the panel is running an interaction.
    bool isAnySelectionInteractionActive() const override;
    bool hasPendingStrokeFinalization() const;
    void flushPendingStrokeFinalization();
    bool isTransformActive() const;

    /// Schedule appearance animation for new project (min zoom → zoom to fit).
    /// Call only when creating a new project (not when opening from file).
    void scheduleNewProjectAppearanceAnimation();
    void setDeferredAppearanceAnimation(bool deferred);
    void setLoadingOverlayDecorationsVisible(bool visible);

    /// Create the render content: build the engine binding (host widget +
    /// session + events) through the Aether integration. Deferred until after
    /// tab transition animation. Called by WorkspaceTab::onTransitionFinishedImpl().
    /// @return true if the render content was created in this call (first time only)
    bool createRenderContent();

    /// True when render content exists and the engine is ready (safe to use
    /// the session capabilities and the semantic view API).
    bool isRenderContentReady() const;

    /// Canvas widgets visibility (View → Canvas widgets menu)
    void setCanvasWidgetVisible(CanvasWidget widget, bool visible);
    bool isCanvasWidgetVisible(CanvasWidget widget) const;
    void setCanvasWidgetVisibility(const CanvasWidgetVisibility& visibility);
    CanvasWidgetVisibility canvasWidgetVisibility() const;

    // === Export mode ===

    /// Toggle export mode on/off (interruptible mid-animation).
    void toggleExportMode();
    bool isExportMode() const;

    /// Set export mode overlay progress (0.0 = normal, 1.0 = fully in export mode).
    /// Fades out canvas overlays (brush control, stylus joystick, popups).
    void setExportModeOverlayProgress(qreal progress);

    /// Export preview: show canvas unmirrored without changing stored flip toggles.
    void setExportPreviewSuppressContentMirror(bool suppress);

    /// Enable or disable canvas interaction (drawing, panning, zooming).
    void setInteractionEnabled(bool enabled);
    bool isInteractionEnabled() const
    {
        return m_interactionEnabled && !m_loadingAppearanceAnimationActive;
    }

    /// True while the startup zoom-in appearance animation is still running. Callers that
    /// hijack the live camera (e.g. grabCanvasImage for thumbnails) must not do so during
    /// this window: grabCanvasImage's setZoom() clears the camera's animating flag, which
    /// the frame-swap completion handler then reads as "animation finished" and freezes the
    /// zoom mid-flight.
    bool isLoadingAppearanceAnimationActive() const { return m_loadingAppearanceAnimationActive; }

    /// Reset overlay widgets (brush control, tool state strip, stylus joystick) to default
    /// positions and visibility. Used when applying default/startup layout or reset layout.
    void resetCanvasOverlaysToDefault();
    void forwardTabletEvent(QTabletEvent* event);

    // === Position picking (e.g. a layer-effect's on-canvas position param) ===

    /// Begins a "click the canvas to set a document position" session. The
    /// next left-click inside the canvas viewport calls \p onPicked with the
    /// clicked document-pixel coordinate and ends the session. A right-click,
    /// switching to any tool other than Hand (switching to/from Hand is exempt
    /// so panning while picking still works), or switching the active layer
    /// cancels it and calls \p onCanceled instead. While active, the brush /
    /// tool-state / joystick overlays are hidden and their input suppressed,
    /// and a small cursor-following label shows the position a click would
    /// pick (see CanvasPositionPickerOverlay).
    void beginPositionPicking(const QPointF& initialDocPos,
        std::function<void(const QPointF&)> onPicked, std::function<void()> onCanceled);
    /// Ends an active session without calling onPicked (calls onCanceled if
    /// one was provided). Safe to call when no session is active.
    void cancelPositionPicking();
    /// Ends an active session, calling onPicked with \p docPos. Called by
    /// CanvasMouseInputHandler when a left-click lands while picking is active.
    void commitPositionPicking(const QPointF& docPos);
    bool isPositionPickerActive() const { return m_positionPickerActive; }

    // === Fill tool ===

    /// Run a fill request and present its preflight facts (plan 7.6.41):
    /// RejectedRegionTooLarge maps to the existing localized radius-limit
    /// popup, every other rejection stays silent exactly as before. The
    /// result is returned for callers that want the status themselves.
    ruwa::core::canvas::CanvasFillRequestResult requestFillAt(int documentX, int documentY);
    ruwa::core::canvas::CanvasFillRequestResult requestClassicFillAt(int documentX, int documentY);

public slots:
    void onSimpleContextAction(int actionId);

signals:
    void renderContentReady();
    /// Emitted once per composited canvas frame. Consumers must stay trivial: this
    /// fires at display refresh rate while the canvas is streaming frames.
    void canvasFrameRendered();
    void zoomChanged(qreal zoom);
    void zoomLimitsChanged(qreal minZoom, qreal maxZoom);
    void cursorPositionChanged(const QPoint& pos);
    void canvasSizeChanged(const QSize& size);
    /// Forwarded from the export panel's matte swatch; the main window owns the
    /// shared color picker.
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);
    void exportFrameChanged(const QRect& frame);
    void canvasBoundsModeChanged(ruwa::core::canvas::CanvasBoundsMode mode);
    void canvasContentChanged();
    void canvasContentRegionChanged(const QRect& worldRect);
    void canvasContentTilesChanged(const QList<QPoint>& tilePositions);
    /// Live edit produced by a declarative parameter overlay. WorkspaceTab
    /// routes it back through LayerEffectsPanel so the existing merged undo
    /// transaction and card synchronization are reused.
    void effectParameterOverlayChanged(const ruwa::core::layers::LayerId& layerId,
        const QUuid& effectId, const QString& key, const QVariant& value);
    void effectParameterOverlayEditFinished(
        const ruwa::core::layers::LayerId& layerId, const QUuid& effectId);
    void fillProcessingLayerChanged(const ruwa::core::layers::LayerId& id);
    /// A pointer press was accepted as a canvas-tool interaction.
    void canvasToolInteractionStarted();
    void toolModeChanged(ToolId tool);
    void brushSelectionContextChanged(ToolId tool, const QString& brushId);
    void transformModeChanged(bool active);
    void colorPicked(const QColor& color);
    /// Emitted when a paint stroke (not erase) completes. Use to add brush color to recent palette.
    void strokePainted();
    void brushOverlayPositionChanged(const QPoint& pos);
    void toolStateOverlayPositionChanged(const QPoint& pos);
    void stylusJoystickPositionChanged(const QPoint& pos);
    /// Joystick / brush HUD / tool bar visibility changed (sync View → Canvas widgets menu).
    void canvasWidgetsVisibilityChanged();
    /// A text editing session opened, closed, or moved its caret / selection.
    /// The Layer Properties panel re-reads the character attributes off it.
    void textEditingStateChanged();

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    // Mouse events for pan/zoom/draw
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    using ToolBrushState = CanvasToolBrushState;
    using TemporaryToolHold = CanvasTemporaryToolHold;

    void onSurfaceResized(uint32_t width, uint32_t height);
    void onRenderSessionReady();
    bool addPixelLayer(std::unique_ptr<aether::TileGrid> grid, const QString& layerName,
        const QString& undoLabel, bool enterTransformAfterAdd);
    void updateStyles();
    void positionBrushOverlayDefault();
    void positionStylusJoystickDefault();
    void scheduleInitialBrushOverlayPlacement();
    void loadGlobalToolState();
    void persistGlobalToolState();
    /// Live size/opacity edits write straight into the active instrument's state,
    /// the same way brush id, settings and color already do. The shared overlay is
    /// a view: it never owns a value that some other tool would inherit later.
    void writeLiveBrushSizeToToolState(qreal size);
    void writeLiveBrushOpacityToToolState(qreal opacity);
    ruwa::core::brushes::BrushSettingsData currentBrushSettings() const;
    void applyBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings);
    void applyToolStateBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings);
    QString resolveBrushSelectionId(
        const QString& requestedBrushId, const QString& fallbackPresetId = QString()) const;
    bool applyBrushSelectionForTool(ToolId tool, const QString& requestedBrushId,
        const QString& fallbackPresetId, bool persistSelection, bool emitSyncSignal);
    void restoreToolState(ToolId tool);
    /// Push a tool's erase/blur/smudge/liquify flags onto the shared TileBrush.
    /// Only safe between strokes — endStroke() reads them to flatten the whole
    /// stroke buffer, so changing them mid-stroke rewrites what is already painted.
    void applyToolPaintModes(ToolId tool);
    /// A tool switch during a live stroke cannot reconfigure the shared TileBrush
    /// (the in-flight stroke must keep the settings it began with), so the apply is
    /// deferred to the end of that stroke instead of being dropped.
    void flushPendingToolStateApply();
    /// Blur and Liquify use the same fixed standard soft brush (hardness 0, round)
    /// and ignore brush selection — only size and strength (flow) are user-adjustable.
    void applyFixedSoftBrush(ToolId tool);
    void setFixedSoftBrushStrength(ToolId tool, qreal strength);
    /// Apply color/opacity when restoring tool state (always applies; does not sync to brush state)
    void applyBrushColorForRestore(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    /// Brush, Eraser, or Blur that currently "owns" the shared size/opacity overlay (Hand → last
    /// draw tool).
    ToolId overlayInstrumentMode() const;
    void emitBrushSelectionContextChanged();
    bool overlayMatchesInstrument(ToolId tool) const;
    void updateCursorManagerOverlay();
    void updateBrushCursorOverlayRadius();
    std::optional<Qt::CursorShape> resolveCursorForPosition(const QPoint& globalPos) const;
    bool playNewProjectAppearanceAnimationIfScheduled();
    void completeLoadingAppearanceAnimation();
    void updateLoadingOverlayGeometry();
    void fadeOutLoadingOverlay();
    void hideLoadingOverlayImmediately();
    QRect normalizedExportFrame(const QRect& frame) const;
    QRect computedAutoExportFrame() const;
    void syncInfiniteExportFrameToContent(bool forceReset = false);
    void publishEffectiveExportFrameIfChanged();
    void setCursorManagerSuppressedByLoading(bool suppressed);
    void syncToolStateOverlayContent();
    ToolId currentInputTool() const override { return toolMode(); }
    /// Engine session behind the binding; null until the render content is
    /// created (plan 7.6.17 input-host seam).
    CanvasEngineSession* inputEngineSession() const
    {
        return m_engineBinding ? &m_engineBinding->session() : nullptr;
    }
    bool inputRenderReady() const override
    {
        auto* session = inputEngineSession();
        return session && session->status() == CanvasEngineStatus::Ready;
    }
    CanvasViewCapability* inputView() const override;
    CanvasPaintingCapability* inputPainting() const override;
    CanvasEditingCapability* inputEditing() const override;
    CanvasTransformCapability* inputTransform() const override;
    CanvasPresentationCapability* inputPresentation() const override;
    QWidget* inputViewportHostWidget() const override { return m_viewportHostWidget; }
    CanvasCursorManager* inputCursorManager() const override { return m_cursorManager; }
    bool hasInputFocus() const override { return hasFocus(); }
    bool hasInputFocusOrCursorOverCanvas() const override
    {
        return hasFocus() || isCursorOverCanvas();
    }
    bool isTransformInputActive() const override;
    bool isInputDrawingActive() const override { return m_isDrawing; }
    void setInputDrawingActive(bool active) override { m_isDrawing = active; }
    bool isInputPanningActive() const override { return m_isPanning; }
    Qt::MouseButton inputPanButton() const override { return m_panButton; }
    bool isInputTabletActive() const override { return m_tabletActive; }
    void setInputTabletActive(bool active) override { m_tabletActive = active; }
    Qt::MouseButtons previousTabletButtons() const override { return m_prevTabletButtons; }
    void setPreviousTabletButtons(Qt::MouseButtons buttons) override
    {
        m_prevTabletButtons = buttons;
    }
    bool isCursorOverCanvas() const override;
    void updateToolCursor() override;
    bool handleWheelZoom(QWheelEvent* event);
    bool isSpaceSelectionMoveActive() const override { return m_spaceSelectionMoveActive; }
    bool isSpaceStrokeMoveActive() const override { return m_spaceStrokeMoveActive; }
    void beginSpaceSelectionMove() override;
    void moveActiveSelectionWithSpace(const QPoint& globalPos);
    void endSpaceSelectionMove() override;
    void beginSpaceStrokeMove() override;
    void moveActiveStrokeWithSpace(const QPoint& globalPos) override;
    void endSpaceStrokeMove() override;
    CanvasSpaceMoveHandler* spaceMoveHandler() { return m_spaceMoveHandler; }

    // Temporary tool hold (hold hotkey = temporary switch, release = revert)
    bool temporaryToolHoldActive() const override { return m_tempToolHold.active; }
    bool temporaryToolHeldKeyIs(int key) const override { return m_tempToolHold.heldKey == key; }
    bool temporaryToolHeldButtonIs(Qt::MouseButton button) const override
    {
        return m_tempToolHold.heldButton == button;
    }
    bool temporaryToolShiftSpaceCombo() const override { return m_tempToolHold.shiftSpaceCombo; }
    void setTemporaryToolShiftSpaceCombo(bool enabled) override
    {
        m_tempToolHold.shiftSpaceCombo = enabled;
    }
    void markTemporaryToolUsed() override
    {
        if (!m_tempToolHold.active) {
            return;
        }
        // "Used" may only count while the hotkey is genuinely still down.
        // Selecting a tool and using it in the same frame beats the key-release
        // event (and the 16 ms poll) to the canvas: the key is already physically
        // up by the time the stroke starts, so this was a tap plus a deliberate,
        // permanent switch. Marking it used would revert the tool as soon as the
        // release is finally noticed. Always-revert gestures (Space/Alt/Ctrl,
        // stylus side button) have no tap meaning and are left alone.
        if (!m_tempToolHold.alwaysRevert && m_tempToolHold.heldKey != 0
            && !isTemporaryToolHoldKeyPressed()) {
            syncTemporaryToolHoldFromPressedKeys();
            return;
        }
        m_tempToolHold.toolWasUsed = true;
    }
    void beginTemporaryToolHoldFromButton(Qt::MouseButton heldButton, ToolId tool) override;
    void endTemporaryTool() override;
    bool finalizeTemporaryToolHoldForKeyRelease(int key) override;
    void setPendingTemporaryToolKey(int key, bool alwaysRevert) override;
    void clearPendingTemporaryToolKey() override;
    void updateTemporaryToolHoldPolling();
    void syncTemporaryToolHoldFromPressedKeys();
    bool isTemporaryToolHoldKeyPressed() const;
    void noteUndoForTemporaryMoveTool() override;
    bool temporaryMoveToolUndoCooldownActive();
    void resetTemporaryMoveToolUndoCooldown();
    std::optional<ToolId> inputToolModeForKey(int key) const override
    {
        return toolModeForKey(key);
    }
    std::optional<ToolId> inputToolModeForKeyEvent(const QKeyEvent* event) const override
    {
        return toolModeForKeyEvent(event);
    }
    QString commandIdForInputToolMode(ToolId mode) const override
    {
        return commandIdForToolMode(mode);
    }
    std::optional<ToolId> toolModeForKey(int key) const;
    std::optional<ToolId> toolModeForKeyEvent(const QKeyEvent* event) const;
    static std::optional<ToolId> toolModeForCommandId(const QString& cmdId);
    static QString commandIdForToolMode(ToolId mode);
    QPointF mapWorldToPanel(const aether::Vector2& worldPos) const;
    void ensureSelectionActionPopup();
    void updateSelectionActionPopup(bool forceShow = false) override;
    void dismissSelectionActionPopupUntilSelectionReset();
    void ensureConfirmationPopup();
    void updateConfirmationPopup();
    QRectF activeSelectionRectInWidget() const;
    QRectF activeTransformRectInWidget() const;
    void createExportModeContent();

    bool handleCanvasMousePress(QMouseEvent* event);
    bool handleEffectParameterOverlayMousePress(QMouseEvent* event);
    bool handleEffectParameterOverlayMouseMove(QMouseEvent* event);
    bool handleEffectParameterOverlayMouseRelease(QMouseEvent* event);
    void ensureEffectParameterOverlay();
    void refreshEffectParameterOverlay();
    void syncEffectParameterOverlayPresentation();
    void finishEffectParameterOverlayDrag(bool notifyEditor);
    int effectParameterOverlayHitTest(const QPointF& globalPosition) const;
    void showBlockedDrawMessageForSelectedLayer() override;
    void showDrawOnBackgroundMessage();
    /// Localized application-side presentation of a fill's
    /// RejectedRegionTooLarge preflight result (plan 7.6.41); other statuses
    /// stay silent. The translation context stays "OpenGLCanvasWidget" so the
    /// existing ru/en translations keep matching the source strings.
    void presentFillRadiusLimitMessage(const ruwa::core::canvas::CanvasFillRequestResult& result);
    void setCtrlModifierPressed(bool pressed) override { m_ctrlPressed = pressed; }
    void setAltModifierPressed(bool pressed) override { m_altPressed = pressed; }
    void updateInputCursorPosition(const QPoint& globalPos) override;
    bool shouldIgnoreTabletInputForOverlay(
        const QPointF& globalPos, bool activeTabletStroke) const override;
    bool routeTabletInputToStylusJoystick(QTabletEvent* event, const QPointF& globalPos,
        Qt::MouseButton effectiveButton, bool activeTabletStroke) override;
    void hideBrushPackOverlayIfNotUserMoved() override;
    void dispatchSyntheticMousePress(QMouseEvent* event) override;
    void dispatchSyntheticMouseMove(QMouseEvent* event) override;
    void dispatchSyntheticMouseRelease(QMouseEvent* event) override;
    void notifyCanvasToolInteractionStarted() override { emit canvasToolInteractionStarted(); }
    void notifyCanvasContentChanged() override { emit canvasContentChanged(); }
    void notifyStrokeSessionEnded() override { flushPendingToolStateApply(); }

    void applyZoomLimits();
    void showZoomInfoOverlay();
    void syncZoomInfoOverlayValue();
    void positionZoomInfoOverlay();
    void updateSelectionSizeOverlay();
    void hideSelectionSizeOverlay();
    bool isCanvasInputEventTarget(QObject* watched) const;
    void endActiveStrokeSession();

    /// Called by CanvasKeyEventHandler when app/window loses focus — ends drawing and emits
    /// canvasContentChanged.
    void endDrawingOnAppDeactivate() override;
    void activateApplicationEventFilter();
    void deactivateApplicationEventFilter();
    bool isActiveCanvasPanel() const;
    /// Ends an open text session on this panel, for when it stops being the
    /// canvas the user is working on (hidden, or another tab took over).
    void endTextEditingSession();

    aether::Vector2 mapToWorld(const QPoint& globalPos) const;
    aether::Vector2 mapToWorld(const QPointF& globalPos) const;
    aether::Vector2 mapToViewportWorld(const QPoint& globalPos) const;
    aether::Vector2 mapToViewportWorld(const QPointF& globalPos) const;
    aether::Vector2 mapInputToViewportWorld(const QPointF& globalPos) const override
    {
        return mapToViewportWorld(globalPos);
    }
    /// TRANSITIONAL QUARANTINE (Stage 1): legacy GL-viewport naming kept for
    /// the call sites that have not moved to isGlobalOverInputViewport yet.
    bool isGlobalOverViewport(const QPoint& globalPos) const;
    bool isGlobalOverViewport(const QPointF& globalPos) const;
    bool isGlobalOverInputViewport(const QPoint& globalPos) const override
    {
        return isGlobalOverViewport(globalPos);
    }
    bool isGlobalOverInputViewport(const QPointF& globalPos) const override
    {
        return isGlobalOverViewport(globalPos);
    }

    /// Quarantined implementation access to the engine's viewport/canvas for
    /// CanvasPanel's own internals and its friend classes. Everything that
    /// crosses the application boundary goes through the semantic view API
    /// above or the engine session; these accessors disappear with the legacy
    /// renderer integration.
    aether::Viewport& accessViewport();
    const aether::Viewport& accessViewport() const;
    aether::Canvas& accessCanvas();
    const aether::Canvas& accessCanvas() const;

    /// Shared teardown for commitPositionPicking/cancelPositionPicking: restores
    /// overlay visibility and clears m_positionPickerActive. Returns the
    /// onPicked/onCanceled callbacks so the caller can invoke the right one
    /// after state is already consistent (in case the callback re-enters).
    std::pair<std::function<void(const QPointF&)>, std::function<void()>>
    endPositionPickingSession();

    /// Delegates to CanvasResizeController when tool is CanvasResize
    void setupCanvasResizeController();
    void setupExportAreaController();
    void updateExportAreaCursor();

private slots:
    void setBrushColor(const QColor& color);

private:
    QSize m_canvasSize;
    QRect m_exportFrame;
    bool m_infiniteExportFrameUserDefined = false;
    ruwa::core::canvas::CanvasBoundsMode m_canvasBoundsMode
        = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QWidget* m_renderPlaceholder = nullptr;
    bool m_renderContentCreated = false;
    bool m_loadingOverlayDecorationsVisible = true;
    CanvasToolStateController* m_toolStateController = nullptr;

    // === Engine binding (Stage 1 decoupling) ===
    // The binding owns the rendering engine integration: the generic host
    // widget the layout holds, the renderer-neutral session and the Qt event
    // relay. It is created by createRenderContent() and destroyed first in the
    // panel destructor, which tears the engine down deterministically.
    std::unique_ptr<CanvasEngineQtBinding> m_engineBinding;
    /// The host widget as the UI sees it (== binding->viewportHostWidget()).
    QWidget* m_viewportHostWidget = nullptr;
    /// TRANSITIONAL QUARANTINE (Stage 1): concrete legacy Aether renderer for
    /// call sites not yet migrated onto session capabilities. Owned by
    /// m_engineBinding; null until the render content is created. Every file
    /// still using it is enumerated in docs/renderer-boundary-quarantine.md.
    aether::OpenGLCanvasWidget* m_glWidget = nullptr;

    /// Overlay covering canvas before/during appearance animation (background color, fades out)
    QWidget* m_loadingOverlay = nullptr;
    QGraphicsOpacityEffect* m_loadingOverlayOpacity = nullptr;
    QPropertyAnimation* m_loadingOverlayFadeAnimation = nullptr;
    ruwa::ui::widgets::DotGridLoadingIndicator* m_loadingIndicator = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingStatusLabel = nullptr;

    // Layer model (stored for deferred application if widget not yet created)
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    CanvasParameterOverlayWidget* m_effectParameterOverlay = nullptr;
    ruwa::core::layers::LayerId m_effectParameterOverlayLayerId;
    QUuid m_effectParameterOverlayEffectId;
    QString m_effectParameterOverlayDragControlId;
    bool m_effectParameterOverlayDragging = false;

    // Brush control overlay (created in createContent for smooth appearance)
    ruwa::ui::widgets::BrushControlOverlay* m_brushOverlay = nullptr;
    ruwa::ui::widgets::CanvasToolStateOverlay* m_toolStateOverlay = nullptr;
    ruwa::ui::widgets::CanvasZoomInfoOverlay* m_zoomInfoOverlay = nullptr;
    ruwa::ui::widgets::CanvasSelectionSizeOverlay* m_selectionSizeOverlay = nullptr;
    ruwa::ui::widgets::CanvasStylusJoystickContainerWidget* m_stylusJoystick = nullptr;
    ruwa::ui::widgets::CanvasPositionPickerOverlay* m_positionPickerOverlay = nullptr;
    QGraphicsOpacityEffect* m_brushOverlayOpacity = nullptr;
    QGraphicsOpacityEffect* m_toolStateOverlayOpacity = nullptr;
    QGraphicsOpacityEffect* m_stylusJoystickOpacity = nullptr;
    ruwa::ui::widgets::ConfirmationPopup* m_confirmationPopup = nullptr;
    ruwa::ui::widgets::SelectionActionPopup* m_selectionActionPopup = nullptr;
    ruwa::ui::widgets::ColorPickerOverlay* m_selectionColorPickerOverlay = nullptr;
    QColor m_selectionFillColor = QColor(255, 255, 255, 255);
    bool m_selectionActionPopupDismissed = false;
    QPointF m_selectionPopupWorldCenter;
    bool m_selectionPopupWorldCenterValid = false;
    bool m_brushOverlayNeedsInitialPlacement = false;
    CanvasWidgetVisibility m_canvasWidgets;
    std::optional<QPoint> m_savedBrushOverlayPosition;
    std::optional<QPointF>
        m_pendingBrushOverlayPositionNormalized; ///< From restore; applied when content ready
    bool m_brushOverlayUserMoved = false; ///< User dragged overlay; use clamp+snap on resize
    bool m_toolStateOverlayUserMoved = false;
    std::optional<QPoint> m_savedToolStateOverlayPosition;
    std::optional<QPointF> m_pendingToolStateOverlayPositionNormalized;
    std::optional<QPoint> m_savedStylusJoystickPosition;
    std::optional<QPointF> m_pendingStylusJoystickPositionNormalized;
    std::optional<bool> m_savedStylusJoystickAbovePanel;
    bool m_stylusJoystickUserMoved = false; ///< User dragged joystick; use clamp+snap on resize
    QSize m_lastContentSize; ///< Previous content size for snap-to-edge on resize

    // Position picking session (see beginPositionPicking)
    bool m_positionPickerActive = false;
    std::function<void(const QPointF&)> m_positionPickerOnPicked;
    std::function<void()> m_positionPickerOnCanceled;
    CanvasWidgetVisibility m_positionPickerPrevWidgets; ///< Restored when the session ends

    // Canvas cursor manager (GL brush/eyedropper cursor when over canvas)
    CanvasCursorManager* m_cursorManager = nullptr;
    bool m_transformDragCursorValid = false;
    Qt::CursorShape m_transformDragCursor = Qt::ArrowCursor;

    // Interaction state
    bool m_isPanning = false;
    bool m_isDrawing = false;
    bool m_isLassoSelecting = false;
    bool m_isLassoFillSelecting = false;
    bool m_isRectSelecting = false;
    bool m_isCircleSelecting = false;
    bool m_canvasResizeAwaitingRotationReset = false;
    bool m_isEyedropping = false;
    QElapsedTimer m_eyedropperUpdateTimer; // Throttle eyedropper move updates (like ColorPicker)
    bool m_isZoomDragging = false;
    bool m_isRotatingView = false;
    bool m_tabletActive = false;
    bool m_dispatchingSyntheticMousePress = false;
    Qt::MouseButtons m_prevTabletButtons
        = Qt::NoButton; ///< Previous tablet buttons state for side-button detection
    std::optional<QPointF>
        m_tabletGlobalOffset; ///< Display-tablet HiDPI coordinate correction offset
    bool m_ctrlPressed = false;
    bool m_altPressed = false;
    bool m_lassoAdd = false;
    bool m_lassoSubtract = false;
    bool m_rectAdd = false;
    bool m_rectSubtract = false;
    bool m_circleAdd = false;
    bool m_circleSubtract = false;
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPointF m_lastMousePos; ///< Global float coordinates; updated during panning for sub-pixel
                            ///< precision
    QPoint m_zoomDragStartPos;
    aether::Vector2 m_zoomAnchorScreen { 0.0f, 0.0f };
    float m_zoomDragStartValue = 1.0f;
    float m_rotateViewLastAngle = 0.0f;

    /// Whether the given tool should erase right now (Eraser tool, or Brush tool
    /// with the eraser-brush state active).
    bool shouldEraseForTool(ToolId tool) const override;
    void setBrushEraserActive(bool active);

    ToolBrushState* toolBrushStateForInstrument(ToolId tool);
    const ToolBrushState* toolBrushStateForInstrument(ToolId tool) const;

    TemporaryToolHold m_tempToolHold;

    /// Tool whose brush settings still have to be pushed to the shared TileBrush and
    /// the overlay once the stroke that blocked the switch finishes.
    std::optional<ToolId> m_pendingToolStateApply;

    // Pending temp-tool key (set in ShortcutOverride, consumed in setToolMode)
    int m_pendingTempToolKey = 0;
    bool m_pendingTempToolAlwaysRevert = false;
    bool m_temporaryMoveToolUndoCooldownActive = false;
    QElapsedTimer m_temporaryMoveToolUndoCooldownTimer;

    bool m_playNewProjectAppearanceAnimation = false;
    bool m_deferLoadingOverlayHideUntilAppearanceAnimation = false;
    bool m_loadingAppearanceAnimationActive = false;
    bool m_loadingAppearanceAnimationRunning = false;
    bool m_cursorManagerSuppressedByLoading = true;
    QTimer* m_tempToolHoldPollTimer = nullptr;

    CanvasResizeController* m_canvasResizeController = nullptr;
    QSize m_canvasResizePreviewSize;
    // setupCanvasResizeController() can run more than once, but its overlay
    // signal handlers are lambdas — Qt::UniqueConnection asserts on non-PMF
    // slots, so guard the one-time wiring with this flag instead.
    bool m_canvasResizeOverlaySignalsConnected = false;
    CanvasTabletHandler* m_tabletHandler = nullptr;
    CanvasMouseInputHandler* m_mouseHandler = nullptr;
    RadialMenuController* m_radialMenuController = nullptr;
    ruwa::ui::widgets::RadialMenuWidget* m_radialMenu = nullptr;
    CanvasSelectionPopupManager* m_popupManager = nullptr;
    CanvasKeyEventHandler* m_keyHandler = nullptr;
    CanvasSpaceMoveHandler* m_spaceMoveHandler = nullptr;
    CanvasImageImportHelper* m_imageImportHelper = nullptr;
    ImageImportSelectionOverlay* m_imageImportSelectionOverlay = nullptr;
    CanvasOverlayLayoutManager* m_overlayLayoutManager = nullptr;
    CanvasViewController* m_viewController = nullptr;
    TextEditingController* m_textEditingController = nullptr;
    QList<QPointer<QWidget>> m_textEditingFocusExclusions;
    /// Open live run from the Character / Paragraph groups: the state it began
    /// at, so the whole drag or typed value collapses into one undo step.
    struct TextEditInteraction {
        bool active = false;
        ruwa::core::layers::LayerId layerId;
        ruwa::core::layers::TextLayerEdit::Property property
            = ruwa::core::layers::TextLayerEdit::Property::FontSize;
        /// False while the layer is open for editing on the canvas: that
        /// session pushes its own step on commit, and a second one from the run
        /// would split one edit in two.
        bool pushUndo = true;
        QString oldText;
        QList<ruwa::core::layers::TextStyleRun> oldRuns;
        ruwa::core::layers::TextLayerTypography oldTypography;
        aether::TransformState oldTransform;
    };
    TextEditInteraction m_textEditInteraction;
    bool m_spaceSelectionMoveActive = false;
    QPoint m_spaceSelectionMoveLastGlobalPos;
    bool m_spaceStrokeMoveActive = false;
    QPoint m_spaceStrokeMoveLastGlobalPos;

    // Export mode
    bool m_interactionEnabled = true;
    qreal m_exportModeOverlayProgress = 0.0;
    ruwa::core::exporting::ExportService* m_exportService = nullptr;
    ExportSettingsPanel* m_exportPanel = nullptr;
    ExportModeController* m_exportController = nullptr;
    ExportAreaController* m_exportAreaController = nullptr;
    QRect m_lastPublishedEffectiveExportFrame;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASPANEL_H
