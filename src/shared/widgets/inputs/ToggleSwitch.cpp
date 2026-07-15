// SPDX-License-Identifier: MPL-2.0

// ToggleSwitch.cpp
#include "ToggleSwitch.h"
#include "shared/style/WidgetStyleManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QEvent>
#include <QPainterPath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_THUMB_PADDING = 3;

qreal adaptiveTrackRadius(const QRectF& rect, qreal requestedRadius)
{
    const qreal maxRadius = qMax<qreal>(1.0, rect.height() * 0.5 - 0.5);
    return qBound<qreal>(1.0, requestedRadius, maxRadius);
}

qreal adaptiveThumbPadding(qreal defaultPadding, const QRectF& rect)
{
    const qreal maxPadding = qMax<qreal>(1.0, rect.height() * 0.22);
    return qBound<qreal>(1.0, defaultPadding, maxPadding);
}

qreal adaptiveInnerInset(qreal defaultInset, const QRectF& rect)
{
    const qreal maxInset = qMax<qreal>(1.0, rect.height() * 0.18);
    return qBound<qreal>(1.0, defaultInset, maxInset);
}

QRectF strokeRect(const QRectF& rect, qreal strokeWidth)
{
    const qreal inset = strokeWidth * 0.5;
    return rect.adjusted(inset, inset, -inset, -inset);
}

void drawTrackFill(QPainter& painter, const QRectF& rect, qreal radius, const QColor& color)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(rect, radius, radius);
}

void drawTrackFill(QPainter& painter, const QRectF& rect, qreal radius, const QBrush& brush)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(brush);
    painter.drawRoundedRect(rect, radius, radius);
}

void drawTrackBorder(
    QPainter& painter, const QRectF& rect, qreal radius, const QColor& top, const QColor& bottom)
{
    const qreal borderWidth = 1.0;
    const QRectF borderRect = strokeRect(rect, borderWidth);

    QLinearGradient gradient(borderRect.topLeft(), borderRect.bottomLeft());
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);

    // Draw border as a filled ring (outer path - inner path) instead of pen stroke.
    // This avoids Qt's common "fatter rounded corners" artifact on thin strokes.
    const qreal outerRadius = radius;
    const QRectF innerRect = rect.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
    const qreal innerRadius = qMax<qreal>(0.0, outerRadius - borderWidth);

    QPainterPath outerPath;
    outerPath.addRoundedRect(rect, outerRadius, outerRadius);

    QPainterPath innerPath;
    if (innerRect.width() > 0.0 && innerRect.height() > 0.0) {
        innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
    }

    const QPainterPath borderRing = outerPath.subtracted(innerPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawPath(borderRing);
}

QColor disabledColor(const QColor& base, qreal alphaFactor)
{
    QColor muted = base;
    muted.setAlphaF(muted.alphaF() * qBound<qreal>(0.0, alphaFactor, 1.0));
    return muted;
}

/// Opaque RGB blend: \a tint with alpha treated as layer over opaque \a base (standard “over”).
QColor tintOverBase(const QColor& base, const QColor& tint, int tintAlpha255)
{
    const qreal a = qBound(0, tintAlpha255, 255) / 255.0;
    const qreal inv = 1.0 - a;
    return QColor(qBound(0, qRound(inv * base.red() + a * tint.red()), 255),
        qBound(0, qRound(inv * base.green() + a * tint.green()), 255),
        qBound(0, qRound(inv * base.blue() + a * tint.blue()), 255));
}
} // namespace

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : ToggleSwitch(false, parent)
{
}

ToggleSwitch::ToggleSwitch(bool initialState, QWidget* parent)
    : ToggleSwitch(InitOptions { initialState, true }, parent)
{
}

