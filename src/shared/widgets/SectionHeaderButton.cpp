// SPDX-License-Identifier: MPL-2.0

#include "shared/widgets/SectionHeaderButton.h"

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QFont>
#include <QPainter>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::core::WidgetStyleManager;

namespace {
constexpr int kBaseHeight = 28;
constexpr int kExpandAnimationMs = 190;
} // namespace

SectionHeaderButton::SectionHeaderButton(QWidget* parent)
    : BaseAnimatedButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setHoverDuration(150);
    setActiveDuration(190);
    updateScaledSize();

    m_expandAnimation = new QVariantAnimation(this);
    m_expandAnimation->setDuration(kExpandAnimationMs);
    m_expandAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_expandAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) {
            m_expandProgress = value.toReal();
            update();
        });

    // The bar height comes from the theme scale, so it has to be re-read when
    // the user changes UI scaling rather than staying at the startup value.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateScaledSize();
        update();
    });
}

void SectionHeaderButton::updateScaledSize()
{
    setFixedHeight(ThemeManager::instance().scaled(kBaseHeight));
}

void SectionHeaderButton::setTitle(const QString& title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    update();
}

void SectionHeaderButton::setExpanded(bool expanded, bool animated)
{
    m_expanded = expanded;
    const qreal target = expanded ? 1.0 : 0.0;

    m_expandAnimation->stop();
    if (!animated) {
        m_expandProgress = target;
        update();
        return;
    }

    m_expandAnimation->setStartValue(m_expandProgress);
    m_expandAnimation->setEndValue(target);
    m_expandAnimation->start();
}

void SectionHeaderButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = WidgetStyleManager::instance().colors();
    const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = ThemeManager::instance().scaled(7);

    QColor fillColor = ThemeColors::withAlpha(colors.surfaceAlt, 0);
    fillColor = ThemeColors::interpolate(fillColor, colors.surfaceHover(), hoverProgress() * 0.18);
    fillColor = ThemeColors::interpolate(fillColor, colors.primary, activeProgress() * 0.04);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fillColor);
    painter.drawRoundedRect(outerRect, radius, radius);

    const int leftPadding = ThemeManager::instance().scaled(10);
    const int rightPadding = ThemeManager::instance().scaled(10);
    const int arrowSize = ThemeManager::instance().scaled(10);
    const int arrowAreaWidth = arrowSize + ThemeManager::instance().scaled(4);
    const int gapWidth = ThemeManager::instance().scaled(8);

    QFont titleFont = painter.font();
    titleFont.setPixelSize(ThemeManager::instance().scaled(11));
    titleFont.setWeight(activeProgress() > 0.5 ? QFont::Medium : QFont::Normal);
    painter.setFont(titleFont);

    const int textWidth
        = qMin(painter.fontMetrics().horizontalAdvance(m_title), qMax(0, width() / 2));

    QRect textRect(leftPadding, 0,
        qMax(0, width() - leftPadding - rightPadding - arrowAreaWidth - gapWidth * 2), height());
    textRect.setWidth(qMin(textRect.width(), textWidth + ThemeManager::instance().scaled(6)));

    painter.setPen(ThemeColors::interpolate(colors.textMuted, colors.text,
        0.38 + activeProgress() * 0.34 + hoverProgress() * 0.18));
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
        painter.fontMetrics().elidedText(m_title, Qt::ElideRight, textRect.width()));

    const int lineStartX = textRect.right() + gapWidth;
    const int lineEndX = width() - rightPadding - arrowAreaWidth - gapWidth;
    if (lineEndX > lineStartX) {
        QColor lineColor = ThemeColors::interpolate(colors.borderSubtle(),
            colors.borderSubtleHover(), hoverProgress() * 0.45 + activeProgress() * 0.2);
        lineColor.setAlphaF(lineColor.alphaF() * (0.55 + hoverProgress() * 0.2));
        painter.setPen(QPen(lineColor, 1.0));
        painter.drawLine(QPointF(lineStartX, height() * 0.5), QPointF(lineEndX, height() * 0.5));
    }

    const QPointF arrowCenter(width() - rightPadding - arrowSize * 0.5, height() * 0.5);
    painter.save();
    painter.translate(arrowCenter);
    painter.rotate(90.0 * m_expandProgress);
    painter.translate(-arrowCenter);
    QPen arrowPen(ThemeColors::interpolate(colors.textMuted, colors.text,
                      0.28 + activeProgress() * 0.48 + hoverProgress() * 0.16),
        ThemeManager::instance().scaled(1.4));
    arrowPen.setCapStyle(Qt::RoundCap);
    arrowPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(arrowPen);
    painter.drawLine(
        QPointF(arrowCenter.x() - arrowSize * 0.35, arrowCenter.y() - arrowSize * 0.35),
        QPointF(arrowCenter.x() + arrowSize * 0.05, arrowCenter.y()));
    painter.drawLine(QPointF(arrowCenter.x() + arrowSize * 0.05, arrowCenter.y()),
        QPointF(arrowCenter.x() - arrowSize * 0.35, arrowCenter.y() + arrowSize * 0.35));
    painter.restore();
}

} // namespace ruwa::ui::widgets
