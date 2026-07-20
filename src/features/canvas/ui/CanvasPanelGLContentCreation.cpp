// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   G L   C O N T E N T
// ==========================================================================

#include "CanvasPanel.h"

#include "CanvasCursorManager.h"
#include "CanvasOverlayLayoutManager.h"
#include "CanvasToolStateController.h"
#include "TextEditingController.h"
#include "CanvasPanelHelpers.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushPackOverlay.h"
#include "features/brush/ui/BrushPackPanel.h"
#include "features/brush/ui/BrushSizeCurve.h"
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
#include <QOpenGLWidget>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <vector>

namespace ruwa::ui::workspace {

bool CanvasPanel::createGLContent()
{
    if (m_glContentCreated) {
        return false;
    }
    m_glContentCreated = true;

    // Remove placeholder
    if (m_glPlaceholder) {
        m_contentLayout->removeWidget(m_glPlaceholder);
        delete m_glPlaceholder;
        m_glPlaceholder = nullptr;
    }

    m_glWidget = new aether::OpenGLCanvasWidget(m_contentWidget);
    m_glWidget->setTabletTracking(true);
    m_glWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);
    m_glWidget->setAcceptDrops(true);
    m_glWidget->setMinimumSize(200, 200);
    m_glWidget->setCanvasBoundsMode(m_canvasBoundsMode);
    m_glWidget->setCanvas(m_canvasSize.width(), m_canvasSize.height());
    m_glWidget->setLassoStabilization(static_cast<float>(lassoStabilization()));
    m_glWidget->setLassoFillStabilization(static_cast<float>(lassoFillStabilization()));
    m_contentLayout->addWidget(m_glWidget);
    m_contentLayout->activate();
    applyZoomLimits();

