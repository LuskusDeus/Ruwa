// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   E X P O R T
// ==========================================================================

#include "CanvasPanel.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/export/ExportAreaController.h"
#include "features/export/ExportModeController.h"
#include "features/export/ExportService.h"
#include "features/export/ExportSettingsPanel.h"
#include "features/export/ExportSettings.h"
#include "features/layers/model/LayerModel.h"
#include "features/selection/SelectionActionPopup.h"
#include "platform/Platform.h"
#include "shared/utils/FileDialogMemory.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "shell/top-bar/MessagePopupManager.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QImageWriter>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace ruwa::ui::workspace {

QRect CanvasPanel::effectiveDisplayFrame() const
{
    if (!hasFiniteDocumentBounds()) {
        if (m_infiniteExportFrameUserDefined && hasExportFrame()) {
            return m_exportFrame;
        }
        return computedAutoExportFrame();
    }
    return hasExportFrame() ? m_exportFrame
                            : QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
}

QRect CanvasPanel::navigatorDisplayFrame() const
{
    if (hasFiniteDocumentBounds()) {
        return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
    }

    QRect bounds;
    if (m_glWidget && m_glWidget->isInitialized()
        && m_glWidget->computeNavigatorContentBounds(bounds)) {
        return bounds;
    }

    if (hasExportFrame()) {
        return m_exportFrame;
    }

    return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
}

QRect CanvasPanel::exportPreviewCameraFrame() const
{
    if (!hasFiniteDocumentBounds()) {
        return computedAutoExportFrame();
    }
    return documentBoundsRect();
}

QRect CanvasPanel::normalizedExportFrame(const QRect& frame) const
{
    if (!hasFiniteDocumentBounds()) {
        const QRect normalized = frame.normalized();
        if (normalized.width() > 0 && normalized.height() > 0) {
            return normalized;
        }
        return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
    }

    if (frame.width() > 0 && frame.height() > 0) {
        const int canvasWidth = qMax(1, m_canvasSize.width());
        const int canvasHeight = qMax(1, m_canvasSize.height());
        const QRect normalized = frame.normalized();
        const int x = std::clamp(normalized.x(), 0, canvasWidth - 1);
        const int y = std::clamp(normalized.y(), 0, canvasHeight - 1);
        const int width = std::clamp(normalized.width(), 1, canvasWidth - x);
        const int height = std::clamp(normalized.height(), 1, canvasHeight - y);
        return QRect(x, y, width, height);
    }
    return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
}

void CanvasPanel::setExportFrame(const QRect& frame)
{
    const QRect normalizedFrame = normalizedExportFrame(frame);
    if (m_exportFrame == normalizedFrame) {
        return;
    }

    if (!hasFiniteDocumentBounds()) {
        m_infiniteExportFrameUserDefined = true;
    }
    m_exportFrame = normalizedFrame;
    if (m_exportAreaController) {
        m_exportAreaController->setExportFrame(m_exportFrame);
    }
    applyZoomLimits();
    publishEffectiveExportFrameIfChanged();
    requestRender();
}

QRect CanvasPanel::defaultExportFrame() const
{
    if (hasFiniteDocumentBounds()) {
        return documentBoundsRect();
    }
    return computedAutoExportFrame();
}

void CanvasPanel::resetExportFrameToDefault()
{
    if (!hasFiniteDocumentBounds()) {
        // Hands the frame back to the content-bounds tracker, which the first
        // user drag had switched off.
        m_infiniteExportFrameUserDefined = false;
        syncInfiniteExportFrameToContent(true);
        applyZoomLimits();
        publishEffectiveExportFrameIfChanged();
        requestRender();
        return;
    }
    setExportFrame(documentBoundsRect());
}

void CanvasPanel::resizeExportFrame(const QSize& size)
{
    if (size.width() <= 0 || size.height() <= 0) {
        return;
    }

    // Anchored at the top-left corner: growing the frame from a numeric field
    // should extend it down and to the right, the way a crop box behaves, not
    // creep the origin around behind the user.
    const QRect current = effectiveDisplayFrame();
    setExportFrame(QRect(current.topLeft(), size));
}

QRect CanvasPanel::computedAutoExportFrame() const
{
    QRect bounds;
    if (m_glWidget && m_glWidget->isInitialized()
        && m_glWidget->computeExportContentBounds(bounds)) {
        return bounds;
    }

    if (hasExportFrame()) {
        return m_exportFrame;
    }
    return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
}

