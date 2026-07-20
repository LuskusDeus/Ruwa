// SPDX-License-Identifier: MPL-2.0

// ToolButton.cpp
#include "ToolButton.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"

#include <QEasingCurve>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

namespace ruwa::ui::workspace {

using namespace ruwa::ui::core;
using namespace ruwa::ui::widgets;

namespace {
const int BASE_GROUP_INDICATOR_SIZE = 7;
const int BASE_GROUP_INDICATOR_MARGIN = 4;
} // namespace

ToolButton::ToolButton(QWidget* parent)
    : ToolButton(Mode::Toggle, parent)
{
}

ToolButton::ToolButton(Mode mode, QWidget* parent)
    : BaseAnimatedButton(parent)
    , m_mode(mode)
{
    setCheckable(mode == Mode::Toggle);

    if (mode == Mode::Toggle) {
        setHoverDuration(200);
        setActiveDuration(250);
        connect(this, &QAbstractButton::toggled, this, &BaseAnimatedButton::setActive);
    } else {
        setHoverDuration(200);
        setActiveDuration(150); // Smooth press flash for action tools
    }

    m_enabledProgress = isEnabled() ? 1.0 : 0.0;
    m_enabledAnimation = new QPropertyAnimation(this, "enabledProgress", this);
    m_enabledAnimation->setDuration(180);
    m_enabledAnimation->setEasingCurve(QEasingCurve::OutCubic);

    updateScaledSize();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateScaledSize();
        update();
    });
}

void ToolButton::setIcon(const QIcon& icon)
{
    m_sourceIcon = icon;
    update();
}

void ToolButton::setIconType(IconProvider::StandardIcon iconType)
{
    auto& theme = ThemeManager::instance();
    m_sourceIcon = theme.icons().getIcon(iconType);
    update();
}

void ToolButton::setBaseSize(int width, int height, int iconSize)
{
    m_baseWidth = qMax(1, width);
    m_baseHeight = qMax(1, height);
    m_baseIconSize = qMax(1, iconSize);
    updateScaledSize();
    update();
}

void ToolButton::setBaseSquareSize(int size, int iconSize)
{
    setBaseSize(size, size, iconSize);
}

void ToolButton::setChromeStyle(ChromeStyle style)
{
    if (m_chromeStyle == style) {
        return;
    }
    m_chromeStyle = style;
    update();
}

void ToolButton::setChromeOpacity(qreal opacity)
{
    const qreal bounded = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_chromeOpacity, bounded)) {
        return;
    }

    m_chromeOpacity = bounded;
    update();
}

void ToolButton::setChromeInsets(int left, int top, int right, int bottom)
{
    m_baseInsetLeft = qMax(0, left);
    m_baseInsetTop = qMax(0, top);
    m_baseInsetRight = qMax(0, right);
    m_baseInsetBottom = qMax(0, bottom);
    update();
}

void ToolButton::setCircularChrome(bool circular)
{
    if (m_circularChrome == circular) {
        return;
    }
    m_circularChrome = circular;
    update();
}

void ToolButton::setBorderVisible(bool visible)
{
    if (m_borderVisible == visible) {
        return;
    }
    m_borderVisible = visible;
    update();
}

void ToolButton::setColorizeIcon(bool colorize)
{
    if (m_colorizeIcon == colorize) {
        return;
    }
    m_colorizeIcon = colorize;
    update();
}

void ToolButton::setMutedNormalIcon(bool muted)
{
    if (m_mutedNormalIcon == muted) {
        return;
    }
    m_mutedNormalIcon = muted;
    update();
}

void ToolButton::setPressFeedbackEnabled(bool enabled)
{
    if (m_pressFeedback == enabled) {
        return;
    }
    m_pressFeedback = enabled;
    update();
}

