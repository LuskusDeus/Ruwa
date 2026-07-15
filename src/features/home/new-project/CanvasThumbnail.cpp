// SPDX-License-Identifier: MPL-2.0

// CanvasThumbnail.cpp
#include "CanvasThumbnail.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QtGlobal>
#include <QtMath>
#include <numeric>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_CONTENT_INSET = 12; // padding from widget edges to content area
const int BASE_GHOST_RADIUS = 5; // corner radius of the dashed ghost rect
const int BASE_ROW_GAP = 4; // gap between text rows
const int BASE_PRESET_FONT_SIZE = 8;
const int BASE_DIM_FONT_SIZE = 12;
const int BASE_MARK_LEN = 10; // arm length of rounded corner mark
const int BASE_MARK_RADIUS = 4; // inner curve radius of corner mark
const int BASE_MARK_GAP = 4; // gap between mark anchor and ghost rect edge

// Infinite-canvas reveal: as metadataProgress falls 1->0 the dashed frame is scaled up
// around its center (NOT by changing aspect) and clipped at the widget edge, fading out.
const qreal GHOST_INFINITE_SCALE = 0.7; // extra scale at full infinite (1.0 -> 1.7x)
const qreal GHOST_INFINITE_MIN_ALPHA = 0.0; // ghost frame fully fades at full infinite

/// Largest rect with aspect arW:arH centered inside `outer` (letterbox / pillarbox outer).
QRectF centeredAspectRect(const QRectF& outer, qreal arW, qreal arH)
{
    if (outer.width() < 1.0 || outer.height() < 1.0 || arW <= 0.0 || arH <= 0.0)
        return outer;
    const qreal targetAr = arW / arH;
    const qreal w = outer.width();
    const qreal h = outer.height();
    qreal rw, rh;
    if (w / h > targetAr) {
        rh = h;
        rw = rh * targetAr;
    } else {
        rw = w;
        rh = rw / targetAr;
    }
    return QRectF(outer.left() + (w - rw) * 0.5, outer.top() + (h - rh) * 0.5, rw, rh);
}
} // namespace

// ============================================================================
// Construction
// ============================================================================

CanvasThumbnail::CanvasThumbnail(const QSize& widgetSize, QWidget* parent)
    : BaseStyledPanel("CanvasThumbnail", parent)
    , m_baseWidgetSize(widgetSize)
{
    style().metrics.baseWidth = m_baseWidgetSize.width();
    style().metrics.baseMinWidth = m_baseWidgetSize.width();
    style().metrics.fixedWidth = false;
    // Registered style used to use fixedWidth; BaseStyledPanel ctor would call setFixedWidth.
    setMaximumWidth(QWIDGETSIZE_MAX);
    // Fills parent (e.g. AspectRatio16x9Frame + shell); outer 16:9 is enforced by the frame.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    applyStyleChanges();
    applyScaledSizeConstraints();

    setHoverEnabled(false);

    setupMorphAnimation();
    setupMetadataAnimation();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &CanvasThumbnail::applyScaledSizeConstraints);
    connect(&WidgetStyleManager::instance(), &WidgetStyleManager::globalSettingsChanged, this,
        &CanvasThumbnail::applyScaledSizeConstraints);
}

CanvasThumbnail::~CanvasThumbnail() = default;

void CanvasThumbnail::setupMorphAnimation()
{
    m_morphAnimation = new QPropertyAnimation(this, "morphProgress", this);
    m_morphAnimation->setDuration(300);
    m_morphAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void CanvasThumbnail::setupMetadataAnimation()
{
    m_metadataAnimation = new QPropertyAnimation(this, "metadataProgress", this);
    m_metadataAnimation->setDuration(300);
    // Symmetric easing so the classic<->infinite reveal feels identical in both directions
    // (OutCubic front-loads the motion, making the grow-out look like a blink).
    m_metadataAnimation->setEasingCurve(QEasingCurve::InOutCubic);
}

void CanvasThumbnail::applyScaledSizeConstraints()
{
    // Fills AspectRatio16x9Frame: width drives the slot; do not force a wide minimum — it breaks
    // narrow home layouts (e.g. letterboxed home tab) and distorts layout vs true 16:9 frame.
    setMinimumSize(1, 1);
    updateGeometry();
}

QSize CanvasThumbnail::sizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();
    const int w = mgr.scaled(m_baseWidgetSize.width());
    const int h = qMax(1, qRound(static_cast<qreal>(w) * 9.0 / 16.0));
    return QSize(w, h);
}

