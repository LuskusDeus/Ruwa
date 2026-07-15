// SPDX-License-Identifier: MPL-2.0

// HorizontalSeparator.cpp
#include "HorizontalSeparator.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

namespace ruwa::ui::widgets {

HorizontalSeparator::HorizontalSeparator(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(HEIGHT + MARGIN_VERTICAL * 2);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Make background transparent
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

QSize HorizontalSeparator::sizeHint() const
{
    return QSize(100, HEIGHT + MARGIN_VERTICAL * 2);
}

QSize HorizontalSeparator::minimumSizeHint() const
{
    return QSize(20, HEIGHT + MARGIN_VERTICAL * 2);
}

void HorizontalSeparator::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Calculate separator line position (centered vertically)
    const int x = m_marginLeft;
    const int y = (height() - HEIGHT) / 2;
    const int w = width() - m_marginLeft - m_marginRight;

    if (m_gradientEnabled) {
        // Gradient mode - with antialiasing for smooth fade
        painter.setRenderHint(QPainter::Antialiasing);

        QLinearGradient gradient(x, y + HEIGHT / 2.0, x + w, y + HEIGHT / 2.0);

        // Use border color from theme
        QColor separatorColor = colors.border;
        QColor transparent = separatorColor;
        transparent.setAlpha(0);

        gradient.setColorAt(0.0, transparent);
        gradient.setColorAt(0.2, separatorColor);
        gradient.setColorAt(0.8, separatorColor);
        gradient.setColorAt(1.0, transparent);

        QPainterPath path;
        path.addRoundedRect(QRectF(x, y, w, HEIGHT), 0.5, 0.5);
        painter.fillPath(path, gradient);
    } else {
        // Solid mode - crisp rendering without antialiasing
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.border);

        // Draw simple rectangle for crisp appearance
        painter.drawRect(x, y, w, HEIGHT);
    }
}

} // namespace ruwa::ui::widgets
