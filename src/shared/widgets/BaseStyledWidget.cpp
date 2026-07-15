// SPDX-License-Identifier: MPL-2.0

// BaseStyledWidget.cpp
#include "BaseStyledWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QTimer>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

// ============================================================================
// Construction
// ============================================================================

BaseStyledWidget::BaseStyledWidget(const QString& styleName, QWidget* parent)
    : QPushButton(parent)
{
    const WidgetStyle* registered = WidgetStyleManager::instance().style(styleName);
    if (registered) {
        m_style = *registered;
    } else {
        m_style = WidgetStyle::defaultButtonStyle();
    }
    initialize();
}

BaseStyledWidget::BaseStyledWidget(const WidgetStyle& style, QWidget* parent)
    : QPushButton(parent)
    , m_style(style)
{
    initialize();
}

BaseStyledWidget::~BaseStyledWidget()
{
    // Animations are parented to this, auto-deleted
}

void BaseStyledWidget::initialize()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);

    setupAnimations();
    updateSizeFromStyle();
    connectSignals();
}

void BaseStyledWidget::setupAnimations()
{
    auto& mgr = WidgetStyleManager::instance();

    // Hover animation
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(mgr.effectiveHoverDuration(m_style));
    m_hoverAnimation->setEasingCurve(mgr.effectiveHoverEasingIn(m_style));

    // Active animation
    m_activeAnimation = new QPropertyAnimation(this, "activeProgress", this);
    m_activeAnimation->setDuration(mgr.effectiveActiveDuration(m_style));
    m_activeAnimation->setEasingCurve(mgr.effectiveActiveEasing(m_style));

    // Glow animation (if enabled in style)
    m_glowAnimation = new QPropertyAnimation(this, "glowSize", this);
    m_glowAnimation->setDuration(m_style.hoverGlow.sizeDuration);
    m_glowAnimation->setEasingCurve(m_style.hoverGlow.sizeEasingIn);
}

void BaseStyledWidget::updateSizeFromStyle()
{
    auto& mgr = WidgetStyleManager::instance();

    if (m_style.metrics.fixedHeight) {
        setFixedHeight(mgr.scaled(m_style.metrics.baseHeight));
    }

    if (m_style.metrics.fixedWidth && m_style.metrics.baseWidth > 0) {
        setFixedWidth(mgr.scaled(m_style.metrics.baseWidth));
    }

    if (m_style.metrics.baseMinWidth > 0) {
        setMinimumWidth(mgr.scaled(m_style.metrics.baseMinWidth));
    }

    if (m_style.metrics.baseMaxWidth > 0) {
        setMaximumWidth(mgr.scaled(m_style.metrics.baseMaxWidth));
    }
}

void BaseStyledWidget::connectSignals()
{
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BaseStyledWidget::onThemeChanged);

    connect(&WidgetStyleManager::instance(), &WidgetStyleManager::globalSettingsChanged, this,
        &BaseStyledWidget::onGlobalSettingsChanged);
}

// ============================================================================
// Style Management
// ============================================================================

void BaseStyledWidget::applyStyleChanges()
{
    updateSizeFromStyle();

    // Update animation durations
    auto& mgr = WidgetStyleManager::instance();
    m_hoverAnimation->setDuration(mgr.effectiveHoverDuration(m_style));
    m_activeAnimation->setDuration(mgr.effectiveActiveDuration(m_style));
    m_glowAnimation->setDuration(m_style.hoverGlow.sizeDuration);

    update();
}

void BaseStyledWidget::setStyle(const WidgetStyle& style)
{
    m_style = style;
    applyStyleChanges();
}

void BaseStyledWidget::setStyle(const QString& styleName)
{
    const WidgetStyle* registered = WidgetStyleManager::instance().style(styleName);
    if (registered) {
        setStyle(*registered);
    }
}

// ============================================================================
// State Control
// ============================================================================

