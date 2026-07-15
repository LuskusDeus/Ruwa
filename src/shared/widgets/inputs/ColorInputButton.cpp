// SPDX-License-Identifier: MPL-2.0

#include "ColorInputButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_PADDING = 8;
const int BASE_SWATCH_BORDER_OFFSET = 2;
const int BASE_SWATCH_RADIUS = 4; // rounded-square swatch in capsule style
const int BASE_BOX_PAD_V = 7;
const int BASE_BOX_PAD_H = 14;
const int BASE_BORDER_RADIUS = 8;
const int HOVER_ANIMATION_DURATION = 150;
} // namespace

ColorInputButton::ColorInputButton(
    const QString& label, const QColor& initialColor, QWidget* parent)
    : ColorInputButton(label, initialColor, ColorInputButtonOptions {}, parent)
{
}

ColorInputButton::ColorInputButton(const QString& label, const QColor& initialColor,
    const ColorInputButtonOptions& options, QWidget* parent)
    : BaseStyledWidget("PanelButton", parent)
    , m_label(label)
    , m_color(initialColor)
    , m_options(options)
{
    style().background.enabled = false;
    style().border.enabled = false;
    // drawContentLayer paints its own hover plate matching each style's shape
    // (capsule pill / boxed rounded-rect / plain); the base hover/glow/press
    // layers below always use a fixed cornerRadius() rect regardless of that
    // shape, which showed through as a mismatched rectangular glow around the
    // capsule. Disabled here, same as CommandInputWidget (another self-painted
    // BaseStyledWidget).
    style().hover.enabled = false;
    style().hoverGlow.enabled = false;
    style().press.enabled = false;

    updateScaledSize();

    m_hoverAnimation = new QPropertyAnimation(this, "hoverAlpha", this);
    m_hoverAnimation->setDuration(HOVER_ANIMATION_DURATION);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &ColorInputButton::onThemeChanged);
}

void ColorInputButton::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        emit colorChanged(color);
        update();
    }
}

void ColorInputButton::setHoverAlpha(qreal alpha)
{
    if (!qFuzzyCompare(m_hoverAlpha, alpha)) {
        m_hoverAlpha = alpha;
        update();
    }
}

void ColorInputButton::setOptions(const ColorInputButtonOptions& options)
{
    m_options = options;
    updateScaledSize();
    update();
}

void ColorInputButton::clearHover()
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverAlpha);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
}

void ColorInputButton::updateScaledSize()
{
    const auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(m_options.baseHeight));
}

void ColorInputButton::onThemeChanged()
{
    updateScaledSize();
    update();
}

void ColorInputButton::enterEvent(QEnterEvent* event)
{
    BaseStyledWidget::enterEvent(event);
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverAlpha);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
}

void ColorInputButton::leaveEvent(QEvent* event)
{
    BaseStyledWidget::leaveEvent(event);
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverAlpha);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
}

