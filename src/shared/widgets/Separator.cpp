// SPDX-License-Identifier: MPL-2.0

// Separator.cpp
#include "Separator.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

namespace ruwa::ui::widgets {

Separator::Separator(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(WIDTH + PADDING); // 6px total width for compact design
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Make background transparent
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    // Set minimum height
    setMinimumHeight(20);
}

QSize Separator::sizeHint() const
{
    return QSize(WIDTH + PADDING, 100);
}

QSize Separator::minimumSizeHint() const
{
    return QSize(WIDTH + PADDING, 20);
}

void Separator::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Don't fill background - keep it transparent

    // Calculate separator line position (centered)
    const int x = (width() - WIDTH) / 2;
    const int y = MARGIN;
    const int h = height() - (MARGIN * 2);

    if (m_gradientEnabled) {
        // Gradient mode - with antialiasing for smooth fade
        painter.setRenderHint(QPainter::Antialiasing);

        QLinearGradient gradient(x + WIDTH / 2.0, y, x + WIDTH / 2.0, y + h);

        // Use separator color from theme (lighter than borderDark)
        QColor separatorColor = colors.border;
        QColor transparent = separatorColor;
        transparent.setAlpha(0);

        gradient.setColorAt(0.0, transparent);
        gradient.setColorAt(0.2, separatorColor);
        gradient.setColorAt(0.8, separatorColor);
        gradient.setColorAt(1.0, transparent);

        QPainterPath path;
        path.addRoundedRect(QRectF(x, y, WIDTH, h), 1.0, 1.0);
        painter.fillPath(path, gradient);
    } else {
        // Solid mode - crisp rendering without antialiasing
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.border); // Use border color

        // Draw simple rectangle for crisp appearance
        painter.drawRect(x, y, WIDTH, h);
    }
}

} // namespace ruwa::ui::widgets