void CanvasPanel::syncInfiniteExportFrameToContent(bool forceReset)
{
    if (hasFiniteDocumentBounds()) {
        return;
    }
    if (m_infiniteExportFrameUserDefined && !forceReset) {
        return;
    }

    const QRect autoFrame = computedAutoExportFrame();
    if (!autoFrame.isValid() || autoFrame.isEmpty()) {
        return;
    }

    m_exportFrame = autoFrame;
    if (m_exportAreaController) {
        m_exportAreaController->setExportFrame(m_exportFrame);
    }
}

void CanvasPanel::publishEffectiveExportFrameIfChanged()
{
    if (!hasFiniteDocumentBounds()) {
        syncInfiniteExportFrameToContent();
    }
    const QRect frame = effectiveDisplayFrame();
    if (m_lastPublishedEffectiveExportFrame == frame) {
        return;
    }
    m_lastPublishedEffectiveExportFrame = frame;
    emit exportFrameChanged(frame);
}

QPixmap CanvasPanel::grabCanvasThumbnail(int maxSize) const
{
    if (!m_glWidget || !m_glWidget->isInitialized())
        return QPixmap();

    auto* glNonConst = const_cast<aether::OpenGLCanvasWidget*>(m_glWidget);
    const bool prevFlipH = m_glWidget->canvasContentFlipHorizontal();
    const bool prevFlipV = m_glWidget->canvasContentFlipVertical();
    glNonConst->setCanvasContentFlipHorizontal(false);
    glNonConst->setCanvasContentFlipVertical(false);

    m_glWidget->setSkipCursorOverlays(true);
    m_glWidget->update();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const QImage image = m_glWidget->grabFramebuffer();
    if (image.isNull()) {
        glNonConst->setCanvasContentFlipHorizontal(prevFlipH);
        glNonConst->setCanvasContentFlipVertical(prevFlipV);
        m_glWidget->setSkipCursorOverlays(false);
        m_glWidget->update();
        return QPixmap();
    }

    const auto& viewport = m_glWidget->viewport();
    const auto& canvas = m_glWidget->canvas();
    const float cw = static_cast<float>(canvas.width());
    const float ch = static_cast<float>(canvas.height());

    const aether::Vector2 p0 = viewport.worldToScreen({ 0.0f, 0.0f });
    const aether::Vector2 p1 = viewport.worldToScreen({ cw, 0.0f });
    const aether::Vector2 p2 = viewport.worldToScreen({ cw, ch });
    const aether::Vector2 p3 = viewport.worldToScreen({ 0.0f, ch });

    const float left = std::round(std::min({ p0.x, p1.x, p2.x, p3.x }));
    const float right = std::round(std::max({ p0.x, p1.x, p2.x, p3.x }));
    const float top = std::round(std::min({ p0.y, p1.y, p2.y, p3.y }));
    const float bottom = std::round(std::max({ p0.y, p1.y, p2.y, p3.y }));

    const int x = qBound(0, static_cast<int>(left), image.width() - 1);
    const int w = qBound(1, static_cast<int>(right - left), image.width() - x);
    const int y = qBound(0, static_cast<int>(top), image.height() - 1);
    const int h = qBound(1, static_cast<int>(bottom - top), image.height() - y);

    glNonConst->setCanvasContentFlipHorizontal(prevFlipH);
    glNonConst->setCanvasContentFlipVertical(prevFlipV);
    m_glWidget->setSkipCursorOverlays(false);
    m_glWidget->update();

    if (w <= 0 || h <= 0)
        return QPixmap::fromImage(image);

    QImage cropped = image.copy(x, y, w, h);
    if (cropped.isNull())
        return QPixmap::fromImage(image);

    QImage scaled = cropped;
    if (cropped.width() > maxSize || cropped.height() > maxSize) {
        scaled = cropped.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return QPixmap::fromImage(scaled);
}

QImage CanvasPanel::exportCanvasImage()
{
    if (!m_glWidget || !m_glWidget->isInitialized()) {
        return QImage();
    }
    const QRect frame = effectiveDisplayFrame();
    return m_glWidget->grabCanvasImage(frame);
}

bool CanvasPanel::fastExportPng(const QString& suggestedBaseName)
{
    const QImage image = exportCanvasImage();
    if (image.isNull()) {
        return false;
    }

    QString suggested = suggestedBaseName.trimmed();
    if (suggested.isEmpty()) {
        suggested = tr("Untitled");
    }
    if (!suggested.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        suggested += QStringLiteral(".png");
    }

    QString path = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kCanvasExport, tr("Fast Export as PNG"), suggested,
        tr("PNG Image (*.png)"));
    if (path.isEmpty()) {
        return false;
    }

    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }

    QImageWriter writer(path);
    if (!writer.write(image)) {
        return false;
    }
    return true;
}