void BaseStyledWidget::setActive(bool active, bool animated)
{
    if (m_isActive == active) {
        if (!animated && m_activeAnimation) {
            m_activeAnimation->stop();
            const qreal target = active ? 1.0 : 0.0;
            if (!qFuzzyCompare(m_activeProgress, target)) {
                m_activeProgress = target;
                update();
            }
        }
        return;
    }

    m_isActive = active;

    if (!animated) {
        if (m_activeAnimation) {
            m_activeAnimation->stop();
        }
        m_activeProgress = active ? 1.0 : 0.0;
        update();
        return;
    }

    animateActive(active);
}

void BaseStyledWidget::setHoverProgress(qreal progress)
{
    if (qFuzzyCompare(m_hoverProgress, progress))
        return;
    m_hoverProgress = progress;
    update();
}

void BaseStyledWidget::setActiveProgress(qreal progress)
{
    if (qFuzzyCompare(m_activeProgress, progress))
        return;
    m_activeProgress = progress;
    update();
}

void BaseStyledWidget::setGlowSize(qreal size)
{
    if (qFuzzyCompare(m_glowSize, size))
        return;
    m_glowSize = size;
    update();
}

// ============================================================================
// Animation Helpers
// ============================================================================

void BaseStyledWidget::animateHover(bool hovered)
{
    auto& mgr = WidgetStyleManager::instance();

    if (!mgr.animationsEnabled() || !mgr.hoverEffectsEnabled()) {
        m_hoverProgress = hovered ? 1.0 : 0.0;
        update();
        return;
    }

    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(
        hovered ? mgr.effectiveHoverEasingIn(m_style) : mgr.effectiveHoverEasingOut(m_style));
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void BaseStyledWidget::animateActive(bool active)
{
    auto& mgr = WidgetStyleManager::instance();

    if (!mgr.animationsEnabled()) {
        m_activeProgress = active ? 1.0 : 0.0;
        update();
        return;
    }

    m_activeAnimation->stop();
    m_activeAnimation->setStartValue(m_activeProgress);
    m_activeAnimation->setEndValue(active ? 1.0 : 0.0);
    m_activeAnimation->start();
}

void BaseStyledWidget::animateGlow(bool show)
{
    auto& mgr = WidgetStyleManager::instance();

    if (!mgr.animationsEnabled() || !mgr.glowEffectsEnabled() || !m_style.hoverGlow.enabled
        || !m_style.hoverGlow.animateSize) {
        m_glowSize = show ? 1.0 : 0.0;
        update();
        return;
    }

    m_glowAnimation->stop();
    m_glowAnimation->setEasingCurve(
        show ? m_style.hoverGlow.sizeEasingIn : m_style.hoverGlow.sizeEasingOut);
    m_glowAnimation->setDuration(show ? m_style.hoverGlow.sizeDuration : 250);
    m_glowAnimation->setStartValue(m_glowSize);
    m_glowAnimation->setEndValue(show ? 1.0 : 0.0);
    m_glowAnimation->start();
}

// ============================================================================
// Event Handlers
// ============================================================================

void BaseStyledWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange) {
        if (!isEnabled()) {
            m_isPressed = false;
            m_hoverAnimation->stop();
            m_glowAnimation->stop();
            m_hoverProgress = 0.0;
            m_glowSize = 0.0;
            setCursor(Qt::ArrowCursor);
        } else {
            setCursor(Qt::PointingHandCursor);
        }
        update();
    }
    QPushButton::changeEvent(event);
}

void BaseStyledWidget::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    if (!isEnabled()) {
        return;
    }
    animateHover(true);
    animateGlow(true);
}

void BaseStyledWidget::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    if (!isEnabled()) {
        return;
    }
    animateHover(false);
    animateGlow(false);
}

void BaseStyledWidget::mousePressEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        QPushButton::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_isPressed = true;
        update();
    }
    QPushButton::mousePressEvent(event);
}

void BaseStyledWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        QPushButton::mouseReleaseEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_isPressed = false;
        update();
    }
    QPushButton::mouseReleaseEvent(event);
}

void BaseStyledWidget::onThemeChanged()
{
    updateSizeFromStyle();
    update();
}

