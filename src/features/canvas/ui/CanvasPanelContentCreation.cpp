// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   C O N T E N T
// ==========================================================================

#include "CanvasPanel.h"

#include "CanvasTabletHandler.h"
#include "CanvasMouseInputHandler.h"
#include "CanvasBrushQuickPopupManager.h"
#include "CanvasSelectionPopupManager.h"
#include "CanvasKeyEventHandler.h"
#include "CanvasSpaceMoveHandler.h"
#include "CanvasImageImportHelper.h"
#include "CanvasOverlayLayoutManager.h"
#include "CanvasOverlayLayout.h"
#include "CanvasCursorManager.h"
#include "CanvasPanelHelpers.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"
#include "features/canvas/ui/CanvasZoomInfoOverlay.h"
#include "features/canvas/ui/CanvasSelectionSizeOverlay.h"
#include "features/canvas/ui/CanvasPositionPickerOverlay.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/undo/UndoManager.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QSizePolicy>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace ruwa::ui::workspace {

QWidget* CanvasPanel::createContent()
{
    m_contentWidget = new QWidget();
    m_contentWidget->setTabletTracking(true);
    m_contentWidget->setAcceptDrops(true);
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);

    // Placeholder until tab transition animation completes (createGLContent replaces it)
    // Visible immediately with theme background — drawn before GL init
    m_glPlaceholder = new QWidget(m_contentWidget);
    m_glPlaceholder->setMinimumSize(200, 200);
    m_glPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_glPlaceholder->setStyleSheet(QString("background-color: %1;")
            .arg(ruwa::ui::core::ThemeManager::instance().colors().background.name()));
    m_glPlaceholder->setAutoFillBackground(true);
    m_contentLayout->addWidget(m_glPlaceholder);

    // Overlay covering canvas with theme background color — fades out when zoom animation starts
    m_loadingOverlay = new QWidget(m_contentWidget);
    m_loadingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_loadingOverlayOpacity = new QGraphicsOpacityEffect(m_loadingOverlay);
    m_loadingOverlayOpacity->setOpacity(1.0);
    m_loadingOverlay->setGraphicsEffect(m_loadingOverlayOpacity);
    auto* loadingLayout = new QVBoxLayout(m_loadingOverlay);
    loadingLayout->setContentsMargins(24, 24, 24, 24);
    loadingLayout->setSpacing(10);
    loadingLayout->setAlignment(Qt::AlignCenter);

    m_loadingIndicator = new ruwa::ui::widgets::DotGridLoadingIndicator(m_loadingOverlay);
    m_loadingIndicator->setFixedSize(42, 42);

    m_loadingTitleLabel = new QLabel(tr("Loading canvas"), m_loadingOverlay);
    m_loadingTitleLabel->setAlignment(Qt::AlignCenter);
    m_loadingTitleLabel->setObjectName(QStringLiteral("canvasLoadingTitle"));

    m_loadingStatusLabel = new QLabel(tr("Preparing layer data..."), m_loadingOverlay);
    m_loadingStatusLabel->setAlignment(Qt::AlignCenter);
    m_loadingStatusLabel->setObjectName(QStringLiteral("canvasLoadingStatus"));

    loadingLayout->addWidget(m_loadingIndicator, 0, Qt::AlignCenter);
    loadingLayout->addWidget(m_loadingTitleLabel, 0, Qt::AlignCenter);
    loadingLayout->addWidget(m_loadingStatusLabel, 0, Qt::AlignCenter);

    m_loadingIndicator->setVisible(m_loadingOverlayDecorationsVisible);
    m_loadingTitleLabel->setVisible(m_loadingOverlayDecorationsVisible);
    m_loadingStatusLabel->setVisible(m_loadingOverlayDecorationsVisible);

    {
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
        m_loadingIndicator->setAccentColor(colors.primary);
    }

    if (m_loadingOverlayDecorationsVisible && m_loadingIndicator) {
        m_loadingIndicator->start();
    }
    if (m_loadingOverlayOpacity) {
        m_loadingOverlayOpacity->setOpacity(1.0);
    }
    m_loadingOverlay->show();
    m_loadingOverlay->raise();
    m_contentWidget->installEventFilter(this);

    // Set overlay geometry immediately so fill is visible from first frame, before canvas init
    updateLoadingOverlayGeometry();

    m_canvasResizeController = new CanvasResizeController(this);
    m_canvasResizeController->setContentWidget(m_contentWidget);
    m_canvasResizeController->setCanvasSize(m_canvasSize);
    m_canvasResizeController->setupRectAnimation();

    m_tabletHandler = new CanvasTabletHandler(this);
    m_mouseHandler = new CanvasMouseInputHandler(this);
    m_brushQuickPopupManager = new CanvasBrushQuickPopupManager(this);
    m_popupManager = new CanvasSelectionPopupManager(this);
    m_keyHandler = new CanvasKeyEventHandler(this);
    m_spaceMoveHandler = new CanvasSpaceMoveHandler(this);
    m_imageImportHelper = new CanvasImageImportHelper(this);
    m_overlayLayoutManager = new CanvasOverlayLayoutManager(this);

    // Create brush overlay immediately so it can fade in smoothly when loading overlay fades out
    m_brushOverlay = new ruwa::ui::widgets::BrushControlOverlay(m_contentWidget);
    m_brushOverlay->setTabletTracking(true);
    m_brushOverlay->setCanvasSize(m_canvasSize);
    m_brushOverlay->setHasFiniteDocumentBounds(hasFiniteDocumentBounds());
    qreal initialOverlaySize = 0.3;
    qreal initialOverlayOpacity = static_cast<qreal>(brushAlpha()) / 255.0;
    const ToolMode initInst = overlayInstrumentMode();
    if (const ToolBrushState* initState = toolBrushStateForInstrument(initInst);
        initState && initState->valid) {
        initialOverlaySize = initState->brushSize;
        initialOverlayOpacity = initState->brushOpacity;
    } else if (m_toolStateController && m_toolStateController->brushState().valid) {
        initialOverlaySize = m_toolStateController->brushState().brushSize;
        initialOverlayOpacity = m_toolStateController->brushState().brushOpacity;
    } else if (m_toolStateController && m_toolStateController->blurState().valid) {
        initialOverlaySize = m_toolStateController->blurState().brushSize;
        initialOverlayOpacity = m_toolStateController->blurState().brushOpacity;
    } else if (m_toolStateController && m_toolStateController->eraserState().valid) {
        initialOverlaySize = m_toolStateController->eraserState().brushSize;
        initialOverlayOpacity = m_toolStateController->eraserState().brushOpacity;
    }
    m_brushOverlay->setBrushSize(qBound(0.0, initialOverlaySize, 1.0));
    m_brushOverlay->setBrushOpacity(qBound(0.0, initialOverlayOpacity, 1.0));
    m_brushOverlay->setBrushColor(currentBrushColor());
    m_brushOverlayOpacity = new QGraphicsOpacityEffect(m_brushOverlay);
    m_brushOverlayOpacity->setOpacity(0.0);
    m_brushOverlay->setGraphicsEffect(m_brushOverlayOpacity);
    m_brushOverlay->hide();
    m_brushOverlay->stackUnder(m_loadingOverlay);
    m_brushOverlayNeedsInitialPlacement = true;
    connect(m_brushOverlay, &ruwa::ui::widgets::BrushControlOverlay::positionChanged, this,
        [this](const QPoint& pos) {
            // The overlay trails the pointer with a lag while dragged — suppress the
            // custom canvas cursor for the duration so it can't flicker on edge crossings.
            if (m_cursorManager) {
                m_cursorManager->setOverlayDragActive(true);
            }
            if (!m_overlayLayoutManager || !m_overlayLayoutManager->engine()) {
                return;
            }
            m_brushOverlayUserMoved = true;
            // A genuine drag overrides any restored normalized position;
            // the engine clamps, applies wall-collision and moves. Saved
            // position + brushOverlayPositionChanged are emitted via itemMoved.
            m_pendingBrushOverlayPositionNormalized.reset();
            m_overlayLayoutManager->engine()->applyDrag(m_brushOverlay, pos);
        });
    connect(m_brushOverlay, &ruwa::ui::widgets::BrushControlOverlay::dragFinished, this, [this]() {
        if (m_cursorManager) {
            m_cursorManager->setOverlayDragActive(false);
        }
        if (m_overlayLayoutManager && m_overlayLayoutManager->engine()) {
            m_overlayLayoutManager->engine()->endDrag(m_brushOverlay);
        }
    });

    if (m_pendingBrushOverlayPositionNormalized.has_value()
        || m_savedBrushOverlayPosition.has_value()) {
        // Saved position will be applied when content geometry is final
        // (hideLoadingOverlayImmediately when content has valid size).
        m_brushOverlayNeedsInitialPlacement = false;
    }

    m_toolStateOverlay = new ruwa::ui::widgets::CanvasToolStateOverlay(m_contentWidget);
    m_toolStateOverlay->setTabletTracking(true);
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::sizeChanged, this,
        [this](const QSize&) {
            if (!m_toolStateOverlay || !m_overlayLayoutManager || !m_contentWidget) {
                return;
            }
            if (m_contentWidget->width() <= 0 || m_contentWidget->height() <= 0) {
                return;
            }
            // Tool HUD always re-anchors top-center as its width animates.
            m_overlayLayoutManager->layoutToolStateOverlay();
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::undoRequested, this,
        [this]() {
            if (m_glWidget) {
                m_glWidget->canvas().undoManager().undo();
            }
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::redoRequested, this,
        [this]() {
            if (m_glWidget) {
                m_glWidget->canvas().undoManager().redo();
            }
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::copyCanvasRequested,
        this, [this]() { copyCanvasToClipboard(); });
    connect(m_toolStateOverlay,
        &ruwa::ui::widgets::CanvasToolStateOverlay::canvasFlipHorizontalRequested, this,
        [this](bool checked) {
            if (!m_glWidget) {
                return;
            }
            if (m_glWidget->canvasContentFlipHorizontal() != checked) {
                toggleCanvasViewFlipHorizontal();
            } else if (m_toolStateOverlay) {
                m_toolStateOverlay->setCanvasFlipStates(m_glWidget->canvasContentFlipHorizontal(),
                    m_glWidget->canvasContentFlipVertical());
            }
        });
    connect(m_toolStateOverlay,
        &ruwa::ui::widgets::CanvasToolStateOverlay::canvasFlipVerticalRequested, this,
        [this](bool checked) {
            if (!m_glWidget) {
                return;
            }
            if (m_glWidget->canvasContentFlipVertical() != checked) {
                toggleCanvasViewFlipVertical();
            } else if (m_toolStateOverlay) {
                m_toolStateOverlay->setCanvasFlipStates(m_glWidget->canvasContentFlipHorizontal(),
                    m_glWidget->canvasContentFlipVertical());
            }
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::brushEraserModeToggled,
        this, [this](bool enabled) { setBrushEraserActive(enabled); });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::brushHardnessChanged,
        this, [this](qreal hardness) {
            auto settings = m_brushOverlay ? m_brushOverlay->brushSettings()
                                           : ruwa::core::brushes::BrushSettingsData {};
            settings.hardness = qBound(0.0f, static_cast<float>(hardness), 1.0f);
            applyToolStateBrushSettings(settings);
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::brushFlowChanged, this,
        [this](qreal flow) {
            auto settings = m_brushOverlay ? m_brushOverlay->brushSettings()
                                           : ruwa::core::brushes::BrushSettingsData {};
            settings.flow = qBound(0.0f, static_cast<float>(flow), 1.0f);
            applyToolStateBrushSettings(settings);
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::blurIntensityChanged,
        this, [this](qreal intensity) {
            // Blur uses the same fixed soft brush as Liquify. Intensity changes
            // its flow without modifying the brush selected in the pack panel.
            setFixedSoftBrushStrength(CanvasToolMode::Blur, intensity);
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::smudgeIntensityChanged,
        this, [this](qreal intensity) {
            auto settings = m_brushOverlay ? m_brushOverlay->brushSettings()
                                           : ruwa::core::brushes::BrushSettingsData {};
            settings.flow = qBound(0.0f, static_cast<float>(intensity), 1.0f);
            applyToolStateBrushSettings(settings);
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::smudgeWetMixChanged,
        this, [this](qreal wetMix) {
            auto settings = m_brushOverlay ? m_brushOverlay->brushSettings()
                                           : ruwa::core::brushes::BrushSettingsData {};
            settings.wetMix = qBound(0.0f, static_cast<float>(wetMix), 1.0f);
            applyToolStateBrushSettings(settings);
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::liquifyStrengthChanged,
        this, [this](qreal strength) {
            setFixedSoftBrushStrength(CanvasToolMode::Liquify, strength);
        });
    connect(m_toolStateOverlay, &ruwa::ui::widgets::CanvasToolStateOverlay::liquifyModeChanged,
        this, [this](int mode) {
            if (m_toolStateController) {
                m_toolStateController->setLiquifyToolMode(mode);
            }
            if (m_glWidget) {
                m_glWidget->brush().setLiquifyToolMode(mode);
            }
        });
    connect(m_toolStateOverlay,
        &ruwa::ui::widgets::CanvasToolStateOverlay::lassoStabilizationChanged, this,
        [this](qreal stabilization) {
            setLassoStabilization(stabilization);
            if (m_toolStateController && m_toolStateController->suppressPersistDuringRestore()) {
                return;
            }
            persistGlobalToolState();
        });
    connect(m_toolStateOverlay,
        &ruwa::ui::widgets::CanvasToolStateOverlay::lassoFillStabilizationChanged, this,
        [this](qreal stabilization) {
            setLassoFillStabilization(stabilization);
            if (m_toolStateController && m_toolStateController->suppressPersistDuringRestore()) {
                return;
            }
            persistGlobalToolState();
        });
    m_toolStateOverlayUserMoved = false;
    m_savedToolStateOverlayPosition.reset();
    m_pendingToolStateOverlayPositionNormalized.reset();
    syncToolStateOverlayContent();
    m_toolStateOverlayOpacity = new QGraphicsOpacityEffect(m_toolStateOverlay);
    m_toolStateOverlayOpacity->setOpacity(0.0);
    m_toolStateOverlay->setGraphicsEffect(m_toolStateOverlayOpacity);
    m_toolStateOverlay->hide();
    m_toolStateOverlay->stackUnder(m_loadingOverlay);

    m_stylusJoystick = new ruwa::ui::widgets::CanvasStylusJoystickContainerWidget(m_contentWidget);
    m_stylusJoystick->setTabletTracking(true);
    if (m_savedStylusJoystickAbovePanel.has_value()) {
        m_stylusJoystick->setJoystickAbovePanel(*m_savedStylusJoystickAbovePanel);
    }
    m_stylusJoystickOpacity = new QGraphicsOpacityEffect(m_stylusJoystick);
    m_stylusJoystickOpacity->setOpacity(0.0);
    m_stylusJoystick->setGraphicsEffect(m_stylusJoystickOpacity);
    m_stylusJoystick->setVisible(m_canvasWidgets[CanvasWidget::Joystick]);
    if (m_loadingOverlay) {
        m_stylusJoystick->stackUnder(m_loadingOverlay);
    }
    connect(m_stylusJoystick,
        &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::positionChanged, this,
        [this](const QPoint& pos) {
            // Suppress the custom canvas cursor while the joystick is dragged (it trails
            // the pointer with a lag, so the pointer keeps crossing its edge → flicker).
            if (m_cursorManager) {
                m_cursorManager->setOverlayDragActive(true);
            }
            if (!m_overlayLayoutManager || !m_overlayLayoutManager->engine()) {
                return;
            }
            m_stylusJoystickUserMoved = true;
            m_pendingStylusJoystickPositionNormalized.reset();
            // Engine clamps, applies wall-collision and moves. Saved position,
            // above-panel order and the change signal go through itemMoved.
            m_overlayLayoutManager->engine()->applyDrag(m_stylusJoystick, pos);
        });
    connect(m_stylusJoystick, &ruwa::ui::widgets::CanvasStylusJoystickContainerWidget::dragFinished,
        this, [this]() {
            if (m_cursorManager) {
                m_cursorManager->setOverlayDragActive(false);
            }
            if (m_overlayLayoutManager && m_overlayLayoutManager->engine()) {
                m_overlayLayoutManager->engine()->endDrag(m_stylusJoystick);
            }
        });
    applyZoomLimits();

    m_zoomInfoOverlay = new ruwa::ui::widgets::CanvasZoomInfoOverlay(m_contentWidget);
    m_zoomInfoOverlay->hideImmediately();
    m_zoomInfoOverlay->stackUnder(m_loadingOverlay);
    positionZoomInfoOverlay();

    m_selectionSizeOverlay = new ruwa::ui::widgets::CanvasSelectionSizeOverlay(m_contentWidget);
    m_selectionSizeOverlay->hideImmediately();
    m_selectionSizeOverlay->stackUnder(m_loadingOverlay);

    m_positionPickerOverlay = new ruwa::ui::widgets::CanvasPositionPickerOverlay(m_contentWidget);

    // Register the overlays with the layout engine and centralize saved-position
    // updates + change signals through its itemMoved callback.
    m_overlayLayoutManager->attachOverlays();
    if (auto* engine = m_overlayLayoutManager->engine()) {
        connect(engine, &CanvasOverlayLayout::itemMoved, this, [this](QWidget* w, const QPoint& p) {
            if (w == m_brushOverlay) {
                m_savedBrushOverlayPosition = p;
                emit brushOverlayPositionChanged(p);
            } else if (w == m_stylusJoystick) {
                m_savedStylusJoystickPosition = p;
                m_savedStylusJoystickAbovePanel = m_stylusJoystick->isJoystickAbovePanel();
                emit stylusJoystickPositionChanged(p);
            }
        });
    }

    m_overlayLayoutManager->scheduleInitialBrushOverlayPlacement();
    if (m_pendingStylusJoystickPositionNormalized.has_value()
        || (m_savedStylusJoystickPosition.has_value() && m_savedStylusJoystickPosition->x() >= 0
            && m_savedStylusJoystickPosition->y() >= 0)) {
        m_stylusJoystickUserMoved = true;
        // Position will be applied in hideLoadingOverlayImmediately when content has valid size
    } else {
        m_overlayLayoutManager->positionStylusJoystickDefault();
    }

    createExportModeContent();
    updateStyles();
    return m_contentWidget;
}

} // namespace ruwa::ui::workspace
