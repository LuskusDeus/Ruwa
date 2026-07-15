// SPDX-License-Identifier: MPL-2.0

#include "AssetToggleButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QEnterEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int DEFAULT_HOVER_LEAVE_MS = 50;

void tintAndDrawPixmap(QPainter& painter, int x, int y, const QPixmap& src, const QColor& color)
{
    if (src.isNull()) {
        return;
    }

    painter.drawPixmap(x, y, ruwa::ui::painting::tintedPixmap(src, color));
}
} // namespace

AssetToggleButton::AssetToggleButton(QWidget* parent)
    : BaseAnimatedButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setCheckable(true);
    setFlat(true);
    setAutoDefault(false);
    setFocusPolicy(Qt::NoFocus);

    connect(this, &QPushButton::toggled, this, [this](bool checked) { setActive(checked); });

    setHoverDuration(180);
    setActiveDuration(220);

    m_hoverLeaveDebounce = new QTimer(this);
    m_hoverLeaveDebounce->setSingleShot(true);
    m_hoverLeaveDebounce->setInterval(DEFAULT_HOVER_LEAVE_MS);
    connect(m_hoverLeaveDebounce, &QTimer::timeout, this, [this]() {
        if (!underMouse()) {
            setHovered(false);
        }
    });

    updateScaledSize();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &AssetToggleButton::onThemeChanged);
}

void AssetToggleButton::setIconType(IconProvider::StandardIcon iconType)
{
    if (m_iconType == iconType) {
        return;
    }
    m_iconType = iconType;
    update();
}

void AssetToggleButton::setBaseSize(int size)
{
    m_baseSize = qMax(1, size);
    updateScaledSize();
}

void AssetToggleButton::setBaseIconSize(int size)
{
    m_baseIconSize = qMax(1, size);
    update();
}

void AssetToggleButton::setHoverLeaveDebounceMs(int ms)
{
    m_hoverLeaveDebounce->setInterval(qMax(0, ms));
}

void AssetToggleButton::enterEvent(QEnterEvent* event)
{
    m_hoverLeaveDebounce->stop();
    BaseAnimatedButton::enterEvent(event);
}

void AssetToggleButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    m_hoverLeaveDebounce->start();
}

void AssetToggleButton::updateScaledSize()
{
    const int s = ThemeManager::instance().scaled(m_baseSize);
    setFixedSize(s, s);
}

void AssetToggleButton::onThemeChanged()
{
    updateScaledSize();
    update();
}

void AssetToggleButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto& colors = ThemeManager::instance().colors();
    const int borderRadius = ThemeManager::instance().scaled(m_baseBorderRadius);
    const qreal ap = activeProgress();
    const qreal hp = isEnabled() ? hoverProgress() : 0.0;
    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    painter.setPen(Qt::NoPen);

    const qreal plateStrength = hp * (1.0 - ap);
    if (plateStrength > 0.001) {
        QColor plate = colors.surfaceElevated();
        plate.setAlpha(qBound(0, qRound(plateStrength * 90), 255));
        painter.setBrush(plate);
        painter.drawRoundedRect(rect, borderRadius, borderRadius);
    }

    if (ap > 0.001) {
        QColor activeBg = colors.primary;
        const int baseAlpha = isEnabled() ? int(200 + ap * 55) : int((200 + ap * 55) * 0.35);
        activeBg.setAlpha(baseAlpha);
        painter.setBrush(activeBg);
        painter.drawRoundedRect(rect, borderRadius, borderRadius);

        const QRectF borderRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath borderPath;
        borderPath.addRoundedRect(borderRect, borderRadius - 0.5, borderRadius - 0.5);

        QColor actBorderTop
            = ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.4 : 0.7);
        const qreal borderAlphaScale = isEnabled() ? 1.0 : 0.35;
        actBorderTop.setAlpha(int(220 * ap * borderAlphaScale));
        QColor actBorderBot = actBorderTop;
        actBorderBot.setAlpha(int(140 * ap * borderAlphaScale));

        QLinearGradient borderGrad(borderRect.topLeft(), borderRect.bottomLeft());
        borderGrad.setColorAt(0.0, actBorderTop);
        borderGrad.setColorAt(1.0, actBorderBot);

        QPen borderPen;
        borderPen.setBrush(borderGrad);
        borderPen.setWidthF(1.0);
        borderPen.setCosmetic(true);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath);
    }

    drawIcon(painter, rect);
}

void AssetToggleButton::drawIcon(QPainter& painter, const QRectF& rect)
{
    const auto& colors = ThemeManager::instance().colors();
    const qreal ap = activeProgress();
    const qreal hp = isEnabled() ? hoverProgress() : 0.0;

    QColor unlockedColor = isEnabled() ? colors.textMuted : colors.textDisabled();
    if (isEnabled()) {
        unlockedColor.setAlpha(int(175 + hp * 60));
    }

    QColor lockedColor = isEnabled() ? colors.textOnPrimary() : colors.textDisabled();
    const QColor iconColor = ThemeColors::interpolate(unlockedColor, lockedColor, ap);

    const int iconSz = ThemeManager::instance().scaled(m_baseIconSize);
    const QPixmap px = IconProvider::instance().getPixmap(m_iconType, QSize(iconSz, iconSz));
    if (px.isNull()) {
        return;
    }

    const int iconX = qRound(rect.center().x() - px.width() * 0.5);
    const int iconY = qRound(rect.center().y() - px.height() * 0.5);
    tintAndDrawPixmap(painter, iconX, iconY, px, iconColor);
}

} // namespace ruwa::ui::widgets
