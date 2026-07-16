// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L
// ==========================================================================

#include "app/TabletToMouseEventFilter.h"
#include "CanvasPanel.h"
#include "CanvasTabletHandler.h"
#include "CanvasMouseInputHandler.h"
#include "CanvasBrushQuickPopupManager.h"
#include "CanvasSelectionPopupManager.h"
#include "CanvasKeyEventHandler.h"
#include "CanvasSpaceMoveHandler.h"
#include "CanvasImageImportHelper.h"
#include "ImageImportSelectionOverlay.h"
#include "CanvasOverlayLayoutManager.h"
#include "CanvasOverlayLayout.h"
#include "CanvasToolStateController.h"
#include "CanvasViewController.h"
#include "TextEditingController.h"

#include "CanvasPanelHelpers.h"
#include "features/canvas/ui/CanvasOverlayContextActions.h"
#include "features/canvas/grid/GridRemap.h"
#include "shared/resources/IconProvider.h"
#include "commands/CommandExecutor.h"
#include "commands/ShortcutManager.h"
#include "features/brush/rendering/DabShapeCache.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "shared/undo/UndoManager.h"
#include "features/layers/model/LayerModel.h"
#include "features/transform/TransformState.h"
#include "shared/tiles/TileTypes.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shell/top-bar/OverlayContainer.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"
#include "features/canvas/ui/CanvasZoomInfoOverlay.h"
#include "features/canvas/ui/CanvasSelectionSizeOverlay.h"
#include "features/canvas/ui/CanvasPositionPickerOverlay.h"
#include "features/canvas/ui/CanvasBrushQuickPopup.h"
#include "features/brush/ui/BrushPackOverlay.h"
#include "features/brush/ui/BrushPackPanel.h"
#include "features/brush/manager/BrushManager.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasStylusJoystickWidget.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "features/selection/SelectionActionPopup.h"
#include "features/color/ColorPickerOverlay.h"
#include "features/color/ColorPicker.h"
#include "features/canvas/ui/CanvasCursorManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/settings/SettingsManager.h"
#include "features/export/ExportSettingsPanel.h"
#include "features/export/ExportAreaController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QVariantAnimation>
#include <QVector>
#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QTransform>
#include <functional>

#include <algorithm>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ruwa::ui::workspace {
namespace {

constexpr int kToolStateOverlayCanvasInteractivePage = 0;
constexpr int kToolStateOverlayCanvasPlaceholderPage = 1;
constexpr qint64 kTemporaryMoveToolUndoCooldownMs = 700;
QPointer<CanvasPanel> g_activeCanvasPanel;

bool isCanvasActivationEventType(QEvent::Type type)
{
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::Wheel:
        return true;
    default:
        return false;
    }
}

ruwa::core::layers::LayerData* findPaintableLayerBelow(
    ruwa::core::layers::LayerModel* layerModel, const ruwa::core::layers::LayerId& layerId)
{
    if (!layerModel || layerId.isNull()) {
        return nullptr;
    }

    const QList<ruwa::core::layers::LayerData*> flatLayers = layerModel->flattenedLayers();
    const int currentIndex = layerModel->indexInFlattenedList(layerId);
    if (currentIndex < 0) {
        return nullptr;
    }

    for (int i = currentIndex + 1; i < flatLayers.size(); ++i) {
        auto* candidate = flatLayers[i];
        if (!candidate || !candidate->visible || candidate->locked) {
            continue;
        }
        if (candidate->isRaster()) {
            return candidate;
        }
    }

    return nullptr;
}

float effectiveMaxBrushRadiusForCanvas(
    int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    return ruwa::ui::widgets::maxBrushRadiusForCanvasMode(
        canvasWidth, canvasHeight, hasFiniteDocumentBounds);
}

float brushRadiusFromNormalizedSizeForCanvas(
    qreal normalizedSize, int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    return ruwa::ui::widgets::brushRadiusFromNormalizedSizeForCanvasMode(
        normalizedSize, canvasWidth, canvasHeight, hasFiniteDocumentBounds);
}

bool isToolSwitchProfilingEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("RUWA_PROFILE_TOOL_SWITCH");
    return enabled;
}

int toolStateOverlayPageForTool(CanvasPanel::ToolMode tool)
{
    using ToolMode = CanvasPanel::ToolMode;

    switch (tool) {
    case ToolMode::Hand:
        return 0;
    case ToolMode::Brush:
        return 1;
    case ToolMode::Eraser:
        return 2;
    case ToolMode::Fill:
        return 3;
    case ToolMode::Eyedropper:
        return 4;
    case ToolMode::Lasso:
        return 5;
    case ToolMode::LassoFill:
        return 6;
    case ToolMode::SquareSelection:
        return 7;
    case ToolMode::CircleSelection:
        return 8;
    case ToolMode::Move:
        return 9;
    case ToolMode::RotateView:
        return 10;
    case ToolMode::CanvasResize:
        return 11;
    case ToolMode::Zoom:
        return 12;
    case ToolMode::ClassicFill:
        return 13;
    case ToolMode::Blur:
        return 14;
    case ToolMode::Text:
        return 15;
    case ToolMode::Smudge:
        return 16;
    case ToolMode::Liquify:
        return 17;
    }

    return 0;
}

bool shouldSyncToolStateOverlayToolPage(CanvasPanel::ToolMode tool)
{
    using ToolMode = CanvasPanel::ToolMode;
    return tool != ToolMode::Hand && tool != ToolMode::Move && tool != ToolMode::Text;
}

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

CanvasPanel::CanvasPanel(const QSize& canvasSize, const QRect& exportFrame, QWidget* parent)
    : DockPanel("Canvas", parent)
    , m_canvasSize(canvasSize)
    , m_exportFrame(exportFrame.isValid() ? exportFrame
                                          : QRect(0, 0, canvasSize.width(), canvasSize.height()))
{
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::Brush);
    setClosable(false);
    setFloatable(true);
    setTitleBarVisible(false);

    setMinimumPanelSize(200, 200);
    setPreferredPanelSize(800, 600);

    setMouseTracking(true);
    setTabletTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    // Ensure tablet-to-mouse filter runs first so it can intercept before canvas handles tablet
    TabletToMouseEventFilter::ensureRunsFirst(qobject_cast<::QApplication*>(qApp));

    m_toolStateController = new CanvasToolStateController(this);
    m_viewController = new CanvasViewController(this);

    m_tempToolHoldPollTimer = new QTimer(this);
    m_tempToolHoldPollTimer->setSingleShot(false);
    m_tempToolHoldPollTimer->setInterval(16);
    connect(m_tempToolHoldPollTimer, &QTimer::timeout, this,
        &CanvasPanel::syncTemporaryToolHoldFromPressedKeys);
    connect(&ruwa::core::CommandExecutor::instance(), &ruwa::core::CommandExecutor::commandExecuted,
        this, [this](const QString& commandId) {
            if (commandId == QLatin1String("edit.undo") && isActiveCanvasPanel()) {
                noteUndoForTemporaryMoveTool();
            }
        });

    m_textEditingController = new TextEditingController(this);

    const auto syncInvalidBrushSelections
        = [this](const QString& fallbackPresetId, bool onlyRemovedBrush,
              const QString& removedBrushId = QString()) {
              bool anyUpdated = false;
              const ToolMode activeTool = brushSelectionToolMode();
              const auto syncTool = [this, &fallbackPresetId, &anyUpdated, activeTool,
                                        onlyRemovedBrush, &removedBrushId](ToolMode tool) {
                  ToolBrushState* state = toolBrushStateForInstrument(tool);
                  if (!state || state->brushId.isEmpty()) {
                      return;
                  }
                  if (onlyRemovedBrush && state->brushId != removedBrushId) {
                      return;
                  }
                  if (!onlyRemovedBrush
                      && !ruwa::core::brushes::BrushManager::instance()
                          .presetIdForBrush(state->brushId)
                          .isEmpty()) {
                      return;
                  }

                  const QString previousBrushId = state->brushId;
                  applyBrushSelectionForTool(
                      tool, QString(), fallbackPresetId, false, tool == activeTool);
                  anyUpdated |= previousBrushId != state->brushId;
              };

              syncTool(ToolMode::Brush);
              syncTool(ToolMode::Eraser);
              syncTool(ToolMode::Blur);
              syncTool(ToolMode::Smudge);

              if (anyUpdated) {
                  persistGlobalToolState();
              }
          };

    auto& brushManager = ruwa::core::brushes::BrushManager::instance();
    connect(&brushManager, &ruwa::core::brushes::BrushManager::brushRemoved, this,
        [this, syncInvalidBrushSelections](const QString& presetId, const QString& brushId) {
            syncInvalidBrushSelections(presetId, true, brushId);
        });
    connect(&brushManager, &ruwa::core::brushes::BrushManager::presetRemoved, this,
        [this, syncInvalidBrushSelections](
            const QString&) { syncInvalidBrushSelections(QString(), false); });
    connect(&brushManager, &ruwa::core::brushes::BrushManager::dataReset, this,
        [this, syncInvalidBrushSelections]() { syncInvalidBrushSelections(QString(), false); });

    loadGlobalToolState();
}

CanvasPanel::~CanvasPanel()
{
    deactivateApplicationEventFilter();
    if (m_contentWidget) {
        m_contentWidget->removeEventFilter(this);
    }
    m_interactionEnabled = false;
    m_isDrawing = false;
    m_tabletActive = false;
    m_glContentCreated = false;

    if (m_canvasResizeController) {
        m_canvasResizeController->setGlWidget(nullptr);
    }
    if (m_exportAreaController) {
        m_exportAreaController->setGlWidget(nullptr);
    }
    if (m_cursorManager) {
        delete m_cursorManager;
        m_cursorManager = nullptr;
    }
    if (m_viewController) {
        delete m_viewController;
        m_viewController = nullptr;
    }
    if (auto* glWidget = std::exchange(m_glWidget, nullptr)) {
        disconnect(glWidget, nullptr, this, nullptr);
        glWidget->hide();
        if (m_contentLayout) {
            m_contentLayout->removeWidget(glWidget);
        }
        delete glWidget;
    }

    persistGlobalToolState();
    if (m_toolStateController) {
        m_toolStateController->flushQueuedSnapshotNoWait();
    }
}

void CanvasPanel::activateApplicationEventFilter()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app || !isVisible()) {
        return;
    }

    if (g_activeCanvasPanel && g_activeCanvasPanel != this) {
        g_activeCanvasPanel->deactivateApplicationEventFilter();
    }

    g_activeCanvasPanel = this;
    app->removeEventFilter(this);
    app->installEventFilter(this);
    TabletToMouseEventFilter::ensureRunsFirst(app);
}

void CanvasPanel::deactivateApplicationEventFilter()
{
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->removeEventFilter(this);
    }

    if (g_activeCanvasPanel == this) {
        g_activeCanvasPanel.clear();
    }
}

bool CanvasPanel::isActiveCanvasPanel() const
{
    return g_activeCanvasPanel == this;
}

void CanvasPanel::loadGlobalToolState()
{
    if (m_toolStateController) {
        m_toolStateController->loadRuntimeState();
    }
}

void CanvasPanel::persistGlobalToolState()
{
    const ToolMode currentTool = toolMode();
    if (CanvasToolStateController::isDrawInstrument(currentTool)) {
        captureToolState(currentTool);
    } else if (m_brushOverlay) {
        const ToolMode inst = overlayInstrumentMode();
        if (CanvasToolStateController::isDrawInstrument(inst)) {
            captureToolState(inst);
        }
    }

    if (m_isEyedropping) {
        return;
    }

    if (m_toolStateController) {
        m_toolStateController->queueSnapshot(
            m_toolStateController->buildSnapshot(), isToolSwitchProfilingEnabled());
    }
}

void CanvasPanel::syncToolStateOverlayContent()
{
    if (!m_toolStateOverlay) {
        return;
    }

    if (auto* undoManager = undoManagerOrNull()) {
        m_toolStateOverlay->setUndoAvailable(undoManager->canUndo());
        m_toolStateOverlay->setRedoAvailable(undoManager->canRedo());
    } else {
        m_toolStateOverlay->setUndoAvailable(false);
        m_toolStateOverlay->setRedoAvailable(false);
    }

    const auto syncToolPageParameters = [this](ToolMode tool, int pageIndex) {
        if (!m_toolStateOverlay) {
            return;
        }

        const ToolBrushState* state = toolBrushStateForInstrument(tool);
        const auto settings
            = (state && state->valid) ? state->settings : ruwa::core::brushes::BrushSettingsData {};
        if (tool == ToolMode::Blur || tool == ToolMode::Smudge || tool == ToolMode::Liquify) {
            m_toolStateOverlay->setToolPageIntensityValue(pageIndex, settings.flow);
            if (tool == ToolMode::Smudge) {
                m_toolStateOverlay->setToolPageWetMixValue(pageIndex, settings.wetMix);
            }
        } else {
            m_toolStateOverlay->setToolPageParameterValues(
                pageIndex, settings.hardness, settings.flow);
        }
    };
    syncToolPageParameters(ToolMode::Brush, toolStateOverlayPageForTool(ToolMode::Brush));
    syncToolPageParameters(ToolMode::Eraser, toolStateOverlayPageForTool(ToolMode::Eraser));
    m_toolStateOverlay->setBrushEraserMode(isBrushEraserActive());
    syncToolPageParameters(ToolMode::Blur, toolStateOverlayPageForTool(ToolMode::Blur));
    syncToolPageParameters(ToolMode::Smudge, toolStateOverlayPageForTool(ToolMode::Smudge));
    syncToolPageParameters(ToolMode::Liquify, toolStateOverlayPageForTool(ToolMode::Liquify));
    if (m_toolStateController) {
        m_toolStateOverlay->setToolPageLiquifyMode(m_toolStateController->liquifyToolMode());
    }
    m_toolStateOverlay->setToolPageStabilizationValue(
        toolStateOverlayPageForTool(ToolMode::Lasso), lassoStabilization());
    m_toolStateOverlay->setToolPageStabilizationValue(
        toolStateOverlayPageForTool(ToolMode::LassoFill), lassoFillStabilization());
    const QSize targetCanvasResizeSize = m_canvasResizePreviewSize.isValid()
        ? m_canvasResizePreviewSize
        : (m_canvasResizeController ? m_canvasResizeController->targetCanvasSize() : m_canvasSize);
    m_toolStateOverlay->setCanvasResizeInfo(m_canvasSize, targetCanvasResizeSize);

    if (m_glWidget) {
        m_toolStateOverlay->setCanvasFlipStates(
            m_glWidget->canvasContentFlipHorizontal(), m_glWidget->canvasContentFlipVertical());
    } else {
        m_toolStateOverlay->setCanvasFlipStates(false, false);
    }

    const ToolMode currentTool = toolMode();
    if (shouldSyncToolStateOverlayToolPage(currentTool)) {
        m_toolStateOverlay->setToolPageIndex(toolStateOverlayPageForTool(currentTool));
    }

    if (isTransformActive()) {
        m_toolStateOverlay->setCanvasPlaceholderText(
            tr("Parameters for transform mode are not available yet."));
        m_toolStateOverlay->setCanvasPageIndex(kToolStateOverlayCanvasPlaceholderPage);
        return;
    }

    if (!isInteractionEnabled()) {
        m_toolStateOverlay->setCanvasPlaceholderText(
            tr("Parameters for this canvas mode are not available yet."));
        m_toolStateOverlay->setCanvasPageIndex(kToolStateOverlayCanvasPlaceholderPage);
        return;
    }

    m_toolStateOverlay->setCanvasPageIndex(kToolStateOverlayCanvasInteractivePage);
}

CanvasPanel::ToolBrushState* CanvasPanel::toolBrushStateForInstrument(ToolMode tool)
{
    return m_toolStateController ? m_toolStateController->stateForInstrument(tool) : nullptr;
}

const CanvasPanel::ToolBrushState* CanvasPanel::toolBrushStateForInstrument(ToolMode tool) const
{
    return m_toolStateController ? m_toolStateController->stateForInstrument(tool) : nullptr;
}

// ==========================================================================
//   C A N V A S   P R O P E R T I E S
// ==========================================================================

