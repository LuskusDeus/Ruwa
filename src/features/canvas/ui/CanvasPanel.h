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
#include "shell/docking/widgets/DockPanel.h"

#include "features/canvas-resize/CanvasResizeController.h"
#include "features/canvas/scene/Canvas.h"
#include "features/canvas/scene/Viewport.h"
#include "features/layers/model/LayerData.h"
#include "shared/types/CanvasWidgets.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QPointF>
#include <QSize>
#include <QStringList>
#include <QRect>
#include <QRectF>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QList>

#include <functional>
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
class OpenGLCanvasWidget;
}

namespace ruwa::core::layers {
class LayerModel;
}

namespace ruwa::ui::widgets {
class CanvasBrushQuickPopup;
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

namespace ruwa::ui::workspace {

class CanvasCursorManager;
class CanvasBrushQuickPopupManager;
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

class CanvasPanel : public ruwa::ui::docking::DockPanel, public CanvasInputHost {
    friend class CanvasMouseInputHandler;
    friend class CanvasBrushQuickPopupManager;
    friend class CanvasSelectionPopupManager;
    friend class CanvasSpaceMoveHandler;
    friend class CanvasImageImportHelper;
    friend class CanvasOverlayLayoutManager;
    friend class CanvasViewController;
    friend class TextEditingController;

    Q_OBJECT

public:
    using ToolMode = CanvasToolMode;
    using PersistedToolState = CanvasPersistedToolState;

    enum TransformContextActionId { TransformActionModeClassic = 1, TransformActionModeDeform = 2 };

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

    // Viewport access
    aether::Viewport& viewport();
    const aether::Viewport& viewport() const;

    // Canvas access
    aether::Canvas& canvas();
    const aether::Canvas& canvas() const;

    /// Undo manager for this canvas. Returns nullptr if GL content not yet created.
    aether::UndoManager* undoManagerOrNull();
    /// Undo manager used by Undo/Redo actions; transform mode can temporarily override it.
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
    PersistedToolState persistedToolState(ToolMode tool) const;
    void setPersistedToolState(ToolMode tool, const PersistedToolState& state);
    void reapplyCurrentToolState();
    ToolMode brushSelectionToolMode() const;
    bool selectBrushForCurrentContext(const QString& brushId);
    QString selectedBrushIdForCurrentContext() const;
    void showBrushQuickPopup(const QPoint& globalPos);
    void hideBrushQuickPopup();
    bool isBrushQuickPopupVisible() const;

