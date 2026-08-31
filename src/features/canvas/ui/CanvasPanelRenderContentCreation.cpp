// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   R E N D E R   C O N T E N T
// ==========================================================================
// Construction and wiring of the canvas render content.
//
// The engine itself is built through the Aether integration (the only place
// that knows the concrete renderer): this file creates the engine binding,
// hosts its generic widget, and wires application behavior to the
// renderer-neutral session capabilities and Qt events.
//
// TRANSITIONAL QUARANTINE (Stage 1): a few deep wiring points still reach the
// legacy renderer through m_glWidget (layer model application, the
// rasterization-confirm callback, brush/tool state application). They are
// enumerated in docs/renderer-boundary-quarantine.md and move onto session
// capabilities as those grow.
// ==========================================================================

#include "CanvasPanel.h"
#include "features/canvas/document/CanvasHistoryFacade.h"
#include "features/canvas/engine/CanvasEngineSession.h"

#include "CanvasCursorManager.h"
#include "CanvasFillProgressPopup.h"
#include "CanvasTransformMetricPresenter.h"
#include "CanvasParameterOverlayWidget.h"
#include "CanvasOverlayLayoutManager.h"
#include "CanvasToolStateController.h"
#include "TextEditingController.h"
#include "CanvasPanelHelpers.h"
#include "app/Application.h"
#include "features/canvas/engine/CanvasEngineQtRuntime.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/engine/CanvasEngineTypes.h"
#include "features/canvas/engine/CanvasEngineQtEvents.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushPackOverlay.h"
#include "features/brush/ui/BrushPackPanel.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/canvas/overlays/ToolCursorIcons.h"
#include "features/canvas/radial-menu/RadialMenuWidget.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"
#include "features/canvas/ui/CanvasZoomInfoOverlay.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasStylusJoystickWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/export/ExportAreaController.h"
#include "features/selection/SelectionActionPopup.h"
#include "features/settings/SettingsManager.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shell/top-bar/OverlayContainer.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "shared/undo/UndoManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QGraphicsOpacityEffect>
#include <QList>
#include <QMessageBox>
#include <QPoint>
#include <QPointF>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <vector>

namespace ruwa::ui::workspace {

bool CanvasPanel::createRenderContent()
{
    if (m_renderContentCreated) {
        return false;
    }
    m_renderContentCreated = true;

    // Remove placeholder
    if (m_renderPlaceholder) {
        m_contentLayout->removeWidget(m_renderPlaceholder);
        delete m_renderPlaceholder;
        m_renderPlaceholder = nullptr;
    }

    // Create the engine binding through the selected neutral runtime. The
    // panel never names the concrete engine (plan 7.31.1 / DoD 9).
    auto* application = ruwa::Application::instance();
    auto* runtime = application ? application->engineRuntime() : nullptr;
    if (!runtime) {
        m_renderContentCreated = false;
        return false;
    }

    CanvasEngineCreateInfo createInfo;
    createInfo.initialCanvasSize = m_canvasSize;
    createInfo.boundsMode = m_canvasBoundsMode;
    createInfo.lassoStabilization = static_cast<float>(lassoStabilization());
    createInfo.lassoFillStabilization = static_cast<float>(lassoFillStabilization());
    // Rasterization confirmation is an application decision injected into the
    // engine (plan 7.15.4) — the renderer itself has no dialog fallback.
    createInfo.rasterizationDecisionProvider
        = [this](const QString& title, const QString& message) {
              return ruwa::ui::widgets::MessagePopupManager::showBlocking(
                  this, title + QStringLiteral("\n\n") + message, tr("Yes"), tr("No"), 360, true);
          };
    m_engineBinding = runtime->createBinding(createInfo, m_contentWidget);

    m_glWidget = aetherLegacyRenderer(*m_engineBinding); // TRANSITIONAL QUARANTINE
    m_viewportHostWidget = m_engineBinding->viewportHostWidget();

    auto& events = m_engineBinding->events();
    auto& session = m_engineBinding->session();

    m_contentLayout->addWidget(m_viewportHostWidget);
    m_contentLayout->activate();
    ensureEffectParameterOverlay();
    refreshEffectParameterOverlay();
    applyZoomLimits();

    auto applyUndoMemoryLimit = [this]() {
        if (!m_engineBinding) {
            return;
        }
        const auto& settings = ruwa::core::SettingsManager::instance().settings();
        const qint64 bytes
            = static_cast<qint64>(settings.performance.undoMemoryLimitMb) * 1024LL * 1024LL;
        m_engineBinding->history().setMemoryLimit(bytes);
    };
    applyUndoMemoryLimit();
    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::undoMemoryLimitChanged, this, [this](int megabytes) {
            if (!m_engineBinding) {
                return;
            }
            const qint64 bytes = static_cast<qint64>(megabytes) * 1024LL * 1024LL;
            m_engineBinding->history().setMemoryLimit(bytes);
        });