void CanvasPanel::setCanvasSize(const QSize& size)
{
    const QSize normalizedSize(qMax(1, size.width()), qMax(1, size.height()));
    if (m_canvasSize == normalizedSize
        && (!hasFiniteDocumentBounds()
            || m_exportFrame == QRect(0, 0, normalizedSize.width(), normalizedSize.height()))) {
        return;
    }

    m_canvasSize = normalizedSize;
    if (hasFiniteDocumentBounds()) {
        m_exportFrame = QRect(0, 0, normalizedSize.width(), normalizedSize.height());
    } else if (!hasExportFrame()) {
        m_exportFrame = QRect(0, 0, normalizedSize.width(), normalizedSize.height());
    }
    if (m_canvasResizeController) {
        if (!hasFiniteDocumentBounds() && m_canvasResizeController->isActive()) {
            m_canvasResizeController->clearOverlay();
        }
        m_canvasResizeController->setCanvasSize(m_canvasSize);
        m_canvasResizeController->setEnabled(hasFiniteDocumentBounds());
    }
    if (m_exportAreaController) {
        m_exportAreaController->setCanvasSize(m_canvasSize);
    }
    m_canvasResizePreviewSize = m_canvasSize;
    if (m_glWidget) {
        m_glWidget->setCanvas(m_canvasSize.width(), m_canvasSize.height());
    }
    if (m_brushOverlay) {
        m_brushOverlay->setCanvasSize(m_canvasSize);
        m_brushOverlay->setHasFiniteDocumentBounds(hasFiniteDocumentBounds());
    }
    if (m_glWidget) {
        const float radius = brushRadiusFromNormalizedSizeForCanvas(brushSizeNormalized(),
            m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
        m_glWidget->brush().setRadius(radius);
        m_glWidget->updateBrushCursorStamp();
    }
    updateBrushCursorOverlayRadius();
    syncToolStateOverlayContent();
    applyZoomLimits();
    emit canvasSizeChanged(m_canvasSize);
    publishEffectiveExportFrameIfChanged();
    requestRender();
}

void CanvasPanel::setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode)
{
    if (m_canvasBoundsMode == mode) {
        return;
    }

    m_canvasBoundsMode = mode;
    if (hasFiniteDocumentBounds()) {
        m_infiniteExportFrameUserDefined = false;
        const QRect frame = normalizedExportFrame(m_exportFrame);
        m_canvasSize = frame.size();
        m_exportFrame = QRect(0, 0, frame.width(), frame.height());
    } else {
        m_infiniteExportFrameUserDefined = false;
        syncInfiniteExportFrameToContent(true);
    }

    if (m_canvasResizeController) {
        m_canvasResizeController->setCanvasSize(m_canvasSize);
        m_canvasResizeController->setEnabled(hasFiniteDocumentBounds());
    }
    if (m_exportAreaController) {
        m_exportAreaController->setCanvasBoundsMode(m_canvasBoundsMode);
    }
    if (m_glWidget) {
        m_glWidget->setCanvasBoundsMode(m_canvasBoundsMode);
        m_glWidget->setCanvas(m_canvasSize.width(), m_canvasSize.height());
        const float radius = brushRadiusFromNormalizedSizeForCanvas(brushSizeNormalized(),
            m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
        m_glWidget->brush().setRadius(radius);
        m_glWidget->updateBrushCursorStamp();
    }
    if (m_brushOverlay) {
        m_brushOverlay->setCanvasSize(m_canvasSize);
        m_brushOverlay->setHasFiniteDocumentBounds(hasFiniteDocumentBounds());
    }
    updateBrushCursorOverlayRadius();
    applyZoomLimits();

    emit canvasBoundsModeChanged(m_canvasBoundsMode);
    if (hasFiniteDocumentBounds()) {
        emit canvasSizeChanged(m_canvasSize);
    }
    publishEffectiveExportFrameIfChanged();
    requestRender();
}

bool CanvasPanel::isCanvasInputEventTarget(QObject* watched) const
{
    if (!m_glWidget || !watched) {
        return false;
    }

    if (watched == m_glWidget) {
        return true;
    }

    auto* widget = qobject_cast<QWidget*>(watched);
    return widget && m_glWidget->isAncestorOf(widget);
}

void CanvasPanel::endDrawingOnAppDeactivate()
{
    endActiveStrokeSession();
}

void CanvasPanel::applyZoomLimits()
{
    if (m_viewController)
        m_viewController->applyZoomLimits();
}

void CanvasPanel::showZoomInfoOverlay()
{
    if (m_viewController)
        m_viewController->showZoomInfoOverlay();
}

void CanvasPanel::syncZoomInfoOverlayValue()
{
    if (m_viewController)
        m_viewController->syncZoomInfoOverlayValue();
}

void CanvasPanel::positionZoomInfoOverlay()
{
    if (m_viewController)
        m_viewController->positionZoomInfoOverlay();
}

void CanvasPanel::updateSelectionSizeOverlay()
{
    if (m_viewController)
        m_viewController->updateSelectionSizeOverlay();
}

void CanvasPanel::hideSelectionSizeOverlay()
{
    if (m_viewController)
        m_viewController->hideSelectionSizeOverlay();
}

aether::Viewport& CanvasPanel::viewport()
{
    return m_viewController->viewport();
}

const aether::Viewport& CanvasPanel::viewport() const
{
    return m_viewController->viewport();
}

aether::Canvas& CanvasPanel::canvas()
{
    return m_viewController->canvas();
}

const aether::Canvas& CanvasPanel::canvas() const
{
    return m_viewController->canvas();
}

aether::UndoManager* CanvasPanel::undoManagerOrNull()
{
    return m_viewController ? m_viewController->undoManagerOrNull() : nullptr;
}

aether::UndoManager* CanvasPanel::activeUndoManagerOrNull()
{
    return m_viewController ? m_viewController->activeUndoManagerOrNull() : nullptr;
}

// ==========================================================================
//   C A M E R A   C O N T R O L S
// ==========================================================================

void CanvasPanel::setZoom(float zoom)
{
    if (m_viewController)
        m_viewController->setZoom(zoom);
}

void CanvasPanel::setZoomSmooth(float zoom)
{
    if (m_viewController)
        m_viewController->setZoomSmooth(zoom);
}

void CanvasPanel::zoomBy(float factor)
{
    if (m_viewController)
        m_viewController->zoomBy(factor);
}

void CanvasPanel::zoomAtWorldPoint(float factor, const QPointF& worldPos)
{
    if (m_viewController)
        m_viewController->zoomAtWorldPoint(factor, worldPos);
}

void CanvasPanel::zoomToFit()
{
    if (m_viewController)
        m_viewController->zoomToFit();
}

void CanvasPanel::toggleCanvasViewFlipHorizontal()
{
    if (m_viewController)
        m_viewController->toggleCanvasViewFlipHorizontal();
}

void CanvasPanel::toggleCanvasViewFlipVertical()
{
    if (m_viewController)
        m_viewController->toggleCanvasViewFlipVertical();
}

void CanvasPanel::centerCanvas()
{
    if (m_viewController)
        m_viewController->centerCanvas();
}

// ==========================================================================
//   B R U S H   C O N T R O L S
// ==========================================================================

void CanvasPanel::setEraseMode(bool erase)
{
    if (!m_glWidget)
        return;
    m_glWidget->brush().setEraseMode(erase);
}

void CanvasPanel::setBrushEraserActive(bool active)
{
    if (!m_toolStateController || !m_toolStateController->setBrushEraserActive(active)) {
        return;
    }

    // The toggle only affects the Brush tool; apply the erase mode immediately
    // when it is the active tool so the next dab reflects the new state.
    if (toolMode() == ToolMode::Brush) {
        setEraseMode(shouldEraseForTool(toolMode()));
    }

    if (m_toolStateOverlay) {
        m_toolStateOverlay->setBrushEraserMode(isBrushEraserActive());
    }

    persistGlobalToolState();
}

bool CanvasPanel::shouldEraseForTool(ToolMode tool) const
{
    return m_toolStateController && m_toolStateController->shouldEraseForTool(tool);
}

void CanvasPanel::setBlurMode(bool blur)
{
    if (!m_glWidget)
        return;
    m_glWidget->brush().setBlurMode(blur);
}

void CanvasPanel::setSmudgeMode(bool smudge)
{
    if (!m_glWidget)
        return;
    m_glWidget->brush().setSmudgeMode(smudge);
}

void CanvasPanel::setLiquifyMode(bool liquify)
{
    if (!m_glWidget)
        return;
    m_glWidget->brush().setLiquifyMode(liquify);
    if (liquify && m_toolStateController) {
        m_glWidget->brush().setLiquifyToolMode(m_toolStateController->liquifyToolMode());
    }
}

void CanvasPanel::setToolMode(ToolMode tool)
{
    const ToolMode currentTool = toolMode();
    const bool profileToolSwitch = isToolSwitchProfilingEnabled();
    QElapsedTimer totalTimer;
    QElapsedTimer stepTimer;
    auto logStep = [&](const char* step) {
        if (!profileToolSwitch) {
            return;
        }
    };
    if (profileToolSwitch) {
        totalTimer.start();
        stepTimer.start();
    }

    // Consume pending temp-tool key (set in ShortcutOverride or Space KeyPress)
    const int pendingKey = m_pendingTempToolKey;
    const bool pendingAlwaysRevert = m_pendingTempToolAlwaysRevert;
    m_pendingTempToolKey = 0;
    m_pendingTempToolAlwaysRevert = false;

    if (currentTool == tool) {
        if (profileToolSwitch) { }
        return;
    }

    // Position picking survives switches to/from the Hand tool (so the user
    // can still pan while lining up a pick) but is cancelled by any other
    // tool switch.
    if (m_positionPickerActive && currentTool != ToolMode::Hand && tool != ToolMode::Hand) {
        cancelPositionPicking();
    }

    if (m_textEditingController && m_textEditingController->isEditing()
        && currentTool == ToolMode::Text && tool != ToolMode::Text) {
        m_textEditingController->commit();
    }
    logStep("text-edit-commit");

    if (m_isLassoFillSelecting && tool != ToolMode::LassoFill) {
        m_isLassoFillSelecting = false;
        if (m_glWidget)
            m_glWidget->cancelLassoFill();
        if (QWidget::mouseGrabber() == this)
            releaseMouse();
    }
    logStep("cancel-lasso-fill");

    if (m_glWidget && tool != ToolMode::Fill) {
        m_glWidget->cancelFillPreview();
    }
    logStep("cancel-fill-preview");

    if (currentTool == ToolMode::CanvasResize && tool != ToolMode::CanvasResize) {
        if (m_canvasResizeController) {
            m_canvasResizeController->clearOverlay(true);
        }
        m_canvasResizeAwaitingRotationReset = false;
    }
    logStep("canvas-resize-exit");

    // Move-only transform commits when switching away from Move tool.
    if (currentTool == ToolMode::Move && tool != ToolMode::Move && m_glWidget
        && m_glWidget->isTransformActive() && m_glWidget->isMoveOnlyTransform()) {
        confirmTransform();
    }
    logStep("move-transform-commit");

    // Quick-shape transform mode exits when tool changes.
    if (m_glWidget && m_isDrawing && m_glWidget->isQuickShapeTransformActive()) {
        m_isDrawing = false;
        m_tabletActive = false;
        m_glWidget->endStroke();
        setEraseMode(shouldEraseForTool(currentTool));
        setBlurMode(currentTool == ToolMode::Blur);
        setSmudgeMode(currentTool == ToolMode::Smudge);
        setLiquifyMode(currentTool == ToolMode::Liquify);
        emit canvasContentChanged();
    }
    logStep("quick-shape-exit");

    // If a temporary tool hold is active and another tool is set externally,
    // cancel the hold
    if (m_tempToolHold.active) {
        std::optional<ToolMode> expectedTool;
        if (m_tempToolHold.shiftSpaceCombo) {
            expectedTool = ToolMode::RotateView;
        } else if (m_tempToolHold.heldButton != Qt::NoButton) {
            expectedTool = (m_tempToolHold.heldButton == Qt::MiddleButton
                               || m_tempToolHold.heldButton == Qt::RightButton)
                ? std::optional<ToolMode>(ToolMode::Eraser)
                : std::nullopt;
        } else {
            expectedTool = toolModeForKey(m_tempToolHold.heldKey);
        }
        if (!expectedTool || tool != *expectedTool) {
            resetTemporaryMoveToolUndoCooldown();
            m_tempToolHold = {};
            updateTemporaryToolHoldPolling();
        }
    }
    logStep("temp-tool-hold");
    hideBrushQuickPopup();

    const ToolMode previousTool = currentTool;

    // Mid-stroke Brush<->Eraser switch: only change erase mode, don't touch brush params
    const bool midStroke = m_isDrawing && m_glWidget && m_glWidget->isDrawing();
    const bool brushEraserSwitch = CanvasToolStateController::isDrawInstrument(currentTool)
        && CanvasToolStateController::isDrawInstrument(tool);

    if (!midStroke || !brushEraserSwitch) {
        // Save current tool state before switching
        if (CanvasToolStateController::isDrawInstrument(currentTool)) {
            if (m_toolStateController) {
                m_toolStateController->setLastDrawTool(currentTool);
            }
            captureCurrentToolState();
        }
    }
    logStep("capture-current-state");

    if (m_toolStateController) {
        m_toolStateController->setCurrentTool(tool);
    }
    setEraseMode(shouldEraseForTool(tool));
    setBlurMode(tool == ToolMode::Blur);
    setSmudgeMode(tool == ToolMode::Smudge);
    setLiquifyMode(tool == ToolMode::Liquify);
    syncToolStateOverlayContent();
    logStep("set-current-tool");

    if (!midStroke || !brushEraserSwitch) {
        if (CanvasToolStateController::isDrawInstrument(tool)) {
            restoreToolState(tool);
        }
    }
    logStep("restore-target-state");

    // Set up temporary tool hold if this switch was triggered by a held key
    if (pendingKey != 0 && !m_tempToolHold.active) {
        m_tempToolHold.active = true;
        m_tempToolHold.previousTool = previousTool;
        m_tempToolHold.heldKey = pendingKey;
        m_tempToolHold.toolWasUsed = false;
        m_tempToolHold.alwaysRevert = pendingAlwaysRevert;
    }
    updateTemporaryToolHoldPolling();
    logStep("setup-temp-hold");

    updateCursorManagerOverlay();
    logStep("update-cursor-overlay");
    updateToolCursor();
    logStep("update-tool-cursor");
    persistGlobalToolState();
    logStep("persist-tool-state");
    emit toolModeChanged(toolMode());
    emitBrushSelectionContextChanged();

    if (profileToolSwitch) { }
}

// ==========================================================================
//   T R A N S F O R M   M O D E
// ==========================================================================

void CanvasPanel::enterTransformMode()
{
    if (!m_glWidget || !m_glWidget->isInitialized())
        return;
    hideBrushQuickPopup();
    m_ctrlPressed = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    m_glWidget->enterTransformMode();
    updateCursorManagerOverlay();
    emit transformModeChanged(true);
    syncToolStateOverlayContent();
    updateSelectionActionPopup();
}

void CanvasPanel::confirmTransform()
{
    if (!m_glWidget)
        return;
    hideBrushQuickPopup();
    m_glWidget->confirmTransform();
    updateCursorManagerOverlay();
    emit transformModeChanged(false);
    syncToolStateOverlayContent();
    updateSelectionActionPopup();
}

void CanvasPanel::cancelTransform()
{
    if (!m_glWidget)
        return;
    hideBrushQuickPopup();
    m_glWidget->cancelTransform();
    updateCursorManagerOverlay();
    emit transformModeChanged(false);
    syncToolStateOverlayContent();
    updateSelectionActionPopup();
}

void CanvasPanel::onSimpleContextAction(int actionId)
{
    using ruwa::ui::canvas_overlay::CanvasOverlayContextActionId;

    if (actionId == static_cast<int>(CanvasOverlayContextActionId::HideBrushControl)) {
        setCanvasWidgetVisible(CanvasWidget::BrushControl, false);
        return;
    }
    if (actionId == static_cast<int>(CanvasOverlayContextActionId::HideToolStateOverlay)) {
        setCanvasWidgetVisible(CanvasWidget::ToolState, false);
        return;
    }
    if (actionId == static_cast<int>(CanvasOverlayContextActionId::HideStylusJoystick)) {
        setCanvasWidgetVisible(CanvasWidget::Joystick, false);
        return;
    }

    if (!m_glWidget || !m_glWidget->isTransformActive()) {
        return;
    }

    switch (actionId) {
    case TransformActionModeClassic:
        m_glWidget->setTransformInteractionMode(aether::TransformInteractionMode::Classic);
        break;
    case TransformActionModeDeform:
        m_glWidget->setTransformInteractionMode(aether::TransformInteractionMode::Deform);
        break;
    default:
        break;
    }
}

bool CanvasPanel::isDrawingActive() const
{
    return m_isDrawing || (m_glWidget && m_glWidget->isDrawing());
}

bool CanvasPanel::isTransformActive() const
{
    return m_glWidget && m_glWidget->isTransformActive();
}

void CanvasPanel::setBrushColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const ToolMode targetTool = overlayInstrumentMode();
    ToolBrushState* target = toolBrushStateForInstrument(targetTool);
    if (!target) {
        return;
    }
    // Hand + last paint brush, eyedropper, etc. → foreground is the brush instrument.
    if (!target->valid) {
        target->valid = true;
    }
    target->color.r = r;
    target->color.g = g;
    target->color.b = b;
    target->color.a = a;
    target->brushOpacity = static_cast<qreal>(a) / 255.0;

    if (overlayMatchesInstrument(targetTool)) {
        applyBrushColorForRestore(r, g, b, a);
    }

    persistGlobalToolState();
    if (m_brushQuickPopupManager && m_brushQuickPopupManager->isBrushQuickPopupVisible()) {
        m_brushQuickPopupManager->refreshBrushQuickPopup();
    }
}

