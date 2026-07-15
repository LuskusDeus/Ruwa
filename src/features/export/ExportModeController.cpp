// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   M O D E   C O N T R O L L E R
// ==========================================================================

#include "ExportModeController.h"
#include "ExportSettingsPanel.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/canvas/scene/Canvas.h"
#include "features/canvas/scene/Viewport.h"
#include "features/canvas/scene/Camera2D.h"

#include <QVariantAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QWidget>

#include <cmath>

namespace ruwa::ui::workspace {

namespace {
constexpr int kAnimationDurationMs = 400;
constexpr qreal kExportPanelWidthRatio = 0.28; // 28% of host widget width
constexpr int kMinExportPanelWidth = 280;
constexpr int kMaxExportPanelWidth = 450;
constexpr int kExportPanelAspectWidth = 3;
constexpr int kExportPanelAspectHeight = 4;
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

    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(kAnimationDurationMs);
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
    m_animation->setDuration(qRound(kAnimationDurationMs * distance));

    m_animation->setStartValue(startVal);
    m_animation->setEndValue(endVal);

    if (entering) {
        m_exportPanel->setVisible(true);
        m_exportPanel->raise();
        if (m_canvasPanel) {
            m_canvasPanel->setInteractionEnabled(false);
        }
    }

    m_animation->start();
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
    if (m_canvasPanel && m_canvasPanel->isGLContentReady() && m_cameraStateSaved) {
        auto& vp = m_canvasPanel->viewport();
        auto& cam = vp.camera();
        const QRect displayFrame = m_canvasPanel->exportPreviewCameraFrame();
        const auto vpSize = vp.size();
        cam.setZoomLimits(0.001f, cam.maxZoom());

        const float p = static_cast<float>(progress);

        const float rotDelta = shortestAngleDeltaRadians(m_savedCameraRotation, 0.0f);
        cam.setRotation(m_savedCameraRotation + p * rotDelta);
        cam.stopAnimation();

        // Export layout reserves the right third for the settings panel and
        // centers the canvas preview in the left two thirds.
        const float columns = static_cast<float>(kCanvasAreaColumns + kPanelAreaColumns);
        const float panelAreaWidth = vpSize.x * (static_cast<float>(kPanelAreaColumns) / columns);
        const float canvasAreaWidth = vpSize.x - panelAreaWidth;
        const float reservedPanelWidth = panelAreaWidth * p;
        const float availableWidth = canvasAreaWidth - 2.0f * kExportZoomPadding;
        const float availableHeight = vpSize.y - 2.0f * kExportZoomPadding;

        if (availableWidth > 50.0f && availableHeight > 50.0f) {
            const float frameWidth = std::max(1.0f, static_cast<float>(displayFrame.width()));
            const float frameHeight = std::max(1.0f, static_cast<float>(displayFrame.height()));
            const float fitZoomX = availableWidth / frameWidth;
            const float fitZoomY = availableHeight / frameHeight;
            const float exportZoom = qMin(fitZoomX, fitZoomY);

            // Interpolate zoom
            const float targetZoom = m_savedCameraZoom + p * (exportZoom - m_savedCameraZoom);

            // Canvas center in world space
            const float canvasCenterX = static_cast<float>(displayFrame.x()) + frameWidth * 0.5f;
            const float canvasCenterY = static_cast<float>(displayFrame.y()) + frameHeight * 0.5f;

            // Offset camera to the right so canvas appears centered in the left portion.
            // The left area center is at (vpSize.x - reservedPanelWidth) / 2.
            // The viewport center is at vpSize.x / 2.
            // Screen offset = reservedPanelWidth / 2. World offset = screenOffset / zoom.
            const float cameraOffsetX = (reservedPanelWidth * 0.5f) / targetZoom;

            // Interpolate position
            const float targetPosX
                = m_savedCameraPosX + p * (canvasCenterX + cameraOffsetX - m_savedCameraPosX);
            const float targetPosY = m_savedCameraPosY + p * (canvasCenterY - m_savedCameraPosY);

            cam.setPosition(targetPosX, targetPosY);
            cam.setZoom(targetZoom);
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
    const int targetPanelHeight = qRound(
        panelBodyWidth * (static_cast<qreal>(kExportPanelAspectHeight) / kExportPanelAspectWidth));
    const int panelHeight = qMin(availablePanelHeight, targetPanelHeight);
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
    if (!m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
        return;
    }
    auto& cam = m_canvasPanel->viewport().camera();
    cam.stopAnimation();
    m_savedCameraZoom = cam.zoom();
    m_savedCameraRotation = cam.rotation();
    auto pos = cam.position();
    m_savedCameraPosX = pos.x;
    m_savedCameraPosY = pos.y;
    m_cameraStateSaved = true;
}

void ExportModeController::restoreCameraState()
{
    if (!m_cameraStateSaved || !m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
        return;
    }
    m_canvasPanel->refreshZoomLimits();
    auto& cam = m_canvasPanel->viewport().camera();
    cam.setPosition(m_savedCameraPosX, m_savedCameraPosY);
    cam.setZoom(m_savedCameraZoom);
    cam.setRotation(m_savedCameraRotation);
    m_canvasPanel->setExportPreviewSuppressContentMirror(false);
    m_canvasPanel->requestRender();
    m_cameraStateSaved = false;
}

} // namespace ruwa::ui::workspace