void BaseStyledWidget::onGlobalSettingsChanged()
{
    auto& mgr = WidgetStyleManager::instance();

    // Update animation durations
    m_hoverAnimation->setDuration(mgr.effectiveHoverDuration(m_style));
    m_activeAnimation->setDuration(mgr.effectiveActiveDuration(m_style));

    // If animations disabled, snap to current state
    if (!mgr.animationsEnabled()) {
        m_hoverProgress = underMouse() ? 1.0 : 0.0;
        m_activeProgress = m_isActive ? 1.0 : 0.0;
        m_glowSize = (underMouse() && m_style.hoverGlow.enabled) ? 1.0 : 0.0;
    }

    update();
}

// ============================================================================
// Helpers
// ============================================================================

int BaseStyledWidget::cornerRadius() const
{
    auto& mgr = WidgetStyleManager::instance();
    return mgr.scaled(mgr.effectiveCornerRadius(m_style));
}

QMargins BaseStyledWidget::contentPadding() const
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& base = m_style.content.basePadding;
    return QMargins(mgr.scaled(base.left()), mgr.scaled(base.top()), mgr.scaled(base.right()),
        mgr.scaled(base.bottom()));
}

QColor BaseStyledWidget::currentTextColor() const
{
    auto& mgr = WidgetStyleManager::instance();

    // Inactive: interpolate textColor -> textHoverColor based on hoverProgress
    QColor inactiveColor = mgr.interpolateColors(
        m_style.content.textColor, m_style.content.textHoverColor, m_hoverProgress);

    // Active color
    QColor activeColor = mgr.resolveColor(m_style.content.textActiveColor);

    // Interpolate inactive -> active based on activeProgress
    return ThemeColors::interpolate(inactiveColor, activeColor, m_activeProgress);
}

QColor BaseStyledWidget::currentSecondaryTextColor() const
{
    auto& mgr = WidgetStyleManager::instance();

    QColor inactiveColor = mgr.resolveColor(m_style.content.secondaryTextColor);

    QColor activeColor;
    if (m_style.content.secondaryTextActiveColor == ColorSource::Custom) {
        // Calculate from background brightness
        activeColor = ThemeColors::adjustBrightness(
            mgr.colors().background, m_style.content.secondaryActiveBrightness);
    } else {
        activeColor = mgr.resolveColor(m_style.content.secondaryTextActiveColor);
    }

    return ThemeColors::interpolate(inactiveColor, activeColor, m_activeProgress);
}

QPixmap BaseStyledWidget::colorizeIcon(const QPixmap& source, const QColor& color) const
{
    if (source.isNull())
        return QPixmap();

    QPixmap colored = source;
    QPainter painter(&colored);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(colored.rect(), color);
    painter.end();

    return colored;
}

// ============================================================================
// Paint Event
// ============================================================================

void BaseStyledWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);

    // Draw layers in order

    // Inactive layers (opacity = 1 - activeProgress)
    painter.save();
    painter.setOpacity(1.0 - m_activeProgress);

    drawBackgroundLayer(painter, rect);
    drawHoverLayer(painter, rect);
    drawHoverGlowLayer(painter, rect);
    // Border drawn last so content doesn't cover it at edges

    // Press effect for inactive state
    if (m_isPressed && m_style.press.enabled) {
        auto& mgr = WidgetStyleManager::instance();
        painter.setPen(Qt::NoPen);
        painter.setBrush(mgr.resolveColor(m_style.press.color));
        painter.drawRoundedRect(rect, cornerRadius(), cornerRadius());
    }

    painter.restore();

    // Active layers (opacity = activeProgress)
    painter.save();
    painter.setOpacity(m_activeProgress);

    drawActiveBackgroundLayer(painter, rect);

    // Active glow (reuse glow layer with different colors)
    if (m_style.hoverGlow.enabled && m_glowSize > 0 && m_hoverProgress > 0) {
        drawHoverGlowLayer(painter, rect); // Will use activeProgress to adjust colors
    }
    // Active border drawn last

    // Press effect for active state
    if (m_isPressed && m_style.press.enabled) {
        auto& mgr = WidgetStyleManager::instance();
        painter.setPen(Qt::NoPen);
        painter.setBrush(mgr.resolveColor(m_style.press.activeColor));
        painter.drawRoundedRect(rect, cornerRadius(), cornerRadius());
    }

    painter.restore();

    // Content layer (always full opacity, uses interpolated colors)
    painter.setOpacity(1.0);
    drawContentLayer(painter, rect);

    // Custom layers hook
    drawCustomLayers(painter, rect);

    // Borders drawn last so content doesn't cover them at edges
    painter.save();
    painter.setOpacity(1.0 - m_activeProgress);
    drawBorderLayer(painter, rect);
    painter.restore();

    painter.save();
    painter.setOpacity(m_activeProgress);
    drawActiveBorderLayer(painter, rect);
    painter.restore();
}

