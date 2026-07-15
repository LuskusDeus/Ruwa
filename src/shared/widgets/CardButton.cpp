// SPDX-License-Identifier: MPL-2.0

#include "CardButton.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>

namespace ruwa::ui::widgets {

CardButton::CardButton(LayoutVariant variant, QWidget* parent)
    : BaseAnimatedButton(parent)
    , m_layoutVariant(variant)
{
    setCheckable(false);
    setCursor(Qt::PointingHandCursor);
    setBaseCornerRadius(variant == LayoutVariant::Card ? 10 : 6);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() { update(); });
}

void CardButton::setBaseCornerRadius(int radius)
{
    m_baseCornerRadius = qMax(0, radius);
    update();
}

void CardButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
    drawButtonChrome(painter, rect);
    drawCardContent(painter, rect);
}

void CardButton::drawCardContent(QPainter& painter, const QRectF& rect)
{
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

void CardButton::drawButtonChrome(QPainter& painter, const QRectF& rect)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int borderRadius = theme.scaled(m_baseCornerRadius);
    const qreal hp = hoverProgress();

    if (hp > 0.001) {
        QColor plate = colors.surfaceElevated();
        plate.setAlpha(qBound(0, qRound(hp * m_hoverPlateAlpha), 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(plate);
        painter.drawRoundedRect(rect, borderRadius, borderRadius);
    }

    if (isPressed()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.shadow(m_pressShadowAlpha));
        painter.drawRoundedRect(rect, borderRadius, borderRadius);
    }
}

QColor CardButton::currentPrimaryTextColor() const
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    return ruwa::ui::core::ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());
}

} // namespace ruwa::ui::widgets