    // Transform snap preferences are pushed, never queried by the renderer
    // (plan 7.27.2): initial snapshot now, refreshed on any settings change.
    pushTransformSnapPolicy();
    connect(&ruwa::core::SettingsManager::instance(), &ruwa::core::SettingsManager::settingsChanged,
        this, [this]() { pushTransformSnapPolicy(); });

    // Keep overlay on top — layout-add puts new widget above overlay, so raise overlay again
    if (m_loadingOverlay) {
        m_loadingOverlay->raise();
    }

    // --- Engine events, translated by the binding into application events ---
    connect(
        &events, &CanvasEngineQtEvents::strokePainted, this, [this]() { emit strokePainted(); });
    // Per-frame heartbeat. Qt repaints every widget overlapping the canvas on
    // each of its frames, so the dock container uses this to park floating
    // panels behind a cached snapshot while the canvas is streaming.
    //
    // Only reported during a real canvas interaction: the canvas also renders
    // continuously for things the user is not driving (marching ants, for one),
    // and panels must stay live then — a parked panel does not show content
    // changes.
    connect(&events, &CanvasEngineQtEvents::framePresented, this, [this]() {
        if (m_effectParameterOverlay) {
            if (m_contentWidget && m_effectParameterOverlay->size() != m_contentWidget->size()) {
                m_effectParameterOverlay->setGeometry(m_contentWidget->rect());
            }
            syncEffectParameterOverlayPresentation();
        }
        const bool interacting
            = m_isDrawing || m_isPanning || isTransformActive() || isCameraAnimating();
        if (interacting) {
            emit canvasFrameRendered();
        }
    });
    connect(&events, &CanvasEngineQtEvents::contentRegionChanged, this,
        [this](const QRect& documentRect) { emit canvasContentRegionChanged(documentRect); });
    connect(&events, &CanvasEngineQtEvents::contentTilesChanged, this,
        [this](
            const QList<QPoint>& tilePositions) { emit canvasContentTilesChanged(tilePositions); });
    connect(&events, &CanvasEngineQtEvents::fillProcessingLayerChanged, this,
        [this](const QUuid& layerId) { emit fillProcessingLayerChanged(layerId); });
    connect(&events, &CanvasEngineQtEvents::viewportMetricsChanged, this,
        &CanvasPanel::onSurfaceResized);
    connect(&events, &CanvasEngineQtEvents::transformModeExited, this, [this](bool applied) {
        if (applied) {
            emit canvasContentChanged();
        }
    });
    connect(&events, &CanvasEngineQtEvents::engineReady, this, [this]() {
        onRenderSessionReady();
        if (m_engineBinding) {
            m_engineBinding->session().painting().updateBrushCursorStamp();
        }
        if (!playNewProjectAppearanceAnimationIfScheduled()
            && !m_deferLoadingOverlayHideUntilAppearanceAnimation) {
            fadeOutLoadingOverlay();
        }
    });