// ============================================================================
// Public API
// ============================================================================

void CanvasThumbnail::setDimensions(const QSize& dimensions)
{
    if (m_targetDimensions == dimensions)
        return;
    m_previousDimensions = m_targetDimensions;
    m_targetDimensions = dimensions;
    startMorphAnimation();
}

void CanvasThumbnail::setDimensions(int width, int height)
{
    setDimensions(QSize(width, height));
}

void CanvasThumbnail::setProjectName(const QString& name)
{
    if (m_projectName == name)
        return;
    m_projectName = name;
    update();
}

void CanvasThumbnail::setInfiniteCanvasEnabled(bool enabled)
{
    if (m_infiniteCanvasEnabled == enabled)
        return;
    m_infiniteCanvasEnabled = enabled;
    startMetadataAnimation();
}

void CanvasThumbnail::startMorphAnimation()
{
    auto& mgr = WidgetStyleManager::instance();

    if (!mgr.animationsEnabled()) {
        m_morphProgress = 1.0;
        update();
        return;
    }

    m_morphAnimation->stop();
    m_morphAnimation->setStartValue(0.0);
    m_morphAnimation->setEndValue(1.0);
    m_morphAnimation->start();
}

void CanvasThumbnail::startMetadataAnimation()
{
    auto& mgr = WidgetStyleManager::instance();
    const qreal targetProgress = m_infiniteCanvasEnabled ? 0.0 : 1.0;

    if (!mgr.animationsEnabled()) {
        m_metadataProgress = targetProgress;
        update();
        return;
    }

    m_metadataAnimation->stop();
    m_metadataAnimation->setStartValue(m_metadataProgress);
    m_metadataAnimation->setEndValue(targetProgress);
    m_metadataAnimation->start();
}

void CanvasThumbnail::setMorphProgress(qreal progress)
{
    if (qFuzzyCompare(m_morphProgress, progress))
        return;
    m_morphProgress = progress;
    update();
}

void CanvasThumbnail::setMetadataProgress(qreal progress)
{
    const qreal bounded = qBound<qreal>(0.0, progress, 1.0);
    if (qFuzzyCompare(m_metadataProgress, bounded))
        return;
    m_metadataProgress = bounded;
    update();
}

// ============================================================================
// Geometry helpers
// ============================================================================

QSizeF CanvasThumbnail::interpolateSize(const QSizeF& from, const QSizeF& to, qreal t) const
{
    return QSizeF(from.width() + (to.width() - from.width()) * t,
        from.height() + (to.height() - from.height()) * t);
}

// Compute the aspect-ratio rect centered within contentArea, morphing between
// previous and target canvas dimensions.
QRectF CanvasThumbnail::computeGhostRect(const QRectF& area) const
{
    const QSizeF prevAspect(m_previousDimensions.width(), m_previousDimensions.height());
    const QSizeF targAspect(m_targetDimensions.width(), m_targetDimensions.height());
    const QSizeF curAspect = interpolateSize(prevAspect, targAspect, m_morphProgress);

    const qreal ch = curAspect.height();
    if (ch <= 0.0)
        return area;
    const qreal ar = curAspect.width() / ch;
    qreal pw, ph;
    // Sub-pixel float noise can push ph barely past area.height() and force pillarboxing
    // on a 16:9 canvas inside a 16:9 cell — use a small tolerance.
    const qreal eps = 0.5;

    if (ar >= 1.0) {
        pw = area.width();
        ph = pw / ar;
        if (ph > area.height() + eps) {
            ph = area.height();
            pw = ph * ar;
        }
    } else {
        ph = area.height();
        pw = ph * ar;
        if (pw > area.width() + eps) {
            pw = area.width();
            ph = pw / ar;
        }
    }

    return QRectF(
        area.x() + (area.width() - pw) * 0.5, area.y() + (area.height() - ph) * 0.5, pw, ph);
}

QString CanvasThumbnail::ratioString(const QSize& size) const
{
    if (size.width() <= 0 || size.height() <= 0)
        return QString();
    int g = std::gcd(size.width(), size.height());
    int rw = size.width() / g;
    int rh = size.height() / g;
    if (rw <= 32 && rh <= 32)
        return QString("%1 : %2").arg(rw).arg(rh);
    qreal r = static_cast<qreal>(size.width()) / size.height();
    return r >= 1.0 ? QString("%1 : 1").arg(r, 0, 'f', 2)
                    : QString("1 : %1").arg(1.0 / r, 0, 'f', 2);
}

