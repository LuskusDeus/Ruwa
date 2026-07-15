// SPDX-License-Identifier: MPL-2.0

#include "MenuButton.h"
#include "features/theme/manager/ThemeManager.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QPainter>

namespace ruwa::ui::widgets {

MenuButton::MenuButton(const QString& text, QWidget* parent)
    : BaseAnimatedButton(parent)
{
    QPushButton::setText(text);
    setCheckable(false);
    setFocusPolicy(Qt::NoFocus);
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
    setHoverDuration(140);
    setActiveDuration(140);
    updateMetrics();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &MenuButton::onThemeChanged);
}

void MenuButton::setPopup(QWidget* popup)
{
    m_popup = popup;
}

void MenuButton::setMenuActive(bool active)
{
    if (m_menuActive == active) {
        return;
    }
    m_menuActive = active;
    setActive(active);
    update();
}

void MenuButton::setText(const QString& text)
{
    if (QPushButton::text() == text) {
        return;
    }
    QPushButton::setText(text);
    updateMetrics();
    update();
}

void MenuButton::setBackgroundInsets(int horizontal, int vertical)
{
    m_baseHorizontalInset = qMax(0, horizontal);
    m_baseVerticalInset = qMax(0, vertical);
    update();
}

void MenuButton::enterEvent(QEnterEvent* event)
{
    BaseAnimatedButton::enterEvent(event);
    emit hoverEntered();
}

void MenuButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int v = theme.scaled(m_baseVerticalInset);
    const int h = theme.scaled(m_baseHorizontalInset);
    const QRectF bgRect = QRectF(rect()).adjusted(h, v, -h, -v);
    const qreal bgRad = qMax(2.0, qMin(4.0, qMin(bgRect.width(), bgRect.height()) / 2.0 - 0.5));

    painter.setPen(Qt::NoPen);
    if (m_menuActive) {
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(bgRect, bgRad, bgRad);
    } else if (isPressed()) {
        painter.setBrush(colors.overlay(0.10));
        painter.drawRoundedRect(bgRect, bgRad, bgRad);
    } else if (hoverProgress() > 0.001) {
        QColor hover = colors.overlay(0.06);
        hover.setAlphaF(hover.alphaF() * hoverProgress());
        painter.setBrush(hover);
        painter.drawRoundedRect(bgRect, bgRad, bgRad);
    }

    const QColor textColor
        = (m_menuActive || hoverProgress() > 0.001) ? colors.text : colors.textMuted;
    painter.setPen(textColor);

    QFont textFont = font();
    textFont.setWeight(m_menuActive ? QFont::DemiBold : QFont::Medium);
    painter.setFont(textFont);
    painter.drawText(rect(), Qt::AlignCenter, QPushButton::text());
}

void MenuButton::updateMetrics()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    QFont textFont = font();
    textFont.setPointSize(theme.scaledFontSize(9));
    setFont(textFont);

    setFixedHeight(theme.scaled(28));
    setMinimumWidth(theme.scaled(50));

    QFontMetrics fm(textFont);
    setFixedWidth(fm.horizontalAdvance(QPushButton::text()) + theme.scaled(24));
    updateGeometry();
}

void MenuButton::onThemeChanged()
{
    updateMetrics();
    update();
}

} // namespace ruwa::ui::widgets