    // --- History events (plan 7.30.1: translated by the binding) ---
    if (m_toolStateOverlay) {
        connect(&events, &CanvasEngineQtEvents::historyCanUndoChanged, this,
            [this](bool) { syncToolStateOverlayContent(); });
        connect(&events, &CanvasEngineQtEvents::historyCanRedoChanged, this,
            [this](bool) { syncToolStateOverlayContent(); });
    }
    connect(&events, &CanvasEngineQtEvents::historyIndexChanged, this, [this](int) {
        syncToolStateOverlayContent();
        requestRender();
        emit canvasContentChanged();
    });
    connect(&events, &CanvasEngineQtEvents::historyCommandApplied, this,
        [this](const QList<QPoint>& tilePositions) {
            if (!tilePositions.isEmpty()) {
                emit canvasContentTilesChanged(tilePositions);
            }
        });

    // Apply stored layer model if set before content creation
    if (m_layerModel) {
        m_glWidget->setLayerModel(m_layerModel); // TRANSITIONAL QUARANTINE
    }

    // Application-owned presentation of engine metric/fill facts (group H):
    // the renderer publishes state, these presenters own the QWidget chrome.
    m_transformMetricPresenter = new CanvasTransformMetricPresenter(m_viewportHostWidget, this);
    m_transformMetricPresenter->setDocumentToViewport([this](const QPointF& documentPos) {
        return m_engineBinding ? m_engineBinding->session().view().viewportFromDocument(documentPos)
                               : documentPos;
    });
    connect(&events, &CanvasEngineQtEvents::transformPresentationChanged,
        m_transformMetricPresenter, &CanvasTransformMetricPresenter::present);

    m_fillProgressPopup = new CanvasFillProgressPopup(m_viewportHostWidget);
    m_fillProgressPopup->setDocumentToViewport([this](const QPointF& documentPos) {
        return m_engineBinding ? m_engineBinding->session().view().viewportFromDocument(documentPos)
                               : documentPos;
    });
    connect(&events, &CanvasEngineQtEvents::fillActivityChanged, m_fillProgressPopup,
        &CanvasFillProgressPopup::presentFillActivity);
    connect(&events, &CanvasEngineQtEvents::presentationSyncRequested, m_fillProgressPopup,
        &CanvasFillProgressPopup::updateAnchor);

    // Renderer initialization failure (plan 7.15.5): the engine reports an
    // owned diagnostic; the application decides the presentation.
    connect(&events, &CanvasEngineQtEvents::engineFailed, this,
        [this](const CanvasEngineDiagnostic& diagnostic) {
            QMessageBox::critical(this, tr("Shader Loading Error"), diagnostic.message);
        });

