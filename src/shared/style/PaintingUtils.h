// SPDX-License-Identifier: MPL-2.0

// PaintingUtils.h - Shared QPainter helpers to eliminate cross-widget duplication
#ifndef RUWA_UI_CORE_STYLE_PAINTINGUTILS_H
#define RUWA_UI_CORE_STYLE_PAINTINGUTILS_H

#include <QColor>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRadialGradient>
#include <QRectF>
#include <QRgba64>
#include <QWidget>
#include <QtMath>
#include <cmath>

#include "shared/rendering/CanvasBackdropSource.h"

namespace ruwa::ui::painting {

/// Alpha of the theme surface tint painted over canvas backdrop blur.
inline constexpr int kBackdropTintAlpha = 112;

namespace detail {
inline constexpr int kBayer4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

inline QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(qBound(0, alpha, 255));
    return color;
}
} // namespace detail

inline QPixmap tintedPixmap(const QPixmap& source, const QColor& color)
{
    if (source.isNull()) {
        return source;
    }

    QPixmap result(source.size());
    result.setDevicePixelRatio(source.devicePixelRatio());
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    return result;
}

/// Draw a 1px cosmetic vertical-gradient border inside @p outerRect.
/// The rect is inset by 0.5px and the rounded-rect radius reduced by 0.5
/// so the stroke lands on pixel centers.
inline void drawGradientBorder(QPainter& painter, const QRectF& outerRect, qreal radius,
    const QColor& topColor, const QColor& bottomColor, qreal penWidth = 1.0)
{
    const QRectF borderRect = outerRect.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal innerRadius = qMax(0.0, radius - 0.5);

    QLinearGradient grad(borderRect.topLeft(), borderRect.bottomLeft());
    grad.setColorAt(0.0, topColor);
    grad.setColorAt(1.0, bottomColor);

    QPainterPath path;
    path.addRoundedRect(borderRect, innerRadius, innerRadius);

    QPen pen;
    pen.setBrush(grad);
    pen.setWidthF(penWidth);
    pen.setCosmetic(true);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

// ----------------------------------------------------------------------------
// Attached popup shape — a panel that hangs from the TopBar seam: a flat top
// edge spanning the full body width that flares inward, with rounded bottom
// corners and a seam-anchored soft shadow. Shared by MessagePopup (centered
// confirmation banner) and MenuPopup (File/Edit/View/Help dropdown) so both read
// as the same "grows out of the topbar" surface.
// ----------------------------------------------------------------------------

/// Bottom corner radius of the attached popup body (logical px, scale at call site).
inline constexpr int kAttachedCornerRadius = 8;
/// Top-edge flare radius (how far the flat top insets to the body sides).
inline constexpr int kAttachedOuterCornerRadiusBase = 10;
/// Soft-shadow extent below the body.
inline constexpr int kAttachedShadowExtentBase = 18;
/// Soft-shadow extent to each side of the body.
inline constexpr int kAttachedShadowSideExtentBase = 14;

/// Fill/outline silhouette of an attached popup. @p sideInset is the top flare
/// radius; @p radius is the bottom corner radius.
inline QPainterPath attachedPopupPath(const QRectF& rect, qreal sideInset, qreal radius)
{
    const qreal topR = qMin(sideInset, rect.height() / 2.0);
    const qreal bottomR = qMin(radius, qMin(rect.width(), rect.height()) / 2.0);
    const qreal left = rect.left() + sideInset;
    const qreal right = rect.right() - sideInset;

    QPainterPath path;
    path.moveTo(rect.topLeft());
    path.lineTo(rect.topRight());
    path.quadTo(QPointF(right, rect.top()), QPointF(right, rect.top() + topR));
    path.lineTo(right, rect.bottom() - bottomR);
    path.quadTo(QPointF(right, rect.bottom()), QPointF(right - bottomR, rect.bottom()));
    path.lineTo(left + bottomR, rect.bottom());
    path.quadTo(QPointF(left, rect.bottom()), QPointF(left, rect.bottom() - bottomR));
    path.lineTo(left, rect.top() + topR);
    path.quadTo(QPointF(left, rect.top()), rect.topLeft());
    path.closeSubpath();
    return path;
}

/// Open border stroke of an attached popup (top edge omitted — it merges with the
/// TopBar seam). @p sideInset is the top flare radius; @p radius the bottom corner.
inline QPainterPath attachedPopupBorderPath(const QRectF& rect, qreal sideInset, qreal radius)
{
    const qreal topR = qMin(sideInset, rect.height() / 2.0);
    const qreal bottomR = qMin(radius, qMin(rect.width(), rect.height()) / 2.0);
    const qreal left = rect.left() + sideInset;
    const qreal right = rect.right() - sideInset;

    QPainterPath path;
    path.moveTo(rect.left(), rect.top());
    path.quadTo(QPointF(left, rect.top()), QPointF(left, rect.top() + topR));
    path.lineTo(left, rect.bottom() - bottomR);
    path.quadTo(QPointF(left, rect.bottom()), QPointF(left + bottomR, rect.bottom()));
    path.lineTo(right - bottomR, rect.bottom());
    path.quadTo(QPointF(right, rect.bottom()), QPointF(right, rect.bottom() - bottomR));
    path.lineTo(right, rect.top() + topR);
    path.quadTo(QPointF(right, rect.top()), QPointF(rect.right(), rect.top()));
    return path;
}

/// Soft shadow for an attached popup. @p bodyRect MUST be the actually-painted body
/// rect (already inset to the visible outline), otherwise the falloff starts away
/// from the popup edge and leaves a visible gap. The shadow fades in from the
/// TopBar seam (top) to full strength by the body bottom.
inline void drawAttachedPopupShadow(QPainter& painter, const QRectF& bodyRect, int sideExtent,
    int bottomExtent, const QColor& shadowColor, bool darkTheme)
{
    if (sideExtent <= 0 || bottomExtent <= 0 || !bodyRect.isValid()) {
        return;
    }

    const QRect shadowBounds
        = bodyRect.adjusted(-sideExtent, 0.0, sideExtent, bottomExtent).toAlignedRect();
    if (shadowBounds.isEmpty()) {
        return;
    }

    const qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : qreal(1);
    const QSize imageSize(
        qMax(1, qCeil(shadowBounds.width() * dpr)), qMax(1, qCeil(shadowBounds.height() * dpr)));

    QImage shadowImage(imageSize, QImage::Format_ARGB32_Premultiplied);
    shadowImage.setDevicePixelRatio(dpr);
    shadowImage.fill(Qt::transparent);

    const QRectF localBody = bodyRect.translated(-shadowBounds.topLeft());
    const qreal maxAlpha = darkTheme ? 105.0 : 64.0;
    const qreal sideSoftness = qMax(1.0, qreal(sideExtent));
    const qreal bottomSoftness = qMax(1.0, qreal(bottomExtent));
    const qreal bodyBottom = localBody.bottom();

    const QColor base = shadowColor;
    for (int y = 0; y < imageSize.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(shadowImage.scanLine(y));
        const qreal ly = (y + 0.5) / dpr;

        qreal vFade = qBound(0.0, ly / qMax(1.0, bodyBottom), 1.0);
        vFade = vFade * vFade * (3.0 - 2.0 * vFade);

        const qreal oy = qMax(0.0, ly - bodyBottom);
        const qreal ny = oy / bottomSoftness;

        for (int x = 0; x < imageSize.width(); ++x) {
            const qreal lx = (x + 0.5) / dpr;

            const qreal ox = qMax(0.0, qMax(localBody.left() - lx, lx - localBody.right()));
            const qreal nx = ox / sideSoftness;

            const qreal dist = qBound(0.0, std::sqrt(nx * nx + ny * ny), 1.0);
            const qreal falloff = 1.0 - dist * dist * (3.0 - 2.0 * dist);

            const int alpha = qBound(0, qRound(maxAlpha * vFade * falloff), 255);
            if (alpha <= 0) {
                continue;
            }
            row[x] = qPremultiply(qRgba(base.red(), base.green(), base.blue(), alpha));
        }
    }

    painter.drawImage(shadowBounds.topLeft(), shadowImage);
}

/// Paints the theme tint over a backdrop blur already composited by the canvas
/// renderer. Returns false until the first blurred frame is available.
inline bool drawBackdropBlurTint(QPainter& painter, QWidget* widget,
    ruwa::shared::rendering::ICanvasBackdropSource* source, const QPainterPath& clipPath,
    const QColor& tint)
{
    if (!widget || !source || !source->backdropAvailable()) {
        return false;
    }
    painter.save();
    painter.setClipPath(clipPath);
    painter.fillRect(widget->rect(), tint);
    painter.restore();
    return true;
}

inline QPixmap blurSnapshotPixmap(const QPixmap& source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }

    const qreal dpr = source.devicePixelRatio();
    const QSize logicalSize(
        qMax(1, qRound(source.width() / dpr)), qMax(1, qRound(source.height() / dpr)));
    constexpr int downsample = 2;
    const QSize smallLogical(
        qMax(1, logicalSize.width() / downsample), qMax(1, logicalSize.height() / downsample));
    const QSize smallDevice(smallLogical.width() * dpr, smallLogical.height() * dpr);

    QImage downscaled = source.toImage()
                            .convertToFormat(QImage::Format_ARGB32_Premultiplied)
                            .scaled(smallDevice, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    downscaled.setDevicePixelRatio(1.0);

    const qreal effectiveRadius = qMax(1.0, qreal(radius) / downsample);
    const int pad = qBound(2, qCeil(effectiveRadius * 2.0), 64);
    const QSize paddedSize(smallDevice.width() + pad * 2, smallDevice.height() + pad * 2);

    QImage padded(paddedSize, QImage::Format_ARGB32_Premultiplied);
    padded.fill(Qt::transparent);
    {
        QPainter p(&padded);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect(QPoint(0, 0), paddedSize), downscaled);
        p.drawImage(QPoint(pad, pad), downscaled);
    }

    QGraphicsScene scene;
    auto* item = new QGraphicsPixmapItem(QPixmap::fromImage(padded));
    auto* effect = new QGraphicsBlurEffect;
    effect->setBlurRadius(effectiveRadius);
    effect->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(effect);
    scene.addItem(item);

    QImage blurredPadded(paddedSize, QImage::Format_ARGB32_Premultiplied);
    blurredPadded.fill(Qt::transparent);
    {
        QPainter p(&blurredPadded);
        const QRectF target(0, 0, paddedSize.width(), paddedSize.height());
        scene.render(&p, target, target);
    }

    QImage cropped = blurredPadded.copy(pad, pad, smallDevice.width(), smallDevice.height());
    QImage upscaled = cropped.scaled(QSize(logicalSize.width() * dpr, logicalSize.height() * dpr),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPixmap result = QPixmap::fromImage(upscaled);
    result.setDevicePixelRatio(dpr);
    return result;
}

inline void drawTonedGlassPanel(QPainter& painter, const QRectF& rect, qreal radius,
    const QSizeF& panelSize, const QPixmap& backdrop, const QColor& surface, const QColor& primary,
    bool darkTheme, const QColor& borderTop, const QColor& borderBottom, qreal borderWidth = 1.0,
    bool darkenEdges = true)
{
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, radius, radius);

    painter.save();
    painter.setClipPath(clipPath);

    if (!backdrop.isNull()) {
        const qreal backdropDpr = backdrop.devicePixelRatio();
        const QRectF sourceRect(
            QPointF(0, 0), QSizeF(backdrop.width() / backdropDpr, backdrop.height() / backdropDpr));
        painter.drawPixmap(QRectF(QPointF(0, 0), panelSize), backdrop, sourceRect);
    } else {
        painter.fillRect(rect, surface);
    }

    QColor baseTint = darkTheme ? QColor(2, 3, 5, 198) : QColor(240, 244, 250, 188);
    QColor accentTint = primary;
    accentTint.setAlpha(darkTheme ? 22 : 32);

    const qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : qreal(1);
    const QSize devSize(qMax(1, qRound(rect.width() * dpr)), qMax(1, qRound(rect.height() * dpr)));

    QImage hiPrec(devSize, QImage::Format_RGBA64_Premultiplied);
    hiPrec.setDevicePixelRatio(dpr);
    hiPrec.fill(Qt::transparent);
    {
        QPainter p(&hiPrec);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(-rect.topLeft());

        QPainterPath tintClip;
        tintClip.addRoundedRect(rect, radius, radius);
        p.setClipPath(tintClip);

        p.fillRect(rect, detail::withAlpha(surface, darkTheme ? 92 : 118));
        p.fillRect(rect, baseTint);

        QLinearGradient sheen(rect.topLeft(), rect.bottomRight());
        sheen.setColorAt(0.0, accentTint);
        sheen.setColorAt(0.50, QColor(255, 255, 255, darkTheme ? 10 : 28));
        sheen.setColorAt(1.0,
            darkenEdges ? QColor(0, 0, 0, darkTheme ? 44 : 18)
                        : QColor(128, 128, 128, darkTheme ? 10 : 8));
        p.fillRect(rect, sheen);

        QRadialGradient vignette(rect.center(), qMax(rect.width(), rect.height()) * 0.72);
        vignette.setColorAt(0.0, QColor(255, 255, 255, darkTheme ? 3 : 13));
        vignette.setColorAt(1.0,
            darkenEdges ? QColor(0, 0, 0, darkTheme ? 82 : 38)
                        : QColor(128, 128, 128, darkTheme ? 12 : 8));
        p.fillRect(rect, vignette);
    }

    QImage dithered(devSize, QImage::Format_ARGB32_Premultiplied);
    dithered.setDevicePixelRatio(dpr);
    const int W = devSize.width();
    const int H = devSize.height();
    for (int y = 0; y < H; ++y) {
        const QRgba64* src = reinterpret_cast<const QRgba64*>(hiPrec.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(dithered.scanLine(y));
        const int* bayerRow = detail::kBayer4[y & 3];
        for (int x = 0; x < W; ++x) {
            const int off = bayerRow[x & 3] * 16;
            const QRgba64 s = src[x];
            const int r = qMin(255, (int(s.red()) + off) / 257);
            const int g = qMin(255, (int(s.green()) + off) / 257);
            const int b = qMin(255, (int(s.blue()) + off) / 257);
            const int a = qMin(255, (int(s.alpha()) + off) / 257);
            dst[x] = qRgba(r, g, b, a);
        }
    }

    painter.drawImage(rect.topLeft(), dithered);
    painter.restore();

    if (borderWidth > 0.0) {
        drawGradientBorder(painter, rect, radius, borderTop, borderBottom, borderWidth);
    }
}

} // namespace ruwa::ui::painting

#endif // RUWA_UI_CORE_STYLE_PAINTINGUTILS_H