void ColorInputButton::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();
    const bool boxedStyle = m_options.boxedStyle;
    const bool capsuleStyle = m_options.capsuleStyle;
    const bool framed = boxedStyle || capsuleStyle;
    const int padding = framed ? mgr.scaled(BASE_BOX_PAD_H) : mgr.scaled(BASE_PADDING);
    const int padV = framed ? mgr.scaled(BASE_BOX_PAD_V) : padding;

    QRectF contentRect = rect;
    int swatchRadius;

    if (capsuleStyle) {
        // Pill frame matching the Color-panel hex input (HexColorInput):
        // surfaceAlt resting plate, soft surfaceElevated hover, gradient border.
        const qreal pillR = qMax(0.0, contentRect.height() * 0.5 - 0.5);
        const QRectF fillRect = contentRect.adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal fillR = qMax(0.0, pillR - 1.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(isEnabled()
                ? colors.surfaceAlt
                : ThemeColors::withAlpha(colors.surfaceAlt, colors.isDark ? 120 : 160));
        painter.drawRoundedRect(fillRect, fillR, fillR);

        if (isEnabled() && m_hoverAlpha > 0.0) {
            QColor plate = colors.surfaceElevated();
            plate.setAlpha(qBound(0, qRound(m_hoverAlpha * 90), 255));
            painter.setBrush(plate);
            painter.drawRoundedRect(fillRect, fillR, fillR);
        }

        const qreal accent = isEnabled() ? m_hoverAlpha : 0.0;
        const QColor borderTop = isEnabled()
            ? ThemeColors::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), accent)
            : ThemeColors::withAlpha(colors.borderSubtle(), colors.isDark ? 14 : 22);
        const QColor borderBottom = ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2);
        ruwa::ui::painting::drawGradientBorder(
            painter, contentRect.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

        swatchRadius = mgr.scaled(BASE_SWATCH_RADIUS);
    } else if (boxedStyle) {
        const int radius = mgr.scaled(BASE_BORDER_RADIUS);
        if (isEnabled()) {
            painter.setBrush(colors.overlayBase());
        } else {
            QColor dimFill = colors.surface;
            dimFill.setAlpha(colors.isDark ? 72 : 110);
            painter.setBrush(dimFill);
        }
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(contentRect, radius, radius);

        if (isEnabled() && m_hoverAlpha > 0.0) {
            QColor hoverBg = colors.surfaceElevated();
            hoverBg.setAlphaF(m_hoverAlpha);
            painter.setBrush(hoverBg);
            painter.drawRoundedRect(contentRect, radius, radius);
        }

        const QColor borderRest = isEnabled()
            ? ThemeColors::interpolate(
                  colors.borderSubtle(), colors.borderSubtleHover(), m_hoverAlpha)
            : ThemeColors::withAlpha(colors.borderSubtle(), colors.isDark ? 14 : 22);
        QPen borderPen(borderRest, 1.0);
        borderPen.setCosmetic(true);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(
            contentRect.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);

        swatchRadius = radius - BASE_SWATCH_BORDER_OFFSET;
    } else {
        const int radius = cornerRadius();
        if (m_hoverAlpha > 0.0) {
            QColor hoverColor = colors.primary;
            hoverColor.setAlphaF(m_options.hoverStrength * m_hoverAlpha);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hoverColor);
            painter.drawRoundedRect(contentRect, radius, radius);
        }
        swatchRadius = radius - BASE_SWATCH_BORDER_OFFSET;
    }

    const int swatchSize = static_cast<int>(contentRect.height()) - (padV * 2);
    const QRectF swatchRect(
        contentRect.left() + padding, contentRect.top() + padV, swatchSize, swatchSize);

    // Rounded swatch path — used both to clip the transparency checkerboard (so
    // it does not spill past the rounded corners) and to fill/stroke the swatch.
    QPainterPath swatchPath;
    swatchPath.addRoundedRect(swatchRect, swatchRadius, swatchRadius);

    if (m_color.alpha() < 255) {
        const int checkSize = 4;
        painter.save();
        painter.setClipPath(swatchPath);
        for (int y = static_cast<int>(swatchRect.top()); y < static_cast<int>(swatchRect.bottom());
            y += checkSize) {
            for (int x = static_cast<int>(swatchRect.left());
                x < static_cast<int>(swatchRect.right()); x += checkSize) {
                bool isLight = ((x / checkSize) + (y / checkSize)) % 2 == 0;
                painter.fillRect(x, y, checkSize, checkSize,
                    isLight ? QColor(200, 200, 200) : QColor(100, 100, 100));
            }
        }
        painter.restore();
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawPath(swatchPath);

    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(swatchPath);

    QRectF textRect = contentRect.adjusted(swatchSize + (padding * 2), 0, -padding, 0);

    const bool hasLabel = m_options.showLabel && !m_label.isEmpty();
    painter.setPen(currentTextColor());
    QFont textFont = font();
    textFont.setBold(m_options.boldLabel);
    painter.setFont(textFont);
    if (hasLabel) {
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }

    if (m_options.showHex) {
        painter.setPen(currentSecondaryTextColor());
        textFont.setBold(false);
        painter.setFont(textFont);
        QString hexStr = m_color.alpha() < 255 ? m_color.name(QColor::HexArgb).toUpper()
                                               : m_color.name(QColor::HexRgb).toUpper();
        painter.drawText(textRect,
            hasLabel ? (Qt::AlignRight | Qt::AlignVCenter) : (Qt::AlignLeft | Qt::AlignVCenter),
            hexStr);
    }
}

void ColorInputButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        BaseStyledWidget::mouseReleaseEvent(event);
        return;
    }

    BaseStyledWidget::mouseReleaseEvent(event);
    emit colorPickerRequested(m_color);
    event->accept();
}

} // namespace ruwa::ui::widgets