    // Brush overlay already created in createContent(); ensure it's under loading overlay
    if (m_brushOverlay) {
        m_brushOverlay->stackUnder(m_loadingOverlay);
    }
    if (m_toolStateOverlay) {
        m_toolStateOverlay->stackUnder(m_loadingOverlay);
    }
    if (m_zoomInfoOverlay) {
        m_zoomInfoOverlay->stackUnder(m_loadingOverlay);
    }
    {
        const ToolId currentTool = toolMode();
        applyToolPaintModes(currentTool);
        if (m_toolStateOverlay) {
            m_toolStateOverlay->setCanvasFlipStates(
                canvasContentFlipHorizontal(), canvasContentFlipVertical());
            m_toolStateOverlay->setBrushEraserMode(isBrushEraserActive());
        }
        const QColor color = currentBrushColor();
        CanvasColorValue brushColor;
        brushColor.r = static_cast<float>(color.redF());
        brushColor.g = static_cast<float>(color.greenF());
        brushColor.b = static_cast<float>(color.blueF());
        brushColor.a = static_cast<float>(color.alphaF());
        session.painting().setBrushColor(brushColor);
        session.painting().setBrushRadius(
            ruwa::ui::widgets::brushRadiusFromNormalizedSizeForCanvasMode(brushSizeNormalized(),
                m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds()));
        applyBrushSettings({});

        // Same-frame GPU backdrop blur. Geometry is sampled from the real
        // widgets immediately before paintGL, so layout animation and blur use
        // one positional source of truth.
        session.presentation().setBackdropRegionProvider([this]() {
            std::vector<ruwa::shared::rendering::CanvasBackdropRegion> regions;
            if (!m_viewportHostWidget || !m_contentWidget) {
                return regions;
            }

            const auto appendRegion = [this, &regions](QWidget* widget, const QRectF& localRect,
                                          qreal cornerRadius, qreal opacity) {
                if (!widget || localRect.isEmpty() || opacity <= 0.001
                    || !widget->isVisibleTo(m_contentWidget)) {
                    return;
                }
                const QPoint integralTopLeft(static_cast<int>(std::floor(localRect.x())),
                    static_cast<int>(std::floor(localRect.y())));
                // The overlay and the render host are siblings, not ancestors
                // of one another. Convert through their real common ancestor.
                const QPoint contentPoint = widget->mapTo(m_contentWidget, integralTopLeft);
                const QPoint mapped = m_viewportHostWidget->mapFrom(m_contentWidget, contentPoint);
                const QPointF fractionalOffset(
                    localRect.x() - integralTopLeft.x(), localRect.y() - integralTopLeft.y());
                // Read live rather than cached on themeChanged: the provider
                // already runs per frame, and this way the tint can never lag a
                // theme switch behind the widget chrome painted over it.
                regions.push_back(
                    { QRectF(QPointF(mapped) + fractionalOffset, localRect.size()), cornerRadius,
                        opacity, ruwa::ui::core::WidgetStyleManager::instance().colors().surface });
            };
            const auto effectOpacity = [](const QGraphicsOpacityEffect* effect) {
                return effect ? effect->opacity() : 1.0;
            };

            if (m_brushOverlay) {
                appendRegion(m_brushOverlay, QRectF(m_brushOverlay->rect()),
                    ruwa::ui::core::ThemeManager::instance().scaled(10),
                    effectOpacity(m_brushOverlayOpacity));
            }
            if (m_toolStateOverlay) {
                appendRegion(m_toolStateOverlay, QRectF(m_toolStateOverlay->rect()),
                    m_toolStateOverlay->height() / 2.0, effectOpacity(m_toolStateOverlayOpacity));
            }
            if (m_zoomInfoOverlay) {
                appendRegion(m_zoomInfoOverlay, QRectF(m_zoomInfoOverlay->rect()),
                    m_zoomInfoOverlay->height() / 2.0, m_zoomInfoOverlay->presentationOpacity());
            }
            if (m_stylusJoystick) {
                const qreal opacity = effectOpacity(m_stylusJoystickOpacity);
                if (auto* joystick = m_stylusJoystick->joystickWidget()) {
                    const QRectF localRect = joystick->backdropBlurRect();
                    appendRegion(joystick, localRect, localRect.width() / 2.0, opacity);
                }
                if (QWidget* zoomPanel = m_stylusJoystick->zoomPanelWidget()) {
                    appendRegion(zoomPanel, QRectF(zoomPanel->rect()),
                        ruwa::ui::core::WidgetStyleManager::instance().scaled(6), opacity);
                }
            }
            if (m_radialMenu) {
                // The radial menu is a scatter of small pieces rather than one
                // silhouette, so it contributes a region per hub, button, label
                // and title. They never overlap, so no feedback between them.
                const qreal opacity = m_radialMenu->showProgress();
                const auto shapes = m_radialMenu->backdropShapes();
                for (const auto& shape : shapes) {
                    appendRegion(m_radialMenu, shape.rect, shape.radius, opacity * shape.opacity);
                }
            }
            return regions;
        });
        const auto syncBackdropOpacity = [this](QGraphicsOpacityEffect* effect) {
            if (!effect) {
                return;
            }
            connect(effect, &QGraphicsOpacityEffect::opacityChanged, this, [this]() {
                if (m_engineBinding) {
                    m_engineBinding->session().presentation().requestBackdropUpdate();
                }
            });
        };
        syncBackdropOpacity(m_brushOverlayOpacity);
        syncBackdropOpacity(m_stylusJoystickOpacity);
        syncBackdropOpacity(m_toolStateOverlayOpacity);

        if (m_brushOverlay) {
            m_brushOverlay->setBackdropSource(m_engineBinding->backdropSource());
            connect(&events, &CanvasEngineQtEvents::backdropAvailabilityChanged, m_brushOverlay,
                QOverload<>::of(&QWidget::update));
            connect(m_viewportHostWidget, &QObject::destroyed, m_brushOverlay, [this]() {
                if (m_brushOverlay) {
                    m_brushOverlay->setBackdropSource(nullptr);
                }
            });
        }
        if (m_stylusJoystick) {
            m_stylusJoystick->setBackdropSource(m_engineBinding->backdropSource());
            connect(&events, &CanvasEngineQtEvents::backdropAvailabilityChanged, m_stylusJoystick,
                &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::refreshBackdropContent);
            connect(m_viewportHostWidget, &QObject::destroyed, m_stylusJoystick, [this]() {
                if (m_stylusJoystick) {
                    m_stylusJoystick->setBackdropSource(nullptr);
                }
            });
        }
        if (m_toolStateOverlay) {
            m_toolStateOverlay->setBackdropSource(m_engineBinding->backdropSource());
            connect(&events, &CanvasEngineQtEvents::backdropAvailabilityChanged, m_toolStateOverlay,
                QOverload<>::of(&QWidget::update));
            connect(m_viewportHostWidget, &QObject::destroyed, m_toolStateOverlay, [this]() {
                if (m_toolStateOverlay) {
                    m_toolStateOverlay->setBackdropSource(nullptr);
                }
            });
        }
        if (m_zoomInfoOverlay) {
            m_zoomInfoOverlay->setBackdropSource(m_engineBinding->backdropSource());
            connect(&events, &CanvasEngineQtEvents::backdropAvailabilityChanged, m_zoomInfoOverlay,
                QOverload<>::of(&QWidget::update));
            connect(m_viewportHostWidget, &QObject::destroyed, m_zoomInfoOverlay, [this]() {
                if (m_zoomInfoOverlay) {
                    m_zoomInfoOverlay->setBackdropSource(nullptr);
                }
            });
        }

        setupCanvasResizeController();
        setupExportAreaController();
        if (m_canvasResizeController) {
            m_canvasResizeController->updateOverlay();
        }
        if (m_exportAreaController && m_exportAreaController->isActive()) {
            m_exportAreaController->updateOverlay();
        }
    }

