// SPDX-License-Identifier: MPL-2.0

// BaseStyledPanel.cpp
#include "BaseStyledPanel.h"

#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>
#include <QLayout>
#include <QShowEvent>
#include <QtMath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

// ============================================================================
// Construction
// ============================================================================

BaseStyledPanel::BaseStyledPanel(const QString& styleName, QWidget* parent)
    : QWidget(parent)
{
    const WidgetStyle* registered = WidgetStyleManager::instance().style(styleName);
    if (registered) {
        m_style = *registered;
    } else {
        m_style = WidgetStyle::panelStyle();
    }
    initialize();
}

BaseStyledPanel::BaseStyledPanel(const WidgetStyle& style, QWidget* parent)
    : QWidget(parent)
    , m_style(style)
{
    initialize();
}

BaseStyledPanel::~BaseStyledPanel() = default;

void BaseStyledPanel::initialize()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    setupAnimations();
    updateSizeFromStyle();
    connectSignals();
}

void BaseStyledPanel::setupAnimations()
{
    auto& mgr = WidgetStyleManager::instance();

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(mgr.effectiveHoverDuration(m_style));
    m_hoverAnimation->setEasingCurve(mgr.effectiveHoverEasingIn(m_style));
}

void BaseStyledPanel::updateSizeFromStyle()
{
    auto& mgr = WidgetStyleManager::instance();

    if (m_style.metrics.fixedHeight && m_style.metrics.baseHeight > 0) {
        setFixedHeight(mgr.scaled(m_style.metrics.baseHeight));
    }

    if (m_style.metrics.fixedWidth && m_style.metrics.baseWidth > 0) {
        setFixedWidth(mgr.scaled(m_style.metrics.baseWidth));
    }

    if (m_style.metrics.baseMinWidth > 0) {
        setMinimumWidth(mgr.scaled(m_style.metrics.baseMinWidth));
    }
}

void BaseStyledPanel::connectSignals()
{
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BaseStyledPanel::onThemeChanged);

    connect(&WidgetStyleManager::instance(), &WidgetStyleManager::globalSettingsChanged, this,
        &BaseStyledPanel::onGlobalSettingsChanged);
}

// ============================================================================
// Style Management
// ============================================================================

void BaseStyledPanel::applyStyleChanges()
{
    updateSizeFromStyle();

    auto& mgr = WidgetStyleManager::instance();
    m_hoverAnimation->setDuration(mgr.effectiveHoverDuration(m_style));

    ensureBorderLayoutMargins();
    update();
}

void BaseStyledPanel::setStyle(const WidgetStyle& style)
{
    m_style = style;
    applyStyleChanges();
}

void BaseStyledPanel::setStyle(const QString& styleName)
{
    const WidgetStyle* registered = WidgetStyleManager::instance().style(styleName);
    if (registered) {
        setStyle(*registered);
    }
}

void BaseStyledPanel::setHoverProgress(qreal progress)
{
    if (qFuzzyCompare(m_hoverProgress, progress))
        return;
    m_hoverProgress = progress;
    update();
}

// ============================================================================
// Events
// ============================================================================

void BaseStyledPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureBorderLayoutMargins();
}

void BaseStyledPanel::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);

    if (!m_hoverEnabled)
        return;

    auto& mgr = WidgetStyleManager::instance();
    if (!mgr.animationsEnabled() || !mgr.hoverEffectsEnabled()) {
        m_hoverProgress = 1.0;
        update();
        return;
    }

    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(mgr.effectiveHoverEasingIn(m_style));
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
}

void BaseStyledPanel::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);

    if (!m_hoverEnabled)
        return;

    auto& mgr = WidgetStyleManager::instance();
    if (!mgr.animationsEnabled() || !mgr.hoverEffectsEnabled()) {
        m_hoverProgress = 0.0;
        update();
        return;
    }

    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(mgr.effectiveHoverEasingOut(m_style));
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
}

void BaseStyledPanel::onThemeChanged()
{
    updateSizeFromStyle();
    ensureBorderLayoutMargins();
    update();
}

void BaseStyledPanel::onGlobalSettingsChanged()
{
    auto& mgr = WidgetStyleManager::instance();
    m_hoverAnimation->setDuration(mgr.effectiveHoverDuration(m_style));

    if (!mgr.animationsEnabled()) {
        m_hoverProgress = underMouse() ? 1.0 : 0.0;
    }

    update();
}

// ============================================================================
// Helpers
// ============================================================================

int BaseStyledPanel::cornerRadius() const
{
    auto& mgr = WidgetStyleManager::instance();
    return mgr.scaled(mgr.effectiveCornerRadius(m_style));
}

QMargins BaseStyledPanel::contentPadding() const
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& base = m_style.content.basePadding;
    return QMargins(mgr.scaled(base.left()), mgr.scaled(base.top()), mgr.scaled(base.right()),
        mgr.scaled(base.bottom()));
}

void BaseStyledPanel::ensureBorderLayoutMargins()
{
    if (!m_style.border.enabled)
        return;

    QLayout* l = layout();
    if (!l)
        return;

    const int m = qCeil(m_style.border.width);
    QMargins margins = l->contentsMargins();
    if (margins.left() < m)
        margins.setLeft(m);
    if (margins.top() < m)
        margins.setTop(m);
    if (margins.right() < m)
        margins.setRight(m);
    if (margins.bottom() < m)
        margins.setBottom(m);
    l->setContentsMargins(margins);
}

// ============================================================================
// Paint
// ============================================================================

void BaseStyledPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);

    drawBackgroundLayer(painter, rect);
    drawHoverLayer(painter, rect);
    drawContentLayer(painter, rect);
    drawBorderLayer(painter, rect); // Last so content doesn't cover border at edges
}

void BaseStyledPanel::drawBackgroundLayer(QPainter& painter, const QRectF& rect)
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

void BaseStyledPanel::drawBorderLayer(QPainter& painter, const QRectF& rect)
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

void BaseStyledPanel::drawHoverLayer(QPainter& painter, const QRectF& rect)
{
    if (!m_style.hover.enabled || m_hoverProgress <= 0 || !m_hoverEnabled)
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

void BaseStyledPanel::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    // Default: nothing. Override in derived classes.
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

} // namespace ruwa::ui::widgets
