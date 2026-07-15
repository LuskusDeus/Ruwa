// SPDX-License-Identifier: MPL-2.0

// AnimatedButton.cpp
#include "AnimatedButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QPainter>
#include <QPainterPath>

namespace ruwa::ui::widgets {

namespace {
const int BASE_MIN_WIDTH = 80;
const int BASE_HEIGHT = 36;
const int BASE_BORDER_RADIUS = 6;
const int BASE_FONT_SIZE = 9;
} // namespace

AnimatedButton::AnimatedButton(const QString& text, QWidget* parent)
    : BaseAnimatedButton(parent)
{
    setText(text);
    setCheckable(false);
    setCursor(Qt::PointingHandCursor);

    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &AnimatedButton::onThemeChanged);
}

void AnimatedButton::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    setMinimumWidth(theme.scaled(BASE_MIN_WIDTH));
    setFixedHeight(theme.scaled(BASE_HEIGHT));

    QFont font = this->font();
    font.setPointSize(theme.scaledFontSize(BASE_FONT_SIZE));
    font.setWeight(QFont::Medium);
    setFont(font);
}

void AnimatedButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);

    const int borderRadius = theme.scaled(BASE_BORDER_RADIUS);

    // Background
    QColor bgColor = colors.primary;
    if (hoverProgress() > 0) {
        bgColor = ruwa::ui::core::ThemeColors::adjustBrightness(
            colors.primary, 1.0 + hoverProgress() * 0.08);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect, borderRadius, borderRadius);

    // Border
    ruwa::ui::painting::drawGradientBorder(painter, rect, borderRadius,
        ruwa::ui::core::ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.2 : 0.9),
        ruwa::ui::core::ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.1 : 0.95));

    // Press effect
    if (isPressed()) {
        painter.setPen(Qt::NoPen);
        QColor pressOverlay = colors.shadow(25);
        painter.setBrush(pressOverlay);
        painter.drawRoundedRect(rect, borderRadius, borderRadius);
    }

    // Text
    painter.setPen(colors.textOnPrimary());
    painter.setFont(font());
    painter.drawText(rect, Qt::AlignCenter, text());
}

void AnimatedButton::onThemeChanged()
{
    updateScaledSizes();
    update();
}

} // namespace ruwa::ui::widgets