void CanvasPanel::applyCurrentBrushColor(const QColor& color)
{
    applyBrushColorForRestore(static_cast<uint8_t>(color.red()),
        static_cast<uint8_t>(color.green()), static_cast<uint8_t>(color.blue()),
        static_cast<uint8_t>(color.alpha()));
}

void CanvasPanel::applyCurrentBrushColorPreservingOpacity(const QColor& color)
{
    const QColor current = currentBrushColor();
    const QColor colorWithCurrentAlpha(color.red(), color.green(), color.blue(), current.alpha());
    if (m_toolStateController) {
        m_toolStateController->setCurrentColor(colorWithCurrentAlpha);
    }

    if (m_brushOverlay) {
        m_brushOverlay->setBrushColor(colorWithCurrentAlpha);
    }
    if (m_glWidget) {
        m_glWidget->brush().setColor(static_cast<uint8_t>(colorWithCurrentAlpha.red()),
            static_cast<uint8_t>(colorWithCurrentAlpha.green()),
            static_cast<uint8_t>(colorWithCurrentAlpha.blue()),
            static_cast<uint8_t>(colorWithCurrentAlpha.alpha()));
    }
}

void CanvasPanel::applyBrushColorForRestore(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (m_toolStateController) {
        m_toolStateController->setCurrentColor(r, g, b, a);
    }
    if (m_brushOverlay) {
        m_brushOverlay->setBrushColor(QColor(r, g, b, a));
        m_brushOverlay->setBrushOpacity(static_cast<qreal>(a) / 255.0);
    }
    if (m_glWidget) {
        m_glWidget->brush().setColor(r, g, b, a);
    }
}

CanvasPanel::ToolMode CanvasPanel::overlayInstrumentMode() const
{
    return m_toolStateController ? m_toolStateController->overlayInstrumentMode() : ToolMode::Brush;
}

bool CanvasPanel::overlayMatchesInstrument(CanvasPanel::ToolMode tool) const
{
    return m_toolStateController && m_toolStateController->overlayMatchesInstrument(tool);
}

void CanvasPanel::setBrushRadius(float radius)
{
    if (m_glWidget) {
        const float maxBrush = effectiveMaxBrushRadiusForCanvas(
            m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
        m_glWidget->brush().setRadius(std::min(radius, maxBrush));
    }
}

qreal CanvasPanel::brushSizeNormalized() const
{
    if (m_brushOverlay) {
        return m_brushOverlay->brushSize();
    }

    if (const ToolBrushState* state = toolBrushStateForInstrument(toolMode());
        state && state->valid) {
        return state->brushSize;
    }
    const ToolMode inst = overlayInstrumentMode();
    if (const ToolBrushState* state = toolBrushStateForInstrument(inst); state && state->valid) {
        return state->brushSize;
    }
    if (m_toolStateController && m_toolStateController->brushState().valid) {
        return m_toolStateController->brushState().brushSize;
    }
    return 0.3;
}

qreal CanvasPanel::brushOpacityNormalized() const
{
    if (m_brushOverlay) {
        return m_brushOverlay->brushOpacity();
    }
    return static_cast<qreal>(brushAlpha()) / 255.0;
}

QColor CanvasPanel::currentBrushColor() const
{
    return m_toolStateController ? m_toolStateController->currentColor() : QColor(0, 0, 0, 255);
}

CanvasPanel::ToolMode CanvasPanel::brushSelectionToolMode() const
{
    return overlayInstrumentMode();
}

void CanvasPanel::setLassoStabilization(qreal stabilization)
{
    if (m_toolStateController) {
        m_toolStateController->setLassoStabilization(stabilization);
    }
    if (m_glWidget) {
        m_glWidget->setLassoStabilization(static_cast<float>(lassoStabilization()));
    }
    if (m_toolStateOverlay) {
        m_toolStateOverlay->setToolPageStabilizationValue(
            toolStateOverlayPageForTool(ToolMode::Lasso), lassoStabilization());
    }
}

void CanvasPanel::setLassoFillStabilization(qreal stabilization)
{
    if (m_toolStateController) {
        m_toolStateController->setLassoFillStabilization(stabilization);
    }
    if (m_glWidget) {
        m_glWidget->setLassoFillStabilization(static_cast<float>(lassoFillStabilization()));
    }
    if (m_toolStateOverlay) {
        m_toolStateOverlay->setToolPageStabilizationValue(
            toolStateOverlayPageForTool(ToolMode::LassoFill), lassoFillStabilization());
    }
}

CanvasPanel::PersistedToolState CanvasPanel::persistedToolState(ToolMode tool) const
{
    PersistedToolState snapshot = m_toolStateController
        ? m_toolStateController->persistedState(tool)
        : PersistedToolState {};

    if (m_brushOverlay && toolMode() == tool) {
        snapshot.brushSize = m_brushOverlay->brushSize();
        snapshot.brushOpacity = m_brushOverlay->brushOpacity();
        snapshot.color = currentBrushColor();
        snapshot.valid = true;
    }

    return snapshot;
}

void CanvasPanel::setPersistedToolState(ToolMode tool, const PersistedToolState& state)
{
    if (m_toolStateController) {
        m_toolStateController->setPersistedState(tool, state);
    }
}

void CanvasPanel::reapplyCurrentToolState()
{
    const ToolMode currentTool = toolMode();
    if (CanvasToolStateController::isDrawInstrument(currentTool)) {
        restoreToolState(currentTool);
        return;
    }
    const ToolMode inst = overlayInstrumentMode();
    if (inst == ToolMode::Eraser && m_toolStateController
        && m_toolStateController->eraserState().valid) {
        restoreToolState(ToolMode::Eraser);
    } else if (inst == ToolMode::Blur && m_toolStateController
        && m_toolStateController->blurState().valid) {
        restoreToolState(ToolMode::Blur);
    } else if (inst == ToolMode::Smudge && m_toolStateController
        && m_toolStateController->smudgeState().valid) {
        restoreToolState(ToolMode::Smudge);
    } else if (inst == ToolMode::Brush && m_toolStateController
        && m_toolStateController->brushState().valid) {
        restoreToolState(ToolMode::Brush);
    } else if (m_toolStateController && m_toolStateController->eraserState().valid) {
        restoreToolState(ToolMode::Eraser);
    } else if (m_toolStateController && m_toolStateController->blurState().valid) {
        restoreToolState(ToolMode::Blur);
    } else if (m_toolStateController && m_toolStateController->smudgeState().valid) {
        restoreToolState(ToolMode::Smudge);
    } else if (m_toolStateController && m_toolStateController->brushState().valid) {
        restoreToolState(ToolMode::Brush);
    }
}

bool CanvasPanel::selectBrushForCurrentContext(const QString& brushId)
{
    return applyBrushSelectionForTool(brushSelectionToolMode(), brushId, QString(), true, true);
}

QString CanvasPanel::selectedBrushIdForCurrentContext() const
{
    if (const ToolBrushState* state = toolBrushStateForInstrument(brushSelectionToolMode())) {
        return state->brushId;
    }
    return {};
}

void CanvasPanel::showBrushQuickPopup(const QPoint& globalPos)
{
    // The quick popup edits brush size/opacity and swaps brushes, so it only
    // makes sense for the painting tools that use a brush.
    switch (toolMode()) {
    case CanvasToolMode::Brush:
    case CanvasToolMode::Eraser:
    case CanvasToolMode::Smudge:
    case CanvasToolMode::Blur:
    case CanvasToolMode::Liquify:
        break;
    default:
        return;
    }

    if (m_brushQuickPopupManager) {
        m_brushQuickPopupManager->showBrushQuickPopup(globalPos);
    }
}

void CanvasPanel::hideBrushQuickPopup()
{
    if (m_brushQuickPopupManager) {
        m_brushQuickPopupManager->hideBrushQuickPopup();
    }
}

bool CanvasPanel::isBrushQuickPopupVisible() const
{
    return m_brushQuickPopupManager && m_brushQuickPopupManager->isBrushQuickPopupVisible();
}

void CanvasPanel::emitBrushSelectionContextChanged()
{
    const ToolMode tool = brushSelectionToolMode();
    emit brushSelectionContextChanged(tool, persistedToolState(tool).brushId);
}

void CanvasPanel::setBrushSizeNormalized(qreal size)
{
    const qreal clamped = qBound(0.0, size, 1.0);

    if (m_brushOverlay) {
        m_brushOverlay->setBrushSize(clamped);
        persistGlobalToolState();
        if (m_brushQuickPopupManager && m_brushQuickPopupManager->isBrushQuickPopupVisible()) {
            m_brushQuickPopupManager->refreshBrushQuickPopup();
        }
        return;
    }

    if (m_glWidget) {
        const float radius = brushRadiusFromNormalizedSizeForCanvas(
            clamped, m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
        m_glWidget->brush().setRadius(radius);
        m_glWidget->updateBrushCursorStamp();
    }
    updateBrushCursorOverlayRadius();
    persistGlobalToolState();
    if (m_brushQuickPopupManager && m_brushQuickPopupManager->isBrushQuickPopupVisible()) {
        m_brushQuickPopupManager->refreshBrushQuickPopup();
    }
}

void CanvasPanel::setBrushOpacityNormalized(qreal opacity)
{
    const qreal clamped = qBound(0.0, opacity, 1.0);

    if (m_brushOverlay) {
        m_brushOverlay->setBrushOpacity(clamped);
    } else if (m_glWidget) {
        QColor color = currentBrushColor();
        const uint8_t alpha = static_cast<uint8_t>(clamped * 255.0);
        color.setAlpha(alpha);
        if (m_toolStateController) {
            m_toolStateController->setCurrentColor(color);
        }
        m_glWidget->brush().setColor(static_cast<uint8_t>(color.red()),
            static_cast<uint8_t>(color.green()), static_cast<uint8_t>(color.blue()), alpha);
    }
    persistGlobalToolState();
    if (m_brushQuickPopupManager && m_brushQuickPopupManager->isBrushQuickPopupVisible()) {
        m_brushQuickPopupManager->refreshBrushQuickPopup();
    }
}

void CanvasPanel::adjustBrushSizeNormalized(qreal delta)
{
    setBrushSizeNormalized(brushSizeNormalized() + delta);
}

void CanvasPanel::adjustBrushOpacityNormalized(qreal delta)
{
    setBrushOpacityNormalized(brushOpacityNormalized() + delta);
}

QPoint CanvasPanel::brushOverlayPosition() const
{
    if (m_brushOverlay) {
        return m_brushOverlay->pos();
    }
    return m_savedBrushOverlayPosition.value_or(QPoint(-1, -1));
}

QPointF CanvasPanel::brushOverlayPositionNormalized() const
{
    if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_brushOverlay) {
        return m_overlayLayoutManager->engine()->normalizedPosition(m_brushOverlay);
    }
    if (!m_contentWidget || m_contentWidget->width() <= 0 || m_contentWidget->height() <= 0) {
        return QPointF(-1, -1);
    }
    const QPoint p = brushOverlayPosition();
    if (p.x() < 0 || p.y() < 0)
        return QPointF(-1, -1);
    return QPointF(static_cast<qreal>(p.x()) / m_contentWidget->width(),
        static_cast<qreal>(p.y()) / m_contentWidget->height());
}

void CanvasPanel::setPendingBrushOverlayPositionNormalized(const QPointF& norm)
{
    if (norm.x() >= 0 && norm.y() >= 0 && norm.x() <= 1 && norm.y() <= 1) {
        m_pendingBrushOverlayPositionNormalized = norm;
        if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_brushOverlay) {
            m_overlayLayoutManager->engine()->setNormalizedPosition(m_brushOverlay, norm);
        }
    }
}

void CanvasPanel::setBrushOverlayPositionFromNormalized(const QPointF& norm)
{
    if (norm.x() < 0 || norm.y() < 0 || norm.x() > 1 || norm.y() > 1)
        return;
    // Keep the normalized value pending even after applying it: a restored
    // position must keep tracking the content size across resizes (the engine
    // re-resolves it from the fraction on every relayout). The first apply may
    // happen while the content is still at a transient (smaller) size during
    // early layout; the engine retains the fraction until the user drags.
    m_pendingBrushOverlayPositionNormalized = norm;
    if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_brushOverlay) {
        m_overlayLayoutManager->engine()->setNormalizedPosition(m_brushOverlay, norm);
        m_savedBrushOverlayPosition = m_brushOverlay->pos();
    }
}

QPoint CanvasPanel::stylusJoystickPosition() const
{
    if (m_stylusJoystick) {
        return m_stylusJoystick->pos();
    }
    return m_savedStylusJoystickPosition.value_or(QPoint(-1, -1));
}

QPointF CanvasPanel::stylusJoystickPositionNormalized() const
{
    if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_stylusJoystick) {
        return m_overlayLayoutManager->engine()->normalizedPosition(m_stylusJoystick);
    }
    if (!m_contentWidget || m_contentWidget->width() <= 0 || m_contentWidget->height() <= 0) {
        return QPointF(-1, -1);
    }
    const QPoint p = stylusJoystickPosition();
    if (p.x() < 0 || p.y() < 0)
        return QPointF(-1, -1);
    return QPointF(static_cast<qreal>(p.x()) / m_contentWidget->width(),
        static_cast<qreal>(p.y()) / m_contentWidget->height());
}

bool CanvasPanel::stylusJoystickAbovePanel() const
{
    if (m_stylusJoystick) {
        return m_stylusJoystick->isJoystickAbovePanel();
    }
    return m_savedStylusJoystickAbovePanel.value_or(true);
}

void CanvasPanel::setPendingStylusJoystickPositionNormalized(const QPointF& norm)
{
    if (norm.x() >= 0 && norm.y() >= 0 && norm.x() <= 1 && norm.y() <= 1) {
        m_pendingStylusJoystickPositionNormalized = norm;
        if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_stylusJoystick) {
            m_overlayLayoutManager->engine()->setNormalizedPosition(m_stylusJoystick, norm);
        }
    }
}

void CanvasPanel::setPendingStylusJoystickAbovePanel(bool above)
{
    m_savedStylusJoystickAbovePanel = above;
    if (m_stylusJoystick) {
        m_stylusJoystick->setJoystickAbovePanel(above);
    }
}

void CanvasPanel::setStylusJoystickPositionFromNormalized(const QPointF& norm)
{
    if (norm.x() < 0 || norm.y() < 0 || norm.x() > 1 || norm.y() > 1)
        return;
    m_pendingStylusJoystickPositionNormalized = norm;
    if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_stylusJoystick
        && m_contentWidget && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
        if (m_savedStylusJoystickAbovePanel.has_value()) {
            m_stylusJoystick->setJoystickAbovePanel(*m_savedStylusJoystickAbovePanel);
        }
        m_overlayLayoutManager->engine()->setNormalizedPosition(m_stylusJoystick, norm);
        m_savedStylusJoystickPosition = m_stylusJoystick->pos();
        m_pendingStylusJoystickPositionNormalized.reset();
    }
}

