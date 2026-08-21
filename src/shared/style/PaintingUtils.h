// SPDX-License-Identifier: MPL-2.0

// PaintingUtils.h - Shared QPainter helpers to eliminate cross-widget duplication
#ifndef RUWA_UI_CORE_STYLE_PAINTINGUTILS_H
#define RUWA_UI_CORE_STYLE_PAINTINGUTILS_H

#include <QColor>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHash>
#include <QImage>
#include <QList>
#include <QLineF>
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

/// Alpha of the theme surface tint painted over canvas backdrop blur. Zeroed
/// while kGlassBareOpticsMode is on: the point of that mode is to leave nothing
/// on screen but the refraction, and this layer would wash it out on its own.
inline constexpr int kBackdropTintAlpha = ruwa::shared::rendering::kGlassBareOpticsMode ? 0 : 112;

/// The same tint for a glass panel that carries body text - a menu, a tooltip,
/// the command palette - rather than the short labels the canvas overlays show.
///
/// Higher than kBackdropTintAlpha, and not as a matter of taste: the muted text
/// tone is defined against a flat theme surface, and at the overlay's opacity
/// the artwork underneath keeps enough of its own value that grey text lands
/// somewhere inside the backdrop's range instead of above it. White text
/// survives either way, which is why the shortfall shows up on secondary text
/// first. Raising this trades some of the refraction for the contrast the text
/// was designed against; the two cannot both be maximal on the same surface.
inline constexpr int kGlassPanelTintAlpha = ruwa::shared::rendering::kGlassBareOpticsMode ? 0 : 160;

/// Shape of the glass in a backdrop-blurred overlay: the widget rect pulled in
/// to where the GPU pass ends its own silhouette (see kGlassSilhouetteInsetPx).
/// Everything painted *as glass* - the tint, the inner shadow - has to use it,
/// or it stands proud of the blur by the inset and shows around the corners,
/// through the overlay's half-transparent border. The border itself is not
/// glass and stays on the widget rect.
inline QRectF glassSilhouetteRect(const QRectF& rect)
{
    const qreal inset = ruwa::shared::rendering::kGlassSilhouetteInsetPx;
    const QRectF inner = rect.adjusted(inset, inset, -inset, -inset);
    return inner.isValid() ? inner : rect;
}

/// Radius that keeps glassSilhouetteRect() concentric with the widget's corners.
inline qreal glassSilhouetteRadius(qreal radius)
{
    return qMax<qreal>(0.0, radius - ruwa::shared::rendering::kGlassSilhouetteInsetPx);
}

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

