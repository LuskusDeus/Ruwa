// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   M O D E   C O N T R O L L E R
// ==========================================================================

#include "ExportModeController.h"
#include "ExportSettingsPanel.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "shared/style/AnimationPolicy.h"

#include <QVariantAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QWidget>

#include <cmath>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::workspace {

namespace {
constexpr int kAnimationDurationMs = 400;
constexpr qreal kExportPanelWidthRatio = 0.28; // 28% of host widget width
constexpr int kMinExportPanelWidth = 280;
constexpr int kMaxExportPanelWidth = 450;
constexpr int kCanvasAreaColumns = 2;
constexpr int kPanelAreaColumns = 1;
constexpr float kExportZoomPadding = 80.0f; // Padding around canvas in export view
constexpr int kPanelMarginX = 12; // Horizontal margin inside panel area
constexpr int kPanelMarginY = 12; // Margin from top and bottom

float shortestAngleDeltaRadians(float fromRad, float toRad)
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kPi = 3.14159265358979323846f;
    float d = toRad - fromRad;
    d = std::fmod(d, kTwoPi);
    if (d > kPi) {
        d -= kTwoPi;
    }
    if (d < -kPi) {
        d += kTwoPi;
    }
    return d;
}
} // namespace

ExportModeController::ExportModeController(QWidget* hostWidget, CanvasPanel* canvasPanel,
    ExportSettingsPanel* exportPanel, QObject* parent)
    : QObject(parent)
    , m_workspace(hostWidget)
    , m_canvasPanel(canvasPanel)
    , m_exportPanel(exportPanel)
{
    m_exportPanel->setVisible(false);

    // Watch host widget for resize events to reposition export panel
    m_workspace->installEventFilter(this);

    // A conditional row appearing or disappearing changes how tall the panel
    // wants to be, and the panel is placed by geometry rather than by a layout,
    // so nothing else would notice.
    connect(m_exportPanel, &ExportSettingsPanel::preferredHeightChanged, this,
        &ExportModeController::updateLayout);

    m_animation = new QVariantAnimation(this);
    m_animation->setEasingCurve(QEasingCurve::InOutCubic);

    connect(m_animation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { applyProgress(value.toReal()); });

    connect(m_animation, &QVariantAnimation::finished, this, [this]() {
        if (m_progress <= 0.01) {
            m_exportPanel->setVisible(false);
            if (m_canvasPanel) {
                m_canvasPanel->setExportModeOverlayProgress(0.0);
                m_canvasPanel->setInteractionEnabled(true);
            }
            restoreCameraState();
        }
        emit exportModeChanged(m_targetActive);
    });
}

bool ExportModeController::isAnimating() const
{
    return m_animation->state() == QAbstractAnimation::Running;
}

void ExportModeController::toggle()
{
    if (m_targetActive) {
        exit();
    } else {
        enter();
    }
}

void ExportModeController::enter()
{
    if (m_targetActive && !isAnimating()) {
        return;
    }
    m_targetActive = true;

    if (!m_cameraStateSaved) {
        saveCameraState();
    }

    startAnimation(true);
}

void ExportModeController::exit()
{
    if (!m_targetActive && !isAnimating()) {
        return;
    }
    m_targetActive = false;
    startAnimation(false);
}

void ExportModeController::startAnimation(bool entering)
{
    m_animation->stop();

    const qreal startVal = m_progress;
    const qreal endVal = entering ? 1.0 : 0.0;

    if (qFuzzyCompare(startVal, endVal)) {
        return;
    }

    // Scale duration proportionally to remaining distance
    const qreal distance = qAbs(endVal - startVal);
    m_animation->setDuration(anim::duration(qRound(kAnimationDurationMs * distance)));

    m_animation->setStartValue(startVal);
    m_animation->setEndValue(endVal);

    if (entering) {
        m_exportPanel->setVisible(true);
        m_exportPanel->raise();
        if (m_canvasPanel) {
            m_canvasPanel->setInteractionEnabled(false);
        }
    }

    anim::start(m_animation);
}

int ExportModeController::exportPanelTargetWidth() const
{
    if (!m_workspace)
        return kMinExportPanelWidth;
    return qBound(kMinExportPanelWidth, qRound(m_workspace->width() * kExportPanelWidthRatio),
        kMaxExportPanelWidth);
}