void CanvasPanel::setPendingStylusJoystickPosition(const QPoint& pos)
{
    if (pos.x() >= 0 && pos.y() >= 0) {
        m_savedStylusJoystickPosition = pos;
        m_stylusJoystickUserMoved = true;
    }
}

void CanvasPanel::setStylusJoystickPosition(const QPoint& pos)
{
    m_savedStylusJoystickPosition = pos;
    m_stylusJoystickUserMoved = true;

    if (m_stylusJoystick && m_contentWidget && m_overlayLayoutManager
        && m_overlayLayoutManager->engine() && m_contentWidget->width() > 0
        && m_contentWidget->height() > 0) {
        if (m_savedStylusJoystickAbovePanel.has_value()) {
            m_stylusJoystick->setJoystickAbovePanel(*m_savedStylusJoystickAbovePanel);
        }
        m_savedStylusJoystickPosition = m_overlayLayoutManager->engine()->setItemPosition(
            m_stylusJoystick, pos, /*animate*/ false);
    }
}

void CanvasPanel::setPendingBrushOverlayPosition(const QPoint& pos)
{
    if (pos.x() >= 0 && pos.y() >= 0) {
        m_savedBrushOverlayPosition = pos;
        m_brushOverlayNeedsInitialPlacement = false;
        m_brushOverlayUserMoved = true;
    }
}

void CanvasPanel::setBrushOverlayPosition(const QPoint& pos)
{
    m_savedBrushOverlayPosition = pos;
    m_brushOverlayNeedsInitialPlacement = false;
    // Mark as user-moved so the overlay preserves its position on content resize.
    m_brushOverlayUserMoved = true;

    if (m_overlayLayoutManager && m_overlayLayoutManager->engine() && m_brushOverlay
        && m_contentWidget && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
        m_savedBrushOverlayPosition = m_overlayLayoutManager->engine()->setItemPosition(
            m_brushOverlay, pos, /*animate*/ false);
    }
}

QPoint CanvasPanel::toolStateOverlayPosition() const
{
    if (m_toolStateOverlay) {
        return m_toolStateOverlay->pos();
    }
    return m_savedToolStateOverlayPosition.value_or(QPoint(-1, -1));
}

QPointF CanvasPanel::toolStateOverlayPositionNormalized() const
{
    if (!m_contentWidget || m_contentWidget->width() <= 0 || m_contentWidget->height() <= 0) {
        return QPointF(-1, -1);
    }
    const QPoint p = toolStateOverlayPosition();
    if (p.x() < 0 || p.y() < 0) {
        return QPointF(-1, -1);
    }
    // Track-normalized (0..1 along movable range). Using p.x / contentWidth biases a wide strip
    // toward the right after reload when the canvas width differs from save time.
    if (!m_toolStateOverlay) {
        return QPointF(static_cast<qreal>(p.x()) / m_contentWidget->width(),
            static_cast<qreal>(p.y()) / m_contentWidget->height());
    }
    const int cw = m_contentWidget->width();
    const int ch = m_contentWidget->height();
    const int ww = m_toolStateOverlay->width();
    const int wh = m_toolStateOverlay->height();
    const int m = kCanvasOverlayEdgeMargin;
    const int trackW = cw - ww - 2 * m;
    const int trackH = ch - wh - 2 * m;
    if (trackW < 1 || trackH < 1) {
        return QPointF(static_cast<qreal>(p.x()) / cw, static_cast<qreal>(p.y()) / ch);
    }
    const qreal nx = static_cast<qreal>(p.x() - m) / static_cast<qreal>(trackW);
    const qreal ny = static_cast<qreal>(p.y() - m) / static_cast<qreal>(trackH);
    return QPointF(qBound(0.0, nx, 1.0), qBound(0.0, ny, 1.0));
}

void CanvasPanel::setPendingToolStateOverlayPositionNormalized(const QPointF& norm)
{
    Q_UNUSED(norm);
    m_pendingToolStateOverlayPositionNormalized.reset();
    m_savedToolStateOverlayPosition.reset();
    m_toolStateOverlayUserMoved = false;
    if (m_overlayLayoutManager && m_toolStateOverlay && m_contentWidget
        && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
        m_overlayLayoutManager->layoutToolStateOverlay();
    }
}

void CanvasPanel::setToolStateOverlayPositionFromNormalized(const QPointF& norm)
{
    Q_UNUSED(norm);
    m_pendingToolStateOverlayPositionNormalized.reset();
    m_savedToolStateOverlayPosition.reset();
    m_toolStateOverlayUserMoved = false;
    if (m_overlayLayoutManager && m_toolStateOverlay && m_contentWidget
        && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
        m_overlayLayoutManager->layoutToolStateOverlay();
    }
}

void CanvasPanel::setPendingToolStateOverlayPosition(const QPoint& pos)
{
    Q_UNUSED(pos);
    m_savedToolStateOverlayPosition.reset();
    m_toolStateOverlayUserMoved = false;
}

void CanvasPanel::setToolStateOverlayPosition(const QPoint& pos)
{
    Q_UNUSED(pos);
    m_savedToolStateOverlayPosition.reset();
    m_toolStateOverlayUserMoved = false;

    if (m_toolStateOverlay && m_contentWidget && m_overlayLayoutManager
        && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
        m_overlayLayoutManager->layoutToolStateOverlay();
    }
}

void CanvasPanel::applyBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings)
{
    if (!m_glWidget)
        return;
    m_glWidget->brush().setBrushSettings(settings);
    if (!settings.dabCustomImagePath.isEmpty()) {
        auto grid
            = aether::DabShapeCache::instance().getCustomAlphaGrid(settings.dabCustomImagePath,
                settings.dabThreshold, settings.dabCompression, settings.dabInterpolation);
        if (!grid.data.empty()) {
            m_glWidget->brush().setDabType(1);
            m_glWidget->brush().setDabShapeMask(grid.data.data(),
                grid.edgeDistance.empty() ? nullptr : grid.edgeDistance.data(), grid.width,
                grid.height);
        } else {
            m_glWidget->brush().setDabShapeMask(nullptr, 0, 0);
        }
    } else if (settings.dabType > 0) {
        auto grid = aether::DabShapeCache::instance().getAlphaGrid(settings.dabType);
        m_glWidget->brush().setDabShapeMask(grid.data.empty() ? nullptr : grid.data.data(),
            grid.edgeDistance.empty() ? nullptr : grid.edgeDistance.data(), grid.width,
            grid.height);
    } else {
        m_glWidget->brush().setDabShapeMask(nullptr, 0, 0);
    }

    m_glWidget->updateBrushCursorStamp();

    if (m_brushOverlay) {
        m_brushOverlay->setBrushSettings(settings);
    }
    if (m_toolStateOverlay) {
        const ToolMode instrument = overlayInstrumentMode();
        if (CanvasToolStateController::isDrawInstrument(instrument)) {
            if (ToolBrushState* state = toolBrushStateForInstrument(instrument)) {
                state->settings = settings;
                state->valid = true;
            }
            if (instrument == ToolMode::Blur || instrument == ToolMode::Smudge
                || instrument == ToolMode::Liquify) {
                const int pageIndex = toolStateOverlayPageForTool(instrument);
                m_toolStateOverlay->setToolPageIntensityValue(pageIndex, settings.flow);
                if (instrument == ToolMode::Smudge) {
                    m_toolStateOverlay->setToolPageWetMixValue(pageIndex, settings.wetMix);
                }
            } else {
                m_toolStateOverlay->setToolPageParameterValues(
                    toolStateOverlayPageForTool(instrument), settings.hardness, settings.flow);
            }
        }
    }
}

void CanvasPanel::applyToolStateBrushSettings(
    const ruwa::core::brushes::BrushSettingsData& settings)
{
    bool appliedThroughPackPanel = false;
    if (auto* packOverlay = m_brushOverlay ? m_brushOverlay->brushPackOverlay() : nullptr) {
        if (auto* packPanel = packOverlay->panel();
            packPanel && !packPanel->selectedBrushId().isEmpty()) {
            packPanel->applySettingsToSelectedBrush(settings);
            appliedThroughPackPanel = true;
        }
    }
    if (!appliedThroughPackPanel) {
        applyBrushSettings(settings);
    }
    if (!m_toolStateController || !m_toolStateController->suppressPersistDuringRestore()) {
        persistGlobalToolState();
    }
}

QString CanvasPanel::resolveBrushSelectionId(
    const QString& requestedBrushId, const QString& fallbackPresetId) const
{
    auto& manager = ruwa::core::brushes::BrushManager::instance();

    if (!requestedBrushId.isEmpty() && !manager.presetIdForBrush(requestedBrushId).isEmpty()) {
        return requestedBrushId;
    }

    if (!fallbackPresetId.isEmpty()) {
        const auto fallbackBrushes = manager.brushesForPreset(fallbackPresetId);
        if (!fallbackBrushes.isEmpty()) {
            return fallbackBrushes.first().id;
        }
    }

    const auto& presets = manager.presets();
    for (const auto& preset : presets) {
        const auto brushes = manager.brushesForPreset(preset.id);
        if (!brushes.isEmpty()) {
            return brushes.first().id;
        }
    }

    return {};
}

bool CanvasPanel::applyBrushSelectionForTool(ToolMode tool, const QString& requestedBrushId,
    const QString& fallbackPresetId, bool persistSelection, bool emitSyncSignal)
{
    ToolBrushState* state = toolBrushStateForInstrument(tool);
    if (!state) {
        return false;
    }

    // Liquify can't have its brush changed: any selection request just (re)applies
    // the fixed standard soft brush, preserving its size/strength.
    if (tool == ToolMode::Liquify) {
        applyLiquifyFixedBrush();
        return true;
    }

    const QString resolvedBrushId = resolveBrushSelectionId(requestedBrushId, fallbackPresetId);
    const bool selectionChanged = !state->valid || state->brushId != resolvedBrushId;

    ruwa::core::brushes::BrushSettingsData resolvedSettings;
    if (!resolvedBrushId.isEmpty()) {
        if (const auto managed
            = ruwa::core::brushes::BrushManager::instance().brushSettings(resolvedBrushId)) {
            resolvedSettings = *managed;
        } else {
            return false;
        }
    }

    state->brushId = resolvedBrushId;
    if (!resolvedBrushId.isEmpty()) {
        state->settings = resolvedSettings;
    } else {
        state->settings = {};
    }
    state->valid = true;

    if (brushSelectionToolMode() == tool) {
        if (!resolvedBrushId.isEmpty()) {
            if (m_glWidget) {
                applyBrushSettings(resolvedSettings);
            } else if (m_brushOverlay) {
                m_brushOverlay->setBrushSettings(resolvedSettings);
            }
        } else if (m_glWidget) {
            applyBrushSettings({});
        } else if (m_brushOverlay) {
            m_brushOverlay->setBrushSettings({});
        }

        if (auto* packOverlay = m_brushOverlay ? m_brushOverlay->brushPackOverlay() : nullptr) {
            if (auto* packPanel = packOverlay->panel()) {
                packPanel->selectBrush(resolvedBrushId);
            }
        }
    }

    if (selectionChanged && emitSyncSignal && !resolvedBrushId.isEmpty()) {
        ruwa::core::brushes::BrushManager::instance().markBrushAsRecentlyUsed(resolvedBrushId);
    }

    if (selectionChanged && persistSelection
        && (!m_toolStateController || !m_toolStateController->suppressPersistDuringRestore())) {
        persistGlobalToolState();
    }
    if (selectionChanged && emitSyncSignal) {
        emit brushSelectionContextChanged(tool, resolvedBrushId);
    }
    if (selectionChanged && m_brushQuickPopupManager
        && m_brushQuickPopupManager->isBrushQuickPopupVisible()) {
        m_brushQuickPopupManager->refreshBrushQuickPopup();
    }

    return !resolvedBrushId.isEmpty();
}

void CanvasPanel::captureToolState(ToolMode tool)
{
    ToolBrushState* state = toolBrushStateForInstrument(tool);
    if (!state) {
        return;
    }
    if (m_glWidget && m_glWidget->isDrawing()) {
        return;
    }
    // The shared overlay shows at most one instrument; never copy its size/opacity into the other.
    if (!overlayMatchesInstrument(tool)) {
        return;
    }
    // Liquify has a fixed brush: only persist its size; keep the forced soft
    // settings (and strength) instead of pulling the overlay's selected brush.
    if (tool == ToolMode::Liquify) {
        state->valid = true;
        state->brushSize = m_brushOverlay ? m_brushOverlay->brushSize() : 0.3;
        return;
    }
    state->valid = true;
    state->brushSize = m_brushOverlay ? m_brushOverlay->brushSize() : 0.3;
    state->brushOpacity = m_brushOverlay ? m_brushOverlay->brushOpacity() : 1.0;
    const QColor color = currentBrushColor();
    state->color.r = static_cast<uint8_t>(color.red());
    state->color.g = static_cast<uint8_t>(color.green());
    state->color.b = static_cast<uint8_t>(color.blue());
    // Opacity slider is the source of truth; the runtime color can lag one instrument behind.
    state->color.a = static_cast<uint8_t>(qBound(0.0, state->brushOpacity, 1.0) * 255.0);
    state->settings = m_brushOverlay ? m_brushOverlay->brushSettings()
                                     : ruwa::core::brushes::BrushSettingsData {};
}

void CanvasPanel::captureCurrentToolState()
{
    const ToolMode currentTool = toolMode();
    if (!CanvasToolStateController::isDrawInstrument(currentTool)) {
        return;
    }
    captureToolState(currentTool);
}

void CanvasPanel::applyLiquifyFixedBrush()
{
    if (!m_glWidget)
        return;

    const ToolBrushState* st = toolBrushStateForInstrument(ToolMode::Liquify);
    const qreal size = (st && st->valid && st->brushSize > 0.0) ? st->brushSize : 0.3;
    const float flow = (st && st->valid) ? qBound(0.0f, st->settings.flow, 1.0f) : 1.0f;

    // Fixed standard soft round brush: defaults except hardness 0 and the current
    // strength (flow). No dab shape / texture / brush selection — the warp only
    // needs a soft circular falloff.
    ruwa::core::brushes::BrushSettingsData s;
    s.hardness = 0.0f;
    s.spacing = 0.01f; // 1% — dense dabs for a smooth warp field
    s.flow = flow;
    applyBrushSettings(s);

    if (m_brushOverlay) {
        m_brushOverlay->setBrushSize(size);
    }
    const float radius = brushRadiusFromNormalizedSizeForCanvas(
        size, m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
    m_glWidget->brush().setRadius(radius);
    m_glWidget->updateBrushCursorStamp();
    updateBrushCursorOverlayRadius();
}

void CanvasPanel::restoreToolState(ToolMode tool)
{
    if (!m_brushOverlay)
        return;

    struct RestoreToolStatePersistSuppressor {
        CanvasToolStateController* controller;
        explicit RestoreToolStatePersistSuppressor(CanvasToolStateController* c)
            : controller(c)
        {
            if (controller) {
                controller->setSuppressPersistDuringRestore(true);
            }
        }
        ~RestoreToolStatePersistSuppressor()
        {
            if (controller) {
                controller->setSuppressPersistDuringRestore(false);
            }
        }
    };
    const RestoreToolStatePersistSuppressor suppressToolStatePersist(m_toolStateController);

    const ToolBrushState* state = toolBrushStateForInstrument(tool);
    if (!state) {
        return;
    }

    // Liquify ignores brush selection entirely: always a fixed standard soft brush.
    if (tool == ToolMode::Liquify) {
        applyLiquifyFixedBrush();
        return;
    }

    if (!state->valid) {
        // First run: no saved settings — use first brush from first pack for Brush/Blur,
        // defaults for Eraser
        if (tool == ToolMode::Eraser) {
            m_brushOverlay->setBrushSize(0.3);
            m_brushOverlay->setBrushOpacity(1.0);
            if (m_glWidget) {
                const float radius = brushRadiusFromNormalizedSizeForCanvas(
                    0.3, m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
                m_glWidget->brush().setRadius(radius);
                m_glWidget->updateBrushCursorStamp();
            }
            updateBrushCursorOverlayRadius();
        }

        if (!applyBrushSelectionForTool(tool, QString(), QString(), false, false) && m_glWidget) {
            applyBrushSettings({});
        }
        return;
    }

    if (!applyBrushSelectionForTool(tool, state->brushId, QString(), false, false) && m_glWidget) {
        applyBrushSettings(state->settings);
    }

    m_brushOverlay->setBrushSize(state->brushSize);
    const uint8_t alphaFromOpacity
        = static_cast<uint8_t>(qBound(0.0, state->brushOpacity, 1.0) * 255.0);
    applyBrushColorForRestore(state->color.r, state->color.g, state->color.b, alphaFromOpacity);

    if (m_glWidget) {
        const float radius = brushRadiusFromNormalizedSizeForCanvas(state->brushSize,
            m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
        m_glWidget->brush().setRadius(radius);
        m_glWidget->updateBrushCursorStamp();
    }
    updateBrushCursorOverlayRadius();
}

// ==========================================================================
//   C O L O R   P A N E L   I N T E G R A T I O N
// ==========================================================================

void CanvasPanel::connectColorPanel(QObject* colorPanel)
{
    if (!colorPanel)
        return;

    connect(colorPanel, SIGNAL(activeColorChanged(QColor)), this, SLOT(setBrushColor(QColor)));
}

// Slot to handle color changes from ColorPanel
void CanvasPanel::setBrushColor(const QColor& color)
{
    setBrushColor(static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha()));
}

// ==========================================================================
//   L A Y E R   M O D E L
// ==========================================================================

void CanvasPanel::setLayerModel(ruwa::core::layers::LayerModel* model)
{
    if (m_layerModel == model) {
        return;
    }

    if (m_layerModel) {
        disconnect(m_layerModel, nullptr, this, nullptr);
    }

    m_layerModel = model;
    if (m_glWidget) {
        m_glWidget->setLayerModel(model);
    }
    if (m_layerModel) {
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layersChanged, this, [this]() {
            publishEffectiveExportFrameIfChanged();
            emit canvasContentChanged();
        });
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerDataChanged, this,
            [this](const ruwa::core::layers::LayerId&) {
                publishEffectiveExportFrameIfChanged();
                emit canvasContentChanged();
            });
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerEffectsChanged, this,
            [this](const ruwa::core::layers::LayerId&, quint64) {
                publishEffectiveExportFrameIfChanged();
                emit canvasContentChanged();
            });
        connect(m_layerModel, &ruwa::core::layers::LayerModel::selectionChanged, this,
            [this](const ruwa::core::layers::LayerId&) { cancelPositionPicking(); });
    }
    publishEffectiveExportFrameIfChanged();
    // If m_glWidget doesn't exist yet, it will be applied in createContent()
}