    // Position overlay on startup after geometry is fully settled.
    m_overlayLayoutManager->scheduleInitialBrushOverlayPlacement();
    m_overlayLayoutManager->positionStylusJoystickDefault();

    // Canvas cursor manager (rendered brush/eyedropper cursor when over canvas)
    m_cursorManager
        = new CanvasCursorManager(m_contentWidget, m_viewportHostWidget, m_brushOverlay, this);
    if (m_stylusJoystick) {
        m_cursorManager->addCursorExclusionWidget(m_stylusJoystick);
    }
    if (m_toolStateOverlay) {
        m_cursorManager->addCursorExclusionWidget(m_toolStateOverlay);
    }
    if (QWidget* win = window()) {
        if (auto* overlay = ruwa::ui::widgets::OverlayContainer::instance(win)) {
            if (auto* msgPopup = overlay->messagePopup()) {
                m_cursorManager->addCursorExclusionWidget(msgPopup);
            }
        }
    }
    if (auto* packOverlay = m_brushOverlay ? m_brushOverlay->brushPackOverlay() : nullptr) {
        if (auto* packPanel = packOverlay->panel()) {
            m_cursorManager->addCursorExclusionWidget(packPanel);
        }
    }
    if (m_selectionActionPopup) {
        m_cursorManager->addCursorExclusionWidget(m_selectionActionPopup);
    }
    if (m_confirmationPopup) {
        m_cursorManager->addCursorExclusionWidget(m_confirmationPopup);
    }
    m_cursorManager->setCursorResolver(
        [this](const QPoint& pos) { return resolveCursorForPosition(pos); });
    m_cursorManager->setBrushCursorCallback([this](const std::optional<QPoint>& globalPos) {
        if (!m_engineBinding || m_engineBinding->session().status() != CanvasEngineStatus::Ready) {
            return;
        }
        auto& session = m_engineBinding->session();
        auto& view = session.view();
        auto& presentation = session.presentation();
        if (!globalPos) {
            presentation.setBrushCursorState(false, 0, 0, 0);
            return;
        }
        const QPoint localPos = m_viewportHostWidget->mapFromGlobal(*globalPos);
        const QSizeF extent = view.viewportExtent();
        const qreal scaleX = m_viewportHostWidget->width() > 0
            ? extent.width() / static_cast<qreal>(m_viewportHostWidget->width())
            : 1.0;
        const qreal scaleY = m_viewportHostWidget->height() > 0
            ? extent.height() / static_cast<qreal>(m_viewportHostWidget->height())
            : 1.0;
        const float centerX = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
        const float centerY = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
        const float cursorScale = static_cast<float>((scaleX + scaleY) * 0.5);
        const float radiusScreen
            = session.painting().brushRadius() * static_cast<float>(view.zoom()) * cursorScale;
        presentation.setBrushCursorState(true, centerX, centerY, radiusScreen);
    });
    m_cursorManager->setEyedropperCursorCallback([this](const std::optional<QPoint>& globalPos) {
        if (!m_engineBinding || m_engineBinding->session().status() != CanvasEngineStatus::Ready) {
            return;
        }
        auto& session = m_engineBinding->session();
        auto& view = session.view();
        auto& presentation = session.presentation();
        if (!globalPos) {
            presentation.setEyedropperCursorState(false, 0, 0);
            return;
        }
        const QPoint localPos = m_viewportHostWidget->mapFromGlobal(*globalPos);
        const QSizeF extent = view.viewportExtent();
        const qreal scaleX = m_viewportHostWidget->width() > 0
            ? extent.width() / static_cast<qreal>(m_viewportHostWidget->width())
            : 1.0;
        const qreal scaleY = m_viewportHostWidget->height() > 0
            ? extent.height() / static_cast<qreal>(m_viewportHostWidget->height())
            : 1.0;
        const float centerX = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
        const float centerY = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
        const QColor sampled = currentBrushColor();
        CanvasColorValue sampledColor;
        sampledColor.r = static_cast<float>(sampled.redF());
        sampledColor.g = static_cast<float>(sampled.greenF());
        sampledColor.b = static_cast<float>(sampled.blueF());
        sampledColor.a = static_cast<float>(sampled.alphaF());
        presentation.setEyedropperCursorState(true, centerX, centerY, sampledColor);
    });
    m_cursorManager->setToolCursorCallback([this](const std::optional<QPoint>& globalPos) {
        if (!m_engineBinding || m_engineBinding->session().status() != CanvasEngineStatus::Ready) {
            return;
        }
        auto& session = m_engineBinding->session();
        auto& view = session.view();
        auto& presentation = session.presentation();
        if (!globalPos) {
            presentation.setToolCursorState(false, 0, 0);
            return;
        }
        const QPoint localPos = m_viewportHostWidget->mapFromGlobal(*globalPos);
        const QSizeF extent = view.viewportExtent();
        const qreal scaleX = m_viewportHostWidget->width() > 0
            ? extent.width() / static_cast<qreal>(m_viewportHostWidget->width())
            : 1.0;
        const qreal scaleY = m_viewportHostWidget->height() > 0
            ? extent.height() / static_cast<qreal>(m_viewportHostWidget->height())
            : 1.0;
        const float centerX = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
        const float centerY = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
        if (m_effectParameterOverlayDragging || effectParameterOverlayHitTest(*globalPos) >= 0) {
            presentation.setToolCursorState(true, centerX, centerY, ToolCursorStyle::Pointer);
            return;
        }
        const ToolId cursorTool = toolMode();
        presentation.setToolCursorState(
            true, centerX, centerY, toolCursorStyle(cursorTool), cursorTool);
    });
    m_cursorManager->setSuppressed(m_cursorManagerSuppressedByLoading);
    updateCursorManagerOverlay();