inline QColor interpolateColor(const QColor& from, const QColor& to, qreal progress)
{
    progress = qBound<qreal>(0.0, progress, 1.0);
    return QColor(static_cast<int>(from.red() + (to.red() - from.red()) * progress),
        static_cast<int>(from.green() + (to.green() - from.green()) * progress),
        static_cast<int>(from.blue() + (to.blue() - from.blue()) * progress),
        static_cast<int>(from.alpha() + (to.alpha() - from.alpha()) * progress));
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

    // Restore, like every other helper here. Leaking this pen means the next
    // fill a caller draws is silently outlined in the border colour, which is
    // where stray 1px outlines on overlay content came from.
    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

// ----------------------------------------------------------------------------
// Liquid glass — the specular pass for on-canvas overlays. Sits on top of the
// backdrop blur: a diagonal primary-tinted sweep riding the outline, plus a pair
// of inner shadows (dark rising from the bottom edge, primary light falling from
// the top edge). The rim shares its geometry with drawGradientBorder(), so the
// two land on the same pixel line instead of reading as a double stroke.
// ----------------------------------------------------------------------------

/// Opacity of the diagonal specular rim.
inline constexpr qreal kLiquidGlassRimOpacity = 0.15;
/// Off for now: the lit top edge and the dark bottom edge are on trial, and the
/// refracting bevel may already be doing their job. The pass is kept intact -
/// flip this back to true to get it, and its GPU counterpart is
/// CanvasBackdropRenderer::kBevelShadeAmount.
inline constexpr bool kLiquidGlassInnerShadowEnabled = false;
/// Opacity of the inner-shadow pair.
inline constexpr qreal kLiquidGlassShadowOpacity = 0.05;
/// Default inner-shadow penetration in logical px (scale at the call site).
inline constexpr qreal kLiquidGlassShadowDepth = 8.0;

/// Axis of the specular sweep, running bottom-left → top-right across @p bounds.
///
/// A linear gradient's bands are perpendicular to its axis, so the axis is NOT the
/// diagonal — it is the normal of the top-left→bottom-right diagonal. That puts the
/// bright midpoint band exactly on that diagonal (through both corners) and the
/// transparent ends on the projections of the other two, at any aspect ratio.
///
/// Driving the axis off the corners directly instead ties its angle to the aspect
/// ratio: on a long capsule it flattens to near-horizontal, the bands turn vertical
/// and read as stripes parallel to the short sides. Pinning it to 45° is no better —
/// the bright band then leaves through the long edges near the centre rather than
/// through the corners.
inline QLineF liquidGlassSweepAxis(const QRectF& bounds)
{
    const qreal w = bounds.width();
    const qreal h = bounds.height();
    const QPointF center = bounds.center();

    const qreal diagonal = std::sqrt(w * w + h * h);
    if (diagonal <= 0.0) {
        return QLineF(center, center);
    }

    const QPointF direction(h / diagonal, -w / diagonal); // right and up
    // Perpendicular distance from the diagonal out to either remaining corner.
    const qreal half = w * h / diagonal;
    return QLineF(center - direction * half, center + direction * half);
}

/// Diagonal highlight along @p outline: transparent at the ends of the 45° sweep
/// across @p bounds, full @p primary at the midpoint.
/// @p outline must already be the stroke geometry (pixel-center inset).
inline void drawLiquidGlassRim(QPainter& painter, const QPainterPath& outline, const QRectF& bounds,
    const QColor& primary, qreal penWidth = 1.0, qreal opacity = kLiquidGlassRimOpacity)
{
    if (outline.isEmpty() || !bounds.isValid() || opacity <= 0.0 || penWidth <= 0.0) {
        return;
    }

    QColor mid = primary;
    mid.setAlphaF(qBound<qreal>(0.0, primary.alphaF() * opacity, 1.0));
    QColor ends = mid;
    ends.setAlpha(0);

    const QLineF axis = liquidGlassSweepAxis(bounds);
    QLinearGradient sweep(axis.p1(), axis.p2());
    sweep.setColorAt(0.0, ends);
    sweep.setColorAt(0.5, mid);
    sweep.setColorAt(1.0, ends);

    QPen pen;
    pen.setBrush(sweep);
    pen.setWidthF(penWidth);
    pen.setCosmetic(true);

    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
    painter.restore();
}

namespace detail {

/// LRU of rendered inner-shadow layers, keyed on everything the raster depends on.
///
/// The on-canvas overlays repaint on every backdrop-blur tick — i.e. every frame
/// while the user draws — and the shadow is by far the most expensive thing they
/// paint: 48 stroked paths into a 16-bit buffer plus a per-pixel dither pass, all
/// on the GUI thread. Since none of its inputs change between those repaints, it
/// is rasterised once and blitted afterwards.
///
/// GUI thread only, like the rest of this header. Capacity covers the four canvas
/// overlays with room for in-flight size variants; a width animation still misses
/// every frame (the geometry genuinely changed) but cannot grow the cache.
inline constexpr int kLiquidGlassCacheCapacity = 12;

struct LiquidGlassCache {
    QHash<quint64, QImage> entries;
    QList<quint64> order; ///< least-recently-used first

    const QImage* find(quint64 key)
    {
        const auto it = entries.constFind(key);
        if (it == entries.constEnd()) {
            return nullptr;
        }
        const qsizetype at = order.indexOf(key);
        if (at >= 0 && at != order.size() - 1) {
            order.move(at, order.size() - 1);
        }
        return &it.value();
    }

    void insert(quint64 key, const QImage& image)
    {
        if (!entries.contains(key)) {
            order.append(key);
        }
        entries.insert(key, image);
        while (order.size() > kLiquidGlassCacheCapacity) {
            entries.remove(order.takeFirst());
        }
    }
};

inline LiquidGlassCache& liquidGlassCache()
{
    static LiquidGlassCache cache;
    return cache;
}

inline quint64 hashCombine(quint64 seed, quint64 value)
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

/// Identity of a path for cache purposes. Coordinates are quantised to 1/16 px so
/// float noise in the layout does not churn the key.
inline quint64 hashPath(quint64 seed, const QPainterPath& path)
{
    const int count = path.elementCount();
    seed = hashCombine(seed, quint64(count));
    for (int i = 0; i < count; ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        seed = hashCombine(seed, quint64(element.type));
        seed = hashCombine(seed, quint64(qint64(qRound(element.x * 16.0))));
        seed = hashCombine(seed, quint64(qint64(qRound(element.y * 16.0))));
    }
    return seed;
}

} // namespace detail

/// Inner shadows hugging the inside of @p shape: @p primary light dropping from
/// the top edge, black rising from the bottom. Softness comes from stacking
/// offset strokes of the outline; the stack is accumulated at 16 bits per channel
/// and Bayer-dithered on the way back to 8, because at these opacities a single
/// layer lands well below one 8-bit step and the ramp would band hard.
///
/// The raster is cached (see detail::LiquidGlassCache) — repeat calls with the same
/// shape, colour, depth and DPR only cost a blit.
inline void drawLiquidGlassInnerShadow(QPainter& painter, const QPainterPath& shape,
    const QColor& primary, qreal depth = kLiquidGlassShadowDepth,
    qreal opacity = kLiquidGlassShadowOpacity)
{
    // Gated here rather than at the call sites so the joystick base, which asks
    // for the shadow directly, follows the same switch as drawLiquidGlass().
    if (!kLiquidGlassInnerShadowEnabled) {
        return;
    }
    if (shape.isEmpty() || depth <= 0.5 || opacity <= 0.0) {
        return;
    }

    const QRectF bounds = shape.boundingRect();
    if (bounds.isEmpty()) {
        return;
    }

    const qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : qreal(1);
    const QRect target = bounds.toAlignedRect();
    const QSize devSize(
        qMax(1, qCeil(target.width() * dpr)), qMax(1, qCeil(target.height() * dpr)));

    quint64 cacheKey = detail::hashPath(0x11A11DC1A55ULL, shape);
    cacheKey = detail::hashCombine(cacheKey, quint64(primary.rgba()));
    cacheKey = detail::hashCombine(cacheKey, quint64(qint64(qRound(depth * 64.0))));
    cacheKey = detail::hashCombine(cacheKey, quint64(qint64(qRound(opacity * 4096.0))));
    cacheKey = detail::hashCombine(cacheKey, quint64(qint64(qRound(dpr * 1024.0))));

    if (const QImage* cached = detail::liquidGlassCache().find(cacheKey)) {
        painter.drawImage(target.topLeft(), *cached);
        return;
    }

    QImage hiPrec(devSize, QImage::Format_RGBA64_Premultiplied);
    if (hiPrec.isNull()) {
        return;
    }
    hiPrec.setDevicePixelRatio(dpr);
    hiPrec.fill(Qt::transparent);

    // More layers than the eye can resolve as steps; the dither hides the rest.
    constexpr int kLayers = 24;
    const qreal layerAlpha = 1.0 - std::pow(1.0 - qBound<qreal>(0.0, opacity, 1.0), 1.0 / kLayers);

    {
        QPainter p(&hiPrec);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(-target.topLeft());
        p.setBrush(Qt::NoBrush);

        QColor light = primary;
        light.setAlphaF(layerAlpha * primary.alphaF());
        QColor lightEnd = light;
        lightEnd.setAlpha(0);

        QColor dark(0, 0, 0);
        dark.setAlphaF(layerAlpha);
        QColor darkEnd = dark;
        darkEnd.setAlpha(0);

        // Each stack is faded out along Y past the midline, so on the left/right
        // edges — where both bands reach equally far in — the light hands over to
        // the dark as one continuous ramp instead of stacking into a hard seam.
        QLinearGradient lightFade(bounds.topLeft(), bounds.bottomLeft());
        lightFade.setColorAt(0.0, light);
        lightFade.setColorAt(0.35, light);
        lightFade.setColorAt(0.68, lightEnd);

        QLinearGradient darkFade(bounds.topLeft(), bounds.bottomLeft());
        darkFade.setColorAt(0.32, darkEnd);
        darkFade.setColorAt(0.65, dark);
        darkFade.setColorAt(1.0, dark);

        for (int i = kLayers; i >= 1; --i) {
            // Offsetting the outline by the band's half-width keeps the stroke on
            // the inside of the leading edge only; the opposite edge lands outside
            // the shape and is masked off below.
            const qreal reach = depth * qreal(i) / qreal(kLayers);

            QPen pen;
            pen.setWidthF(reach * 2.0);
            pen.setJoinStyle(Qt::RoundJoin);

            pen.setBrush(lightFade);
            p.setPen(pen);
            p.drawPath(shape.translated(0.0, reach));

            pen.setBrush(darkFade);
            p.setPen(pen);
            p.drawPath(shape.translated(0.0, -reach));
        }

        // Antialiased mask instead of setClipPath(), whose edge is hard-aliased and
        // would stair-step the rounded corners.
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(shape);
    }

    QImage dithered(devSize, QImage::Format_ARGB32_Premultiplied);
    if (dithered.isNull()) {
        return;
    }
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
            const int a = qMin(255, (int(s.alpha()) + off) / 257);
            const int r = qMin(a, (int(s.red()) + off) / 257);
            const int g = qMin(a, (int(s.green()) + off) / 257);
            const int b = qMin(a, (int(s.blue()) + off) / 257);
            dst[x] = qRgba(r, g, b, a);
        }
    }

    detail::liquidGlassCache().insert(cacheKey, dithered);
    painter.drawImage(target.topLeft(), dithered);
}