void CanvasPanel::selectLayerContent(const ruwa::core::layers::LayerId& id)
{
    if (!m_layerModel || id.isNull()) {
        return;
    }

    auto* layer = m_layerModel->layerById(id);
    if (!layer) {
        return;
    }

    if (m_layerModel->selectedLayerId() != id) {
        m_layerModel->setSelectedLayer(id);
    }

    confirmTransform();
    if (m_glWidget) {
        m_glWidget->selectActiveLayerContent();
    }
    updateSelectionActionPopup(true);
}

bool CanvasPanel::startTextLayerEditing(const ruwa::core::layers::LayerId& id)
{
    if (!m_layerModel || !m_textEditingController || id.isNull()) {
        return false;
    }

    auto* layer = m_layerModel->layerById(id);
    if (!layer || !layer->isText()) {
        return false;
    }

    confirmTransform();
    if (toolMode() != ToolMode::Text) {
        setToolMode(ToolMode::Text);
    }
    return m_textEditingController->startExistingLayer(id);
}

bool CanvasPanel::clearLayerPixelContent(const ruwa::core::layers::LayerId& id)
{
    if (!m_glWidget || id.isNull()) {
        return false;
    }
    return m_glWidget->clearLayerPixelContent(id);
}

bool CanvasPanel::rasterizeSmartLayer(const ruwa::core::layers::LayerId& id)
{
    if (!m_glWidget || id.isNull()) {
        return false;
    }
    return m_glWidget->rasterizeSmartLayerById(id);
}

bool CanvasPanel::applyLayerMask(const ruwa::core::layers::LayerId& id)
{
    if (!m_glWidget || id.isNull()) {
        return false;
    }
    return m_glWidget->applyLayerMask(id);
}

bool CanvasPanel::invertLayerMask(const ruwa::core::layers::LayerId& id)
{
    if (!m_glWidget || id.isNull()) {
        return false;
    }
    return m_glWidget->invertLayerMask(id);
}

bool CanvasPanel::applyLayerEffects(const ruwa::core::layers::LayerId& id)
{
    if (!m_glWidget || id.isNull()) {
        return false;
    }
    return m_glWidget->applyLayerEffects(id);
}

bool CanvasPanel::fillLayerMaskFromActiveSelection(const ruwa::core::layers::LayerId& id)
{
    if (!m_glWidget || id.isNull()) {
        return false;
    }
    return m_glWidget->fillLayerMaskFromActiveSelection(id);
}

void CanvasPanel::clearSelectionMask()
{
    if (!m_glWidget) {
        return;
    }

    m_glWidget->clearSelectionMask();
    updateSelectionActionPopup();
}

bool CanvasPanel::fillSelectionWithCurrentColor()
{
    if (!m_glWidget) {
        return false;
    }

    const bool filled = m_glWidget->fillSelectionWithColor(currentBrushColor());
    if (filled) {
        emit canvasContentChanged();
    }
    updateSelectionActionPopup(true);
    return filled;
}

void CanvasPanel::showBlockedDrawMessageForSelectedLayer()
{
    if (!m_layerModel) {
        return;
    }

    auto* layer = m_layerModel->selectedLayer();
    if (!layer) {
        return;
    }

    if (layer->isBackground()) {
        showDrawOnBackgroundMessage();
        return;
    }

    if (!layer->isGroup()) {
        return;
    }

    QList<ruwa::ui::widgets::MessageButton> buttons;
    auto* layerBelow = findPaintableLayerBelow(m_layerModel, layer->id);
    if (layerBelow) {
        const auto layerBelowId = layerBelow->id;
        buttons = { { tr("Cancel"), false, []() {} },
            { tr("Select Layer Below"), true, [this, layerBelowId]() {
                 if (!m_layerModel || !m_layerModel->contains(layerBelowId)) {
                     return;
                 }
                 m_layerModel->setSelectedLayer(layerBelowId);
             } } };
    } else {
        buttons = { { tr("OK"), true, []() { } } };
    }

    const QString message = layerBelow
        ? tr("The selected group cannot be drawn on. Select the layer below instead?")
        : tr("The selected group cannot be drawn on, and there is no drawable raster layer below "
             "it.");

    ruwa::ui::widgets::MessagePopupManager::show(this, message, buttons, 360);
}

void CanvasPanel::showDrawOnBackgroundMessage()
{
    ruwa::ui::widgets::MessagePopupManager::show(this,
        tr("The Background layer cannot be drawn on. Create a new empty layer above it?"),
        { { tr("Cancel"), false, []() {} },
            { tr("Create New Layer"), true,
                [this]() {
                    if (!m_layerModel)
                        return;
                    auto* newLayer = m_layerModel->createLayer(tr("Layer"), 0);
                    if (newLayer) {
                        m_layerModel->setSelectedLayer(newLayer->id);
                    }
                } } },
        360);
}

// ==========================================================================
//   R E N D E R I N G
// ==========================================================================

void CanvasPanel::notifyContentChanged()
{
    publishEffectiveExportFrameIfChanged();
    emit canvasContentChanged();
}

void CanvasPanel::refreshZoomLimits()
{
    applyZoomLimits();
}

void CanvasPanel::requestRender()
{
    if (m_glWidget && m_glWidget->isInitialized()) {
        m_glWidget->requestRender();
    }
    if (m_selectionActionPopup || m_confirmationPopup
        || (m_glWidget && (m_glWidget->hasSelectionMask() || m_glWidget->isTransformActive()))
        || (m_canvasResizeController && m_canvasResizeController->isActive())) {
        updateSelectionActionPopup();
    }
}

// ==========================================================================
//   C A N V A S   R E A D I N E S S
// ==========================================================================

bool CanvasPanel::isGLContentReady() const
{
    return m_glWidget && m_glWidget->isInitialized();
}

void CanvasPanel::onThemeChanged()
{
    if (m_loadingOverlay && m_loadingOverlay->isVisible()) {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        m_loadingOverlay->setStyleSheet(QString(R"(
            QWidget {
                background-color: %1;
            }
            QLabel#canvasLoadingTitle {
                color: %2;
                font-size: 18px;
                font-weight: 600;
                background: transparent;
            }
            QLabel#canvasLoadingStatus {
                color: %3;
                font-size: 12px;
                background: transparent;
            }
        )")
                .arg(colors.background.name(), colors.text.name(), colors.textMuted.name()));
        if (m_loadingIndicator) {
            m_loadingIndicator->setAccentColor(colors.primary);
        }
    }
    updateStyles();
    if (m_toolStateOverlay && m_overlayLayoutManager) {
        m_overlayLayoutManager->layoutToolStateOverlay();
    }
    requestRender();
}

void CanvasPanel::showEvent(QShowEvent* event)
{
    DockPanel::showEvent(event);
    updateLoadingOverlayGeometry();
    if (m_overlayLayoutManager) {
        m_overlayLayoutManager->scheduleInitialBrushOverlayPlacement();
        if (!m_stylusJoystickUserMoved) {
            m_overlayLayoutManager->positionStylusJoystickDefault();
        }
    }
    activateApplicationEventFilter();
}

void CanvasPanel::hideEvent(QHideEvent* event)
{
    deactivateApplicationEventFilter();
    DockPanel::hideEvent(event);
}

// ==========================================================================
//   M O U S E   E V E N T S
// ==========================================================================

aether::Vector2 CanvasPanel::mapToWorld(const QPoint& globalPos) const
{
    return m_viewController ? m_viewController->mapToWorld(globalPos)
                            : aether::Vector2 { 0.0f, 0.0f };
}

aether::Vector2 CanvasPanel::mapToViewportWorld(const QPoint& globalPos) const
{
    return m_viewController ? m_viewController->mapToViewportWorld(globalPos)
                            : aether::Vector2 { 0.0f, 0.0f };
}

aether::Vector2 CanvasPanel::mapToWorld(const QPointF& globalPos) const
{
    return m_viewController ? m_viewController->mapToWorld(globalPos)
                            : aether::Vector2 { 0.0f, 0.0f };
}

aether::Vector2 CanvasPanel::mapToViewportWorld(const QPointF& globalPos) const
{
    return m_viewController ? m_viewController->mapToViewportWorld(globalPos)
                            : aether::Vector2 { 0.0f, 0.0f };
}

bool CanvasPanel::isGlobalOverGlViewport(const QPoint& globalPos) const
{
    return m_viewController && m_viewController->isGlobalOverGlViewport(globalPos);
}

bool CanvasPanel::isGlobalOverGlViewport(const QPointF& globalPos) const
{
    return m_viewController && m_viewController->isGlobalOverGlViewport(globalPos);
}

QPointF CanvasPanel::mapWorldToPanel(const aether::Vector2& worldPos) const
{
    return m_viewController ? m_viewController->mapWorldToPanel(worldPos) : QPointF();
}

void CanvasPanel::ensureSelectionActionPopup()
{
    if (m_popupManager) {
        m_popupManager->ensureSelectionActionPopup();
    }
}

void CanvasPanel::ensureConfirmationPopup()
{
    if (m_popupManager) {
        m_popupManager->ensureConfirmationPopup();
    }
}

QRectF CanvasPanel::activeSelectionRectInWidget() const
{
    if (!m_glWidget) {
        return QRectF();
    }
    QRectF worldRect;
    if (!m_glWidget->selectionBoundsWorld(worldRect) || worldRect.isEmpty()) {
        return QRectF();
    }

    // Project all four corners, then build screen-space AABB.
    // Using only TL/BR is incorrect when camera rotation/zoom animation is active.
    const QPointF p1 = mapWorldToPanel(
        { static_cast<float>(worldRect.left()), static_cast<float>(worldRect.top()) });
    const QPointF p2 = mapWorldToPanel(
        { static_cast<float>(worldRect.right()), static_cast<float>(worldRect.top()) });
    const QPointF p3 = mapWorldToPanel(
        { static_cast<float>(worldRect.right()), static_cast<float>(worldRect.bottom()) });
    const QPointF p4 = mapWorldToPanel(
        { static_cast<float>(worldRect.left()), static_cast<float>(worldRect.bottom()) });

    const qreal minX = qMin(qMin(p1.x(), p2.x()), qMin(p3.x(), p4.x()));
    const qreal maxX = qMax(qMax(p1.x(), p2.x()), qMax(p3.x(), p4.x()));
    const qreal minY = qMin(qMin(p1.y(), p2.y()), qMin(p3.y(), p4.y()));
    const qreal maxY = qMax(qMax(p1.y(), p2.y()), qMax(p3.y(), p4.y()));
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

QRectF CanvasPanel::activeTransformRectInWidget() const
{
    if (!m_glWidget || !m_glWidget->isTransformActive()) {
        return QRectF();
    }
    const aether::Rect worldRect = m_glWidget->transformController().state().transformedAABB();
    const QPointF p1 = mapWorldToPanel({ worldRect.left(), worldRect.top() });
    const QPointF p2 = mapWorldToPanel({ worldRect.right(), worldRect.top() });
    const QPointF p3 = mapWorldToPanel({ worldRect.right(), worldRect.bottom() });
    const QPointF p4 = mapWorldToPanel({ worldRect.left(), worldRect.bottom() });

    const qreal minX = qMin(qMin(p1.x(), p2.x()), qMin(p3.x(), p4.x()));
    const qreal maxX = qMax(qMax(p1.x(), p2.x()), qMax(p3.x(), p4.x()));
    const qreal minY = qMin(qMin(p1.y(), p2.y()), qMin(p3.y(), p4.y()));
    const qreal maxY = qMax(qMax(p1.y(), p2.y()), qMax(p3.y(), p4.y()));
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

void CanvasPanel::updateSelectionActionPopup(bool forceShow)
{
    if (m_popupManager) {
        m_popupManager->updateSelectionActionPopup(forceShow);
    }
}

void CanvasPanel::dismissSelectionActionPopupUntilSelectionReset()
{
    if (m_popupManager) {
        m_popupManager->dismissSelectionActionPopupUntilSelectionReset();
    }
}

void CanvasPanel::updateConfirmationPopup()
{
    if (m_popupManager) {
        m_popupManager->updateConfirmationPopup();
    }
}

void CanvasPanel::resizeEvent(QResizeEvent* event)
{
    const QSize contentSizeBeforeLayout = m_contentWidget ? m_contentWidget->size() : QSize();
    DockPanel::resizeEvent(event);
    if (m_overlayLayoutManager) {
        m_overlayLayoutManager->scheduleInitialBrushOverlayPlacement();
        if (!m_stylusJoystickUserMoved) {
            m_overlayLayoutManager->positionStylusJoystickDefault();
        }
    }
    // After dock splitter drags, child geometry updates post–resizeEvent; reclamp tool strip like
    // brush/joystick.
    if (m_overlayLayoutManager && m_toolStateOverlay && m_contentWidget) {
        QTimer::singleShot(0, this, [this, contentSizeBeforeLayout]() {
            if (!m_overlayLayoutManager || !m_contentWidget || !m_toolStateOverlay) {
                return;
            }
            const QSize newSz = m_contentWidget->size();
            if (newSz.width() <= 0 || newSz.height() <= 0) {
                return;
            }
            if (m_pendingToolStateOverlayPositionNormalized.has_value()) {
                setToolStateOverlayPositionFromNormalized(
                    *m_pendingToolStateOverlayPositionNormalized);
            } else {
                m_overlayLayoutManager->repositionToolStateOverlayOnResize(
                    newSz, contentSizeBeforeLayout);
            }
        });
    }
    updateSelectionActionPopup();
}

void CanvasPanel::wheelEvent(QWheelEvent* event)
{
    if (!isInteractionEnabled()) {
        DockPanel::wheelEvent(event);
        return;
    }
    if (!handleWheelZoom(event)) {
        DockPanel::wheelEvent(event);
    }
}

bool CanvasPanel::handleWheelZoom(QWheelEvent* event)
{
    if (m_cursorManager) {
        // The application-level input arbiter has selected the wheel's mouse
        // source before this handler runs. Refresh the custom cursor immediately
        // so it cannot remain at the previous direct-WinTab sample.
        m_cursorManager->refreshCursorPosition();
    }
    return m_viewController && m_viewController->handleWheelZoom(event);
}

void CanvasPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!isInteractionEnabled()) {
        DockPanel::dragEnterEvent(event);
        return;
    }
    if (m_imageImportHelper && m_imageImportHelper->canImportImageFromMime(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    DockPanel::dragEnterEvent(event);
}

void CanvasPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (m_imageImportHelper && m_imageImportHelper->canImportImageFromMime(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    DockPanel::dragMoveEvent(event);
}

void CanvasPanel::dropEvent(QDropEvent* event)
{
    if (m_imageImportHelper) {
        if (m_imageImportHelper->importImageFromMime(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
    }
    DockPanel::dropEvent(event);
}

// ==========================================================================
//   T A B L E T   E V E N T S
// ==========================================================================

void CanvasPanel::forwardTabletEvent(QTabletEvent* event)
{
    tabletEvent(event);
}

void CanvasPanel::tabletEvent(QTabletEvent* event)
{
    if (!isInteractionEnabled()) {
        event->ignore();
        return;
    }
    if (m_tabletHandler) {
        m_tabletHandler->handleTabletEvent(event);
    } else {
        event->ignore();
    }
}

void CanvasPanel::mousePressEvent(QMouseEvent* event)
{
    if (!isInteractionEnabled()) {
        if (m_exportAreaController && m_exportAreaController->isActive() && m_glWidget) {
            const QPoint globalPos = event->globalPosition().toPoint();
            const QPoint localPos = m_glWidget->mapFromGlobal(globalPos);
            if (m_exportAreaController->handleMousePress(
                    mapToWorld(event->globalPosition()), globalPos, localPos, event->button())) {
                updateExportAreaCursor();
                event->accept();
                return;
            }
        }
        DockPanel::mousePressEvent(event);
        return;
    }
    if (m_mouseHandler && m_mouseHandler->handleMousePress(event)) {
        return;
    }
    DockPanel::mousePressEvent(event);
}

void CanvasPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!isInteractionEnabled()) {
        DockPanel::mouseDoubleClickEvent(event);
        return;
    }
    if (m_mouseHandler && m_mouseHandler->handleMouseDoubleClick(event)) {
        return;
    }
    DockPanel::mouseDoubleClickEvent(event);
}

void CanvasPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!isInteractionEnabled()) {
        if (m_exportAreaController && m_exportAreaController->isActive() && m_glWidget) {
            const QPoint globalPos = event->globalPosition().toPoint();
            const QPoint localPos = m_glWidget->mapFromGlobal(globalPos);
            if (m_exportAreaController->handleMouseMove(
                    mapToWorld(event->globalPosition()), globalPos, localPos)) {
                updateExportAreaCursor();
                event->accept();
                return;
            }
            updateExportAreaCursor();
        }
        DockPanel::mouseMoveEvent(event);
        return;
    }
    if (m_mouseHandler && m_mouseHandler->handleMouseMove(event)) {
        return;
    }
    DockPanel::mouseMoveEvent(event);
}

void CanvasPanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (!isInteractionEnabled()) {
        if (m_exportAreaController && m_exportAreaController->isActive() && m_glWidget) {
            const QPoint globalPos = event->globalPosition().toPoint();
            if (m_exportAreaController->handleMouseRelease(
                    mapToWorld(event->globalPosition()), globalPos, event->button())) {
                updateExportAreaCursor();
                event->accept();
                return;
            }
            updateExportAreaCursor();
        }
        DockPanel::mouseReleaseEvent(event);
        return;
    }
    if (m_mouseHandler && m_mouseHandler->handleMouseRelease(event)) {
        event->accept();
    } else {
        DockPanel::mouseReleaseEvent(event);
    }

    if (!isAnySelectionInteractionActive() && m_spaceSelectionMoveActive) {
        endSpaceSelectionMove();
    }
    if (!isAnySelectionInteractionActive() && QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    updateSelectionActionPopup();
    if (m_textEditingController && m_textEditingController->isEditing()) {
        m_textEditingController->refreshFormattingPopup();
    }
}

// ==========================================================================
//   K E Y   E V E N T S
// ==========================================================================

void CanvasPanel::keyPressEvent(QKeyEvent* event)
{
    if (!isInteractionEnabled()) {
        DockPanel::keyPressEvent(event);
        return;
    }
    if (m_canvasResizeController && m_canvasResizeController->isActive()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            m_canvasResizeController->applySelection();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            m_canvasResizeController->clearOverlay();
            event->accept();
            return;
        }
    }

    if (m_glWidget && m_glWidget->isTransformActive()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            confirmTransform();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            cancelTransform();
            event->accept();
            return;
        }
    }
    DockPanel::keyPressEvent(event);
}