    connect(&events, &CanvasEngineQtEvents::viewZoomChanged, this,
        &CanvasPanel::updateBrushCursorOverlayRadius);
    connect(&events, &CanvasEngineQtEvents::viewZoomChanged, this, &CanvasPanel::zoomChanged);
    connect(&events, &CanvasEngineQtEvents::viewZoomChanged, this,
        [this](qreal) { syncZoomInfoOverlayValue(); });
    connect(&events, &CanvasEngineQtEvents::viewZoomChanged, this, [this]() {
        if (m_canvasResizeController && m_canvasResizeController->isActive()) {
            m_canvasResizeController->updateOverlay();
        }
        if (m_exportAreaController && m_exportAreaController->isActive()) {
            m_exportAreaController->updateOverlay();
        }
        updateSelectionActionPopup();
    });
    connect(&events, &CanvasEngineQtEvents::framePresented, this, [this]() {
        if (!m_loadingAppearanceAnimationActive || !m_loadingAppearanceAnimationRunning
            || !m_engineBinding) {
            return;
        }
        if (m_engineBinding->session().view().isCameraAnimating()) {
            return;
        }
        completeLoadingAppearanceAnimation();
    });
    connect(&events, &CanvasEngineQtEvents::presentationSyncRequested, this,
        [this](const CanvasViewSnapshot&) {
            syncEffectParameterOverlayPresentation();
            if (m_canvasResizeController && m_canvasResizeController->isActive()) {
                m_canvasResizeController->updateOverlay();
            }
            if (m_exportAreaController && m_exportAreaController->isActive()) {
                m_exportAreaController->updateOverlay();
            }
            updateSelectionActionPopup();
        });

