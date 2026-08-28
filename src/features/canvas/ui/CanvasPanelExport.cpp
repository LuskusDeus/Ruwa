// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   E X P O R T
// ==========================================================================

#include "CanvasPanel.h"

#include "features/canvas/engine/CanvasEngineSession.h"
#include "features/export/ExportAreaController.h"
#include "features/export/ExportModeController.h"
#include "features/export/ExportService.h"
#include "features/export/ExportSettingsPanel.h"
#include "features/export/ExportSettings.h"
#include "features/layers/model/LayerModel.h"
#include "features/selection/SelectionActionPopup.h"
#include "platform/Platform.h"
#include "shared/tiles/TileFormat.h"
#include "shared/utils/FileDialogMemory.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "shell/top-bar/MessagePopupManager.h"

#include <QCoreApplication>
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
#include <cstring>
#include <memory>
#include <utility>

namespace ruwa::ui::workspace {

namespace {

/// Wrap a captured CPU surface as a Qt image. The bytes are copied as-is and
/// the alpha semantics travel in the QImage format label — never re-derived.
QImage surfaceToQImage(const ruwa::shared::imaging::PixelSurface& surface)
{
    using ruwa::shared::imaging::PixelAlpha;
    using ruwa::shared::imaging::PixelStorage;

    if (surface.isNull()) {
        return QImage();
    }
    if (surface.storage() == PixelStorage::Float32) {
        // UI consumers want 8-bit images; precision reduction happens here,
        // once, on the way out.
        return surfaceToQImage(surface.convertedTo(PixelStorage::UInt8));
    }

    QImage image(surface.width(), surface.height(),
        surface.alphaMode() == PixelAlpha::Premultiplied ? QImage::Format_RGBA8888_Premultiplied
                                                         : QImage::Format_RGBA8888);
    if (image.isNull()) {
        return QImage();
    }
    for (int y = 0; y < surface.height(); ++y) {
        std::memcpy(image.scanLine(y), surface.scanLine(y),
            static_cast<size_t>(surface.width()) * 4);
    }
    return image;
}

} // anonymous namespace

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
    if (isRenderContentReady()) {
        auto content = inputEngineSession()->capture().navigatorContentBounds();
        if (content.isOk() && content.value()) {
            return *content.value();
        }
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
    if (isRenderContentReady()) {
        auto content = inputEngineSession()->capture().exportContentBounds();
        if (content.isOk() && content.value()) {
            return *content.value();
        }
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
    CanvasEngineSession* session = inputEngineSession();
    if (!session || session->status() != CanvasEngineStatus::Ready) {
        return QPixmap();
    }

    // The thumbnail captures what the engine presents: the whole transaction
    // (mirror suppression, cursor suppression, frame pump, readback, restore)
    // is the engine's atomic capture, not something the UI orchestrates.
    CanvasPresentedViewCaptureRequest request;
    request.suppressViewMirror = true;
    request.includeRenderedPointer = false;
    auto capture = session->capture().capturePresentedView(request);
    if (!capture.isOk()) {
        return QPixmap();
    }

    const QImage frame = surfaceToQImage(capture.value().surface);
    if (frame.isNull()) {
        return QPixmap();
    }

    // Project the document rect's corners through the captured presentation's
    // own view snapshot and crop to their bounding box — the region the canvas
    // occupied in exactly that frame.
    const QSizeF documentSize(m_canvasSize.width(), m_canvasSize.height());
    const QPointF corners[4] = {
        canvasSurfacePointFromDocument(capture.value().view, QPointF(0.0, 0.0), documentSize),
        canvasSurfacePointFromDocument(
            capture.value().view, QPointF(documentSize.width(), 0.0), documentSize),
        canvasSurfacePointFromDocument(
            capture.value().view, QPointF(documentSize.width(), documentSize.height()), documentSize),
        canvasSurfacePointFromDocument(
            capture.value().view, QPointF(0.0, documentSize.height()), documentSize),
    };

    const float left = static_cast<float>(
        std::round(std::min({ corners[0].x(), corners[1].x(), corners[2].x(), corners[3].x() })));
    const float right = static_cast<float>(
        std::round(std::max({ corners[0].x(), corners[1].x(), corners[2].x(), corners[3].x() })));
    const float top = static_cast<float>(
        std::round(std::min({ corners[0].y(), corners[1].y(), corners[2].y(), corners[3].y() })));
    const float bottom = static_cast<float>(
        std::round(std::max({ corners[0].y(), corners[1].y(), corners[2].y(), corners[3].y() })));

    const int x = qBound(0, static_cast<int>(left), frame.width() - 1);
    const int w = qBound(1, static_cast<int>(right - left), frame.width() - x);
    const int y = qBound(0, static_cast<int>(top), frame.height() - 1);
    const int h = qBound(1, static_cast<int>(bottom - top), frame.height() - y);

    if (w <= 0 || h <= 0)
        return QPixmap::fromImage(frame);

    QImage cropped = frame.copy(x, y, w, h);
    if (cropped.isNull())
        return QPixmap::fromImage(frame);

    QImage scaled = cropped;
    if (cropped.width() > maxSize || cropped.height() > maxSize) {
        scaled = cropped.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return QPixmap::fromImage(scaled);
}

QImage CanvasPanel::exportCanvasImage()
{
    CanvasEngineSession* session = inputEngineSession();
    if (!session || session->status() != CanvasEngineStatus::Ready) {
        return QImage();
    }

    CanvasDocumentCaptureRequest request;
    request.region = effectiveDisplayFrame();
    request.alphaMode = ruwa::shared::imaging::PixelAlpha::Straight;
    auto capture = session->capture().captureDocumentRegion(request);
    if (!capture.isOk()) {
        return QImage();
    }
    return surfaceToQImage(capture.value());
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
    CanvasEngineSession* session = inputEngineSession();
    if (!session || session->status() != CanvasEngineStatus::Ready) {
        return QImage();
    }

    CanvasDocumentCaptureRequest request;
    request.region = navigatorDisplayFrame();
    request.alphaMode = ruwa::shared::imaging::PixelAlpha::Straight;
    auto capture = session->capture().captureDocumentRegion(request);
    if (!capture.isOk()) {
        return QImage();
    }

    QImage full = surfaceToQImage(capture.value());
    if (full.isNull())
        return QImage();
    if (full.width() <= maxSize && full.height() <= maxSize) {
        return full;
    }
    return full.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage CanvasPanel::getCanvasRegionThumbnail(const QRect& worldRect, const QSize& targetSize) const
{
    CanvasEngineSession* session = inputEngineSession();
    if (!session || session->status() != CanvasEngineStatus::Ready) {
        return QImage();
    }

    const QRect normalizedRect = worldRect.normalized();
    if (!normalizedRect.isValid() || normalizedRect.isEmpty() || !targetSize.isValid()) {
        return QImage();
    }

    CanvasDocumentCaptureRequest request;
    request.region = normalizedRect;
    request.alphaMode = ruwa::shared::imaging::PixelAlpha::Straight;
    auto capture = session->capture().captureDocumentRegion(request);
    if (!capture.isOk()) {
        return QImage();
    }

    QImage image = surfaceToQImage(capture.value());
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
    CanvasEngineSession* session = inputEngineSession();
    if (!session || session->status() != CanvasEngineStatus::Ready) {
        return QImage();
    }

    CanvasResampledCaptureRequest request;
    request.region = worldRect;
    request.outputSize = targetSize;
    auto render = session->capture().renderDocumentRegion(request);
    if (!render.isOk()) {
        return QImage();
    }
    return surfaceToQImage(render.value());
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
    CanvasEngineSession* session = inputEngineSession();
    if (!m_exportPanel || !session || session->status() != CanvasEngineStatus::Ready) {
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

    CanvasResampledCaptureRequest request;
    request.region = frame;
    request.outputSize = target;
    auto render = session->capture().renderDocumentRegion(request);
    if (render.isOk()) {
        const QImage sample = surfaceToQImage(render.value());
        if (!sample.isNull()) {
            m_exportPanel->setExportContentSample(sample);
        }
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

    if (!isRenderContentReady()) {
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

    CanvasDocumentCaptureRequest request;
    request.region = frame;
    request.includeCanvasBackground = settings.includeCanvasBackground;
    request.highPrecision = wants16Bit || deepDocument;
    // The export pipeline resamples before it converts, and resampling is
    // only correct on premultiplied data — hand it the readback as-is and let
    // it convert exactly once, at the end.
    request.alphaMode = ruwa::shared::imaging::PixelAlpha::Premultiplied;

    auto capture = inputEngineSession()->capture().captureDocumentRegion(request);
    if (!capture.isOk()) {
        if (capture.error().code == CanvasErrorCode::OutOfMemory) {
            return reject(tr("Not enough memory to capture a %1 x %2 px image.")
                    .arg(frame.width())
                    .arg(frame.height()));
        }
        return reject(tr("The export area could not be captured."));
    }

    QString error;
    if (!exportService()->start(capture.takeValue(), settings, &error)) {
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
    if (CanvasEngineSession* session = inputEngineSession()) {
        session->view().setExportPreviewHideBoardLayers(progress > 1e-5);
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
    if (CanvasEngineSession* session = inputEngineSession()) {
        session->view().setExportPreviewSuppressContentMirror(suppress);
    }
}

} // namespace ruwa::ui::workspace