void CanvasPanel::keyReleaseEvent(QKeyEvent* event)
{
    DockPanel::keyReleaseEvent(event);
}

bool CanvasPanel::eventFilter(QObject* watched, QEvent* event)
{
    const bool canvasInputTarget = isCanvasInputEventTarget(watched);
    if (canvasInputTarget && isCanvasActivationEventType(event->type()) && !isActiveCanvasPanel()) {
        activateApplicationEventFilter();
    }

    if (!isActiveCanvasPanel() && !canvasInputTarget) {
        return DockPanel::eventFilter(watched, event);
    }

    if (canvasInputTarget && !isInteractionEnabled()) {
        if (m_exportAreaController && m_exportAreaController->isActive() && m_glWidget) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                const QPoint globalPos = mouseEvent->globalPosition().toPoint();
                const QPoint localPos = m_glWidget->mapFromGlobal(globalPos);
                if (m_exportAreaController->handleMousePress(
                        mapToWorld(mouseEvent->globalPosition()), globalPos, localPos,
                        mouseEvent->button())) {
                    updateExportAreaCursor();
                    return true;
                }
                break;
            }
            case QEvent::MouseMove: {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                const QPoint globalPos = mouseEvent->globalPosition().toPoint();
                const QPoint localPos = m_glWidget->mapFromGlobal(globalPos);
                if (m_exportAreaController->handleMouseMove(
                        mapToWorld(mouseEvent->globalPosition()), globalPos, localPos)) {
                    updateExportAreaCursor();
                    return true;
                }
                updateExportAreaCursor();
                return true;
            }
            case QEvent::MouseButtonRelease: {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (m_exportAreaController->handleMouseRelease(
                        mapToWorld(mouseEvent->globalPosition()),
                        mouseEvent->globalPosition().toPoint(), mouseEvent->button())) {
                    updateExportAreaCursor();
                    return true;
                }
                updateExportAreaCursor();
                return true;
            }
            default:
                break;
            }
        }
        // Block all pointer/wheel input while interaction is disabled (e.g. export mode)
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::Wheel:
            return true; // Consume and discard
        default:
            break;
        }
    }
    if (canvasInputTarget) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (m_mouseHandler
                && m_mouseHandler->handleMousePress(static_cast<QMouseEvent*>(event))) {
                return true;
            }
            break;
        case QEvent::MouseButtonDblClick:
            if (m_mouseHandler
                && m_mouseHandler->handleMouseDoubleClick(static_cast<QMouseEvent*>(event))) {
                return true;
            }
            break;
        case QEvent::MouseMove:
            if (m_mouseHandler
                && m_mouseHandler->handleMouseMove(static_cast<QMouseEvent*>(event))) {
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_mouseHandler
                && m_mouseHandler->handleMouseRelease(static_cast<QMouseEvent*>(event))) {
                if (!isAnySelectionInteractionActive() && m_spaceSelectionMoveActive) {
                    endSpaceSelectionMove();
                }
                if (!isAnySelectionInteractionActive() && QWidget::mouseGrabber() == this) {
                    releaseMouse();
                }
                updateSelectionActionPopup();
                return true;
            }
            break;
        case QEvent::TabletPress:
            [[fallthrough]];
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
            if (m_tabletHandler) {
                m_tabletHandler->handleTabletEvent(static_cast<QTabletEvent*>(event));
                if (event->isAccepted()) {
                    return true;
                }
            }
            break;
        case QEvent::Wheel:
            if (handleWheelZoom(static_cast<QWheelEvent*>(event))) {
                return true;
            }
            break;
        default:
            break;
        }
    }

    // When a tablet stroke is in progress, continue routing tablet events to
    // the handler even when the stylus moves outside the GL viewport.  This is
    // the tablet equivalent of grabMouse() which only affects mouse events.
    if (!canvasInputTarget && m_tabletActive && m_isDrawing && m_tabletHandler) {
        if (event->type() == QEvent::TabletMove || event->type() == QEvent::TabletRelease) {
            m_tabletHandler->handleTabletEvent(static_cast<QTabletEvent*>(event));
            if (event->isAccepted()) {
                return true;
            }
        }
    }

    if (!isInteractionEnabled()
        && (watched == this || watched == m_contentWidget || watched == m_glWidget)
        && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove
            || event->type() == QEvent::Drop)) {
        return true; // Block drag/drop in export mode
    }

    if (m_imageImportHelper
        && (watched == this || watched == m_contentWidget || watched == m_glWidget)
        && event->type() == QEvent::DragEnter) {
        auto* de = static_cast<QDragEnterEvent*>(event);
        if (m_imageImportHelper->canImportImageFromMime(de->mimeData())) {
            de->acceptProposedAction();
            return true;
        }
    } else if (m_imageImportHelper
        && (watched == this || watched == m_contentWidget || watched == m_glWidget)
        && event->type() == QEvent::DragMove) {
        auto* dme = static_cast<QDragMoveEvent*>(event);
        if (m_imageImportHelper->canImportImageFromMime(dme->mimeData())) {
            dme->acceptProposedAction();
            return true;
        }
    } else if (m_imageImportHelper
        && (watched == this || watched == m_contentWidget || watched == m_glWidget)
        && event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        if (m_imageImportHelper->importImageFromMime(de->mimeData())) {
            de->acceptProposedAction();
            return true;
        }
    }

    if (watched == m_contentWidget && event->type() == QEvent::Resize) {
        auto* resizeEvent = static_cast<QResizeEvent*>(event);
        QSize newSize = resizeEvent->size();
        QSize oldSize = resizeEvent->oldSize();
        if (m_overlayLayoutManager) {
            m_overlayLayoutManager->onContentResized(newSize, oldSize);
        }
        positionZoomInfoOverlay();
        updateLoadingOverlayGeometry();
        if (m_canvasResizeController
            && (m_canvasResizeController->isActive()
                || m_canvasResizeController->isInteractionActive())) {
            m_canvasResizeController->updateOverlay();
        }
        if (m_exportAreaController && m_exportAreaController->isActive()) {
            m_exportAreaController->updateOverlay();
        }
        if (m_textEditingController && m_textEditingController->isEditing()) {
            m_textEditingController->refreshFormattingPopup();
        }
    } else if (m_textEditingController && m_textEditingController->isEditing()
        && m_textEditingController->isEditorEventTarget(watched)
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::ShortcutOverride)) {
        return DockPanel::eventFilter(watched, event);
    } else if (m_textEditingController && m_textEditingController->isEditing()
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::ShortcutOverride)
        && !m_textEditingController->isEditorEventTarget(watched)
        && (watched == this || watched == m_contentWidget || watched == m_glWidget)) {
        // When focus did not land on the off-screen editor, keep canvas
        // shortcuts from eating typing keys and pass the key press to the
        // editor that owns the text document.
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (event->type() == QEvent::ShortcutOverride) {
            m_textEditingController->ensureEditorHasFocus();
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::KeyPress) {
                m_textEditingController->cancel();
            }
            return true;
        }
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            if (event->type() == QEvent::KeyPress) {
                m_textEditingController->commit();
            }
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            m_textEditingController->handleRedirectedKeyPress(keyEvent);
        } else {
            m_textEditingController->ensureEditorHasFocus();
        }
        return true;
    } else if (isInteractionEnabled() && m_keyHandler
        && m_keyHandler->handleEvent(watched, event)) {
        return true;
    } else if (event->type() == QEvent::TabletLeaveProximity) {
        endActiveStrokeSession();
    } else if ((event->type() == QEvent::TabletMove || event->type() == QEvent::TabletPress
                   || event->type() == QEvent::TabletRelease)
        && m_cursorManager) {
        // Don't update cursor when an external widget has mouse grab (e.g. dragging dock splitter,
        // slider in another panel) — TabletToMouseEventFilter will synthesize for the grabber.
        QWidget* grabber = QWidget::mouseGrabber();
        if (!grabber || isAncestorOf(grabber)) {
            auto* te = static_cast<QTabletEvent*>(event);
            m_cursorManager->updateCursorPosition(te->globalPosition().toPoint());
        }
    }
    return DockPanel::eventFilter(watched, event);
}

bool CanvasPanel::isAnySelectionInteractionActive() const
{
    return m_isLassoSelecting || m_isLassoFillSelecting || m_isRectSelecting || m_isCircleSelecting
        || (m_canvasResizeController && m_canvasResizeController->isSelectingOrMoving());
}

