// SPDX-License-Identifier: MPL-2.0

// DockPanelCloseButton.cpp
#include "DockPanelCloseButton.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtGlobal>

namespace ruwa::ui::docking {

namespace {

constexpr int kBaseButtonSize = 15;
constexpr int kBaseIconSize = 9;
constexpr qreal kBaseCornerRadius = 3.0;

} // namespace

DockPanelCloseButton::DockPanelCloseButton(QWidget* parent)
    : BaseAnimatedButton(parent)
{
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"));
    setHoverDuration(150);

    applyTheme();
}

void DockPanelCloseButton::applyTheme(int maxExtent)
{
    int size = ruwa::ui::core::ThemeManager::instance().scaled(kBaseButtonSize);
    if (maxExtent > 0) {
        size = qMin(size, maxExtent);
    }
    setFixedSize(size, size);
    update();
}

void DockPanelCloseButton::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    const qreal hot = qBound(0.0, qMax(hoverProgress(), isPressed() ? 1.0 : 0.0), 1.0);

    if (hot > 0.001) {
        const qreal radius = theme.scaled(kBaseCornerRadius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.overlay((isPressed() ? 0.14 : 0.09) * hot));
        painter.drawRoundedRect(QRectF(rect()), radius, radius);
    }

    const QColor iconColor
        = ruwa::ui::core::ThemeColors::interpolate(colors.textMuted, colors.text, hot);

    const int iconSize = qMax(1, qMin(theme.scaled(kBaseIconSize), width() - 4));
    const QRect iconRect((width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);
    ruwa::ui::core::IconProvider::instance()
        .getColoredIcon(ruwa::ui::core::IconProvider::StandardIcon::Close, iconColor)
        .paint(&painter, iconRect);
}

} // namespace ruwa::ui::docking
