// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_STYLE_GLASSPANEL_H
#define RUWA_SHARED_STYLE_GLASSPANEL_H

#include "shared/style/PaintingUtils.h"

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QRect>

class QPainter;
class QWidget;

namespace ruwa::ui::painting {

/// Optics of one raster glass panel, in the terms the GPU pass understands.
/// The defaults are the on-canvas glass; a panel only names what it differs in.
struct GlassPanelOptics {
    /// Frost pyramid depth; negative takes the renderer's own default. One
    /// level below that is roughly half the blur. See CanvasBackdropRegion.
    int frostLevels = -1;
    /// Scales the optical thickness and the splay together. The lens keeps its
    /// shape and only bends less.
    qreal refractionStrength = 1.0;
    /// Colour the frost is pulled towards inside the shader, before the panel
    /// paints its own tint. Invalid leaves the backdrop untinted.
    QColor surfaceTint;
    /// Radius of the raster blur that stands in when the GPU path is
    /// unavailable, in unscaled logical pixels.
    int fallbackBlurRadius = 24;
    /// Painted over the captured backdrop before it is handed on as glass, for
    /// a panel floating above something the capture cannot see yet - a dimming
    /// overlay that animates in after the capture is taken, say. Invalid, the
    /// default, leaves the capture alone.
    QColor backdropOverlay;
};

/// One plate to be cut out of the same captured backdrop.
struct GlassPanelPlate {
    QRect globalRect;
    qreal cornerRadius = 0.0;
    GlassPanelOptics optics;
};

/// Captures what is behind @p plates in the window of @p source and turns each
/// into glass, returning one panel-sized pixmap per plate, in order.
///
/// The plates share one capture and one GPU pass, so a widget made of several
/// panes - a search bar and its list, say - pays for one window grab rather
/// than one per pane. A plate that does not lie wholly inside the window comes
/// back null while the rest still render.
///
/// @p source is any widget living in the window that provides the backdrop:
/// the panel itself when it is a child of that window, or the widget a popup
/// was opened from when the popup is its own window.
QList<QPixmap> captureGlassBackdrops(QWidget* source, const QList<GlassPanelPlate>& plates);

/// Single-plate captureGlassBackdrops(). Null when there is nothing sensible
/// to capture. The result carries the window's device pixel ratio.
QPixmap captureGlassBackdrop(QWidget* source, const QRect& globalPanelRect, qreal cornerRadius,
    const GlassPanelOptics& optics = {});

/// Logical-pixel margin a caller must leave around a plate in a backdrop of
/// its own making, at @p devicePixelRatio. Inside it lies everything the
/// refraction and the frost reach for; a backdrop that stops sooner is read as
/// its own clamped edge.
int glassCaptureMarginPx(qreal devicePixelRatio);

/// Glass over a backdrop the caller drew itself rather than grabbed from a
/// window - a widget that composes its own background can hand it straight
/// over instead of asking Qt to render the window again.
///
/// @p plateRect is in @p backdrop's logical coordinates and must sit at least
/// glassCaptureMarginPx() inside it. The result is the plate, cropped.
QPixmap renderGlassBackdrop(const QPixmap& backdrop, const QRect& plateRect, qreal cornerRadius,
    const GlassPanelOptics& optics = {});

/// Paints a glass panel: the captured backdrop, the theme tint on the glass
/// silhouette, the border on the panel rect, and the liquid-glass rim - the
/// same stack, in the same order, that the on-canvas overlays paint over the
/// GPU backdrop. Pass the panel rect itself: the silhouette inset is applied
/// here, exactly as the shader applies its own.
///
/// A null @p backdrop falls back to an opaque surface fill, like an overlay
/// before its first composited frame.
void drawGlassSurface(QPainter& painter, const QRectF& rect, qreal radius, const QPixmap& backdrop,
    const QColor& surface, const QColor& primary, const QColor& borderTop,
    const QColor& borderBottom, qreal borderWidth = 1.0, int tintAlpha = kGlassPanelTintAlpha);

} // namespace ruwa::ui::painting

#endif // RUWA_SHARED_STYLE_GLASSPANEL_H