// ============================================================================
// Layer Implementations
// ============================================================================

void BaseStyledWidget::drawBackgroundLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.background.enabled)
        return;

    auto& mgr = WidgetStyleManager::instance();

    QColor bgColor = mgr.resolveColor(m_style.background.color, m_style.background.customColor);
    const qreal bgOpacity = qBound(0.0, m_style.background.opacity, 1.0);
    if (bgOpacity < 1.0) {
        bgColor.setAlphaF(bgColor.alphaF() * bgOpacity);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect, cornerRadius(), cornerRadius());
}

void BaseStyledWidget::drawBorderLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.border.enabled)
        return;

    auto& mgr = WidgetStyleManager::instance();
    const qreal radius = cornerRadius() - 0.5;
    const qreal w = m_style.border.width;

    QRectF outerRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    QRectF innerRect = outerRect.adjusted(w, w, -w, -w);
    const qreal innerRadius = qMax(0.0, radius - w);

    QPainterPath outerPath;
    outerPath.addRoundedRect(outerRect, radius, radius);

    QPainterPath innerPath;
    if (innerRect.width() > 0 && innerRect.height() > 0) {
        innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
    }

    QPainterPath borderRing = outerPath.subtracted(innerPath);
    painter.setPen(Qt::NoPen);

    if (m_style.border.style == BorderStyle::VerticalGradient) {
        QLinearGradient gradient(outerRect.topLeft(), outerRect.bottomLeft());

        QColor topBase = mgr.resolveColor(m_style.border.topColor, m_style.border.customTopColor);
        QColor topHover = mgr.resolveColor(m_style.border.hoverTopColor);
        QColor topColor = m_style.border.animateOnHover
            ? ThemeColors::interpolate(topBase, topHover, m_hoverProgress)
            : topBase;

        QColor bottomColor
            = mgr.resolveColor(m_style.border.bottomColor, m_style.border.customBottomColor);
        if (m_style.border.animateOnHover) {
            QColor bottomHover = mgr.resolveColor(m_style.border.hoverBottomColor);
            bottomColor = ThemeColors::interpolate(bottomColor, bottomHover, m_hoverProgress);
        }

        gradient.setColorAt(0.0, topColor);
        gradient.setColorAt(1.0, bottomColor);
        painter.setBrush(gradient);
    } else if (m_style.border.style == BorderStyle::Solid) {
        QColor borderColor = mgr.resolveColor(m_style.border.color, m_style.border.customColor);
        painter.setBrush(borderColor);
    } else {
        return;
    }

    painter.drawPath(borderRing);
}

void BaseStyledWidget::drawHoverLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.hover.enabled || m_hoverProgress <= 0)
        return;

    auto& mgr = WidgetStyleManager::instance();
    if (!mgr.hoverEffectsEnabled())
        return;

    QColor hoverColor = mgr.resolveColor(m_style.hover.color, m_style.hover.customColor);
    hoverColor.setAlphaF(hoverColor.alphaF() * m_hoverProgress * m_style.hover.maxOpacity);

    painter.setPen(Qt::NoPen);
    painter.setBrush(hoverColor);
    painter.drawRoundedRect(rect, cornerRadius(), cornerRadius());
}

