// SPDX-License-Identifier: MPL-2.0

// SidebarButton.cpp
#include "SidebarButton.h"
#include "features/theme/manager/ThemeManager.h"

#include <QEasingCurve>
#include <QPainter>

namespace ruwa::ui::widgets {

SidebarButton::SidebarButton(const QString& text, const QIcon& icon, QWidget* parent)
    : BaseStyledWidget("SidebarButton", parent)
{
    setText(text);
    if (!icon.isNull()) {
        setIcon(icon);
    }
    setCheckable(false);
}

void SidebarButton::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    painter.save();
    if (!isEnabled()) {
        painter.setOpacity(0.45);
    }

    auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
    const auto& content = style().content;
    const QMargins padding = contentPadding();
    const int activeShift = mgr.scaled(8);

    QRectF contentRect
        = rect.adjusted(padding.left(), padding.top(), -padding.right(), -padding.bottom());
    contentRect.translate(activeShift * activeProgress(), 0.0);

    const int iconSize = mgr.scaled(content.baseIconSize);
    const int iconTextGap = mgr.scaled(content.baseIconTextGap);
    const QColor textColor = currentTextColor();

    int contentX = qRound(contentRect.left());

    if (content.iconPosition != ruwa::ui::core::IconPosition::None && !icon().isNull()) {
        QRect iconRect;

        switch (content.iconPosition) {
        case ruwa::ui::core::IconPosition::Left:
            iconRect = QRect(contentX, (height() - iconSize) / 2, iconSize, iconSize);
            contentX += iconSize + iconTextGap;
            break;
        case ruwa::ui::core::IconPosition::Right:
            iconRect = QRect(qRound(contentRect.right()) - iconSize, (height() - iconSize) / 2,
                iconSize, iconSize);
            break;
        case ruwa::ui::core::IconPosition::Center:
            iconRect
                = QRect((width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);
            iconRect.translate(activeShift * activeProgress(), 0);
            break;
        default:
            break;
        }

        QPixmap pixmap = icon().pixmap(QSize(iconSize, iconSize));
        if (!pixmap.isNull()) {
            if (content.colorizeIcon) {
                pixmap = colorizeIcon(pixmap, textColor);
            }
            painter.drawPixmap(iconRect, pixmap);
        }
    }

    if (content.iconPosition != ruwa::ui::core::IconPosition::Center && !text().isEmpty()) {
        painter.setPen(textColor);

        QFont font = this->font();
        font.setPointSize(mgr.scaledFontSize(content.baseFontSize));
        if (content.boldWhenActive && activeProgress() > 0.5) {
            font.setBold(true);
        }
        painter.setFont(font);

        QRect textRect;
        Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;

        switch (content.textAlignment) {
        case ruwa::ui::core::ContentAlignment::Left:
            textRect = QRect(contentX, 0, qRound(contentRect.right()) - contentX, height());
            alignment = Qt::AlignLeft | Qt::AlignVCenter;
            break;
        case ruwa::ui::core::ContentAlignment::Center:
            textRect = contentRect.toRect();
            alignment = Qt::AlignCenter;
            break;
        case ruwa::ui::core::ContentAlignment::Right:
            textRect = QRect(contentX, 0, qRound(contentRect.right()) - contentX, height());
            alignment = Qt::AlignRight | Qt::AlignVCenter;
            break;
        }

        painter.drawText(textRect, alignment, text());
    }

    painter.restore();
}

void SidebarButton::drawCustomLayers(QPainter& painter, const QRectF& rect)
{
    const qreal progress = activeProgress();
    if (progress <= 0.001) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    const int insetLeft = theme.scaled(8);
    const qreal pillWidth = qMax(3.0, qreal(theme.scaled(4)));
    constexpr qreal kRevealStart = 0.18;
    const qreal delayedProgress
        = qBound(0.0, (progress - kRevealStart) / (1.0 - kRevealStart), 1.0);
    const qreal reveal = QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(delayedProgress);
    const qreal targetHeight = qreal(theme.scaled(18));
    const qreal pillHeight = targetHeight * reveal;
    const qreal x = rect.left() + insetLeft;
    const qreal y = rect.center().y() - pillHeight / 2.0;
    QColor pillColor = colors.textOnPrimary();
    pillColor.setAlphaF(pillColor.alphaF() * reveal);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pillColor);
    painter.drawRoundedRect(QRectF(x, y, pillWidth, pillHeight), pillWidth / 2.0, pillWidth / 2.0);
    painter.restore();
}

} // namespace ruwa::ui::widgets