ToggleSwitch::ToggleSwitch(const InitOptions& options, QWidget* parent)
    : BaseStyledWidget("ToggleSwitch", parent)
{
    setCheckable(true);
    setCursor(options.enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setupThumbAnimation();

    // Set initial state without animation
    m_thumbPosition = options.checked ? 1.0 : 0.0;
    if (options.checked) {
        setActive(true, false);
    }

    setEnabled(options.enabled);
}

void ToggleSwitch::setupThumbAnimation()
{
    m_thumbAnimation = new QPropertyAnimation(this, "thumbPosition", this);
    m_thumbAnimation->setDuration(200);
    m_thumbAnimation->setEasingCurve(QEasingCurve::InOutCubic);
}

void ToggleSwitch::setChecked(bool checked, TransitionMode mode)
{
    const bool animateChrome = (mode == TransitionMode::Animated);

    if (isActive() == checked) {
        if (mode == TransitionMode::Instant) {
            setActive(checked, false);
            animateThumb(checked, mode);
        }
        return;
    }

    setActive(checked, animateChrome);
    animateThumb(checked, mode);
    emit toggled(checked);
}

void ToggleSwitch::toggle()
{
    setChecked(!isChecked());
}

void ToggleSwitch::setThumbPosition(qreal position)
{
    if (qFuzzyCompare(m_thumbPosition, position))
        return;
    m_thumbPosition = position;
    update();
}

void ToggleSwitch::animateThumb(bool checked, TransitionMode mode)
{
    auto& mgr = WidgetStyleManager::instance();

    if (mode == TransitionMode::Instant || !mgr.animationsEnabled()) {
        m_thumbPosition = checked ? 1.0 : 0.0;
        update();
        return;
    }

    m_thumbAnimation->stop();
    m_thumbAnimation->setStartValue(m_thumbPosition);
    m_thumbAnimation->setEndValue(checked ? 1.0 : 0.0);
    m_thumbAnimation->start();
}

void ToggleSwitch::mousePressEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        toggle();
        event->accept();
        return;
    }
    BaseStyledWidget::mousePressEvent(event);
}

void ToggleSwitch::changeEvent(QEvent* event)
{
    if (event && event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    BaseStyledWidget::changeEvent(event);
}

void ToggleSwitch::drawBackgroundLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();
    const qreal radius = adaptiveTrackRadius(rect, cornerRadius());

    QColor trackOff = colors.overlayBase();
    if (!isEnabled()) {
        // Muted flat track — avoid warm primary tint (reads too “active” when disabled).
        trackOff = ThemeColors::adjustBrightness(colors.border, colors.isDark ? 0.42 : 0.92);
    }

    drawTrackFill(painter, rect, radius, trackOff);
}

void ToggleSwitch::drawBorderLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();
    const qreal radius = adaptiveTrackRadius(rect, cornerRadius());

    QColor top = colors.borderSubtle();
    QColor bottom = colors.borderSubtle();
    bottom.setAlpha(bottom.alpha() / 2);

    if (!isEnabled()) {
        top = disabledColor(top, 0.38);
        bottom = disabledColor(bottom, 0.38);
    }

    drawTrackBorder(painter, rect, radius, top, bottom);
}

void ToggleSwitch::drawHoverLayer(QPainter& painter, const QRectF& rect)
{
    if (!isEnabled() || hoverProgress() <= 0.0) {
        return;
    }

    auto& mgr = WidgetStyleManager::instance();
    if (!mgr.hoverEffectsEnabled()) {
        return;
    }

    const qreal radius = adaptiveTrackRadius(rect, cornerRadius());
    QColor hover = mgr.colors().overlayHover();
    hover.setAlphaF(hover.alphaF() * hoverProgress());
    drawTrackFill(painter, rect, radius, hover);
}