/// Liquid-glass pass for a rounded-rect overlay. Call it right after the
/// backdrop tint and the regular border: the rim reuses drawGradientBorder()'s
/// inset/radius so both strokes coincide.
inline void drawLiquidGlass(QPainter& painter, const QRectF& rect, qreal radius,
    const QColor& primary, qreal shadowDepth = kLiquidGlassShadowDepth, qreal rimWidth = 1.0)
{
    if (!rect.isValid()) {
        return;
    }

    // The shadow is glass and stops on the backdrop's silhouette; the rim below
    // is a border and stays on the widget's own stroke.
    const QRectF shapeRect = glassSilhouetteRect(rect);
    const qreal shapeRadius = glassSilhouetteRadius(radius);
    QPainterPath shape;
    shape.addRoundedRect(shapeRect, shapeRadius, shapeRadius);

    const QRectF rimRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal rimRadius = qMax<qreal>(0.0, radius - 0.5);
    QPainterPath rim;
    rim.addRoundedRect(rimRect, rimRadius, rimRadius);

    drawLiquidGlassInnerShadow(painter, shape, primary, shadowDepth);
    drawLiquidGlassRim(painter, rim, rimRect, primary, rimWidth);
}

/**
 * Paints the shared raised-key frame used by shortcut inputs and tooltip
 * shortcut hints. Text is intentionally left to the caller so the same frame
 * can be used at different compact sizes.
 */
