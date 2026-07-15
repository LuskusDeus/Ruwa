// SPDX-License-Identifier: MPL-2.0

// CategoryItemWidget.cpp
#include "features/settings/shortcuts/CategoryItemWidget.h"

#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/style/PaintingUtils.h"

#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;

namespace {
constexpr int BASE_ROW_HEIGHT = 46;
constexpr int BASE_PADDING_H = 12;
constexpr int BASE_PADDING_V = 8;
constexpr int BASE_ICON_SIZE = 22;
constexpr int BASE_ICON_TEXT_GAP = 12;
constexpr int BASE_TITLE_FONT = 13;
constexpr int BASE_SUBTITLE_FONT = 10;
constexpr int BASE_COUNT_FONT = 11;
constexpr int BASE_RADIUS = 10;
constexpr int HOVER_ANIM_MS = 140;
constexpr int SELECTION_ANIM_MS = 220;
} // namespace

CategoryItemWidget::CategoryItemWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Repaint on theme change (this widget paints theme colours directly).
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() { update(); });
}

CategoryItemWidget::~CategoryItemWidget() = default;

void CategoryItemWidget::setTitle(const QString& title)
{
    if (m_title == title)
        return;
    m_title = title;
    update();
}

void CategoryItemWidget::setSubtitle(const QString& subtitle)
{
    if (m_subtitle == subtitle)
        return;
    m_subtitle = subtitle;
    update();
}

void CategoryItemWidget::setCount(int count)
{
    if (m_count == count)
        return;
    m_count = count;
    update();
}

void CategoryItemWidget::setIcon(const QIcon& icon)
{
    m_icon = icon;
    update();
}

void CategoryItemWidget::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    startSelectionAnimation(selected);
}

QSize CategoryItemWidget::sizeHint() const
{
    const auto& theme = ThemeManager::instance();
    return QSize(0, theme.scaled(BASE_ROW_HEIGHT));
}

QSize CategoryItemWidget::minimumSizeHint() const
{
    return sizeHint();
}

void CategoryItemWidget::startHoverAnimation(bool entering)
{
    if (m_hoverAnim) {
        m_hoverAnim->stop();
        m_hoverAnim->deleteLater();
        m_hoverAnim = nullptr;
    }
    const qreal target = entering ? 1.0 : 0.0;
    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->setDuration(HOVER_ANIM_MS);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_hoverProgress = v.toReal();
        update();
    });
    m_hoverAnim->start();
}

void CategoryItemWidget::startSelectionAnimation(bool selecting)
{
    if (m_selectionAnim) {
        m_selectionAnim->stop();
        m_selectionAnim->deleteLater();
        m_selectionAnim = nullptr;
    }
    const qreal target = selecting ? 1.0 : 0.0;
    m_selectionAnim = new QVariantAnimation(this);
    m_selectionAnim->setStartValue(m_selectionProgress);
    m_selectionAnim->setEndValue(target);
    m_selectionAnim->setDuration(SELECTION_ANIM_MS);
    m_selectionAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_selectionAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_selectionProgress = v.toReal();
        update();
    });
    m_selectionAnim->start();
}

void CategoryItemWidget::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    startHoverAnimation(true);
}

void CategoryItemWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    startHoverAnimation(false);
}

void CategoryItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CategoryItemWidget::paintEvent(QPaintEvent* /*event*/)
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF itemRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = theme.scaled(BASE_RADIUS);

    // === Background ===
    // Hover only shows when not (or partially) selected.
    const qreal hoverWeight = m_hoverProgress * (1.0 - m_selectionProgress);
    const QColor hoverBg = colors.overlay(0.06 + 0.04 * m_hoverProgress);

    QColor bg = ThemeColors::withAlpha(hoverBg, qRound(hoverBg.alpha() * hoverWeight));
    // Selected = filled with the primary/accent colour.
    const QColor selectedBg = colors.primary;
    bg = ThemeColors::interpolate(bg, selectedBg, m_selectionProgress);

    if (bg.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(itemRect, radius, radius);
    }

    // === Foreground colours ===
    // Idle title is slightly dimmed; brightens up on hover, then inverts on selection.
    const qreal dimWeight = (1.0 - m_hoverProgress) * (1.0 - m_selectionProgress);
    const QColor idleTitle
        = ThemeColors::interpolate(colors.text, colors.textMuted, 0.85 * dimWeight);
    const QColor titleColor
        = ThemeColors::interpolate(idleTitle, colors.textOnPrimary(), m_selectionProgress);
    const QColor subtitleColor = ThemeColors::interpolate(
        colors.textMuted, ThemeColors::withAlpha(colors.textOnPrimary(), 180), m_selectionProgress);
    const QColor countColor = subtitleColor;

    const int paddingH = theme.scaled(BASE_PADDING_H);
    const int paddingV = theme.scaled(BASE_PADDING_V);
    const int iconSize = theme.scaled(BASE_ICON_SIZE);
    const int iconGap = theme.scaled(BASE_ICON_TEXT_GAP);

    // === Icon ===
    if (!m_icon.isNull()) {
        const QRect iconRect(paddingH, (height() - iconSize) / 2, iconSize, iconSize);

        // Tint icon: text colour idle → background colour when selected.
        QPixmap pm = m_icon.pixmap(QSize(iconSize, iconSize) * devicePixelRatioF());
        pm.setDevicePixelRatio(devicePixelRatioF());
        if (!pm.isNull()) {
            painter.drawPixmap(iconRect, ruwa::ui::painting::tintedPixmap(pm, titleColor));
        }
    }

    // === Count text on the right ===
    QFont countFont = font();
    countFont.setPixelSize(theme.scaled(BASE_COUNT_FONT));
    painter.setFont(countFont);
    const QFontMetrics countFm(countFont);
    const QString countText = m_count > 0 ? QString::number(m_count) : QString();
    const int countW = countText.isEmpty() ? 0 : countFm.horizontalAdvance(countText);
    if (!countText.isEmpty()) {
        const QRect countRect(
            width() - paddingH - countW, paddingV, countW, height() - paddingV * 2);
        painter.setPen(countColor);
        painter.drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, countText);
    }

    // === Title + subtitle ===
    const int textLeft = paddingH + (m_icon.isNull() ? 0 : (iconSize + iconGap));
    const int textRight
        = width() - paddingH - (countText.isEmpty() ? 0 : (countW + theme.scaled(8)));
    const int textWidth = qMax(0, textRight - textLeft);

    QFont titleFont = font();
    titleFont.setPixelSize(theme.scaled(BASE_TITLE_FONT));
    titleFont.setWeight(QFont::DemiBold);
    QFontMetrics titleFm(titleFont);

    QFont subtitleFont = font();
    subtitleFont.setPixelSize(theme.scaled(BASE_SUBTITLE_FONT));
    QFontMetrics subtitleFm(subtitleFont);

    const bool hasSubtitle = !m_subtitle.trimmed().isEmpty();
    const int titleH = titleFm.height();
    const int subtitleH = hasSubtitle ? subtitleFm.height() : 0;
    const int totalH = titleH + (hasSubtitle ? (subtitleH + theme.scaled(2)) : 0);
    const int textTop = (height() - totalH) / 2;

    painter.setFont(titleFont);
    painter.setPen(titleColor);
    const QRect titleRect(textLeft, textTop, textWidth, titleH);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
        titleFm.elidedText(m_title, Qt::ElideRight, textWidth));

    if (hasSubtitle) {
        painter.setFont(subtitleFont);
        painter.setPen(subtitleColor);
        const QRect subtitleRect(
            textLeft, titleRect.bottom() + theme.scaled(2), textWidth, subtitleH);
        painter.drawText(subtitleRect, Qt::AlignLeft | Qt::AlignVCenter,
            subtitleFm.elidedText(m_subtitle, Qt::ElideRight, textWidth));
    }
}

} // namespace ruwa::ui::widgets