    // Connect brush control signals
    connect(m_brushOverlay, &ruwa::ui::widgets::BrushControlOverlay::brushSizeChanged, this,
        [this](qreal size) {
            writeLiveBrushSizeToToolState(size);
            if (m_engineBinding) {
                const float radius = ruwa::ui::widgets::brushRadiusFromNormalizedSizeForCanvasMode(
                    size, m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
                auto& painting = m_engineBinding->session().painting();
                painting.setBrushRadius(radius);
                painting.updateBrushCursorStamp();
            }
            updateBrushCursorOverlayRadius();
            if (m_toolStateController && m_toolStateController->suppressPersistDuringRestore()) {
                return;
            }
            persistGlobalToolState();
        });

    connect(m_brushOverlay, &ruwa::ui::widgets::BrushControlOverlay::brushOpacityChanged, this,
        [this](qreal opacity) {
            writeLiveBrushOpacityToToolState(opacity);
            if (m_engineBinding) {
                // Convert 0.0-1.0 to 0-255
                uint8_t alpha = static_cast<uint8_t>(opacity * 255.0);
                // Use stored RGB values, only change alpha
                QColor color = currentBrushColor();
                color.setAlpha(alpha);
                if (m_toolStateController) {
                    m_toolStateController->setCurrentColor(color);
                }
                CanvasColorValue brushColor;
                brushColor.r = static_cast<float>(color.redF());
                brushColor.g = static_cast<float>(color.greenF());
                brushColor.b = static_cast<float>(color.blueF());
                brushColor.a = static_cast<float>(alpha) / 255.0f;
                m_engineBinding->session().painting().setBrushColor(brushColor);
            }
            if (m_toolStateController && m_toolStateController->suppressPersistDuringRestore()) {
                return;
            }
            persistGlobalToolState();
        });

    if (m_stylusJoystick) {
        connect(&events, &CanvasEngineQtEvents::viewZoomChanged, m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::setZoom);
        connect(&events, &CanvasEngineQtEvents::viewRotationChanged, m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::setRotation);
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::zoomToFitRequested, this,
            &CanvasPanel::zoomToFit);
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::zoomChangeRequested, this,
            [this](qreal zoom) {
                if (m_engineBinding
                    && m_engineBinding->session().status() == CanvasEngineStatus::Ready) {
                    setZoomSmooth(static_cast<float>(zoom));
                }
            });
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::panRequested, this,
            [this](const QPointF& v) {
                if (!isInteractionEnabled() || isExportMode()) {
                    return;
                }
                if (!m_engineBinding
                    || m_engineBinding->session().status() != CanvasEngineStatus::Ready) {
                    return;
                }
                const qreal len = std::hypot(v.x(), v.y());
                if (len < 0.02) {
                    return;
                }
                auto& view = m_engineBinding->session().view();
                // Convert joystick screen-direction movement to document delta
                // through the view mapping, so pan direction stays correct for
                // any rotation.
                const QSizeF extent = view.viewportExtent();
                const QPointF centerScreen(extent.width() * 0.5, extent.height() * 0.5);
                const qreal screenSpeed = 12.0 * qBound(0.25, len, 1.0);
                const QPointF stepScreen(v.x() * screenSpeed, v.y() * screenSpeed);
                const QPointF movedScreen(
                    centerScreen.x() + stepScreen.x(), centerScreen.y() + stepScreen.y());
                const QPointF worldPrev = view.documentFromViewport(centerScreen);
                const QPointF worldCurr = view.documentFromViewport(movedScreen);
                view.moveCameraBy(worldCurr - worldPrev);
                requestRender();
                if (m_canvasResizeController && m_canvasResizeController->isInteractionActive()) {
                    m_canvasResizeController->updateOverlay();
                }
                updateSelectionActionPopup();
            });
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::rotationRequested, this,
            [this](qreal delta) {
                if (!isInteractionEnabled() || isExportMode()) {
                    return;
                }
                if (!m_engineBinding
                    || m_engineBinding->session().status() != CanvasEngineStatus::Ready) {
                    return;
                }
                if (std::abs(delta) < 1e-4) {
                    return;
                }
                m_engineBinding->session().view().addRotationRadians(delta);
                requestRender();
                if (m_canvasResizeController && m_canvasResizeController->isInteractionActive()) {
                    m_canvasResizeController->updateOverlay();
                }
            });
    }

    if (auto* packOverlay = m_brushOverlay->brushPackOverlay()) {
        if (auto* packPanel = packOverlay->panel()) {
            connect(packPanel, &ruwa::ui::widgets::BrushPackPanel::brushSelectionRequested, this,
                &CanvasPanel::selectBrushForCurrentContext);
            connect(packPanel, &ruwa::ui::widgets::BrushPackPanel::activeBrushSettingsChanged, this,
                [this](const ruwa::core::brushes::BrushSettingsData& settings) {
                    applyBrushSettings(settings);
                });
            packPanel->selectBrush(selectedBrushIdForCurrentContext());
        }
    }

    if (CanvasToolStateController::isDrawInstrument(toolMode())) {
        restoreToolState(toolMode());
    }

    updateStyles();
    return true;
}

} // namespace ruwa::ui::workspace