void CanvasPanel::endActiveStrokeSession()
{
    if (!m_glWidget || (!m_isDrawing && !m_glWidget->isDrawing())) {
        return;
    }

    m_isDrawing = false;
    m_tabletActive = false;
    m_prevTabletButtons = Qt::NoButton;
    if (m_spaceStrokeMoveActive) {
        endSpaceStrokeMove();
    }
    if (QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    m_glWidget->endStroke();
    const ToolMode currentTool = toolMode();
    setEraseMode(shouldEraseForTool(currentTool));
    setBlurMode(currentTool == ToolMode::Blur);
    setSmudgeMode(currentTool == ToolMode::Smudge);
    setLiquifyMode(currentTool == ToolMode::Liquify);
    emit canvasContentChanged();
}

void CanvasPanel::beginSpaceSelectionMove()
{
    if (m_spaceMoveHandler)
        m_spaceMoveHandler->beginSpaceSelectionMove();
}

void CanvasPanel::moveActiveSelectionWithSpace(const QPoint& globalPos)
{
    if (m_spaceMoveHandler)
        m_spaceMoveHandler->moveActiveSelectionWithSpace(globalPos);
}

void CanvasPanel::endSpaceSelectionMove()
{
    if (m_spaceMoveHandler)
        m_spaceMoveHandler->endSpaceSelectionMove();
}

void CanvasPanel::beginSpaceStrokeMove()
{
    if (m_spaceMoveHandler)
        m_spaceMoveHandler->beginSpaceStrokeMove();
}

void CanvasPanel::moveActiveStrokeWithSpace(const QPoint& globalPos)
{
    if (m_spaceMoveHandler)
        m_spaceMoveHandler->moveActiveStrokeWithSpace(globalPos);
}

void CanvasPanel::endSpaceStrokeMove()
{
    if (m_spaceMoveHandler)
        m_spaceMoveHandler->endSpaceStrokeMove();
}

bool CanvasPanel::isTransformInputActive() const
{
    return m_glWidget && m_glWidget->isTransformActive();
}

void CanvasPanel::beginTemporaryToolHoldFromButton(Qt::MouseButton heldButton, ToolMode tool)
{
    if (m_tempToolHold.active) {
        return;
    }

    m_tempToolHold.active = true;
    m_tempToolHold.previousTool = toolMode();
    m_tempToolHold.heldKey = 0;
    m_tempToolHold.heldButton = heldButton;
    m_tempToolHold.toolWasUsed = false;
    m_tempToolHold.alwaysRevert = true;
    m_tempToolHold.shiftSpaceCombo = false;
    setToolMode(tool);
}

void CanvasPanel::setPendingTemporaryToolKey(int key, bool alwaysRevert)
{
    m_pendingTempToolKey = key;
    m_pendingTempToolAlwaysRevert = alwaysRevert;
}

void CanvasPanel::clearPendingTemporaryToolKey()
{
    m_pendingTempToolKey = 0;
    m_pendingTempToolAlwaysRevert = false;
}

void CanvasPanel::updateInputCursorPosition(const QPoint& globalPos)
{
    if (m_cursorManager) {
        m_cursorManager->updateCursorPosition(globalPos);
    }
}

bool CanvasPanel::shouldIgnoreTabletInputForOverlay(
    const QPointF& globalPos, bool activeTabletStroke) const
{
    if (activeTabletStroke) {
        return false;
    }

    if (m_brushOverlay && m_brushOverlay->isVisible()) {
        const QRect overlayRect(m_brushOverlay->mapToGlobal(QPoint(0, 0)), m_brushOverlay->size());
        if (overlayRect.contains(globalPos.toPoint())) {
            return true;
        }
    }

    if (m_selectionActionPopup && m_selectionActionPopup->isVisible()) {
        const QRect popupRect(
            m_selectionActionPopup->mapToGlobal(QPoint(0, 0)), m_selectionActionPopup->size());
        if (popupRect.contains(globalPos.toPoint())) {
            return true;
        }
    }

    if (m_confirmationPopup && m_confirmationPopup->isVisible()) {
        const QRect popupRect(
            m_confirmationPopup->mapToGlobal(QPoint(0, 0)), m_confirmationPopup->size());
        if (popupRect.contains(globalPos.toPoint())) {
            return true;
        }
    }

    if (m_selectionColorPickerOverlay && m_selectionColorPickerOverlay->isActive()
        && m_selectionColorPickerOverlay->picker()
        && m_selectionColorPickerOverlay->picker()->isVisible()) {
        auto* picker = m_selectionColorPickerOverlay->picker();
        const QRect pickerRect(picker->mapToGlobal(QPoint(0, 0)), picker->size());
        if (pickerRect.contains(globalPos.toPoint())) {
            return true;
        }
    }

    return false;
}

bool CanvasPanel::routeTabletInputToStylusJoystick(QTabletEvent* event, const QPointF& globalPos,
    Qt::MouseButton effectiveButton, bool activeTabletStroke)
{
    if (activeTabletStroke || !event || !m_stylusJoystick || !m_stylusJoystick->isVisible()) {
        return false;
    }

    const QPointF localPos = m_stylusJoystick->mapFromGlobal(globalPos);
    const bool routeToJoystick = m_stylusJoystick->hitTestInteractiveArea(localPos)
        || m_stylusJoystick->isJoystickInteractionActive();
    if (!routeToJoystick) {
        return false;
    }

    QWidget* target = m_stylusJoystick;
    QPointF posInTarget = localPos;
    if (auto* joystickWidget = m_stylusJoystick->joystickWidget();
        joystickWidget && joystickWidget->isInteractionActive()) {
        posInTarget = joystickWidget->mapFrom(m_stylusJoystick, localPos);
        target = joystickWidget;
    } else {
        while (QWidget* child = target->childAt(qRound(posInTarget.x()), qRound(posInTarget.y()))) {
            posInTarget = child->mapFrom(target, posInTarget);
            target = child;
        }
    }

    switch (event->type()) {
    case QEvent::TabletPress:
        if (effectiveButton != Qt::NoButton) {
            QMouseEvent syntheticPress(QEvent::MouseButtonPress, posInTarget, globalPos,
                effectiveButton, event->buttons(), event->modifiers());
            QCoreApplication::sendEvent(target, &syntheticPress);
        }
        event->accept();
        return true;
    case QEvent::TabletMove: {
        QMouseEvent syntheticMove(QEvent::MouseMove, posInTarget, globalPos, Qt::NoButton,
            event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(target, &syntheticMove);
        event->accept();
        return true;
    }
    case QEvent::TabletRelease:
        if (effectiveButton != Qt::NoButton) {
            QMouseEvent syntheticRelease(QEvent::MouseButtonRelease, posInTarget, globalPos,
                effectiveButton, event->buttons(), event->modifiers());
            QCoreApplication::sendEvent(target, &syntheticRelease);
        }
        event->accept();
        return true;
    default:
        event->ignore();
        return true;
    }
}

void CanvasPanel::hideBrushPackOverlayIfNotUserMoved()
{
    if (!m_brushOverlay) {
        return;
    }
    if (auto* packOverlay = m_brushOverlay->brushPackOverlay()) {
        if (!packOverlay->isUserMoved()) {
            packOverlay->hidePanel();
        }
    }
}

void CanvasPanel::dispatchSyntheticMousePress(QMouseEvent* event)
{
    mousePressEvent(event);
}

void CanvasPanel::dispatchSyntheticMouseMove(QMouseEvent* event)
{
    mouseMoveEvent(event);
}

void CanvasPanel::dispatchSyntheticMouseRelease(QMouseEvent* event)
{
    mouseReleaseEvent(event);
}

// ==========================================================================
//   P R I V A T E
// ==========================================================================

void CanvasPanel::onSurfaceResized(uint32_t width, uint32_t height)
{
    Q_UNUSED(width);
    Q_UNUSED(height);
    applyZoomLimits();
    updateBrushCursorOverlayRadius();

    if (m_brushOverlayNeedsInitialPlacement && m_overlayLayoutManager) {
        m_overlayLayoutManager->positionBrushOverlayDefault();
        m_brushOverlayNeedsInitialPlacement = false;
    }
    if (m_lastContentSize.isEmpty() && m_contentWidget) {
        m_lastContentSize = m_contentWidget->size();
    }

    const QSize previousContentSize = m_lastContentSize;
    const QSize newSize = m_contentWidget ? m_contentWidget->size() : QSize();

    if (m_brushOverlay && m_contentWidget && m_overlayLayoutManager
        && m_overlayLayoutManager->engine()) {
        // The engine re-resolves the brush overlay from its tracked fraction (or
        // anchor default if never moved), so a single relayout call covers both
        // restored-normalized and user-dragged cases across content resizes.
        m_overlayLayoutManager->engine()->relayoutItem(m_brushOverlay, /*animate*/ false);
        m_savedBrushOverlayPosition = m_brushOverlay->pos();
    }
    if (m_stylusJoystick && m_contentWidget && m_overlayLayoutManager) {
        if (m_stylusJoystickUserMoved) {
            m_overlayLayoutManager->repositionStylusJoystickOnResize(newSize, previousContentSize);
        } else {
            m_overlayLayoutManager->positionStylusJoystickDefault();
        }
    }
    if (m_toolStateOverlay && m_overlayLayoutManager && m_contentWidget) {
        // Tool HUD always re-anchors top-center (not user-draggable).
        m_overlayLayoutManager->layoutToolStateOverlay();
    }
    m_lastContentSize = newSize;

    requestRender();
    if (m_canvasResizeController
        && (m_canvasResizeController->isActive()
            || m_canvasResizeController->isInteractionActive())) {
        m_canvasResizeController->updateOverlay();
    }
    updateSelectionActionPopup();
    if (m_textEditingController && m_textEditingController->isEditing()) {
        m_textEditingController->refreshFormattingPopup();
    }
}

void CanvasPanel::positionBrushOverlayDefault()
{
    if (m_overlayLayoutManager)
        m_overlayLayoutManager->positionBrushOverlayDefault();
}

void CanvasPanel::positionStylusJoystickDefault()
{
    if (m_overlayLayoutManager)
        m_overlayLayoutManager->positionStylusJoystickDefault();
}

void CanvasPanel::scheduleInitialBrushOverlayPlacement()
{
    if (m_overlayLayoutManager)
        m_overlayLayoutManager->scheduleInitialBrushOverlayPlacement();
}

void CanvasPanel::setCanvasWidgetVisible(CanvasWidget widget, bool visible)
{
    const bool before = isCanvasWidgetVisible(widget);
    if (m_overlayLayoutManager)
        m_overlayLayoutManager->setCanvasWidgetVisible(widget, visible);
    if (before != isCanvasWidgetVisible(widget)) {
        emit canvasWidgetsVisibilityChanged();
    }
}

void CanvasPanel::setCanvasWidgetVisibility(const CanvasWidgetVisibility& visibility)
{
    for (const CanvasWidget widget : kCanvasWidgets) {
        setCanvasWidgetVisible(widget, visibility[widget]);
    }
}

void CanvasPanel::resetCanvasOverlaysToDefault()
{
    m_pendingBrushOverlayPositionNormalized.reset();
    m_pendingToolStateOverlayPositionNormalized.reset();
    m_pendingStylusJoystickPositionNormalized.reset();
    m_savedBrushOverlayPosition.reset();
    m_savedToolStateOverlayPosition.reset();
    m_savedStylusJoystickPosition.reset();
    m_savedStylusJoystickAbovePanel = true;
    m_brushOverlayUserMoved = false;
    m_toolStateOverlayUserMoved = false;
    m_stylusJoystickUserMoved = false;

    if (m_overlayLayoutManager) {
        for (const CanvasWidget widget : kCanvasWidgets) {
            m_overlayLayoutManager->setCanvasWidgetVisible(widget, true);
        }
        if (m_stylusJoystick) {
            m_stylusJoystick->setJoystickAbovePanel(true);
        }
        if (m_contentWidget && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
            m_overlayLayoutManager->positionBrushOverlayDefault();
            m_overlayLayoutManager->positionStylusJoystickDefault();
            m_overlayLayoutManager->layoutToolStateOverlay();
        } else {
            m_brushOverlayNeedsInitialPlacement = true;
        }
    }
}

bool CanvasPanel::isCanvasWidgetVisible(CanvasWidget widget) const
{
    return m_canvasWidgets[widget];
}

CanvasWidgetVisibility CanvasPanel::canvasWidgetVisibility() const
{
    CanvasWidgetVisibility visibility;
    for (const CanvasWidget widget : kCanvasWidgets) {
        visibility[widget] = isCanvasWidgetVisible(widget);
    }
    return visibility;
}

void CanvasPanel::beginPositionPicking(const QPointF& initialDocPos,
    std::function<void(const QPointF&)> onPicked, std::function<void()> onCanceled)
{
    // Starting a new session while one is already active (e.g. clicking a
    // different effect's position capsule) cancels the old one first.
    if (m_positionPickerActive) {
        cancelPositionPicking();
    }

    m_positionPickerActive = true;
    m_positionPickerOnPicked = std::move(onPicked);
    m_positionPickerOnCanceled = std::move(onCanceled);

    m_positionPickerPrevWidgets = canvasWidgetVisibility();
    for (const CanvasWidget widget : kCanvasWidgets) {
        setCanvasWidgetVisible(widget, false);
    }

    if (m_positionPickerOverlay) {
        m_positionPickerOverlay->setDocumentPosition(initialDocPos);
        m_positionPickerOverlay->followCursor(m_positionPickerOverlay->parentWidget()
                ? m_positionPickerOverlay->parentWidget()->mapFromGlobal(QCursor::pos())
                : QPoint());
        m_positionPickerOverlay->show();
        m_positionPickerOverlay->raise();
    }

    // Replace whatever cursor the current tool would show (custom brush tip,
    // GL eyedropper, etc.) with a plain crosshair over the canvas — picking
    // mode isn't that tool anymore, it's "click anywhere to set a point".
    updateToolCursor();
}

std::pair<std::function<void(const QPointF&)>, std::function<void()>>
CanvasPanel::endPositionPickingSession()
{
    m_positionPickerActive = false;

    setCanvasWidgetVisibility(m_positionPickerPrevWidgets);

    if (m_positionPickerOverlay) {
        m_positionPickerOverlay->hide();
    }

    // Restore whatever cursor the current tool normally shows.
    updateToolCursor();
    if (m_cursorManager) {
        m_cursorManager->refreshCursorPosition();
    }

    // Move out before the caller invokes either callback: it may
    // synchronously start a new session (e.g. a widget re-arming itself).
    auto onPicked = std::move(m_positionPickerOnPicked);
    auto onCanceled = std::move(m_positionPickerOnCanceled);
    m_positionPickerOnPicked = nullptr;
    m_positionPickerOnCanceled = nullptr;
    return { std::move(onPicked), std::move(onCanceled) };
}

void CanvasPanel::cancelPositionPicking()
{
    if (!m_positionPickerActive) {
        return;
    }
    auto [onPicked, onCanceled] = endPositionPickingSession();
    Q_UNUSED(onPicked);
    if (onCanceled) {
        onCanceled();
    }
}

void CanvasPanel::commitPositionPicking(const QPointF& docPos)
{
    if (!m_positionPickerActive) {
        return;
    }
    auto [onPicked, onCanceled] = endPositionPickingSession();
    Q_UNUSED(onCanceled);
    if (onPicked) {
        onPicked(docPos);
    }
}

void CanvasPanel::setInteractionEnabled(bool enabled)
{
    m_interactionEnabled = enabled;
    if (!enabled) {
        // End any active drawing/interaction
        endActiveStrokeSession();
        if (m_glWidget)
            m_glWidget->endPanSampling();
        if (m_zoomInfoOverlay)
            m_zoomInfoOverlay->hideImmediately();
        if (m_selectionSizeOverlay)
            m_selectionSizeOverlay->hideImmediately();
        m_isPanning = false;
        m_isZoomDragging = false;
        m_isRotatingView = false;
        m_isEyedropping = false;

        // Suppress custom cursors (GL brush/eyedropper overlays + tool cursors)
        if (m_cursorManager) {
            m_cursorManager->setSuppressed(true);
        }
        // Reset to default arrow cursor on canvas widgets
        setCursor(Qt::ArrowCursor);
        if (m_glWidget) {
            m_glWidget->setCursor(Qt::ArrowCursor);
        }
        if (m_contentWidget) {
            m_contentWidget->setCursor(Qt::ArrowCursor);
        }
    } else {
        // Restore cursor manager and tool cursor
        if (m_cursorManager) {
            m_cursorManager->setSuppressed(false);
            m_cursorManager->refreshCursorPosition();
        }
        updateCursorManagerOverlay();
        updateToolCursor();
    }
    syncToolStateOverlayContent();
}

void CanvasPanel::updateCursorManagerOverlay()
{
    if (!m_cursorManager || !isInteractionEnabled())
        return;

    const bool transformActive = m_glWidget && m_glWidget->isTransformActive();
    const ToolMode currentTool = toolMode();
    if (!transformActive) {
        m_transformDragCursorValid = false;
    }
    const bool useBrush
        = !transformActive && CanvasToolStateController::isDrawInstrument(currentTool);
    const bool useEyedropper = !transformActive && (currentTool == ToolMode::Eyedropper);
    m_cursorManager->setUseGLBrushCursor(useBrush);
    m_cursorManager->setUseGLEyedropperCursor(useEyedropper);
    m_cursorManager->setActiveOverlay(nullptr);

    if (!transformActive) {
        m_cursorManager->clearRequestedCursor();
    }
    if (!useBrush && m_glWidget) {
        m_glWidget->setBrushCursorState(false, 0, 0, 0);
    }
    if (!useEyedropper && m_glWidget) {
        m_glWidget->setEyedropperCursorState(false, 0, 0);
    }
    m_cursorManager->refreshCursorPosition();
}

void CanvasPanel::updateToolCursor()
{
    if (!isInteractionEnabled())
        return;
    const ToolMode currentTool = toolMode();
    if (m_positionPickerActive && currentTool != ToolMode::Hand) {
        // Picking mode shows a plain crosshair over the canvas for every tool
        // except Hand — Hand keeps its own open/closed-hand cursor below so
        // panning while picking still reads as panning.
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(Qt::CrossCursor);
            m_cursorManager->refreshCursorPosition();
        } else {
            setCursor(Qt::CrossCursor);
        }
        return;
    }
    if (m_isPanning) {
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(Qt::ClosedHandCursor);
            m_cursorManager->refreshCursorPosition();
        } else {
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }
    if (currentTool == ToolMode::Hand) {
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(Qt::OpenHandCursor);
            m_cursorManager->refreshCursorPosition();
        } else {
            setCursor(Qt::OpenHandCursor);
        }
        return;
    }
    if (currentTool == ToolMode::Move) {
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(Qt::SizeAllCursor);
            m_cursorManager->refreshCursorPosition();
        } else {
            setCursor(Qt::SizeAllCursor);
        }
        return;
    }
    if (currentTool == ToolMode::CanvasResize && m_canvasResizeController) {
        const QPoint pos = QCursor::pos();
        const Qt::CursorShape cursor = m_canvasResizeController->cursorForPosition(pos);
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(cursor);
            m_cursorManager->updateCursorPosition(pos);
        } else {
            setCursor(cursor);
        }
        return;
    }
    if (m_cursorManager && m_glWidget && m_glWidget->isTransformActive()) {
        const QPoint pos = QCursor::pos();
        Qt::CursorShape cursor = Qt::ArrowCursor;
        if (m_glWidget->isMoveOnlyTransform()) {
            cursor = Qt::SizeAllCursor;
        } else {
            aether::Vector2 worldPos = mapToWorld(pos);
            float zoom = m_glWidget->viewport().camera().zoom();
            auto& tc = m_glWidget->transformController();
            const auto hit = tc.hitTestDetailed(worldPos, zoom);
            cursor = detail::cursorForTransformHandle(hit, tc.cornersActAsRotationHandles());
        }
        m_cursorManager->setRequestedCursor(cursor);
        m_cursorManager->updateCursorPosition(pos);
    } else if (currentTool == ToolMode::RotateView) {
        const auto rotCursor = m_isRotatingView ? Qt::ClosedHandCursor : Qt::CrossCursor;
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(rotCursor);
            m_cursorManager->refreshCursorPosition();
        } else {
            setCursor(rotCursor);
        }
    } else if (currentTool == ToolMode::Text) {
        if (m_cursorManager) {
            m_cursorManager->setRequestedCursor(Qt::IBeamCursor);
            m_cursorManager->refreshCursorPosition();
        } else {
            setCursor(Qt::IBeamCursor);
        }
    } else {
        if (m_cursorManager)
            m_cursorManager->clearRequestedCursor();
        setCursor(Qt::ArrowCursor);
    }
}