void ToggleSwitch::drawActiveBackgroundLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();
    const qreal outerRadius = adaptiveTrackRadius(rect, cornerRadius());
    const qreal inset = adaptiveInnerInset(mgr.scaled(1.8), rect);
    const QRectF innerRect = rect.adjusted(inset, inset, -inset, -inset);
    if (innerRect.width() <= 0.0 || innerRect.height() <= 0.0) {
        return;
    }

    const qreal innerRadius = qMax<qreal>(1.0, outerRadius - inset);
    QColor top = colors.primary;
    QColor bottom = colors.primaryHover();

    if (isEnabled()) {
        if (hoverProgress() > 0.0) {
            const qreal boost = 1.0 + (1.05 - 1.0) * hoverProgress();
            top = ThemeColors::adjustBrightness(top, boost);
            bottom = ThemeColors::adjustBrightness(bottom, boost);
        }
    } else {
        // Disabled + on: neutral base + semi-transparent primary (readable “on” without full
        // accent).
        const qreal lo = colors.isDark ? 0.50 : 0.88;
        const qreal hi = colors.isDark ? 0.64 : 0.94;
        const QColor baseTop = ThemeColors::adjustBrightness(colors.border, lo);
        const QColor baseBottom = ThemeColors::adjustBrightness(colors.border, hi);
        const int aTop = colors.isDark ? 92 : 105;
        const int aBottom = colors.isDark ? 76 : 92;
        top = tintOverBase(baseTop, colors.primary, aTop);
        bottom = tintOverBase(baseBottom, colors.primaryHover(), aBottom);
    }

    QLinearGradient fillGradient(innerRect.topLeft(), innerRect.bottomLeft());
    fillGradient.setColorAt(0.0, top);
    fillGradient.setColorAt(1.0, bottom);

    drawTrackFill(painter, innerRect, innerRadius, fillGradient);
}

void ToggleSwitch::drawActiveBorderLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();
    const qreal outerRadius = adaptiveTrackRadius(rect, cornerRadius());
    const qreal inset = adaptiveInnerInset(mgr.scaled(1.8), rect);
    const QRectF innerRect = rect.adjusted(inset, inset, -inset, -inset);
    if (innerRect.width() <= 0.0 || innerRect.height() <= 0.0) {
        return;
    }
    const qreal innerRadius = qMax<qreal>(1.0, outerRadius - inset);

    QColor top;
    QColor bottom;
    if (isEnabled()) {
        top = ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.20 : 0.95);
        bottom = ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 0.82 : 0.72);
        top.setAlpha(130);
        bottom.setAlpha(110);
    } else {
        top = ThemeColors::adjustBrightness(colors.borderLight(), colors.isDark ? 0.55 : 0.88);
        bottom = ThemeColors::adjustBrightness(colors.border, colors.isDark ? 0.45 : 0.92);
        top.setAlpha(55);
        bottom.setAlpha(45);
    }

    drawTrackBorder(painter, innerRect, innerRadius, top, bottom);
}

void ToggleSwitch::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    const qreal basePadding = qMax<qreal>(1.0, mgr.scaled(BASE_THUMB_PADDING));
    const qreal thumbPadding = adaptiveThumbPadding(basePadding, rect);
    const qreal availableW = qMax<qreal>(0.0, rect.width() - thumbPadding * 2.0);
    const qreal availableH = qMax<qreal>(0.0, rect.height() - thumbPadding * 2.0);
    const qreal thumbSize = qMax<qreal>(4.0, qMin(availableW, availableH));

    // Calculate thumb position
    const qreal thumbY = rect.y() + (rect.height() - thumbSize) * 0.5;
    const qreal thumbXOff = rect.x() + thumbPadding;
    const qreal thumbXOn = rect.right() - thumbSize - thumbPadding;
    const qreal thumbX = thumbXOff + (thumbXOn - thumbXOff) * m_thumbPosition;

    QRectF thumbRect(thumbX, thumbY, thumbSize, thumbSize);

    // Thumb color - inverted based on switch state
    // OFF: light thumb (white on dark theme, dark on light theme)
    // ON: dark thumb (dark on dark theme, light on light theme)
    QColor thumbOff = colors.isDark ? QColor(255, 255, 255) : QColor(40, 40, 40);
    QColor thumbOn = colors.isDark ? QColor(40, 40, 40) : QColor(255, 255, 255);
    QColor thumbColor = ThemeColors::interpolate(thumbOff, thumbOn, m_thumbPosition);
    if (!isEnabled()) {
        thumbColor = ThemeColors::interpolate(colors.textDisabled(), thumbColor, 0.68);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(thumbColor);
    painter.drawEllipse(thumbRect);
}

} // namespace ruwa::ui::widgets