bool CanvasPanel::copyCanvasToClipboard()
{
    const QImage image = exportCanvasImage();
    if (image.isNull()) {
        return false;
    }

    constexpr int max8K = 7680;
    if (image.width() > max8K || image.height() > max8K) {
        const QString errMsg = QCoreApplication::translate("MessagePopupManager",
            "Image resolution exceeds 8K (7680x4320). Maximum dimension: 7680 px.");
        ruwa::ui::widgets::MessagePopupManager::show(this, errMsg,
            { { QCoreApplication::translate("MessagePopupManager", "OK"), false, []() { } } }, 320);
        return false;
    }

    std::unique_ptr<ruwa::platform::Platform> platform(ruwa::platform::Platform::create());
    if (!platform) {
        return false;
    }

    platform->copyImageToClipboard(image);
    ruwa::ui::widgets::MessagePopupManager::showImageCopied(this, image);
    return true;
}

QImage CanvasPanel::getFullCanvasThumbnail(int maxSize) const
{
    if (!m_glWidget || !m_glWidget->isInitialized()) {
        return QImage();
    }
    const QRect frame = navigatorDisplayFrame();
    QImage full = m_glWidget->grabCanvasImage(frame);
    if (full.isNull())
        return QImage();
    if (full.width() <= maxSize && full.height() <= maxSize) {
        return full;
    }
    return full.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage CanvasPanel::getCanvasRegionThumbnail(const QRect& worldRect, const QSize& targetSize) const
{
    if (!m_glWidget || !m_glWidget->isInitialized()) {
        return QImage();
    }

    const QRect normalizedRect = worldRect.normalized();
    if (!normalizedRect.isValid() || normalizedRect.isEmpty() || !targetSize.isValid()) {
        return QImage();
    }

    QImage image = m_glWidget->grabCanvasImage(normalizedRect);
    if (image.isNull()) {
        return QImage();
    }
    if (image.size() == targetSize) {
        return image;
    }
    return image.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage CanvasPanel::renderNavigatorOverviewTile(
    const QRect& worldRect, const QSize& targetSize) const
{
    if (!m_glWidget || !m_glWidget->isInitialized()) {
        return QImage();
    }
    return m_glWidget->renderCompositedRegion(worldRect, targetSize);
}

namespace {

/// Format a byte count the way a file manager would.
QString formatByteSize(qint64 bytes)
{
    if (bytes < 1000) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1000 * 1000) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1000.0, 'f', 1));
    }
    if (bytes < 1000LL * 1000LL * 1000LL) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / 1000000.0, 'f', 1));
    }
    return QStringLiteral("%1 GB").arg(QString::number(bytes / 1000000000.0, 'f', 1));
}

} // anonymous namespace

ruwa::core::exporting::ExportService* CanvasPanel::exportService()
{
    if (!m_exportService) {
        m_exportService = new ruwa::core::exporting::ExportService(this);

        connect(m_exportService, &ruwa::core::exporting::ExportService::started, this, [this]() {
            if (m_exportPanel) {
                m_exportPanel->setExportInProgress(true);
            }
        });

        connect(m_exportService, &ruwa::core::exporting::ExportService::finished, this,
            [this](const ruwa::core::exporting::ExportResult& result) {
                if (m_exportPanel) {
                    m_exportPanel->setExportInProgress(false);
                }
                if (result.cancelled) {
                    return;
                }

                if (!result.ok) {
                    ruwa::ui::widgets::MessagePopupManager::show(this,
                        tr("Export failed: %1").arg(result.errorText),
                        { { tr("OK"), false, []() { } } }, 380);
                    return;
                }

                QString message = tr("Exported %1 (%2)")
                                      .arg(QFileInfo(result.path).fileName(),
                                          formatByteSize(result.fileSizeBytes));
                if (!result.warnings.isEmpty()) {
                    message += QStringLiteral("\n") + result.warnings.join(QStringLiteral("\n"));
                }
                ruwa::ui::widgets::MessagePopupManager::show(
                    this, message, { { tr("OK"), false, []() { } } }, 380);
            });
    }
    return m_exportService;
}

void CanvasPanel::refreshExportPanelSample()
{
    if (!m_exportPanel || !m_glWidget || !m_glWidget->isInitialized()) {
        return;
    }
    const QRect frame = effectiveDisplayFrame();
    if (!frame.isValid() || frame.isEmpty()) {
        return;
    }

    // Small on purpose: the panel's size estimate trial-encodes this sample
    // and scales by pixel count, so the sample carries all the information
    // that matters while keeping the GPU round trip and the encode cheap
    // enough to run on every debounced frame change. 1024 per side is the
    // sweet spot: minifying harder than that smooths out the brush-level
    // detail deflate bills the user for, and the PNG estimate sags.
    constexpr int kSampleMaxSide = 1024;
    QSize target = frame.size().scaled(kSampleMaxSide, kSampleMaxSide, Qt::KeepAspectRatio);
    target.setWidth(qMax(1, target.width()));
    target.setHeight(qMax(1, target.height()));

    const QImage sample = m_glWidget->renderCompositedRegion(frame, target);
    if (!sample.isNull()) {
        m_exportPanel->setExportContentSample(sample);
    }
}