// ============================================================================
// Drawing
// ============================================================================

void CanvasThumbnail::drawBackgroundLayer(QPainter& painter, const QRectF& rect)
{
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

void CanvasThumbnail::drawBorderLayer(QPainter& painter, const QRectF& rect)
{
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

void CanvasThumbnail::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();

    const qreal inset = mgr.scaled(BASE_CONTENT_INSET);
    const QRectF padded = rect.adjusted(inset, inset, -inset, -inset);
    // Layout width/height is often not exactly 16:9; fitting the dashed preview in the full
    // padded rect makes a true 16:9 preset look pillarboxed. Use a strict 16:9 frame here.
    const QRectF contentArea = centeredAspectRect(padded, 16.0, 9.0);

    if (contentArea.width() < 8.0 || contentArea.height() < 8.0)
        return;

    painter.setRenderHint(QPainter::Antialiasing);

    // Ghost rect: aspect-ratio morphing rect centered in content area
    const QRectF ghostRect = computeGhostRect(contentArea);

    // 0 = classic (framed dimensions), 1 = infinite (grown past the container, faded).
    const qreal infiniteAmount = 1.0 - qBound<qreal>(0.0, m_metadataProgress, 1.0);

    if (infiniteAmount <= 0.0001) {
        drawGhostRect(painter, ghostRect);
        drawCornerMarks(painter, ghostRect);
    } else {
        // Enlarge the frame in its current form (uniform scale about its center) and clip to
        // the widget bounds so it bleeds past the container edges, fading as it grows. The
        // dashed/corner pens are cosmetic, so scaling keeps line width and dash spacing crisp.
        const qreal scale = 1.0 + infiniteAmount * GHOST_INFINITE_SCALE;
        const qreal opacity = 1.0 - infiniteAmount * (1.0 - GHOST_INFINITE_MIN_ALPHA);

        painter.save();
        painter.setClipRect(rect);
        const QPointF center = ghostRect.center();
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);
        painter.setOpacity(opacity);
        drawGhostRect(painter, ghostRect);
        drawCornerMarks(painter, ghostRect);
        painter.restore();
    }

    // Text/dimension numbers fade independently with metadataProgress.
    drawCenteredText(painter, ghostRect);
}

void CanvasThumbnail::drawGhostRect(QPainter& painter, const QRectF& ghostRect) const
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    const qreal radius = mgr.scaled(BASE_GHOST_RADIUS);

    QPen pen(colors.borderSubtle(), 1.0);
    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern({ 4.0, 4.0 });
    pen.setCapStyle(Qt::RoundCap);
    pen.setCosmetic(true);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(ghostRect, radius, radius);
}

void CanvasThumbnail::drawCornerMarks(QPainter& painter, const QRectF& ghostRect) const
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    const qreal markLen = mgr.scaled(BASE_MARK_LEN);
    const qreal markRadius = mgr.scaled(BASE_MARK_RADIUS);
    const qreal gap = mgr.scaled(BASE_MARK_GAP);

    QPen pen(colors.primary, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Draws one rounded L-shaped corner mark.
    //
    // Each mark has two arms meeting at a rounded corner.
    // The anchor (x, y) is the outer tip of the corner, offset by `gap`
    // outward from the ghostRect edge so there's breathing room.
    //
    // hDir/vDir: direction the arms extend away from the anchor corner.
    //
    // Arc angles (Qt convention: 0°=east, 90°=up/north on screen, CCW=positive):
    //   TL (right=F, bottom=F): 90° → 180°,  sweep +90
    //   TR (right=T, bottom=F): 90° → 0°,    sweep −90
    //   BL (right=F, bottom=T): 270° → 180°, sweep −90
    //   BR (right=T, bottom=T): 270° → 360°, sweep +90
    //
    // Pattern: sweep sign = (right XOR bottom) ? −90 : +90
    auto drawMark = [&](bool right, bool bottom) {
        // Anchor: corner of ghostRect shifted outward by gap
        const qreal x = right ? ghostRect.right() + gap : ghostRect.left() - gap;
        const qreal y = bottom ? ghostRect.bottom() + gap : ghostRect.top() - gap;

        // Arms extend inward (toward the ghost rect center)
        const qreal hDir = right ? -1.0 : 1.0;
        const qreal vDir = bottom ? -1.0 : 1.0;

        // Arc bounding rect centered at (x + hDir*r, y + vDir*r)
        const QRectF arcRect(x + (hDir > 0 ? 0.0 : -2.0 * markRadius),
            y + (vDir > 0 ? 0.0 : -2.0 * markRadius), 2.0 * markRadius, 2.0 * markRadius);

        const qreal startAngle = bottom ? 270.0 : 90.0;
        const qreal sweepAngle = (right != bottom) ? -90.0 : 90.0;

        QPainterPath path;
        path.moveTo(x + hDir * markLen, y); // outer end of horizontal arm
        path.lineTo(x + hDir * markRadius, y); // run to arc entry
        path.arcTo(arcRect, startAngle, sweepAngle); // rounded inner corner
        path.lineTo(x, y + vDir * markLen); // outer end of vertical arm

        painter.drawPath(path);
    };

    drawMark(false, false); // top-left
    drawMark(true, false); // top-right
    drawMark(false, true); // bottom-left
    drawMark(true, true); // bottom-right
}