void BaseStyledWidget::drawHoverGlowLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.hoverGlow.enabled || m_glowSize <= 0 || m_hoverProgress <= 0)
        return;

    auto& mgr = WidgetStyleManager::instance();
    if (!mgr.glowEffectsEnabled())
        return;

    const auto& glow = m_style.hoverGlow;

    // Calculate glow parameters
    QPointF center(rect.width() / 2.0, 0);
    qreal minRadius = rect.height() * glow.minRadius;
    qreal maxRadius = rect.height() * glow.maxRadius;
    qreal currentRadius = minRadius + (maxRadius - minRadius) * m_glowSize;

    qreal stretchFactor = glow.stretchFactor + (rect.width() / 300.0);
    stretchFactor = qMin(stretchFactor, glow.maxStretch);

    QRadialGradient gradient(center, currentRadius, center);

    // Glow color with opacity based on hover progress
    // For active state, we use overlay color from theme
    qreal opacity = m_hoverProgress * glow.maxOpacity;
    if (m_activeProgress > 0) {
        // Boost opacity for active state (like SidebarButton does)
        opacity = m_hoverProgress * (glow.maxOpacity + (0.59 - glow.maxOpacity) * m_activeProgress);
    }

    QColor glowCenter = mgr.colors().overlay(opacity);
    QColor glowEdge = mgr.colors().overlay(0.0);

    gradient.setColorAt(0.0, glowCenter);
    gradient.setColorAt(0.65 + 0.05 * m_activeProgress, glowEdge); // Slightly larger for active

    // Apply stretch transform
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.scale(stretchFactor, 1.0);
    transform.translate(-center.x(), -center.y());

    painter.save();
    painter.setTransform(transform);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(transform.inverted().mapRect(rect), cornerRadius(), cornerRadius());
    painter.restore();
}

void BaseStyledWidget::drawActiveBackgroundLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.activeBackground.enabled)
        return;

    auto& mgr = WidgetStyleManager::instance();

    // Use inner area when active border is present, so primary doesn't bleed past the border
    QRectF fillRect = rect;
    qreal fillRadius = cornerRadius();
    if (m_style.activeBorder.enabled) {
        const qreal radius = cornerRadius() - 0.5;
        const qreal w = m_style.activeBorder.width;
        QRectF outerRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
        fillRect = outerRect.adjusted(w, w, -w, -w);
        fillRadius = qMax(0.0, radius - w);
    }
    if (fillRect.width() <= 0 || fillRect.height() <= 0)
        return;

    // Primary color with optional hover brightness boost
    QColor primaryBg
        = mgr.resolveColor(m_style.activeBackground.color, m_style.activeBackground.customColor);
    if (m_hoverProgress > 0) {
        qreal boost = 1.0 + (m_style.activeBackground.hoverBrightnessBoost - 1.0) * m_hoverProgress;
        primaryBg = ThemeColors::adjustBrightness(primaryBg, boost);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(primaryBg);
    painter.drawRoundedRect(fillRect, fillRadius, fillRadius);

    // Bottom shadow gradient
    if (m_style.activeBackground.bottomShadow) {
        QLinearGradient shadowGrad(fillRect.bottomLeft(), fillRect.topLeft());
        QColor darkBottom
            = mgr.colors().shadow(int(255 * m_style.activeBackground.bottomShadowOpacity));
        QColor darkTop = mgr.colors().shadow(0);
        shadowGrad.setColorAt(0.0, darkBottom);
        shadowGrad.setColorAt(m_style.activeBackground.bottomShadowExtent, darkTop);

        painter.setBrush(shadowGrad);
        painter.drawRoundedRect(fillRect, fillRadius, fillRadius);
    }
}