    // Layer model integration
    void setLayerModel(ruwa::core::layers::LayerModel* model);
    void selectLayerContent(const ruwa::core::layers::LayerId& id);
    bool startTextLayerEditing(const ruwa::core::layers::LayerId& id);
    /// Clear raster layer pixels (GL). No-op if GL not ready or layer not editable.
    bool clearLayerPixelContent(const ruwa::core::layers::LayerId& id);
    bool rasterizeSmartLayer(const ruwa::core::layers::LayerId& id);
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
    void setToolMode(ToolMode tool) override;
    ToolMode toolMode() const
    {
        return m_toolStateController ? m_toolStateController->currentTool() : ToolMode::Hand;
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
    void enterTransformMode();
    void confirmTransform();
    void cancelTransform();
    bool isDrawingActive() const override;
    bool isTransformActive() const;

    /// Schedule appearance animation for new project (min zoom → zoom to fit).
    /// Call only when creating a new project (not when opening from file).
    void scheduleNewProjectAppearanceAnimation();
    void setDeferredAppearanceAnimation(bool deferred);
    void setLoadingOverlayDecorationsVisible(bool visible);

    /// Create the OpenGL widget (deferred until after tab transition animation).
    /// Called by WorkspaceTab::onTransitionFinishedImpl().
    /// @return true if GL content was created in this call (first time only)
    bool createGLContent();

    /// True when GL content exists and is initialized (safe to use viewport/canvas).
    bool isGLContentReady() const;

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

public slots:
    void onSimpleContextAction(int actionId);

signals:
    void glContentReady();
    void zoomChanged(qreal zoom);
    void cursorPositionChanged(const QPoint& pos);
    void canvasSizeChanged(const QSize& size);
    void exportFrameChanged(const QRect& frame);
    void canvasBoundsModeChanged(ruwa::core::canvas::CanvasBoundsMode mode);
    void canvasContentChanged();
    void canvasContentRegionChanged(const QRect& worldRect);
    void canvasContentTilesChanged(const QList<QPoint>& tilePositions);
    void fillProcessingLayerChanged(const ruwa::core::layers::LayerId& id);
    void toolModeChanged(ToolMode tool);
    void brushSelectionContextChanged(ToolMode tool, const QString& brushId);
    void transformModeChanged(bool active);
    void colorPicked(const QColor& color);
    /// Emitted when a paint stroke (not erase) completes. Use to add brush color to recent palette.
    void strokePainted();
    void brushOverlayPositionChanged(const QPoint& pos);
    void toolStateOverlayPositionChanged(const QPoint& pos);
    void stylusJoystickPositionChanged(const QPoint& pos);
    /// Joystick / brush HUD / tool bar visibility changed (sync View → Canvas widgets menu).
    void canvasWidgetsVisibilityChanged();

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
    void onGLInitialized();
    void updateStyles();
    void positionBrushOverlayDefault();
    void positionStylusJoystickDefault();
    void scheduleInitialBrushOverlayPlacement();
    void loadGlobalToolState();
    void persistGlobalToolState();
    void captureToolState(ToolMode tool);
    void applyBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings);
    void applyToolStateBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings);
    QString resolveBrushSelectionId(
        const QString& requestedBrushId, const QString& fallbackPresetId = QString()) const;
    bool applyBrushSelectionForTool(ToolMode tool, const QString& requestedBrushId,
        const QString& fallbackPresetId, bool persistSelection, bool emitSyncSignal);
    void captureCurrentToolState();
    void restoreToolState(ToolMode tool);
    /// Liquify uses a fixed standard soft brush (hardness 0, round) with no brush
    /// selection — only its size and strength (flow) are user-adjustable.
    void applyLiquifyFixedBrush();
    /// Apply color/opacity when restoring tool state (always applies; does not sync to brush state)
    void applyBrushColorForRestore(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    /// Brush, Eraser, or Blur that currently "owns" the shared size/opacity overlay (Hand → last
    /// draw tool).
    ToolMode overlayInstrumentMode() const;
    void emitBrushSelectionContextChanged();
    bool overlayMatchesInstrument(ToolMode tool) const;
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
    ToolMode currentInputTool() const override { return toolMode(); }
    aether::OpenGLCanvasWidget* inputGlWidget() const override { return m_glWidget; }
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
    bool isAnySelectionInteractionActive() const override;
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
        if (m_tempToolHold.active)
            m_tempToolHold.toolWasUsed = true;
    }
    void beginTemporaryToolHoldFromButton(Qt::MouseButton heldButton, ToolMode tool) override;
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
    std::optional<ToolMode> inputToolModeForKey(int key) const override
    {
        return toolModeForKey(key);
    }
    std::optional<ToolMode> inputToolModeForKeyEvent(const QKeyEvent* event) const override
    {
        return toolModeForKeyEvent(event);
    }
    QString commandIdForInputToolMode(ToolMode mode) const override
    {
        return commandIdForToolMode(mode);
    }
    std::optional<ToolMode> toolModeForKey(int key) const;
    std::optional<ToolMode> toolModeForKeyEvent(const QKeyEvent* event) const;
    static std::optional<ToolMode> toolModeForCommandId(const QString& cmdId);
    static QString commandIdForToolMode(ToolMode mode);
    QPointF mapWorldToPanel(const aether::Vector2& worldPos) const;
    void ensureSelectionActionPopup();
    void updateSelectionActionPopup(bool forceShow = false) override;
    void dismissSelectionActionPopupUntilSelectionReset();
    void ensureConfirmationPopup();
    void updateConfirmationPopup();
    QRectF activeSelectionRectInWidget() const;
    QRectF activeTransformRectInWidget() const;
    void createExportModeContent();