bool CanvasPanel::startExport(ruwa::core::exporting::ExportSettings& settings)
{
    namespace exporting = ruwa::core::exporting;

    const auto reject = [this](const QString& message) {
        ruwa::ui::widgets::MessagePopupManager::show(
            this, message, { { tr("OK"), false, []() { } } }, 380);
        return false;
    };

    if (!m_glWidget || !m_glWidget->isInitialized()) {
        return reject(tr("The canvas is not ready to export yet."));
    }

    const QRect frame = effectiveDisplayFrame();
    if (!frame.isValid() || frame.isEmpty()) {
        return reject(tr("The export area is empty."));
    }
    if (settings.outputSize.isEmpty()) {
        settings.outputSize = frame.size();
    }

    // The destination is a field in the panel now, not a save dialog, so the
    // overwrite question is ours to ask — nothing else asks it any more. Asked
    // before the capture, because capturing a large frame is expensive and
    // discarding it on "no" would be work nobody requested.
    const QString targetPath = settings.absolutePath();
    if (!targetPath.isEmpty() && QFileInfo::exists(targetPath)) {
        const bool overwrite = ruwa::ui::widgets::MessagePopupManager::showBlocking(this,
            tr("\"%1\" already exists. Replace it?").arg(QFileInfo(targetPath).fileName()),
            tr("Replace"), tr("Cancel"), 380);
        if (!overwrite) {
            return false;
        }
    }

    // Capture in float only when something downstream can actually carry the
    // extra bits: a 16-bit file, or a document whose tiles are deeper than 8
    // bits (where an 8-bit readback would quantize before any resampling).
    // An 8-bit document written to an 8-bit file has nothing more to give, and
    // float would cost four times the memory for an identical result.
    const bool deepDocument
        = m_layerModel && m_layerModel->documentTileFormat() != aether::TilePixelFormat::RGBA8;
    const bool wants16Bit = settings.bitDepth == exporting::ExportBitDepth::Bit16
        && exporting::formatCapabilities(settings.format).supports16Bit;

    aether::OpenGLCanvasWidget::CanvasCaptureOptions capture;
    capture.includeCanvasBackground = settings.includeCanvasBackground;
    capture.highPrecision = wants16Bit || deepDocument;

    ruwa::shared::imaging::PixelSurface surface = m_glWidget->captureCanvasSurface(frame, capture);
    if (surface.isNull()) {
        return reject(tr("Not enough memory to capture a %1 x %2 px image.")
                .arg(frame.width())
                .arg(frame.height()));
    }

    QString error;
    if (!exportService()->start(std::move(surface), settings, &error)) {
        if (m_exportPanel) {
            m_exportPanel->setExportInProgress(false);
        }
        return reject(error);
    }
    return true;
}

void CanvasPanel::setExportBaseName(const QString& baseName)
{
    if (m_exportPanel) {
        m_exportPanel->setSuggestedBaseName(baseName);
    }
}

void CanvasPanel::toggleExportMode()
{
    if (m_exportController) {
        m_exportController->toggle();
    }
}

bool CanvasPanel::isExportMode() const
{
    return m_exportController && m_exportController->isExportMode();
}

void CanvasPanel::setExportModeOverlayProgress(qreal progress)
{
    m_exportModeOverlayProgress = progress;
    const qreal overlayOpacity = 1.0 - progress;
    if (m_glWidget) {
        m_glWidget->setExportPreviewHideBoardLayers(progress > 1e-5);
    }

    if (m_brushOverlayOpacity) {
        m_brushOverlayOpacity->setOpacity(overlayOpacity);
    }
    if (m_stylusJoystickOpacity) {
        m_stylusJoystickOpacity->setOpacity(overlayOpacity);
    }
    if (m_toolStateOverlayOpacity) {
        m_toolStateOverlayOpacity->setOpacity(overlayOpacity);
    }
    if (m_confirmationPopup) {
        m_confirmationPopup->setVisible(progress < 0.5);
    }
    if (m_selectionActionPopup) {
        m_selectionActionPopup->setVisible(progress < 0.5);
    }
}

void CanvasPanel::setExportPreviewSuppressContentMirror(bool suppress)
{
    if (m_glWidget) {
        m_glWidget->setExportPreviewSuppressContentMirror(suppress);
    }
}

} // namespace ruwa::ui::workspace