void ToolButton::setHasGroupIndicator(bool hasGroupIndicator)
{
    if (m_hasGroupIndicator == hasGroupIndicator) {
        return;
    }
    m_hasGroupIndicator = hasGroupIndicator;
    update();
}

void ToolButton::setEnabledProgress(qreal progress)
{
    if (qFuzzyCompare(m_enabledProgress, progress)) {
        return;
    }
    m_enabledProgress = progress;
    update();
}

void ToolButton::changeEvent(QEvent* event)
{
    BaseAnimatedButton::changeEvent(event);

    if (event->type() == QEvent::EnabledChange) {
        const qreal target = isEnabled() ? 1.0 : 0.0;
        m_enabledAnimation->stop();
        // Only animate when on screen; an enable/disable set during construction
        // (before the button is shown) must snap so the first frame is correct.
        if (isVisible()) {
            m_enabledAnimation->setStartValue(m_enabledProgress);
            m_enabledAnimation->setEndValue(target);
            m_enabledAnimation->start();
        } else {
            setEnabledProgress(target);
        }
    }
}

QSize ToolButton::sizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();
    return QSize(mgr.scaled(m_baseWidth), mgr.scaled(m_baseHeight));
}

void ToolButton::updateScaledSize()
{
    auto& mgr = WidgetStyleManager::instance();
    m_iconSize = mgr.scaled(m_baseIconSize);
    setFixedSize(mgr.scaled(m_baseWidth), mgr.scaled(m_baseHeight));
}

void ToolButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ThemeManager::instance().colors();
    auto& mgr = WidgetStyleManager::instance();
    const bool enabled = isEnabled();

    QRectF rect
        = this->rect().adjusted(mgr.scaled(m_baseInsetLeft) + 0.5, mgr.scaled(m_baseInsetTop) + 0.5,
            -mgr.scaled(m_baseInsetRight) - 0.5, -mgr.scaled(m_baseInsetBottom) - 0.5);
    const qreal radius = m_circularChrome ? qMin(rect.width(), rect.height()) * 0.5
                                          : mgr.scaled(m_baseRadius);

    // Background layer
    painter.setPen(Qt::NoPen);

    if (enabled && m_chromeStyle == ChromeStyle::Surface) {
        QColor bgColor = colors.surfaceAlt;
        if (m_pressFeedback && m_isPressed) {
            bgColor = bgColor.darker(110);
        }
        bgColor.setAlphaF(bgColor.alphaF() * m_chromeOpacity);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, radius, radius);
    }

    // Interpolate background based on activeProgress
    if (enabled && activeProgress() > 0 && m_chromeStyle == ChromeStyle::Toolbar) {
        QColor bgColor = colors.primary;
        if (m_pressFeedback && m_isPressed) {
            bgColor = colors.primaryPressed();
        }
        bgColor.setAlphaF(bgColor.alphaF() * activeProgress());
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, radius, radius);
    }

    // Hover state (only when not active)
    if (enabled && hoverProgress() > 0 && activeProgress() < 1.0) {
        QColor hoverColor;
        qreal hoverAlpha = 0.0;
        if (m_chromeStyle == ChromeStyle::PrimaryHover) {
            hoverColor = colors.primary;
            hoverAlpha = (colors.isDark ? 0.14 : 0.10) * hoverProgress()
                * (1.0 - activeProgress());
        } else {
            hoverColor = m_chromeStyle == ChromeStyle::Overlay ? colors.overlay(0.06)
                                                               : colors.surfaceHover();
            hoverAlpha = hoverColor.alphaF() * hoverProgress() * (1.0 - activeProgress());
            if (m_chromeStyle == ChromeStyle::Surface) {
                hoverAlpha *= m_chromeOpacity;
            }
        }
        hoverColor.setAlphaF(hoverAlpha);
        painter.setBrush(hoverColor);
        painter.drawRoundedRect(rect, radius, radius);
    }

    // Pressed overlay for non-checked buttons
    if (enabled && m_pressFeedback && m_isPressed && activeProgress() < 1.0) {
        QColor pressColor;
        qreal pressAlpha = 0.0;
        if (m_chromeStyle == ChromeStyle::PrimaryHover) {
            pressColor = colors.primary;
            pressAlpha = (colors.isDark ? 0.22 : 0.17) * (1.0 - activeProgress());
        } else {
            pressColor = m_chromeStyle == ChromeStyle::Overlay ? colors.overlay(0.10)
                                                               : colors.surfaceHover();
            pressAlpha = pressColor.alphaF() * 1.5 * (1.0 - activeProgress());
            if (m_chromeStyle == ChromeStyle::Surface) {
                pressAlpha *= m_chromeOpacity;
            }
        }
        pressColor.setAlphaF(qBound(0.0, pressAlpha, 1.0));
        painter.setBrush(pressColor);
        painter.drawRoundedRect(rect, radius, radius);
    }

    if (m_borderVisible) {
        QColor borderColor = colors.border;
        borderColor.setAlpha(80);
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect, radius, radius);
        painter.setPen(Qt::NoPen);
    }

    // Draw icon with interpolated color
    if (!m_sourceIcon.isNull()) {
        auto lerpColor = [](const QColor& a, const QColor& b, qreal t) {
            return QColor(qRound(a.red() + (b.red() - a.red()) * t),
                qRound(a.green() + (b.green() - a.green()) * t),
                qRound(a.blue() + (b.blue() - a.blue()) * t),
                qRound(a.alpha() + (b.alpha() - a.alpha()) * t));
        };

        // Enabled appearance (active-interpolated) ...
        QColor normalColor = m_mutedNormalIcon ? colors.textMuted : colors.text;
        if (m_chromeStyle == ChromeStyle::PrimaryHover) {
            normalColor = lerpColor(normalColor, colors.primary, hoverProgress());
        }
        const QColor activeColor = colors.textOnPrimary();
        const QColor enabledColor = lerpColor(normalColor, activeColor, activeProgress());

        // ... then fade between disabled and enabled as the button toggles state.
        const QColor iconColor = lerpColor(colors.textDisabled(), enabledColor, m_enabledProgress);

        QPixmap basePix = m_sourceIcon.pixmap(m_iconSize, m_iconSize);
        QPixmap coloredPix
            = m_colorizeIcon ? ruwa::ui::painting::tintedPixmap(basePix, iconColor) : basePix;

        int x = (width() - coloredPix.width() / coloredPix.devicePixelRatio()) / 2;
        int y = (height() - coloredPix.height() / coloredPix.devicePixelRatio()) / 2;
        painter.drawPixmap(x, y, coloredPix);
    }

    if (m_hasGroupIndicator) {
        QColor indicatorColor = enabled ? colors.textMuted : colors.textDisabled();
        if (enabled && activeProgress() > 0.0) {
            QColor activeColor = colors.textOnPrimary();
            int r = indicatorColor.red()
                + (activeColor.red() - indicatorColor.red()) * activeProgress();
            int g = indicatorColor.green()
                + (activeColor.green() - indicatorColor.green()) * activeProgress();
            int b = indicatorColor.blue()
                + (activeColor.blue() - indicatorColor.blue()) * activeProgress();
            indicatorColor = QColor(r, g, b);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(indicatorColor);

        const qreal indicatorSize = mgr.scaled(BASE_GROUP_INDICATOR_SIZE);
        const qreal indicatorMargin = mgr.scaled(BASE_GROUP_INDICATOR_MARGIN);
        const qreal right = width() - indicatorMargin;
        const qreal bottom = height() - indicatorMargin;

        QPolygonF arrow;
        arrow << QPointF(right - indicatorSize, bottom) << QPointF(right, bottom)
              << QPointF(right, bottom - indicatorSize);
        painter.drawPolygon(arrow);
    }
}

} // namespace ruwa::ui::workspace
