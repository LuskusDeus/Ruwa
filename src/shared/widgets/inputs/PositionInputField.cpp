// SPDX-License-Identifier: MPL-2.0

#include "PositionInputField.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_PADDING_H = 14;
const int HOVER_ANIMATION_DURATION = 150;
} // namespace

PositionInputField::PositionInputField(QWidget* parent)
    : BaseStyledWidget("PanelButton", parent)
{
    // Same self-painted-capsule contract as ColorInputButton: the base
    // hover/glow/press layers always use a rectangular cornerRadius() rect,
    // which would show through the pill shape drawn here.
    style().background.enabled = false;
    style().border.enabled = false;
    style().hover.enabled = false;
    style().hoverGlow.enabled = false;
    style().press.enabled = false;

    m_hoverAnimation = new QPropertyAnimation(this, "hoverAlpha", this);
    m_hoverAnimation->setDuration(HOVER_ANIMATION_DURATION);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &PositionInputField::onThemeChanged);
}

void PositionInputField::setPosition(const QPointF& pos)
{
    if (m_position != pos) {
        m_position = pos;
        emit positionChanged(pos);
        update();
    }
}

void PositionInputField::setDecimals(int decimals)
{
    m_decimals = qMax(0, decimals);
    update();
}

void PositionInputField::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        update();
    }
}

void PositionInputField::setHoverAlpha(qreal alpha)
{
    if (!qFuzzyCompare(m_hoverAlpha, alpha)) {
        m_hoverAlpha = alpha;
        update();
    }
}

void PositionInputField::onThemeChanged()
{
    update();
}

void PositionInputField::enterEvent(QEnterEvent* event)
{
    BaseStyledWidget::enterEvent(event);
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverAlpha);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
}

void PositionInputField::leaveEvent(QEvent* event)
{
    BaseStyledWidget::leaveEvent(event);
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverAlpha);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
}

void PositionInputField::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    // Pill frame matching the Color-panel hex input / ColorInputButton's
    // capsuleStyle: surfaceAlt resting plate, soft surfaceElevated hover,
    // gradient border — tinted with the accent colour while a pick is pending.
    const qreal pillR = qMax(0.0, rect.height() * 0.5 - 0.5);
    const QRectF fillRect = rect.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillR = qMax(0.0, pillR - 1.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(isEnabled()
            ? colors.surfaceAlt
            : ThemeColors::withAlpha(colors.surfaceAlt, colors.isDark ? 120 : 160));
    painter.drawRoundedRect(fillRect, fillR, fillR);

    if (isEnabled() && m_active) {
        QColor plate = colors.primary;
        plate.setAlpha(36);
        painter.setBrush(plate);
        painter.drawRoundedRect(fillRect, fillR, fillR);
    } else if (isEnabled() && m_hoverAlpha > 0.0) {
        QColor plate = colors.surfaceElevated();
        plate.setAlpha(qBound(0, qRound(m_hoverAlpha * 90), 255));
        painter.setBrush(plate);
        painter.drawRoundedRect(fillRect, fillR, fillR);
    }

    const qreal accent = isEnabled() ? m_hoverAlpha : 0.0;
    QColor borderTop = isEnabled()
        ? ThemeColors::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), accent)
        : ThemeColors::withAlpha(colors.borderSubtle(), colors.isDark ? 14 : 22);
    if (isEnabled() && m_active) {
        borderTop = colors.primary;
    }
    const QColor borderBottom = ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(
        painter, rect.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

    const int padding = mgr.scaled(BASE_PADDING_H);
    QRectF textRect = rect.adjusted(padding, 0, -padding, 0);

    const QString xText = QStringLiteral("X: %1").arg(m_position.x(), 0, 'f', m_decimals);
    const QString yText = QStringLiteral("Y: %1").arg(m_position.y(), 0, 'f', m_decimals);

    painter.setPen(currentTextColor());
    painter.setFont(font());
    const QString combined = xText + QStringLiteral("   ") + yText;
    painter.drawText(textRect, Qt::AlignCenter, combined);
}

void PositionInputField::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        BaseStyledWidget::mouseReleaseEvent(event);
        return;
    }

    BaseStyledWidget::mouseReleaseEvent(event);
    emit pickRequested();
    event->accept();
}

} // namespace ruwa::ui::widgets