    auto applyUndoMemoryLimit = [this]() {
        if (!m_glWidget)
            return;
        const auto& settings = ruwa::core::SettingsManager::instance().settings();
        const qint64 bytes
            = static_cast<qint64>(settings.performance.undoMemoryLimitMb) * 1024LL * 1024LL;
        m_glWidget->canvas().undoManager().setMemoryLimit(bytes);
    };
    applyUndoMemoryLimit();
    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::undoMemoryLimitChanged, this, [this](int megabytes) {
            if (!m_glWidget)
                return;
            const qint64 bytes = static_cast<qint64>(megabytes) * 1024LL * 1024LL;
            m_glWidget->canvas().undoManager().setMemoryLimit(bytes);
        });

    // Keep overlay on top — layout-add puts new widget above overlay, so raise overlay again
    if (m_loadingOverlay) {
        m_loadingOverlay->raise();
    }
    connect(m_glWidget, &aether::OpenGLCanvasWidget::strokePainted, this,
        [this]() { emit strokePainted(); });
    connect(m_glWidget, &aether::OpenGLCanvasWidget::contentRegionChanged, this,
        [this](const QRect& worldRect) { emit canvasContentRegionChanged(worldRect); });
    connect(m_glWidget, &aether::OpenGLCanvasWidget::contentTilesChanged, this,
        [this](
            const QList<QPoint>& tilePositions) { emit canvasContentTilesChanged(tilePositions); });
    connect(m_glWidget, &aether::OpenGLCanvasWidget::fillProcessingLayerChanged, this,
        [this](const QUuid& layerId) { emit fillProcessingLayerChanged(layerId); });
    connect(m_glWidget, &aether::OpenGLCanvasWidget::surfaceResized, this,
        &CanvasPanel::onSurfaceResized);
    connect(
        m_glWidget, &aether::OpenGLCanvasWidget::transformModeExited, this, [this](bool applied) {
            if (applied) {
                emit canvasContentChanged();
            }
        });
    connect(m_glWidget, &aether::OpenGLCanvasWidget::initialized, this, [this]() {
        onGLInitialized();
        if (m_glWidget) {
            m_glWidget->updateBrushCursorStamp();
        }
        if (!playNewProjectAppearanceAnimationIfScheduled()
            && !m_deferLoadingOverlayHideUntilAppearanceAnimation) {
            fadeOutLoadingOverlay();
        }
    });
    if (m_toolStateOverlay) {
        auto& undoManager = m_glWidget->canvas().undoManager();
        connect(&undoManager, &aether::UndoManager::canUndoChanged, this,
            [this](bool) { syncToolStateOverlayContent(); });
        connect(&undoManager, &aether::UndoManager::canRedoChanged, this,
            [this](bool) { syncToolStateOverlayContent(); });
    }
    connect(
        &m_glWidget->canvas().undoManager(), &aether::UndoManager::indexChanged, this, [this](int) {
            syncToolStateOverlayContent();
            requestRender();
            emit canvasContentChanged();
        });
    connect(&m_glWidget->canvas().undoManager(), &aether::UndoManager::commandApplied, this,
        [this](const QList<QPoint>& tilePositions) {
            if (!tilePositions.isEmpty()) {
                emit canvasContentTilesChanged(tilePositions);
            }
        });

    // Apply stored layer model if set before widget creation
    if (m_layerModel) {
        m_glWidget->setLayerModel(m_layerModel);
    }

    m_glWidget->setRasterizationConfirmCallback(
        [this](const QString& title, const QString& message) {
            return ruwa::ui::widgets::MessagePopupManager::showBlocking(
                this, title + QStringLiteral("\n\n") + message, tr("Yes"), tr("No"), 360, true);
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
    if (m_glWidget) {
        const ToolMode currentTool = toolMode();
        setEraseMode(shouldEraseForTool(currentTool));
        setBlurMode(currentTool == ToolMode::Blur);
        setSmudgeMode(currentTool == ToolMode::Smudge);
        setLiquifyMode(currentTool == ToolMode::Liquify);
        if (m_toolStateOverlay) {
            m_toolStateOverlay->setCanvasFlipStates(
                m_glWidget->canvasContentFlipHorizontal(), m_glWidget->canvasContentFlipVertical());
            m_toolStateOverlay->setBrushEraserMode(isBrushEraserActive());
        }
        const QColor color = currentBrushColor();
        m_glWidget->brush().setColor(static_cast<uint8_t>(color.red()),
            static_cast<uint8_t>(color.green()), static_cast<uint8_t>(color.blue()),
            static_cast<uint8_t>(color.alpha()));
        m_glWidget->brush().setRadius(ruwa::ui::widgets::brushRadiusFromNormalizedSizeForCanvasMode(
            m_brushOverlay->brushSize(), m_canvasSize.width(), m_canvasSize.height(),
            hasFiniteDocumentBounds()));
        applyBrushSettings({});

        // Same-frame GPU backdrop blur. Geometry is sampled from the real
        // widgets immediately before paintGL, so layout animation and blur use
        // one positional source of truth.
        m_glWidget->setBackdropRegionProvider([this]() {
            std::vector<aether::CanvasBackdropRegion> regions;
            if (!m_glWidget || !m_contentWidget) {
                return regions;
            }

            const auto appendRegion = [this, &regions](QWidget* widget, const QRectF& localRect,
                                          qreal cornerRadius, qreal opacity) {
                if (!widget || localRect.isEmpty() || opacity <= 0.001
                    || !widget->isVisibleTo(m_contentWidget)) {
                    return;
                }
                const QPoint integralTopLeft(
                    static_cast<int>(std::floor(localRect.x())),
                    static_cast<int>(std::floor(localRect.y())));
                // The overlay and OpenGL canvas are siblings, not ancestors of
                // one another. Convert through their real common ancestor.
                const QPoint contentPoint
                    = widget->mapTo(m_contentWidget, integralTopLeft);
                const QPoint mapped
                    = m_glWidget->mapFrom(m_contentWidget, contentPoint);
                const QPointF fractionalOffset(localRect.x() - integralTopLeft.x(),
                    localRect.y() - integralTopLeft.y());
                regions.push_back({ QRectF(QPointF(mapped) + fractionalOffset, localRect.size()),
                    cornerRadius, opacity });
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
            return regions;
        });
        const auto syncBackdropOpacity = [this](QGraphicsOpacityEffect* effect) {
            if (!effect) {
                return;
            }
            connect(effect, &QGraphicsOpacityEffect::opacityChanged, m_glWidget,
                [this]() {
                    if (m_glWidget) {
                        m_glWidget->requestBackdropUpdate();
                    }
                });
        };
        syncBackdropOpacity(m_brushOverlayOpacity);
        syncBackdropOpacity(m_stylusJoystickOpacity);
        syncBackdropOpacity(m_toolStateOverlayOpacity);

        if (m_brushOverlay) {
            m_brushOverlay->setBackdropSource(m_glWidget);
            connect(m_glWidget, &aether::OpenGLCanvasWidget::backdropAvailabilityChanged,
                m_brushOverlay, QOverload<>::of(&QWidget::update));
            connect(m_glWidget, &QObject::destroyed, m_brushOverlay, [this]() {
                if (m_brushOverlay) {
                    m_brushOverlay->setBackdropSource(nullptr);
                }
            });
        }
        if (m_stylusJoystick) {
            m_stylusJoystick->setBackdropSource(m_glWidget);
            connect(m_glWidget, &aether::OpenGLCanvasWidget::backdropAvailabilityChanged,
                m_stylusJoystick,
                &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::refreshBackdropContent);
            connect(m_glWidget, &QObject::destroyed, m_stylusJoystick, [this]() {
                if (m_stylusJoystick) {
                    m_stylusJoystick->setBackdropSource(nullptr);
                }
            });
        }
        if (m_toolStateOverlay) {
            m_toolStateOverlay->setBackdropSource(m_glWidget);
            connect(m_glWidget, &aether::OpenGLCanvasWidget::backdropAvailabilityChanged,
                m_toolStateOverlay, QOverload<>::of(&QWidget::update));
            connect(m_glWidget, &QObject::destroyed, m_toolStateOverlay, [this]() {
                if (m_toolStateOverlay) {
                    m_toolStateOverlay->setBackdropSource(nullptr);
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

    // Canvas cursor manager (GL brush/eyedropper cursor when over canvas)
    m_cursorManager = new CanvasCursorManager(m_contentWidget, m_glWidget, m_brushOverlay, this);
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
        if (!m_glWidget || !m_glWidget->isInitialized())
            return;
        if (!globalPos) {
            m_glWidget->setBrushCursorState(false, 0, 0, 0);
            return;
        }
        const QPoint localPos = m_glWidget->mapFromGlobal(*globalPos);
        const qreal scaleX = m_glWidget->width() > 0
            ? static_cast<qreal>(m_glWidget->viewport().width())
                / static_cast<qreal>(m_glWidget->width())
            : 1.0;
        const qreal scaleY = m_glWidget->height() > 0
            ? static_cast<qreal>(m_glWidget->viewport().height())
                / static_cast<qreal>(m_glWidget->height())
            : 1.0;
        const float centerX = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
        const float centerY = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
        const float cursorScale = static_cast<float>((scaleX + scaleY) * 0.5);
        const float radiusScreen
            = m_glWidget->brush().radius() * m_glWidget->viewport().camera().zoom() * cursorScale;
        m_glWidget->setBrushCursorState(true, centerX, centerY, radiusScreen);
    });
    m_cursorManager->setEyedropperCursorCallback([this](const std::optional<QPoint>& globalPos) {
        if (!m_glWidget || !m_glWidget->isInitialized())
            return;
        if (!globalPos) {
            m_glWidget->setEyedropperCursorState(false, 0, 0);
            return;
        }
        const QPoint localPos = m_glWidget->mapFromGlobal(*globalPos);
        const qreal scaleX = m_glWidget->width() > 0
            ? static_cast<qreal>(m_glWidget->viewport().width())
                / static_cast<qreal>(m_glWidget->width())
            : 1.0;
        const qreal scaleY = m_glWidget->height() > 0
            ? static_cast<qreal>(m_glWidget->viewport().height())
                / static_cast<qreal>(m_glWidget->height())
            : 1.0;
        const float centerX = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
        const float centerY = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);
        m_glWidget->setEyedropperCursorState(true, centerX, centerY, currentBrushColor());
    });
    m_cursorManager->setSuppressed(m_cursorManagerSuppressedByLoading);
    updateCursorManagerOverlay();

    connect(m_glWidget, &aether::OpenGLCanvasWidget::cameraZoomChanged, this,
        &CanvasPanel::updateBrushCursorOverlayRadius);
    connect(m_glWidget, &aether::OpenGLCanvasWidget::cameraZoomChanged, this,
        [this](qreal) { syncZoomInfoOverlayValue(); });
    connect(m_glWidget, &aether::OpenGLCanvasWidget::cameraZoomChanged, this, [this]() {
        if (m_canvasResizeController && m_canvasResizeController->isActive()) {
            m_canvasResizeController->updateOverlay();
        }
        if (m_exportAreaController && m_exportAreaController->isActive()) {
            m_exportAreaController->updateOverlay();
        }
        updateSelectionActionPopup();
        if (m_textEditingController && m_textEditingController->isEditing()) {
            m_textEditingController->refreshFormattingPopup();
        }
    });
    connect(m_glWidget, &QOpenGLWidget::frameSwapped, this, [this]() {
        if (!m_loadingAppearanceAnimationActive || !m_loadingAppearanceAnimationRunning
            || !m_glWidget) {
            return;
        }
        if (m_glWidget->viewport().camera().isAnimating()) {
            return;
        }
        completeLoadingAppearanceAnimation();
    });
    connect(m_glWidget, &QOpenGLWidget::aboutToCompose, this, [this]() {
        if (m_canvasResizeController && m_canvasResizeController->isActive()) {
            m_canvasResizeController->updateOverlay();
        }
        if (m_exportAreaController && m_exportAreaController->isActive()) {
            m_exportAreaController->updateOverlay();
        }
        updateSelectionActionPopup();
        if (m_textEditingController && m_textEditingController->isEditing()) {
            m_textEditingController->refreshFormattingPopup();
        }
    });

    // Connect brush control signals
    connect(m_brushOverlay, &ruwa::ui::widgets::BrushControlOverlay::brushSizeChanged, this,
        [this](qreal size) {
            if (m_glWidget) {
                const float radius = ruwa::ui::widgets::brushRadiusFromNormalizedSizeForCanvasMode(
                    size, m_canvasSize.width(), m_canvasSize.height(), hasFiniteDocumentBounds());
                m_glWidget->brush().setRadius(radius);
                m_glWidget->updateBrushCursorStamp();
            }
            updateBrushCursorOverlayRadius();
            if (m_toolStateController && m_toolStateController->suppressPersistDuringRestore()) {
                return;
            }
            persistGlobalToolState();
        });

    connect(m_brushOverlay, &ruwa::ui::widgets::BrushControlOverlay::brushOpacityChanged, this,
        [this](qreal opacity) {
            if (m_glWidget) {
                // Convert 0.0-1.0 to 0-255
                uint8_t alpha = static_cast<uint8_t>(opacity * 255.0);
                // Use stored RGB values, only change alpha
                QColor color = currentBrushColor();
                color.setAlpha(alpha);
                if (m_toolStateController) {
                    m_toolStateController->setCurrentColor(color);
                }
                m_glWidget->brush().setColor(static_cast<uint8_t>(color.red()),
                    static_cast<uint8_t>(color.green()), static_cast<uint8_t>(color.blue()), alpha);
            }
            if (m_toolStateController && m_toolStateController->suppressPersistDuringRestore()) {
                return;
            }
            persistGlobalToolState();
        });

    if (m_stylusJoystick) {
        connect(m_glWidget, &aether::OpenGLCanvasWidget::cameraZoomChanged, m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::setZoom);
        connect(m_glWidget, &aether::OpenGLCanvasWidget::cameraRotationChanged, m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::setRotation);
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::zoomToFitRequested, this,
            &CanvasPanel::zoomToFit);
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::zoomChangeRequested, this,
            [this](qreal zoom) {
                if (m_glWidget && m_glWidget->isInitialized()) {
                    setZoomSmooth(static_cast<float>(zoom));
                }
            });
        connect(m_stylusJoystick,
            &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::panRequested, this,
            [this](const QPointF& v) {
                if (!isInteractionEnabled() || isExportMode()) {
                    return;
                }
                if (!m_glWidget || !m_glWidget->isInitialized()) {
                    return;
                }
                auto& cam = m_glWidget->viewport().camera();
                const qreal len = std::hypot(v.x(), v.y());
                if (len < 0.02) {
                    return;
                }
                // Convert joystick screen-direction movement to world delta through
                // camera screenToWorld, so pan direction stays correct for any rotation.
                auto& vp = m_glWidget->viewport();
                const aether::Vector2 viewportSize = vp.size();
                const aether::Vector2 centerScreen(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
                const qreal screenSpeed = 12.0 * qBound(0.25, len, 1.0);
                const aether::Vector2 stepScreen(static_cast<float>(v.x() * screenSpeed),
                    static_cast<float>(v.y() * screenSpeed));
                const aether::Vector2 movedScreen(
                    centerScreen.x + stepScreen.x, centerScreen.y + stepScreen.y);
                const aether::Vector2 worldPrev = cam.screenToWorld(centerScreen, viewportSize);
                const aether::Vector2 worldCurr = cam.screenToWorld(movedScreen, viewportSize);
                cam.move(worldCurr - worldPrev);
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
                if (!m_glWidget || !m_glWidget->isInitialized()) {
                    return;
                }
                auto& cam = m_glWidget->viewport().camera();
                if (std::abs(delta) < 1e-4) {
                    return;
                }
                cam.addRotation(static_cast<float>(delta));
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
