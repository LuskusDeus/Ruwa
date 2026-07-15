// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerAddImageWidget.cpp
#include "WelcomeBannerAddImageWidget.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_PADDING = 4;
/// Bottom band for "Add image" label (tile matches preview height; grid uses the rest)
const int BASE_LABEL_BAND = 16;
const int BASE_CONTENT_RADIUS = 3;
const int BASE_CELL_SIZE = 16;
const int BASE_CELL_SPACING = 4;
const int BASE_CELL_RADIUS = 2;
const qreal BASE_BORDER_WIDTH = 1.2;
const qreal BASE_PLUS_STROKE = 1.5;
const int BASE_PLUS_MARGIN = 4;
} // namespace

WelcomeBannerAddImageWidget::WelcomeBannerAddImageWidget(QWidget* parent)
    : BaseStyledWidget(QStringLiteral("WelcomeBannerPreview"), parent)
{
    setCursor(Qt::PointingHandCursor);
}

QSize WelcomeBannerAddImageWidget::sizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();
    return QSize(mgr.scaled(style().metrics.baseWidth), mgr.scaled(style().metrics.baseHeight));
}

void WelcomeBannerAddImageWidget::changeEvent(QEvent* event)
{
    BaseStyledWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        update();
    }
}

void WelcomeBannerAddImageWidget::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = ThemeManager::instance().colors();

    const int padding = mgr.scaled(BASE_PADDING);
    const int labelBand = mgr.scaled(BASE_LABEL_BAND);
    const int contentRadius = mgr.scaled(BASE_CONTENT_RADIUS);

    QRectF contentRect = rect.adjusted(padding, padding, -padding, -(padding + labelBand));

    painter.setRenderHint(QPainter::Antialiasing);

    QColor contentBg = ThemeColors::adjustBrightness(colors.surface, 0.85);
    painter.setPen(Qt::NoPen);
    painter.setBrush(contentBg);
    painter.drawRoundedRect(contentRect, contentRadius, contentRadius);

    const int cellSize = mgr.scaled(BASE_CELL_SIZE);
    const int cellSpacing = mgr.scaled(BASE_CELL_SPACING);
    const int cellRadius = mgr.scaled(BASE_CELL_RADIUS);
    const qreal borderWidth = mgr.scaled(BASE_BORDER_WIDTH);

    int gridWidth = cellSize * 2 + cellSpacing;
    int gridHeight = cellSize * 2 + cellSpacing;

    qreal gridLeft = contentRect.center().x() - gridWidth / 2.0;
    qreal gridTop = contentRect.center().y() - gridHeight / 2.0;

    QColor borderColor
        = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress() * 0.5);
    borderColor = ThemeColors::withAlpha(borderColor, 80 + static_cast<int>(hoverProgress() * 50));

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            qreal x = gridLeft + col * (cellSize + cellSpacing);
            qreal y = gridTop + row * (cellSize + cellSpacing);
            QRectF cellRect(x, y, cellSize, cellSize);

            bool isFirstCell = (row == 0 && col == 0);
            bool isLastCell = (row == 1 && col == 1);

            if (isLastCell) {
                const qreal plusStroke = mgr.scaled(BASE_PLUS_STROKE);
                const int plusMargin = mgr.scaled(BASE_PLUS_MARGIN);

                QPointF cellCenter = cellRect.center();
                qreal plusLen = (cellSize - plusMargin * 2) / 2.0;

                QColor plusColor
                    = ThemeColors::interpolate(colors.textMuted, colors.primary, hoverProgress());

                painter.setPen(QPen(plusColor, plusStroke, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(cellCenter.x() - plusLen, cellCenter.y()),
                    QPointF(cellCenter.x() + plusLen, cellCenter.y()));
                painter.drawLine(QPointF(cellCenter.x(), cellCenter.y() - plusLen),
                    QPointF(cellCenter.x(), cellCenter.y() + plusLen));
            } else if (isFirstCell) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(borderColor);
                painter.drawRoundedRect(cellRect, cellRadius, cellRadius);
            } else {
                painter.setPen(QPen(borderColor, borderWidth));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(cellRect, cellRadius, cellRadius);
            }
        }
    }

    QRectF nameRect(rect.x() + padding, rect.bottom() - padding - labelBand,
        rect.width() - 2 * padding, labelBand);

    QColor textColor = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());

    painter.setPen(textColor);
    QFont nameFont = font();
    nameFont.setPointSize(mgr.scaledFontSize(8));
    painter.setFont(nameFont);
    const char* ctx = metaObject()->className();
    painter.drawText(
        nameRect, Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate(ctx, "Add image"));
}

void WelcomeBannerAddImageWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
    }
    BaseStyledWidget::mousePressEvent(event);
}

void WelcomeBannerAddImageWidget::mouseReleaseEvent(QMouseEvent* event)
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
