// SPDX-License-Identifier: MPL-2.0

#include "ColorSlotSwitchWidget.h"

#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeManager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QtMath>

namespace ruwa::ui::widgets {

namespace {
constexpr qreal kSwatchRadius = 4.0;
constexpr qreal kMaskGap = 1.5; // visual gap between top swatch's edge and bottom swatch
constexpr qreal kHoverOutlineGrow = 2.0;
constexpr qreal kEdgeInset = 3.0; // reserve room for outlines so they don't clip
constexpr int kSwapButtonSize = 12;
constexpr int kSwapIconSize = 10;
constexpr qreal kSwapHoverRadius = 2.0;
constexpr qreal kSwapOffsetX = -2.0; // negative = shift toward fg square (left)
constexpr qreal kSwapOffsetY = -2.5; // negative = shift up
constexpr int kAnimDuration = 180;
} // namespace

ColorSlotSwitchWidget::ColorSlotSwitchWidget(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFixedSize(sizeHint());

    auto makeAnim = [this](const char* prop, int dur, QEasingCurve::Type easing) {
        auto* a = new QPropertyAnimation(this, prop, this);
        a->setDuration(dur);
        a->setEasingCurve(easing);
        return a;
    };
    m_hoverFgAnim = makeAnim("hoverFg", kAnimDuration, QEasingCurve::OutCubic);
    m_hoverBgAnim = makeAnim("hoverBg", kAnimDuration, QEasingCurve::OutCubic);
    m_hoverSwapAnim = makeAnim("hoverSwap", kAnimDuration, QEasingCurve::OutCubic);
}

void ColorSlotSwitchWidget::setForegroundColor(const QColor& color)
{
    QColor opaque = color;
    opaque.setAlpha(255);
    if (m_foregroundColor == opaque)
        return;
    m_foregroundColor = opaque;
    update();
}

void ColorSlotSwitchWidget::setBackgroundColor(const QColor& color)
{
    QColor opaque = color;
    opaque.setAlpha(255);
    if (m_backgroundColor == opaque)
        return;
    m_backgroundColor = opaque;
    update();
}

void ColorSlotSwitchWidget::setActiveForeground(bool isForeground)
{
    if (m_activeForeground == isForeground)
        return;
    m_activeForeground = isForeground;
    update();
}

void ColorSlotSwitchWidget::setHoverFg(qreal p)
{
    m_hoverFg = qBound(0.0, p, 1.0);
    update();
}
void ColorSlotSwitchWidget::setHoverBg(qreal p)
{
    m_hoverBg = qBound(0.0, p, 1.0);
    update();
}
void ColorSlotSwitchWidget::setHoverSwap(qreal p)
{
    m_hoverSwap = qBound(0.0, p, 1.0);
    update();
}

QSize ColorSlotSwitchWidget::sizeHint() const
{
    return { 44, 42 };
}
QSize ColorSlotSwitchWidget::minimumSizeHint() const
{
    return sizeHint();
}

QRectF ColorSlotSwitchWidget::foregroundRect() const
{
    const qreal available = qMin(width(), height()) - 2 * kEdgeInset - 10;
    const qreal side = qMax<qreal>(14, available);
    return QRectF(kEdgeInset, kEdgeInset, side, side);
}

QRectF ColorSlotSwitchWidget::backgroundRect() const
{
    const qreal available = qMin(width(), height()) - 2 * kEdgeInset - 10;
    const qreal side = qMax<qreal>(14, available);
    return QRectF(width() - side - kEdgeInset, height() - side - kEdgeInset, side, side);
}

QRectF ColorSlotSwitchWidget::swapButtonRect() const
{
    // Top-right empty corner of the widget (above bg, right of fg).
    const QRectF fg = foregroundRect();
    const QRectF bg = backgroundRect();
    const qreal s = kSwapButtonSize;
    qreal cx = (fg.right() + width()) * 0.5 + kSwapOffsetX;
    qreal cy = (0 + bg.top()) * 0.5 + kSwapOffsetY;
    // Keep the button fully inside the widget.
    cx = qBound<qreal>(s * 0.5, cx, width() - s * 0.5);
    cy = qBound<qreal>(s * 0.5, cy, height() - s * 0.5);
    return QRectF(cx - s * 0.5, cy - s * 0.5, s, s);
}

ColorSlotSwitchWidget::HitTarget ColorSlotSwitchWidget::hitTest(const QPoint& pos) const
{
    if (swapButtonRect().contains(pos))
        return HitTarget::Swap;
    // Foreground is on top — test it first.
    if (foregroundRect().contains(pos))
        return HitTarget::Foreground;
    if (backgroundRect().contains(pos))
        return HitTarget::Background;
    return HitTarget::None;
}

void ColorSlotSwitchWidget::startAnimation(QPropertyAnimation* anim, qreal target)
{
    if (!anim)
        return;
    anim->stop();
    anim->setStartValue(anim->propertyName() == QByteArray("hoverFg") ? m_hoverFg
            : anim->propertyName() == QByteArray("hoverBg")           ? m_hoverBg
                                                                      : m_hoverSwap);
    anim->setEndValue(target);
    anim->start();
}

void ColorSlotSwitchWidget::updateHoverState(HitTarget target)
{
    startAnimation(m_hoverFgAnim, target == HitTarget::Foreground ? 1.0 : 0.0);
    startAnimation(m_hoverBgAnim, target == HitTarget::Background ? 1.0 : 0.0);
    startAnimation(m_hoverSwapAnim, target == HitTarget::Swap ? 1.0 : 0.0);
}

void ColorSlotSwitchWidget::mouseMoveEvent(QMouseEvent* event)
{
    updateHoverState(hitTest(event->pos()));
    QWidget::mouseMoveEvent(event);
}

void ColorSlotSwitchWidget::leaveEvent(QEvent* event)
{
    updateHoverState(HitTarget::None);
    QWidget::leaveEvent(event);
}

void ColorSlotSwitchWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const HitTarget target = hitTest(event->pos());
    switch (target) {
    case HitTarget::Swap:
        emit swapRequested();
        break;
    case HitTarget::Foreground:
        if (!m_activeForeground) {
            m_activeForeground = true;
            emit activeSlotChanged(true);
            update();
        }
        break;
    case HitTarget::Background:
        if (m_activeForeground) {
            m_activeForeground = false;
            emit activeSlotChanged(false);
            update();
        }
        break;
    case HitTarget::None:
        break;
    }
    event->accept();
}

void ColorSlotSwitchWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF fgRect = foregroundRect();
    const QRectF bgRect = backgroundRect();
    const bool fgOnTop = m_activeForeground;

    const QRectF topRect = fgOnTop ? fgRect : bgRect;
    const QRectF bottomRect = fgOnTop ? bgRect : fgRect;
    const QColor topColor = fgOnTop ? m_foregroundColor : m_backgroundColor;
    const QColor bottomColor = fgOnTop ? m_backgroundColor : m_foregroundColor;
    const qreal hoverTop = fgOnTop ? m_hoverFg : m_hoverBg;
    const qreal hoverBottom = fgOnTop ? m_hoverBg : m_hoverFg;

    // --- Bottom swatch (cut by mask of top swatch's outer edge for visual separation) ---
    QPainterPath bottomPath;
    bottomPath.addRoundedRect(bottomRect, kSwatchRadius, kSwatchRadius);

    QPainterPath topMask;
    const QRectF topMaskRect = topRect.adjusted(-kMaskGap, -kMaskGap, kMaskGap, kMaskGap);
    topMask.addRoundedRect(topMaskRect, kSwatchRadius + kMaskGap, kSwatchRadius + kMaskGap);

    QPainterPath bottomVisible = bottomPath.subtracted(topMask);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(bottomColor);
    painter.drawPath(bottomVisible);
    painter.restore();

    // --- Bottom swatch hover outline (clipped so it doesn't appear under top swatch) ---
    auto drawHoverOutline = [&](const QRectF& rect, qreal hover) {
        if (hover < 0.001)
            return;
        QColor c(255, 255, 255, qBound(0, qRound(160 * hover), 255));
        const QRectF outer = rect.adjusted(
            -kHoverOutlineGrow, -kHoverOutlineGrow, kHoverOutlineGrow, kHoverOutlineGrow);
        painter.setPen(QPen(c, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(
            outer, kSwatchRadius + kHoverOutlineGrow, kSwatchRadius + kHoverOutlineGrow);
    };
    if (hoverBottom > 0.001) {
        painter.save();
        QPainterPath clip;
        clip.addRect(rect());
        clip = clip.subtracted(topMask);
        painter.setClipPath(clip);
        drawHoverOutline(bottomRect, hoverBottom);
        painter.restore();
    }

    // --- Top swatch ---
    painter.setPen(Qt::NoPen);
    painter.setBrush(topColor);
    painter.drawRoundedRect(topRect, kSwatchRadius, kSwatchRadius);

    drawHoverOutline(topRect, hoverTop);

    // --- Swap button ---
    const QRectF btnRect = swapButtonRect();
    if (m_hoverSwap > 0.001) {
        QColor plate(255, 255, 255, qBound(0, qRound(50 * m_hoverSwap), 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(plate);
        painter.drawRoundedRect(btnRect, kSwapHoverRadius, kSwapHoverRadius);
    }
    {
        // Icon color: textMuted at rest → text on hover, for visibility on any panel.
        const auto& themeColors = ruwa::ui::core::ThemeManager::instance().colors();
        const QColor iconColor = ruwa::ui::core::ThemeColors::interpolate(
            themeColors.textMuted, themeColors.text, m_hoverSwap);
        const QPixmap base = ruwa::ui::core::IconProvider::instance().getPixmap(
            ruwa::ui::core::IconProvider::StandardIcon::Swap, QSize(kSwapIconSize, kSwapIconSize));
        if (!base.isNull()) {
            QPixmap colored = base;
            QPainter ip(&colored);
            ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
            ip.fillRect(colored.rect(), iconColor);
            ip.end();
            const QPointF iconPos(btnRect.center().x() - kSwapIconSize * 0.5,
                btnRect.center().y() - kSwapIconSize * 0.5);
            painter.drawPixmap(iconPos, colored);
        }
    }
}

} // namespace ruwa::ui::widgets