void CanvasThumbnail::drawCenteredText(QPainter& painter, const QRectF& ghostRect) const
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    const int rowGap = mgr.scaled(BASE_ROW_GAP);
    const int dimFontSz = mgr.scaledFontSize(BASE_DIM_FONT_SIZE);
    const int smallFontSz = mgr.scaledFontSize(BASE_PRESET_FONT_SIZE);
    const int dimH = dimFontSz + 4;
    const int smallH = smallFontSz + 2;
    const qreal metadataProgress = qBound<qreal>(0.0, m_metadataProgress, 1.0);

    // Interpolated dimension numbers (morph animation)
    const int dispW = m_previousDimensions.width()
        + int((m_targetDimensions.width() - m_previousDimensions.width()) * m_morphProgress);
    const int dispH = m_previousDimensions.height()
        + int((m_targetDimensions.height() - m_previousDimensions.height()) * m_morphProgress);

    const bool hasProjectName = !m_projectName.isEmpty();
    const qreal metadataH = dimH + rowGap + smallH;
    const qreal totalH = hasProjectName ? smallH + metadataProgress * (rowGap + metadataH)
                                        : metadataProgress * metadataH;
    qreal curY = ghostRect.center().y() - totalH * 0.5;

    QColor textMain = colors.text;
    textMain.setAlphaF(0.75 * metadataProgress);

    QColor textMuted = colors.textMuted;
    QColor metadataMuted = textMuted;
    metadataMuted.setAlphaF(textMuted.alphaF() * metadataProgress);

    QFont smallFont = painter.font();
    smallFont.setPointSize(smallFontSz);
    smallFont.setBold(false);

    auto drawScaledText = [&](const QRectF& textRect, const QFont& font, const QColor& color,
                              const QString& text, qreal scale) {
        if (color.alphaF() <= 0.01 || text.isEmpty())
            return;

        painter.save();
        const QPointF center = textRect.center();
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);
        painter.setPen(color);
        painter.setFont(font);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, text);
        painter.restore();
    };

    // Row 1: project name (optional)
    if (hasProjectName) {
        painter.setPen(textMuted);
        painter.setFont(smallFont);
        painter.drawText(QRectF(ghostRect.left(), curY, ghostRect.width(), smallH),
            Qt::AlignHCenter | Qt::AlignVCenter, m_projectName.toUpper());
        curY += smallH + rowGap * metadataProgress;
    }

    // Row 2: dimensions
    QFont dimFont = smallFont;
    dimFont.setPointSize(dimFontSz);
    dimFont.setBold(true);
    const qreal detailsScale = 0.84 + 0.16 * metadataProgress;
    drawScaledText(QRectF(ghostRect.left(), curY, ghostRect.width(), dimH), dimFont, textMain,
        QString("%1 \u00d7 %2").arg(dispW).arg(dispH), detailsScale);
    curY += (dimH + rowGap) * metadataProgress;

    // Row 3: aspect ratio
    drawScaledText(QRectF(ghostRect.left(), curY, ghostRect.width(), smallH), smallFont,
        metadataMuted, ratioString(m_targetDimensions), detailsScale);
}

} // namespace ruwa::ui::widgets