inline void drawKeycapFrame(QPainter& painter, const QRectF& rect, qreal radius, qreal bottomDepth,
    const QColor& fill, const QColor& borderTop, const QColor& borderBottom)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, radius, radius);

    const qreal depth = qBound<qreal>(0.0, bottomDepth, rect.height() * 0.35);
    if (depth > 0.5) {
        // The skirt is the border's own color, shaded across its own height rather
        // than blended into the fill: the whole borderTop..borderBottom ramp is spent
        // inside the skirt band, so the gradient survives being only a few px tall.
        const QRectF skirtRect(rect.left(), rect.bottom() - depth, rect.width(), depth + 1.0);
        QLinearGradient skirtGradient(skirtRect.topLeft(), skirtRect.bottomLeft());
        skirtGradient.setColorAt(0.0, borderTop);
        skirtGradient.setColorAt(1.0, borderBottom);

        painter.save();
        painter.setClipRect(skirtRect);
        painter.setBrush(skirtGradient);
        painter.drawRoundedRect(rect, radius, radius);
        painter.restore();
    }

    constexpr qreal borderWidth = 1.0;
    const QRectF outerRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    const QRectF innerRect
        = outerRect.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
    const qreal outerRadius = qMax<qreal>(0.0, radius - 0.5);
    const qreal innerRadius = qMax<qreal>(0.0, outerRadius - borderWidth);

    QPainterPath outerPath;
    outerPath.addRoundedRect(outerRect, outerRadius, outerRadius);

    QPainterPath innerPath;
    if (innerRect.width() > 0 && innerRect.height() > 0) {
        innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
    }

    QLinearGradient gradient(outerRect.topLeft(), outerRect.bottomLeft());
    gradient.setColorAt(0.0, borderTop);
    gradient.setColorAt(1.0, borderBottom);

    painter.setBrush(gradient);
    painter.drawPath(outerPath.subtracted(innerPath));

    if (depth > 0.5) {
        QPainterPath bottomClip;
        bottomClip.addRect(
            QRectF(outerRect.left(), outerRect.bottom() - depth, outerRect.width(), depth + 1.0));

        QColor bottomAccent = borderBottom;
        bottomAccent.setAlpha(qMin(255, qRound(bottomAccent.alpha() * 1.65)));
        QLinearGradient bottomGradient(outerRect.topLeft(), outerRect.bottomLeft());
        bottomGradient.setColorAt(0.0, borderBottom);
        bottomGradient.setColorAt(1.0, bottomAccent);

        painter.setBrush(bottomGradient);
        painter.drawPath(outerPath.intersected(bottomClip));
    }
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

/// Gaussian blur of a snapshot on the CPU.
///
/// Two callers left, and neither is a glass panel: WelcomeUpdatePanel blurs an
/// image as decoration, and GlassPanel.cpp uses this as its fallback when the
/// GPU glass cannot run. Panels themselves go through captureGlassBackdrop().
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

} // namespace ruwa::ui::painting

#endif // RUWA_UI_CORE_STYLE_PAINTINGUTILS_H
