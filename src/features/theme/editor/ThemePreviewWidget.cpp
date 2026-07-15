// SPDX-License-Identifier: MPL-2.0

// ThemePreviewWidget.cpp
#include "ThemePreviewWidget.h"
#include "features/theme/manager/ThemePreset.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QtMath>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
// Layout constants (base values, will be scaled)
const int BASE_PADDING = 6;
const int BASE_BOTTOM_PADDING = 20;
const int BASE_NAME_HEIGHT = 14;
const int BASE_NAME_MARGIN_BOTTOM = 16;
const int BASE_NAME_RIGHT_PADDING = 24;
const int BASE_CHECKMARK_OFFSET = 10;
const int BASE_CHECKMARK_SIZE = 4;

// Mock window constants
const int BASE_MOCK_BORDER_RADIUS = 3;
const int BASE_TITLEBAR_HEIGHT = 8;
const int BASE_TITLEBAR_RADIUS = 6;
const qreal BASE_DOT_SIZE = 1.5;
const int BASE_DOT_SPACING_X = 5;
const int BASE_SIDEBAR_WIDTH = 18;
const int BASE_SIDEBAR_ITEM_MARGIN = 2;
const int BASE_SIDEBAR_ITEM_TOP = 3;
const int BASE_SIDEBAR_ITEM_SPACING = 6;
const int BASE_SIDEBAR_ITEM_WIDTH = 14;
const int BASE_SIDEBAR_ITEM_HEIGHT = 4;
const int BASE_CONTENT_LEFT = 20;
const int BASE_CONTENT_TOP = 10;
const int BASE_CONTENT_RIGHT = 22;
const int BASE_CONTENT_BOTTOM = 12;
const int BASE_CONTENT_BAR_HEIGHT = 3;
const int BASE_CONTENT_BAR_SPACING = 5;
const int BASE_CONTENT_BAR2_HEIGHT = 2;

enum ThemePreviewMenuAction : int {
    Unfavorite = 1,
    DeleteCustomTheme = 2,
};
} // namespace

QVariantMap ThemePreviewWidget::contextMenuContext() const
{
    QVariantList actions;

    QVariantMap unfav;
    unfav.insert(QStringLiteral("id"), Unfavorite);
    unfav.insert(QStringLiteral("text"), tr("Unfavorite"));
    unfav.insert(QStringLiteral("danger"), false);
    unfav.insert(
        QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::List));
    actions.append(unfav);

    if (!m_preset.isBuiltIn) {
        QVariantMap del;
        del.insert(QStringLiteral("id"), DeleteCustomTheme);
        del.insert(QStringLiteral("text"), tr("Delete"));
        del.insert(QStringLiteral("danger"), true);
        del.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Trash));
        actions.append(del);
    }

    QVariantMap ctx;
    ctx.insert(QStringLiteral("simpleActions"), QVariant::fromValue(actions));
    return ctx;
}

void ThemePreviewWidget::onSimpleContextAction(int actionId)
{
    auto& tm = ThemeManager::instance();
    const QUuid id = m_preset.id;
    if (actionId == Unfavorite) {
        tm.updatePresetFavorite(id, false);
    } else if (actionId == DeleteCustomTheme && !m_preset.isBuiltIn) {
        tm.removeCustomPreset(id);
    }
}

void ThemePreviewWidget::changeEvent(QEvent* event)
{
    BaseStyledWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        update();
    }
}

ThemePreviewWidget::ThemePreviewWidget(const ThemePreset& preset, QWidget* parent)
    : BaseStyledWidget("ThemePreview", parent)
    , m_preset(preset)
{
    style().metrics.fixedHeight = false;
    applyStyleChanges();
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Connect clicked to toggle selection
    connect(this, &QPushButton::clicked, this, [this]() {
        if (!isSelected()) {
            setSelected(true);
        }
    });
}

QSize ThemePreviewWidget::sizeHint() const
{
    return previewSizeHint();
}

QSize ThemePreviewWidget::minimumSizeHint() const
{
    return previewSizeHint();
}

void ThemePreviewWidget::setSelected(bool selected)
{
    if (isActive() == selected)
        return;

    setActive(selected);

    if (selected) {
        emit this->selected(m_preset);
    }
}

void ThemePreviewWidget::drawActiveBackgroundLayer(QPainter& painter, const QRectF& rect)
{
    // Use preset's background color instead of current theme's primary
    auto& mgr = WidgetStyleManager::instance();
    const int radius = cornerRadius();

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_preset.background);
    painter.drawRoundedRect(rect, radius, radius);
}