void ExportModeController::applyProgress(qreal progress)
{
    m_progress = progress;

    // Update export panel geometry (overlay inside content widget)
    updateLayout();

    // Fade canvas overlays
    if (m_canvasPanel) {
        m_canvasPanel->setExportModeOverlayProgress(progress);
    }

    // Export preview: no content mirror (stored toggles restored after exit).
    if (m_canvasPanel) {
        m_canvasPanel->setExportPreviewSuppressContentMirror(progress > 1e-5);
    }

    // Animate camera: shift canvas to the left, zoom to fit, rotate smoothly to 0°
    if (m_canvasPanel && m_canvasPanel->isRenderContentReady() && m_cameraStateSaved) {
        const QRect displayFrame = m_canvasPanel->exportPreviewCameraFrame();
        const qreal vpWidth = m_canvasPanel->viewportExtent().width();
        const qreal vpHeight = m_canvasPanel->viewportExtent().height();
        m_canvasPanel->setCameraZoomLimits(0.001, m_canvasPanel->maxZoom());

        const qreal p = progress;

        const qreal rotDelta = shortestAngleDeltaRadians(m_savedCameraRotation, 0.0f);
        m_canvasPanel->setCameraRotationRadians(m_savedCameraRotation + p * rotDelta);
        m_canvasPanel->stopCameraAnimation();

        // Export layout reserves the right third for the settings panel and
        // centers the canvas preview in the left two thirds.
        const qreal columns = kCanvasAreaColumns + kPanelAreaColumns;
        const qreal panelAreaWidth = vpWidth * (kPanelAreaColumns / columns);
        const qreal canvasAreaWidth = vpWidth - panelAreaWidth;
        const qreal reservedPanelWidth = panelAreaWidth * p;
        const qreal availableWidth = canvasAreaWidth - 2.0 * kExportZoomPadding;
        const qreal availableHeight = vpHeight - 2.0 * kExportZoomPadding;

        if (availableWidth > 50.0 && availableHeight > 50.0) {
            const qreal frameWidth = std::max<qreal>(1.0, displayFrame.width());
            const qreal frameHeight = std::max<qreal>(1.0, displayFrame.height());
            const qreal fitZoomX = availableWidth / frameWidth;
            const qreal fitZoomY = availableHeight / frameHeight;
            const qreal exportZoom = qMin(fitZoomX, fitZoomY);

            // Interpolate zoom
            const qreal targetZoom = m_savedCameraZoom + p * (exportZoom - m_savedCameraZoom);

            // Canvas center in world space
            const qreal canvasCenterX = displayFrame.x() + frameWidth * 0.5;
            const qreal canvasCenterY = displayFrame.y() + frameHeight * 0.5;

            // Offset camera to the right so canvas appears centered in the left portion.
            // The left area center is at (vpWidth - reservedPanelWidth) / 2.
            // The viewport center is at vpWidth / 2.
            // Screen offset = reservedPanelWidth / 2. World offset = screenOffset / zoom.
            const qreal cameraOffsetX = (reservedPanelWidth * 0.5) / targetZoom;

            // Interpolate position
            const qreal targetPosX
                = m_savedCameraPosX + p * (canvasCenterX + cameraOffsetX - m_savedCameraPosX);
            const qreal targetPosY = m_savedCameraPosY + p * (canvasCenterY - m_savedCameraPosY);

            m_canvasPanel->setCameraPosition(QPointF(targetPosX, targetPosY));
            m_canvasPanel->setCameraZoom(targetZoom);
        }
        m_canvasPanel->requestRender();
    }
}

void ExportModeController::updateLayout()
{
    if (!m_workspace) {
        return;
    }

    const int totalWidth = m_workspace->width();
    const int totalHeight = m_workspace->height();
    const int totalColumns = kCanvasAreaColumns + kPanelAreaColumns;
    const int canvasAreaWidth
        = qRound(totalWidth * (static_cast<qreal>(kCanvasAreaColumns) / totalColumns));
    const int panelAreaX = canvasAreaWidth;
    const int panelAreaWidth = qMax(0, totalWidth - panelAreaX);
    const int maxPanelBodyWidth = qMax(0, panelAreaWidth - 2 * kPanelMarginX);
    const int panelBodyWidth = qMin(exportPanelTargetWidth(), maxPanelBodyWidth);
    const int availablePanelHeight = qMax(0, totalHeight - 2 * kPanelMarginY);
    // The panel is as tall as its settings need, never taller than the window
    // allows. It used to be a fixed 3:4 box, which stopped being tenable once
    // the settings outgrew a single screenful — the content now scrolls inside
    // whatever height is left instead of being clipped by a ratio.
    const int panelHeight = qMin(availablePanelHeight, m_exportPanel->preferredHeight());
    const int visiblePanelX = panelAreaX + (panelAreaWidth - panelBodyWidth) / 2;
    const int panelY = kPanelMarginY + (availablePanelHeight - panelHeight) / 2;

    // When fully visible: centered in the right third.
    // When hidden: left edge at totalWidth (off-screen right).
    const int panelX = totalWidth - qRound((totalWidth - visiblePanelX) * m_progress);

    m_exportPanel->setGeometry(panelX, panelY, panelBodyWidth, panelHeight);
}

bool ExportModeController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_workspace && event->type() == QEvent::Resize) {
        if (m_progress > 0.01) {
            updateLayout();
            // Re-apply camera for new viewport size
            applyProgress(m_progress);
        }
    }
    return QObject::eventFilter(watched, event);
}

void ExportModeController::saveCameraState()
{
    if (!m_canvasPanel || !m_canvasPanel->isRenderContentReady()) {
        return;
    }
    m_canvasPanel->stopCameraAnimation();
    m_savedCameraZoom = m_canvasPanel->currentZoom();
    m_savedCameraRotation = m_canvasPanel->cameraRotationRadians();
    const QPointF pos = m_canvasPanel->cameraPosition();
    m_savedCameraPosX = pos.x();
    m_savedCameraPosY = pos.y();
    m_cameraStateSaved = true;
}

void ExportModeController::restoreCameraState()
{
    if (!m_cameraStateSaved || !m_canvasPanel || !m_canvasPanel->isRenderContentReady()) {
        return;
    }
    m_canvasPanel->refreshZoomLimits();
    m_canvasPanel->setCameraPosition(QPointF(m_savedCameraPosX, m_savedCameraPosY));
    m_canvasPanel->setCameraZoom(m_savedCameraZoom);
    m_canvasPanel->setCameraRotationRadians(m_savedCameraRotation);
    m_canvasPanel->setExportPreviewSuppressContentMirror(false);
    m_canvasPanel->requestRender();
    m_cameraStateSaved = false;
}

} // namespace ruwa::ui::workspace
