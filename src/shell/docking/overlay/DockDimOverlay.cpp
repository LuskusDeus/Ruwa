// SPDX-License-Identifier: MPL-2.0

// DockDimOverlay.cpp
#include "DockDimOverlay.h"

#include <QPainter>
#include <QVariantAnimation>
#include <QEasingCurve>

namespace ruwa::ui::docking {

DockDimOverlay::DockDimOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setVisible(false);

    // Default dim color (dark overlay)
    m_dimColor = QColor(0, 0, 0);

    // Setup animation
    m_animation = new QVariantAnimation(this);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_animation, &QVariantAnimation::valueChanged, this,
        &DockDimOverlay::onAnimationValueChanged);
    connect(m_animation, &QVariantAnimation::finished, this, &DockDimOverlay::onAnimationFinished);
}

DockDimOverlay::~DockDimOverlay()
{
    if (m_animation) {
        m_animation->stop();
    }
}

// ============================================================================
// State
// ============================================================================

void DockDimOverlay::showForTarget(QWidget* target)
{
    if (!target) {
        hideAnimated();
        return;
    }

    // If same target and visible - just update geometry
    if (m_target == target && isVisible()) {
        // If was hiding, reverse to showing
        if (m_hiding) {
            m_hiding = false;
            if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
                m_animation->stop();
            }
            // Continue from current opacity
            startAnimation(m_maxOpacity);
        }
        // Always update geometry (target might be animating)
        updateGeometryFromTarget();
        update(); // Force repaint
        return;
    }

    // Target changed - start fresh animation
    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    m_target = target;
    m_hiding = false;
    m_currentOpacity = 0.0;

    updateGeometryFromTarget();
    show();

    startAnimation(m_maxOpacity);
}

void DockDimOverlay::hideAnimated()
{
    if (!isVisible() && m_currentOpacity <= 0.0) {
        return;
    }

    m_hiding = true;
    startAnimation(0.0);
}

void DockDimOverlay::hideImmediate()
{
    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    m_currentOpacity = 0.0;
    m_hiding = false;
    m_target = nullptr;
    hide();
}

bool DockDimOverlay::isActiveOrShowing() const
{
    return isVisible() && !m_hiding;
}

// ============================================================================
// Appearance
// ============================================================================

void DockDimOverlay::setDimColor(const QColor& color)
{
    m_dimColor = color;
    update();
}

void DockDimOverlay::setMaxOpacity(qreal opacity)
{
    m_maxOpacity = qBound(0.0, opacity, 1.0);
}

void DockDimOverlay::setAnimationDuration(int ms)
{
    m_animationDuration = qMax(0, ms);
}

void DockDimOverlay::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    // Use a dark color based on background
    m_dimColor = colors.background.darker(150);
    update();
}

// ============================================================================
// Events
// ============================================================================

void DockDimOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (m_currentOpacity <= 0.0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor fillColor = m_dimColor;
    fillColor.setAlphaF(m_currentOpacity);

    painter.fillRect(rect(), fillColor);
}

// ============================================================================
// Private Slots
// ============================================================================

void DockDimOverlay::onAnimationValueChanged(const QVariant& value)
{
    m_currentOpacity = value.toReal();

    // Update geometry during animation in case target moves
    if (!m_hiding && m_target) {
        updateGeometryFromTarget();
    }

    update();
}

void DockDimOverlay::onAnimationFinished()
{
    if (m_hiding) {
        m_hiding = false;
        m_target = nullptr;
        hide();
    }
}

// ============================================================================
// Private
// ============================================================================

void DockDimOverlay::updateGeometryFromTarget()
{
    if (!m_target || !parentWidget()) {
        return;
    }

    // Get target's geometry in parent's coordinate space
    QRect targetRect = m_target->rect();
    QPoint targetTopLeft = m_target->mapToGlobal(targetRect.topLeft());
    QPoint localTopLeft = parentWidget()->mapFromGlobal(targetTopLeft);

    setGeometry(QRect(localTopLeft, targetRect.size()));
}

void DockDimOverlay::startAnimation(qreal targetOpacity)
{
    if (!m_animation) {
        return;
    }

    // Stop current animation
    if (m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    // Skip animation if duration is 0
    if (m_animationDuration <= 0) {
        m_currentOpacity = targetOpacity;
        if (targetOpacity <= 0.0 && m_hiding) {
            m_hiding = false;
            m_target = nullptr;
            hide();
        }
        update();
        return;
    }

    m_animation->setStartValue(m_currentOpacity);
    m_animation->setEndValue(targetOpacity);
    m_animation->setDuration(m_animationDuration);
    m_animation->start();
}

} // namespace ruwa::ui::docking