void ThemePreviewWidget::drawActiveBorderLayer(QPainter& painter, const QRectF& rect)
{
    // Use preset's primary color for selection border
    auto& mgr = WidgetStyleManager::instance();
    const int radius = cornerRadius();

    painter.setBrush(Qt::NoBrush);

    QRectF borderRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath borderPath;
    borderPath.addRoundedRect(borderRect, radius - 0.5, radius - 0.5);

    QLinearGradient gradient(borderRect.topLeft(), borderRect.bottomLeft());
    QColor topColor = ThemeColors::adjustBrightness(m_preset.primary, 1.2);
    QColor bottomColor = m_preset.primary;

    gradient.setColorAt(0.0, topColor);
    gradient.setColorAt(1.0, bottomColor);

    QPen pen;
    pen.setBrush(gradient);
    pen.setWidthF(style().activeBorder.width);
    pen.setCosmetic(true);

    painter.setPen(pen);
    painter.drawPath(borderPath);
}

QSize ThemePreviewWidget::previewSizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();

    const int padding = mgr.scaled(BASE_PADDING);
    const int titleBarHeight = mgr.scaled(BASE_TITLEBAR_HEIGHT);
    const int sidebarItemTop = mgr.scaled(BASE_SIDEBAR_ITEM_TOP);
    const int sidebarItemSpacing = mgr.scaled(BASE_SIDEBAR_ITEM_SPACING);
    const int sidebarItemHeight = mgr.scaled(BASE_SIDEBAR_ITEM_HEIGHT);
    const int contentTop = mgr.scaled(BASE_CONTENT_TOP);
    const int contentBottom = mgr.scaled(BASE_CONTENT_BOTTOM);
    const int contentBarHeight = mgr.scaled(BASE_CONTENT_BAR_HEIGHT);
    const int contentBarSpacing = mgr.scaled(BASE_CONTENT_BAR_SPACING);
    const int contentBar2Height = mgr.scaled(BASE_CONTENT_BAR2_HEIGHT);

    QFont nameFont = font();
    nameFont.setPointSize(mgr.scaledFontSize(8));
    nameFont.setBold(true);
    const QFontMetrics nameMetrics(nameFont);
    const int nameHeight = qMax(mgr.scaled(BASE_NAME_HEIGHT), nameMetrics.height());
    const int nameMarginBottom = mgr.scaled(BASE_NAME_MARGIN_BOTTOM);
    const int nameBottomInset = qMax(padding, nameMarginBottom - nameHeight);
    const int bottomPadding = nameHeight + nameBottomInset + padding;

    const int sidebarContentHeight
        = titleBarHeight + sidebarItemTop + sidebarItemHeight + sidebarItemSpacing * 2 + padding;
    const int mainContentHeight = contentTop + contentBarSpacing
        + qMax(contentBarHeight, contentBar2Height) + contentBottom;
    const int windowHeight = qMax(sidebarContentHeight, mainContentHeight);

    const int width = mgr.scaled(style().metrics.baseWidth);
    const int baseHeight = mgr.scaled(style().metrics.baseHeight);
    const int contentHeight
        = padding + windowHeight + bottomPadding + qCeil(style().activeBorder.width) + 1;

    return QSize(width, qMax(baseHeight, contentHeight));
}

void ThemePreviewWidget::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();

    const int padding = mgr.scaled(BASE_PADDING);
    const int nameRightPadding = mgr.scaled(BASE_NAME_RIGHT_PADDING);

    QFont nameFont = font();
    nameFont.setPointSize(mgr.scaledFontSize(8));
    nameFont.setBold(activeProgress() > 0.5);
    const QFontMetrics nameMetrics(nameFont);
    const int nameHeight = qMax(mgr.scaled(BASE_NAME_HEIGHT), nameMetrics.height());
    const int nameMarginBottom = mgr.scaled(BASE_NAME_MARGIN_BOTTOM);
    const int nameBottomInset = qMax(padding, nameMarginBottom - nameHeight);
    const int bottomPadding = nameHeight + nameBottomInset + padding;

    // Draw mock window
    QRectF windowRect = rect.adjusted(padding, padding, -padding, -bottomPadding);
    drawMockWindow(painter, windowRect);

    QRectF nameRect(rect.x() + padding, rect.bottom() - nameBottomInset - nameHeight,
        rect.width() - nameRightPadding, nameHeight);

    // Text color interpolates based on hover + selection
    QColor textColor = ThemeColors::interpolate(
        m_preset.textMuted, m_preset.text, hoverProgress() + activeProgress() * 0.5);

    painter.setPen(textColor);
    painter.setFont(nameFont);
    const QString displayName = ThemePreset::translatedDisplayName(m_preset);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, displayName);

    // Draw checkmark when selected
    if (activeProgress() > 0.0) {
        painter.save();
        painter.setOpacity(activeProgress());

        const int checkmarkOffset = mgr.scaled(BASE_CHECKMARK_OFFSET);
        const int checkmarkSize = mgr.scaled(BASE_CHECKMARK_SIZE);

        qreal cx = rect.right() - checkmarkOffset;
        qreal cy = rect.bottom() - checkmarkOffset;

        painter.setPen(QPen(m_preset.primary, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(cx - checkmarkSize, cy), QPointF(cx - 1, cy + 3));
        painter.drawLine(QPointF(cx - 1, cy + 3), QPointF(cx + 3, cy - 2));

        painter.restore();
    }
}