void CanvasPanel::updateExportAreaCursor()
{
    if (!m_exportAreaController || !m_exportAreaController->isActive()) {
        return;
    }

    const Qt::CursorShape cursor = m_exportAreaController->cursorForPosition(QCursor::pos());
    setCursor(cursor);
    if (m_glWidget) {
        m_glWidget->setCursor(cursor);
    }
    if (m_contentWidget) {
        m_contentWidget->setCursor(cursor);
    }
}

bool CanvasPanel::isCursorOverCanvas() const
{
    if (m_cursorManager) {
        return m_cursorManager->isOverCanvas(QCursor::pos());
    }
    if (!m_glWidget || !m_contentWidget)
        return false;
    const QRect r(m_glWidget->mapToGlobal(QPoint(0, 0)), m_glWidget->size());
    if (!r.contains(QCursor::pos()))
        return false;
    if (m_brushOverlay && m_brushOverlay->isVisible()) {
        const QRect o(m_brushOverlay->mapToGlobal(QPoint(0, 0)), m_brushOverlay->size());
        if (o.contains(QCursor::pos()))
            return false;
    }
    if (m_toolStateOverlay && m_toolStateOverlay->isVisible()) {
        const QRect t(m_toolStateOverlay->mapToGlobal(QPoint(0, 0)), m_toolStateOverlay->size());
        if (t.contains(QCursor::pos()))
            return false;
    }
    return true;
}

std::optional<Qt::CursorShape> CanvasPanel::resolveCursorForPosition(const QPoint& globalPos) const
{
    if (!m_glWidget) {
        return std::nullopt;
    }

    if (!isInteractionEnabled() && m_exportAreaController && m_exportAreaController->isActive()) {
        return m_exportAreaController->cursorForPosition(globalPos);
    }

    // Keep hover cursor resolution in one place for cursor-manager driven updates.
    const ToolMode currentTool = toolMode();
    if (currentTool == ToolMode::CanvasResize && !m_glWidget->isTransformActive()
        && m_canvasResizeController) {
        return m_canvasResizeController->cursorForPosition(globalPos);
    }
    if (currentTool == ToolMode::Text && !m_glWidget->isTransformActive()) {
        return Qt::IBeamCursor;
    }

    if (!m_glWidget->isTransformActive()) {
        return std::nullopt;
    }
    if (m_isPanning)
        return Qt::ClosedHandCursor;
    if (m_transformDragCursorValid) {
        return m_transformDragCursor;
    }
    if (m_glWidget->isMoveOnlyTransform()) {
        return Qt::SizeAllCursor;
    }
    aether::Vector2 worldPos = mapToWorld(globalPos);
    float zoom = m_glWidget->viewport().camera().zoom();
    auto& tc = m_glWidget->transformController();
    const auto hit = tc.hitTestDetailed(worldPos, zoom);
    return detail::cursorForTransformHandle(hit, tc.cornersActAsRotationHandles());
}

void CanvasPanel::updateBrushCursorOverlayRadius()
{
    if (m_cursorManager) {
        m_cursorManager->refreshCursorPosition();
    }
}

void CanvasPanel::importImageFilesBelowSelectedKeepingSelection(const QStringList& filePaths)
{
    if (m_imageImportHelper)
        m_imageImportHelper->importImageFilesBelowSelectedKeepingSelection(filePaths);
}

void CanvasPanel::importImageBelowSelectedKeepingSelection(
    const QImage& image, const QString& layerName)
{
    if (m_imageImportHelper) {
        m_imageImportHelper->importImageBelowSelectedKeepingSelection(image, layerName);
    }
}

void CanvasPanel::promptImportImageFiles(const QStringList& filePaths)
{
    if (m_imageImportHelper) {
        m_imageImportHelper->promptImportImageFiles(filePaths);
    }
}

bool CanvasPanel::importImageFromClipboard()
{
    return m_imageImportHelper ? m_imageImportHelper->importImageFromClipboard() : false;
}

void CanvasPanel::updateStyles()
{
    if (!m_contentWidget)
        return;

    const auto& c = colors();

    m_contentWidget->setStyleSheet(QString("background: %1;").arg(c.background.darker(120).name()));

    if (m_glWidget) {
        m_glWidget->setBackgroundColor(
            aether::Color::fromRGB(c.background.red(), c.background.green(), c.background.blue()));

        m_glWidget->setCheckerColors(
            aether::Color::fromRGB(c.canvas().red(), c.canvas().green(), c.canvas().blue()),
            aether::Color::fromRGB(
                c.canvasGrid().red(), c.canvasGrid().green(), c.canvasGrid().blue()));
    }
}

// ==========================================================================
//   T E M P O R A R Y   T O O L   H O L D
// ==========================================================================

void CanvasPanel::endTemporaryTool()
{
    if (!m_tempToolHold.active)
        return;

    const bool wasUsed = m_tempToolHold.toolWasUsed;
    const bool alwaysRevert = m_tempToolHold.alwaysRevert;
    const ToolMode previousTool = m_tempToolHold.previousTool;
    resetTemporaryMoveToolUndoCooldown();
    m_tempToolHold = {};
    updateTemporaryToolHoldPolling();

    if ((wasUsed || alwaysRevert) && toolMode() != previousTool) {
        // Revert to previous tool (prevent setToolMode from re-entering hold)
        m_pendingTempToolKey = 0;
        m_pendingTempToolAlwaysRevert = false;
        setToolMode(previousTool);
    }
    // If tool was NOT used (and not always-revert), keep the new tool (permanent switch)
}

void CanvasPanel::noteUndoForTemporaryMoveTool()
{
    if (!m_tempToolHold.active || m_tempToolHold.heldKey != Qt::Key_Control
        || m_tempToolHold.previousTool == ToolMode::Move || toolMode() != ToolMode::Move) {
        return;
    }

    m_temporaryMoveToolUndoCooldownActive = true;
    m_temporaryMoveToolUndoCooldownTimer.start();
}

bool CanvasPanel::temporaryMoveToolUndoCooldownActive()
{
    if (!m_temporaryMoveToolUndoCooldownActive) {
        return false;
    }

    const bool temporaryCtrlMove = m_tempToolHold.active
        && m_tempToolHold.heldKey == Qt::Key_Control
        && m_tempToolHold.previousTool != ToolMode::Move && toolMode() == ToolMode::Move;
    if (!temporaryCtrlMove || !m_temporaryMoveToolUndoCooldownTimer.isValid()
        || m_temporaryMoveToolUndoCooldownTimer.elapsed() >= kTemporaryMoveToolUndoCooldownMs) {
        resetTemporaryMoveToolUndoCooldown();
        return false;
    }

    return true;
}

void CanvasPanel::resetTemporaryMoveToolUndoCooldown()
{
    m_temporaryMoveToolUndoCooldownActive = false;
}

bool CanvasPanel::finalizeTemporaryToolHoldForKeyRelease(int key)
{
    if (!m_tempToolHold.active) {
        return false;
    }

    const bool shiftSpaceRelease
        = m_tempToolHold.shiftSpaceCombo && (key == Qt::Key_Space || key == Qt::Key_Shift);
    const bool normalKeyRelease = !m_tempToolHold.shiftSpaceCombo && key == m_tempToolHold.heldKey;
    if (!shiftSpaceRelease && !normalKeyRelease) {
        return false;
    }

    if (key == Qt::Key_Control && m_glWidget && m_glWidget->isTransformActive()
        && m_glWidget->isMoveOnlyTransform()) {
        confirmTransform();
    }

    endTemporaryTool();
    return true;
}

void CanvasPanel::updateTemporaryToolHoldPolling()
{
    if (!m_tempToolHoldPollTimer) {
        return;
    }

    const bool shouldPoll = m_tempToolHold.active && m_tempToolHold.heldKey != 0;
    if (shouldPoll) {
        if (!m_tempToolHoldPollTimer->isActive()) {
            m_tempToolHoldPollTimer->start();
        }
    } else if (m_tempToolHoldPollTimer->isActive()) {
        m_tempToolHoldPollTimer->stop();
    }
}

void CanvasPanel::syncTemporaryToolHoldFromPressedKeys()
{
    if (!m_tempToolHold.active || m_tempToolHold.heldKey == 0) {
        updateTemporaryToolHoldPolling();
        return;
    }

    if (isTemporaryToolHoldKeyPressed()) {
        return;
    }

    finalizeTemporaryToolHoldForKeyRelease(m_tempToolHold.heldKey);
}

bool CanvasPanel::isTemporaryToolHoldKeyPressed() const
{
    if (!m_tempToolHold.active || m_tempToolHold.heldKey == 0) {
        return false;
    }

#ifdef Q_OS_WIN
    auto isVirtualKeyPressed
        = [](int virtualKey) { return (GetAsyncKeyState(virtualKey) & 0x8000) != 0; };

    if (m_tempToolHold.shiftSpaceCombo) {
        return isVirtualKeyPressed(VK_SHIFT) && isVirtualKeyPressed(VK_SPACE);
    }

    switch (m_tempToolHold.heldKey) {
    case Qt::Key_Control:
        return isVirtualKeyPressed(VK_CONTROL);
    case Qt::Key_Alt:
        return isVirtualKeyPressed(VK_MENU);
    case Qt::Key_Shift:
        return isVirtualKeyPressed(VK_SHIFT);
    case Qt::Key_Space:
        return isVirtualKeyPressed(VK_SPACE);
    default: {
        // For letter keys (A-Z) and other keys stored as Qt::Key,
        // map back to Windows VK code to check physical key state
        // (layout-independent: Qt::Key_A == 0x41 == VK_A on Windows)
        const int heldKey = m_tempToolHold.heldKey;
        if (heldKey >= Qt::Key_A && heldKey <= Qt::Key_Z) {
            return isVirtualKeyPressed(heldKey);
        }
        if (heldKey >= Qt::Key_0 && heldKey <= Qt::Key_9) {
            return isVirtualKeyPressed(heldKey);
        }
        // OEM keys: map Qt key back to VK code
        static const QHash<int, int> qtKeyToVK = {
            { Qt::Key_BracketLeft, 0xDB },
            { Qt::Key_BracketRight, 0xDD },
            { Qt::Key_Semicolon, 0xBA },
            { Qt::Key_Apostrophe, 0xDE },
            { Qt::Key_Comma, 0xBC },
            { Qt::Key_Period, 0xBE },
            { Qt::Key_Slash, 0xBF },
            { Qt::Key_Backslash, 0xDC },
            { Qt::Key_Minus, 0xBD },
            { Qt::Key_Equal, 0xBB },
            { Qt::Key_QuoteLeft, 0xC0 },
        };
        if (auto it = qtKeyToVK.find(heldKey); it != qtKeyToVK.end()) {
            return isVirtualKeyPressed(*it);
        }
        return false;
    }
    }
#endif

    const Qt::KeyboardModifiers modifiers = QGuiApplication::queryKeyboardModifiers();
    switch (m_tempToolHold.heldKey) {
    case Qt::Key_Control:
        return modifiers.testFlag(Qt::ControlModifier);
    case Qt::Key_Alt:
        return modifiers.testFlag(Qt::AltModifier);
    case Qt::Key_Shift:
        return modifiers.testFlag(Qt::ShiftModifier);
    default:
        return false;
    }
}

std::optional<CanvasPanel::ToolMode> CanvasPanel::toolModeForKey(int key) const
{
    // Space is always temporary Hand (pan), regardless of shortcut config
    if (key == Qt::Key_Space)
        return ToolMode::Hand;
    // Alt is always temporary Eyedropper (pipette), regardless of shortcut config
    if (key == Qt::Key_Alt)
        return ToolMode::Eyedropper;
    // Ctrl is always temporary Move (pan), regardless of shortcut config
    if (key == Qt::Key_Control)
        return ToolMode::Move;

    // Look up command mapped to this key via ShortcutManager
    const auto& sm = ruwa::core::ShortcutManager::instance();
    QString cmdId = sm.commandForShortcut(QKeySequence(key));
    if (cmdId.isEmpty())
        return std::nullopt;

    return toolModeForCommandId(cmdId);
}

std::optional<CanvasPanel::ToolMode> CanvasPanel::toolModeForKeyEvent(const QKeyEvent* event) const
{
    if (!event)
        return std::nullopt;

    const int key = event->key();
    // Modifier keys are layout-independent, handle them directly
    if (key == Qt::Key_Space)
        return ToolMode::Hand;
    if (key == Qt::Key_Alt)
        return ToolMode::Eyedropper;
    if (key == Qt::Key_Control)
        return ToolMode::Move;

    // Layout-independent lookup via ShortcutManager
    const auto& sm = ruwa::core::ShortcutManager::instance();
    QString cmdId = sm.commandForKeyEvent(event);
    if (cmdId.isEmpty())
        return std::nullopt;

    return toolModeForCommandId(cmdId);
}

std::optional<CanvasPanel::ToolMode> CanvasPanel::toolModeForCommandId(const QString& cmdId)
{
    static const QHash<QString, ToolMode> map = {
        { "tools.hand", ToolMode::Hand },
        { "tools.brush", ToolMode::Brush },
        { "tools.blur", ToolMode::Blur },
        { "tools.smudge", ToolMode::Smudge },
        { "tools.eraser", ToolMode::Eraser },
        { "tools.fill", ToolMode::Fill },
        { "tools.classic-fill", ToolMode::ClassicFill },
        { "tools.eyedropper", ToolMode::Eyedropper },
        { "tools.lasso", ToolMode::Lasso },
        { "tools.lasso-fill", ToolMode::LassoFill },
        { "tools.square-selection", ToolMode::SquareSelection },
        { "tools.circle-selection", ToolMode::CircleSelection },
        { "tools.move", ToolMode::Move },
        { "tools.rotate-view", ToolMode::RotateView },
        { "tools.canvas-resize", ToolMode::CanvasResize },
        { "tools.zoom", ToolMode::Zoom },
        { "tools.text", ToolMode::Text },
    };
    auto it = map.find(cmdId);
    return it != map.end() ? std::optional<ToolMode>(*it) : std::nullopt;
}

QString CanvasPanel::commandIdForToolMode(ToolMode mode)
{
    static const QHash<ToolMode, QString> map = {
        { ToolMode::Hand, "tools.hand" },
        { ToolMode::Brush, "tools.brush" },
        { ToolMode::Blur, "tools.blur" },
        { ToolMode::Smudge, "tools.smudge" },
        { ToolMode::Eraser, "tools.eraser" },
        { ToolMode::Fill, "tools.fill" },
        { ToolMode::ClassicFill, "tools.classic-fill" },
        { ToolMode::Eyedropper, "tools.eyedropper" },
        { ToolMode::Lasso, "tools.lasso" },
        { ToolMode::LassoFill, "tools.lasso-fill" },
        { ToolMode::SquareSelection, "tools.square-selection" },
        { ToolMode::CircleSelection, "tools.circle-selection" },
        { ToolMode::Move, "tools.move" },
        { ToolMode::RotateView, "tools.rotate-view" },
        { ToolMode::CanvasResize, "tools.canvas-resize" },
        { ToolMode::Zoom, "tools.zoom" },
        { ToolMode::Text, "tools.text" },
    };
    return map.value(mode);
}

} // namespace ruwa::ui::workspace
