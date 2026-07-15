// SPDX-License-Identifier: MPL-2.0

// CustomThemesNavigatorWidget.cpp
#include "CustomThemesNavigatorWidget.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeManager.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QtMath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
// Layout constants (base values, will be scaled)
const int BASE_PADDING = 6;
const int BASE_BOTTOM_PADDING = 20;
const int BASE_NAME_HEIGHT = 14;
const int BASE_NAME_MARGIN_BOTTOM = 16;
const int BASE_NAME_RIGHT_PADDING = 6;

// Content area styling
const int BASE_CONTENT_RADIUS = 3;

// Grid constants
const int BASE_CELL_SIZE = 16;
const int BASE_CELL_SPACING = 4;
const int BASE_CELL_RADIUS = 2;
const qreal BASE_BORDER_WIDTH = 1.2;

// Plus icon constants
const qreal BASE_PLUS_STROKE = 1.5;
const int BASE_PLUS_MARGIN = 4;
} // namespace

CustomThemesNavigatorWidget::CustomThemesNavigatorWidget(QWidget* parent)
    : BaseStyledWidget("ThemePreview", parent)
{
    style().metrics.fixedHeight = false;
    applyStyleChanges();
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

QSize CustomThemesNavigatorWidget::sizeHint() const
{
    return previewSizeHint();
}

QSize CustomThemesNavigatorWidget::minimumSizeHint() const
{
    return previewSizeHint();
}

void CustomThemesNavigatorWidget::changeEvent(QEvent* event)
{
    BaseStyledWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        update();
    }
}

QSize CustomThemesNavigatorWidget::previewSizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();

    const int padding = mgr.scaled(BASE_PADDING);
    const int cellSize = mgr.scaled(BASE_CELL_SIZE);
    const int cellSpacing = mgr.scaled(BASE_CELL_SPACING);

    QFont nameFont = font();
    nameFont.setPointSize(mgr.scaledFontSize(8));
    const QFontMetrics nameMetrics(nameFont);
    const int nameHeight = qMax(mgr.scaled(BASE_NAME_HEIGHT), nameMetrics.height());
    const int nameMarginBottom = mgr.scaled(BASE_NAME_MARGIN_BOTTOM);
    const int nameBottomInset = qMax(padding, nameMarginBottom - nameHeight);
    const int bottomPadding = nameHeight + nameBottomInset + padding;

    const int gridHeight = cellSize * 2 + cellSpacing;
    const int width = mgr.scaled(style().metrics.baseWidth);
    const int baseHeight = mgr.scaled(style().metrics.baseHeight);
    const int contentHeight
        = padding + gridHeight + bottomPadding + qCeil(style().activeBorder.width) + 1;

    return QSize(width, qMax(baseHeight, contentHeight));
}

void CustomThemesNavigatorWidget::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = ThemeManager::instance().colors();

    const int padding = mgr.scaled(BASE_PADDING);
    const int contentRadius = mgr.scaled(BASE_CONTENT_RADIUS);
    const int nameRightPadding = mgr.scaled(BASE_NAME_RIGHT_PADDING);

    QFont nameFont = font();
    nameFont.setPointSize(mgr.scaledFontSize(8));
    const QFontMetrics nameMetrics(nameFont);
    const int nameHeight = qMax(mgr.scaled(BASE_NAME_HEIGHT), nameMetrics.height());
    const int nameMarginBottom = mgr.scaled(BASE_NAME_MARGIN_BOTTOM);
    const int nameBottomInset = qMax(padding, nameMarginBottom - nameHeight);
    const int bottomPadding = nameHeight + nameBottomInset + padding;

    // Calculate content area
    QRectF contentRect = rect.adjusted(padding, padding, -padding, -bottomPadding);

    painter.setRenderHint(QPainter::Antialiasing);

    // === Draw content background ===
    QColor contentBg = ThemeColors::adjustBrightness(colors.surface, 0.85);
    painter.setPen(Qt::NoPen);
    painter.setBrush(contentBg);
    painter.drawRoundedRect(contentRect, contentRadius, contentRadius);

    // === Draw 2x2 grid of cells ===
    const int cellSize = mgr.scaled(BASE_CELL_SIZE);
    const int cellSpacing = mgr.scaled(BASE_CELL_SPACING);
    const int cellRadius = mgr.scaled(BASE_CELL_RADIUS);
    const qreal borderWidth = mgr.scaled(BASE_BORDER_WIDTH);

    // Calculate grid total size and center it
    int gridWidth = cellSize * 2 + cellSpacing;
    int gridHeight = cellSize * 2 + cellSpacing;

    qreal gridLeft = contentRect.center().x() - gridWidth / 2.0;
    qreal gridTop = contentRect.center().y() - gridHeight / 2.0;

    // Border color with hover interpolation
    QColor borderColor
        = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress() * 0.5);
    borderColor = ThemeColors::withAlpha(borderColor, 80 + static_cast<int>(hoverProgress() * 50));

    // Draw 4 cells (2x2 grid)
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            qreal x = gridLeft + col * (cellSize + cellSpacing);
            qreal y = gridTop + row * (cellSize + cellSpacing);
            QRectF cellRect(x, y, cellSize, cellSize);

            bool isFirstCell = (row == 0 && col == 0);
            bool isLastCell = (row == 1 && col == 1);

            if (isLastCell) {
                // Last cell: no border, only "+" icon
                const qreal plusStroke = mgr.scaled(BASE_PLUS_STROKE);
                const int plusMargin = mgr.scaled(BASE_PLUS_MARGIN);

                QPointF cellCenter = cellRect.center();
                qreal plusLen = (cellSize - plusMargin * 2) / 2.0;

                // Plus color - brighter on hover
                QColor plusColor
                    = ThemeColors::interpolate(colors.textMuted, colors.primary, hoverProgress());

                painter.setPen(QPen(plusColor, plusStroke, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(cellCenter.x() - plusLen, cellCenter.y()),
                    QPointF(cellCenter.x() + plusLen, cellCenter.y()));
                painter.drawLine(QPointF(cellCenter.x(), cellCenter.y() - plusLen),
                    QPointF(cellCenter.x(), cellCenter.y() + plusLen));
            } else if (isFirstCell) {
                // First cell: filled
                painter.setPen(Qt::NoPen);
                painter.setBrush(borderColor);
                painter.drawRoundedRect(cellRect, cellRadius, cellRadius);
            } else {
                // Other cells: border only
                painter.setPen(QPen(borderColor, borderWidth));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(cellRect, cellRadius, cellRadius);
            }
        }
    }

    // === Draw "Custom Themes" text at bottom ===
    QRectF nameRect(rect.x() + padding, rect.bottom() - nameBottomInset - nameHeight,
        rect.width() - padding - nameRightPadding, nameHeight);

    // Text color interpolates based on hover
    QColor textColor = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());

    painter.setPen(textColor);
    painter.setFont(nameFont);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, tr("Custom Themes"));
}

void CustomThemesNavigatorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
    }
    BaseStyledWidget::mousePressEvent(event);
}

void CustomThemesNavigatorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        if (rect().contains(event->pos())) {
            emit clicked();
        }
    }
    BaseStyledWidget::mouseReleaseEvent(event);
}

} // namespace ruwa::ui::widgets