void ThemePreviewWidget::drawMockWindow(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();

    const int mockBorderRadius = mgr.scaled(BASE_MOCK_BORDER_RADIUS);
    const int titleBarHeight = mgr.scaled(BASE_TITLEBAR_HEIGHT);
    const int titleBarRadius = mgr.scaled(BASE_TITLEBAR_RADIUS);
    const qreal dotSize = mgr.scaled(BASE_DOT_SIZE);
    const int dotSpacingX = mgr.scaled(BASE_DOT_SPACING_X);
    const int sidebarWidth = mgr.scaled(BASE_SIDEBAR_WIDTH);
    const int sidebarItemMargin = mgr.scaled(BASE_SIDEBAR_ITEM_MARGIN);
    const int sidebarItemTop = mgr.scaled(BASE_SIDEBAR_ITEM_TOP);
    const int sidebarItemSpacing = mgr.scaled(BASE_SIDEBAR_ITEM_SPACING);
    const int sidebarItemWidth = mgr.scaled(BASE_SIDEBAR_ITEM_WIDTH);
    const int sidebarItemHeight = mgr.scaled(BASE_SIDEBAR_ITEM_HEIGHT);
    const int contentLeft = mgr.scaled(BASE_CONTENT_LEFT);
    const int contentTop = mgr.scaled(BASE_CONTENT_TOP);
    const int contentRight = mgr.scaled(BASE_CONTENT_RIGHT);
    const int contentBottom = mgr.scaled(BASE_CONTENT_BOTTOM);
    const int contentBarHeight = mgr.scaled(BASE_CONTENT_BAR_HEIGHT);
    const int contentBarSpacing = mgr.scaled(BASE_CONTENT_BAR_SPACING);
    const int contentBar2Height = mgr.scaled(BASE_CONTENT_BAR2_HEIGHT);

    // Window background
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_preset.surface);
    painter.drawRoundedRect(rect, mockBorderRadius, mockBorderRadius);

    // Title bar
    QRectF titleBar(rect.x(), rect.y(), rect.width(), titleBarHeight);
    painter.setBrush(m_preset.surfaceAlt);

    QPainterPath titlePath;
    titlePath.moveTo(titleBar.bottomLeft());
    titlePath.lineTo(titleBar.x(), titleBar.y() + mockBorderRadius);
    titlePath.arcTo(titleBar.x(), titleBar.y(), titleBarRadius, titleBarRadius, 180, -90);
    titlePath.lineTo(titleBar.right() - mockBorderRadius, titleBar.y());
    titlePath.arcTo(
        titleBar.right() - titleBarRadius, titleBar.y(), titleBarRadius, titleBarRadius, 90, -90);
    titlePath.lineTo(titleBar.bottomRight());
    titlePath.closeSubpath();
    painter.drawPath(titlePath);

    // Traffic light dots
    qreal dotY = titleBar.center().y();
    painter.setBrush(m_preset.error);
    painter.drawEllipse(QPointF(rect.x() + dotSpacingX, dotY), dotSize, dotSize);
    painter.setBrush(m_preset.warning);
    painter.drawEllipse(QPointF(rect.x() + dotSpacingX * 2, dotY), dotSize, dotSize);
    painter.setBrush(m_preset.success);
    painter.drawEllipse(QPointF(rect.x() + dotSpacingX * 3, dotY), dotSize, dotSize);

    // Sidebar
    QRectF sidebar(
        rect.x(), rect.y() + titleBarHeight, sidebarWidth, rect.height() - titleBarHeight);
    painter.setBrush(m_preset.surfaceAlt);
    painter.drawRect(sidebar);

    // Sidebar items
    for (int i = 0; i < 3; ++i) {
        QRectF item(sidebar.x() + sidebarItemMargin,
            sidebar.y() + sidebarItemTop + i * sidebarItemSpacing, sidebarItemWidth,
            sidebarItemHeight);
        painter.setBrush(i == 0 ? m_preset.primary : ThemePreset::withAlpha(m_preset.text, 40));
        painter.drawRoundedRect(item, 1, 1);
    }

    // Content area
    QRectF content(rect.x() + contentLeft, rect.y() + contentTop, rect.width() - contentRight,
        rect.height() - contentBottom);

    // Content bars
    painter.setBrush(m_preset.text);
    painter.drawRoundedRect(
        QRectF(content.x(), content.y(), content.width() * 0.5, contentBarHeight), 1, 1);

    painter.setBrush(ThemePreset::withAlpha(m_preset.textMuted, 100));
    painter.drawRoundedRect(QRectF(content.x(), content.y() + contentBarSpacing,
                                content.width() * 0.35, contentBar2Height),
        1, 1);
}

} // namespace ruwa::ui::widgets
