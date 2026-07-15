// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   E X P O R T
// ==========================================================================

#include "CanvasPanel.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/export/ExportAreaController.h"
#include "features/export/ExportModeController.h"
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

#include <algorithm>
#include <cmath>
#include <memory>

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

QRect CanvasPanel::composerDisplayFrame() const
{
    if (hasFiniteDocumentBounds()) {
        return QRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
    }

    QRect bounds;
    if (m_glWidget && m_glWidget->isInitialized()
        && m_glWidget->computeComposerContentBounds(bounds)) {
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
    const QRect frame = composerDisplayFrame();
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

QImage CanvasPanel::renderComposerOverviewTile(
    const QRect& worldRect, const QSize& targetSize) const
{
    if (!m_glWidget || !m_glWidget->isInitialized()) {
        return QImage();
    }
    return m_glWidget->renderCompositedRegion(worldRect, targetSize);
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