    void showBlockedDrawMessageForSelectedLayer() override;
    void showDrawOnBackgroundMessage();
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
    void notifyCanvasContentChanged() override { emit canvasContentChanged(); }

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

    aether::Vector2 mapToWorld(const QPoint& globalPos) const;
    aether::Vector2 mapToWorld(const QPointF& globalPos) const;
    aether::Vector2 mapToViewportWorld(const QPoint& globalPos) const;
    aether::Vector2 mapToViewportWorld(const QPointF& globalPos) const;
    aether::Vector2 mapInputToViewportWorld(const QPointF& globalPos) const override
    {
        return mapToViewportWorld(globalPos);
    }
    bool isGlobalOverGlViewport(const QPoint& globalPos) const;
    bool isGlobalOverGlViewport(const QPointF& globalPos) const;
    bool isGlobalOverInputViewport(const QPoint& globalPos) const override
    {
        return isGlobalOverGlViewport(globalPos);
    }
    bool isGlobalOverInputViewport(const QPointF& globalPos) const override
    {
        return isGlobalOverGlViewport(globalPos);
    }

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
    QWidget* m_glPlaceholder = nullptr;
    bool m_glContentCreated = false;
    bool m_loadingOverlayDecorationsVisible = true;
    CanvasToolStateController* m_toolStateController = nullptr;

    /// Overlay covering canvas before/during appearance animation (background color, fades out)
    QWidget* m_loadingOverlay = nullptr;
    QGraphicsOpacityEffect* m_loadingOverlayOpacity = nullptr;
    QPropertyAnimation* m_loadingOverlayFadeAnimation = nullptr;
    ruwa::ui::widgets::DotGridLoadingIndicator* m_loadingIndicator = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingStatusLabel = nullptr;

    aether::OpenGLCanvasWidget* m_glWidget = nullptr;

    // Layer model (stored for deferred application if widget not yet created)
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;

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
    bool shouldEraseForTool(ToolMode tool) const override;
    void setBrushEraserActive(bool active);

    ToolBrushState* toolBrushStateForInstrument(ToolMode tool);
    const ToolBrushState* toolBrushStateForInstrument(ToolMode tool) const;

    TemporaryToolHold m_tempToolHold;

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
    CanvasBrushQuickPopupManager* m_brushQuickPopupManager = nullptr;
    ruwa::ui::widgets::CanvasBrushQuickPopup* m_brushQuickPopup = nullptr;
    CanvasSelectionPopupManager* m_popupManager = nullptr;
    CanvasKeyEventHandler* m_keyHandler = nullptr;
    CanvasSpaceMoveHandler* m_spaceMoveHandler = nullptr;
    CanvasImageImportHelper* m_imageImportHelper = nullptr;
    ImageImportSelectionOverlay* m_imageImportSelectionOverlay = nullptr;
    CanvasOverlayLayoutManager* m_overlayLayoutManager = nullptr;
    CanvasViewController* m_viewController = nullptr;
    TextEditingController* m_textEditingController = nullptr;
    bool m_spaceSelectionMoveActive = false;
    QPoint m_spaceSelectionMoveLastGlobalPos;
    bool m_spaceStrokeMoveActive = false;
    QPoint m_spaceStrokeMoveLastGlobalPos;

    // Export mode
    bool m_interactionEnabled = true;
    qreal m_exportModeOverlayProgress = 0.0;
    ExportSettingsPanel* m_exportPanel = nullptr;
    ExportModeController* m_exportController = nullptr;
    ExportAreaController* m_exportAreaController = nullptr;
    QRect m_lastPublishedEffectiveExportFrame;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASPANEL_H