void BaseStyledWidget::drawActiveBorderLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.activeBorder.enabled)
        return;

    auto& mgr = WidgetStyleManager::instance();
    const qreal radius = cornerRadius() - 0.5;
    const qreal w = m_style.activeBorder.width;

    QRectF outerRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    QRectF innerRect = outerRect.adjusted(w, w, -w, -w);
    const qreal innerRadius = qMax(0.0, radius - w);

    QPainterPath outerPath;
    outerPath.addRoundedRect(outerRect, radius, radius);

    QPainterPath innerPath;
    if (innerRect.width() > 0 && innerRect.height() > 0) {
        innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
    }

    QPainterPath borderRing = outerPath.subtracted(innerPath);
    painter.setPen(Qt::NoPen);

    if (m_style.activeBorder.style == BorderStyle::VerticalGradient) {
        QLinearGradient gradient(outerRect.topLeft(), outerRect.bottomLeft());

        QColor topColor, bottomColor;

        if (m_style.activeBorder.useExplicitColors) {
            topColor = mgr.resolveColor(
                m_style.activeBorder.topColor, m_style.activeBorder.customTopColor);
            bottomColor = mgr.resolveColor(
                m_style.activeBorder.bottomColor, m_style.activeBorder.customBottomColor);
        } else {
            topColor = ThemeColors::adjustBrightness(
                mgr.colors().background, m_style.activeBorder.topBrightness);
            bottomColor = ThemeColors::adjustBrightness(
                mgr.colors().background, m_style.activeBorder.bottomBrightness);
        }

        gradient.setColorAt(0.0, topColor);
        gradient.setColorAt(1.0, bottomColor);
        painter.setBrush(gradient);
    } else {
        return;
    }

    painter.drawPath(borderRing);
}

void BaseStyledWidget::drawPressLayer(QPainter& painter, const QRectF& rect)
{
    // Press layer is handled in paintEvent for correct opacity handling
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

void BaseStyledWidget::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    painter.save();
    if (!isEnabled()) {
        painter.setOpacity(0.45);
    }

    auto& mgr = WidgetStyleManager::instance();
    const auto& content = m_style.content;
    const QMargins padding = contentPadding();

    QRectF contentRect
        = rect.adjusted(padding.left(), padding.top(), -padding.right(), -padding.bottom());

    int iconSize = mgr.scaled(content.baseIconSize);
    int iconTextGap = mgr.scaled(content.baseIconTextGap);

    QColor textColor = currentTextColor();

    // Calculate positions based on icon position
    int contentX = contentRect.left();

    // Draw icon if present
    if (content.iconPosition != IconPosition::None && !icon().isNull()) {
        QRect iconRect;

        switch (content.iconPosition) {
        case IconPosition::Left:
            iconRect = QRect(contentX, (height() - iconSize) / 2, iconSize, iconSize);
            contentX += iconSize + iconTextGap;
            break;
        case IconPosition::Right:
            iconRect = QRect(
                contentRect.right() - iconSize, (height() - iconSize) / 2, iconSize, iconSize);
            break;
        case IconPosition::Center:
            iconRect
                = QRect((width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);
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

    // Draw text if not icon-only
    if (content.iconPosition != IconPosition::Center && !text().isEmpty()) {
        painter.setPen(textColor);

        QFont font = this->font();
        font.setPointSize(mgr.scaledFontSize(content.baseFontSize));
        if (content.boldWhenActive && m_activeProgress > 0.5) {
            font.setBold(true);
        }
        painter.setFont(font);

        QRect textRect;
        Qt::Alignment alignment;

        switch (content.textAlignment) {
        case ContentAlignment::Left:
            textRect = QRect(contentX, 0, contentRect.right() - contentX, height());
            alignment = Qt::AlignLeft | Qt::AlignVCenter;
            break;
        case ContentAlignment::Center:
            textRect = contentRect.toRect();
            alignment = Qt::AlignCenter;
            break;
        case ContentAlignment::Right:
            textRect = QRect(contentX, 0, contentRect.right() - contentX, height());
            alignment = Qt::AlignRight | Qt::AlignVCenter;
            break;
        }

        painter.drawText(textRect, alignment, text());
    }

    painter.restore();
}

void BaseStyledWidget::drawCustomLayers(QPainter& painter, const QRectF& rect)
{
    // Default implementation does nothing
    // Override in derived classes for custom content
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

} // namespace ruwa::ui::widgets
