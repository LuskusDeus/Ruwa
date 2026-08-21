// SPDX-License-Identifier: MPL-2.0

#include "shared/style/GlassPanel.h"

#include "features/canvas/rendering/CanvasBackdropRenderer.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/rendering/OffscreenGlassRenderer.h"
#include "shared/style/PaintingUtils.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <QtMath>

#include <vector>

namespace ruwa::ui::painting {

namespace {

/// Fills in everything a plate takes from the shared model, leaving the caller
/// only the geometry it measured for itself.
ruwa::shared::rendering::GlassPlate makeGlassPlate(
    const QRectF& rectInImage, qreal cornerRadiusInImage, const GlassPanelOptics& optics)
{
    ruwa::shared::rendering::GlassPlate plate;
    plate.rect = rectInImage;
    plate.cornerRadius = cornerRadiusInImage;
    plate.surfaceTint = optics.surfaceTint;
    plate.frostLevels = optics.frostLevels;
    plate.refractionStrength = optics.refractionStrength;
    // Every result on this path is cropped back to its plate, so a shadow
    // reaching outside it would be cut off mid-fade; the panels here draw
    // their own anyway.
    plate.shadowStrength = 0.0;
    return plate;
}

/// The dim is a flat wash over everything behind the panel, so laying it on
/// the finished plate is the same picture as laying it on the capture and
/// refracting that - and it costs one fill instead of a second grab.
void applyBackdropOverlay(QPixmap& panel, const QColor& overlay)
{
    if (panel.isNull() || !overlay.isValid() || overlay.alpha() == 0) {
        return;
    }
    QPainter painter(&panel);
    painter.fillRect(QRectF(QPointF(0, 0), panel.deviceIndependentSize()), overlay);
}

} // namespace

int glassCaptureMarginPx(qreal devicePixelRatio)
{
    const qreal ratio = qMax<qreal>(devicePixelRatio, 0.01);
    return qCeil(aether::CanvasBackdropRenderer::requiredMarginDevicePx(ratio, false) / ratio);
}

QList<QPixmap> captureGlassBackdrops(QWidget* source, const QList<GlassPanelPlate>& plates)
{
    QList<QPixmap> results(plates.size());
    if (!source || plates.isEmpty()) {
        return results;
    }
    QWidget* window = source->window();
    if (!window) {
        return results;
    }

    const QRect windowRect(QPoint(0, 0), window->size());
    QList<QRect> panelsInWindow;
    panelsInWindow.reserve(plates.size());
    QRect captureRect;
    for (const GlassPanelPlate& plate : plates) {
        const QRect panel(
            window->mapFromGlobal(plate.globalRect.topLeft()), plate.globalRect.size());
        // A panel hanging off the window has no backdrop on that side to
        // refract, and the composite would read one clamped edge texel across
        // the whole overhang. There is nothing sensible to capture, so this
        // plate comes back null while its neighbours still render.
        const bool usable = !panel.isEmpty() && windowRect.contains(panel);
        panelsInWindow.append(usable ? panel : QRect());
        if (usable) {
            captureRect = captureRect.united(panel);
        }
    }
    if (captureRect.isEmpty()) {
        return results;
    }

    const qreal devicePixelRatio = window->devicePixelRatioF();
    const int margin = glassCaptureMarginPx(devicePixelRatio);
    captureRect = captureRect.adjusted(-margin, -margin, margin, margin).intersected(windowRect);
    if (captureRect.isEmpty()) {
        return results;
    }

    // The top-level WINDOW, never the intermediate widget: Qt has a dedicated
    // path for QWidget::grab() on a window that composites every
    // QOpenGLWidget child's framebuffer into the result. render() on a
    // non-window widget skips that step, and the canvas comes back
    // transparent - glass over a hole where the artwork should be.
    const QPixmap snapshot = window->grab(captureRect);
    if (snapshot.isNull()) {
        return results;
    }

    // Read the scale off the grab rather than assuming it: grab() honours the
    // device pixel ratio, and the crops below have to land on whole pixels of
    // whatever it actually produced.
    const qreal imageScale = snapshot.width() / qreal(captureRect.width());
    const auto scaled = [imageScale](int value) { return qRound(value * imageScale); };

    QImage backdrop = snapshot.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QList<QRect> platesInImage;
    platesInImage.reserve(plates.size());
    std::vector<ruwa::shared::rendering::GlassPlate> glassPlates;
    glassPlates.reserve(plates.size());
    for (int i = 0; i < plates.size(); ++i) {
        const QRect& panel = panelsInWindow.at(i);
        if (panel.isEmpty()) {
            platesInImage.append(QRect());
            continue;
        }
        const QRect inImage(
            QPoint(scaled(panel.x() - captureRect.x()), scaled(panel.y() - captureRect.y())),
            QSize(scaled(panel.width()), scaled(panel.height())));
        platesInImage.append(inImage);

        glassPlates.push_back(makeGlassPlate(
            QRectF(inImage), plates.at(i).cornerRadius * imageScale, plates.at(i).optics));
    }

    const bool composed
        = ruwa::shared::rendering::OffscreenGlassRenderer::instance().composeInPlace(
            backdrop, imageScale, glassPlates);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    for (int i = 0; i < plates.size(); ++i) {
        const QRect& inImage = platesInImage.at(i);
        if (inImage.isEmpty()) {
            continue;
        }
        QPixmap panel = QPixmap::fromImage(backdrop.copy(inImage));
        panel.setDevicePixelRatio(devicePixelRatio);
        applyBackdropOverlay(panel, plates.at(i).optics.backdropOverlay);
        if (!composed) {
            panel = blurSnapshotPixmap(panel, theme.scaled(plates.at(i).optics.fallbackBlurRadius));
            panel.setDevicePixelRatio(devicePixelRatio);
        }
        results[i] = panel;
    }
    return results;
}

QPixmap captureGlassBackdrop(QWidget* source, const QRect& globalPanelRect, qreal cornerRadius,
    const GlassPanelOptics& optics)
{
    GlassPanelPlate plate;
    plate.globalRect = globalPanelRect;
    plate.cornerRadius = cornerRadius;
    plate.optics = optics;
    return captureGlassBackdrops(source, { plate }).value(0);
}

QPixmap renderGlassBackdrop(const QPixmap& backdrop, const QRect& plateRect, qreal cornerRadius,
    const GlassPanelOptics& optics)
{
    if (backdrop.isNull() || plateRect.isEmpty()) {
        return {};
    }

    const qreal devicePixelRatio = backdrop.devicePixelRatio();
    const auto scaled = [devicePixelRatio](int value) { return qRound(value * devicePixelRatio); };
    const QRect plateInImage(QPoint(scaled(plateRect.x()), scaled(plateRect.y())),
        QSize(scaled(plateRect.width()), scaled(plateRect.height())));
    if (!QRect(QPoint(0, 0), backdrop.size()).contains(plateInImage)) {
        return {};
    }

    QImage image = backdrop.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const std::vector<ruwa::shared::rendering::GlassPlate> plates { makeGlassPlate(
        QRectF(plateInImage), cornerRadius * devicePixelRatio, optics) };
    const bool composed
        = ruwa::shared::rendering::OffscreenGlassRenderer::instance().composeInPlace(
            image, devicePixelRatio, plates);

    QPixmap panel = QPixmap::fromImage(image.copy(plateInImage));
    panel.setDevicePixelRatio(devicePixelRatio);
    applyBackdropOverlay(panel, optics.backdropOverlay);
    if (!composed) {
        panel = blurSnapshotPixmap(
            panel, ruwa::ui::core::ThemeManager::instance().scaled(optics.fallbackBlurRadius));
        panel.setDevicePixelRatio(devicePixelRatio);
    }
    return panel;
}

void drawGlassSurface(QPainter& painter, const QRectF& rect, qreal radius, const QPixmap& backdrop,
    const QColor& surface, const QColor& primary, const QColor& borderTop,
    const QColor& borderBottom, qreal borderWidth, int tintAlpha)
{
    if (!rect.isValid()) {
        return;
    }

    // Everything painted as glass stops on the silhouette the shader itself
    // ends on; the border below is not glass and stays on the panel rect.
    const QRectF shapeRect = glassSilhouetteRect(rect);
    const qreal shapeRadius = glassSilhouetteRadius(radius);
    QPainterPath shape;
    shape.addRoundedRect(shapeRect, shapeRadius, shapeRadius);

    painter.save();
    painter.setPen(Qt::NoPen);
    if (backdrop.isNull()) {
        QColor opaque = surface;
        opaque.setAlpha(200);
        painter.setBrush(opaque);
        painter.drawPath(shape);
    } else {
        painter.setClipPath(shape);
        painter.drawPixmap(rect, backdrop, QRectF(QPointF(0, 0), backdrop.deviceIndependentSize()));
        QColor tint = surface;
        tint.setAlpha(qBound(0, tintAlpha, 255));
        painter.fillRect(rect, tint);
    }
    painter.restore();

    // A cosmetic pen of width zero still strokes a hairline, so a panel that
    // asked for no border has to be given none rather than a thin one. The rim
    // below is not chrome but the material itself, and stays either way.
    if (borderWidth > 0.0) {
        drawGradientBorder(painter, rect, radius, borderTop, borderBottom, borderWidth);
    }
    const qreal shadowDepth
        = qMin(ruwa::ui::core::ThemeManager::instance().scaled(kLiquidGlassShadowDepth),
            rect.height() * 0.25);
    drawLiquidGlass(painter, rect, radius, primary, shadowDepth);
}

} // namespace ruwa::ui::painting
