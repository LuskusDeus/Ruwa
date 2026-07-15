// SPDX-License-Identifier: MPL-2.0

// BaseAnimatedButton.cpp
#include "BaseAnimatedButton.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace ruwa::ui::widgets {

namespace {

bool isCursorEffectivelyOverButton(const BaseAnimatedButton* button)
{
    if (!button) {
        return false;
    }

    const QPoint globalPos = QCursor::pos();
    const QPoint localPos = button->mapFromGlobal(globalPos);
    if (!button->rect().contains(localPos)) {
        return false;
    }

    QWidget* topWidgetAtCursor = QApplication::widgetAt(globalPos);
    while (topWidgetAtCursor) {
        if (topWidgetAtCursor == button) {
            return true;
        }
        topWidgetAtCursor = topWidgetAtCursor->parentWidget();
    }

    return false;
}

void reconcileHoverStateLater(BaseAnimatedButton* button)
{
    QPointer<BaseAnimatedButton> guard(button);
    QTimer::singleShot(0, button, [guard]() {
        if (!guard) {
            return;
        }

        guard->setHovered(isCursorEffectivelyOverButton(guard));
    });
}

} // anonymous namespace

BaseAnimatedButton::BaseAnimatedButton(QWidget* parent)
    : QPushButton(parent)
{
    // Initialize hover animation
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(200);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // Initialize active state animation
    m_activeAnimation = new QPropertyAnimation(this, "activeProgress", this);
    m_activeAnimation->setDuration(250);
    m_activeAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    // Configure widget for custom painting
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    // Avoid native QPushButton chrome repainting over custom paint (common hover flicker).
    setFlat(true);
    setAutoDefault(false);
    setDefault(false);
}

void BaseAnimatedButton::setActive(bool active)
{
    if (m_isActive == active)
        return;

    m_isActive = active;

    // Animate to new state
    m_activeAnimation->stop();
    m_activeAnimation->setStartValue(m_activeProgress);
    m_activeAnimation->setEndValue(active ? 1.0 : 0.0);
    m_activeAnimation->start();
}

void BaseAnimatedButton::setActiveImmediate(bool active)
{
    m_activeAnimation->stop();
    m_isActive = active;
    setActiveProgress(active ? 1.0 : 0.0);
}

void BaseAnimatedButton::setHoverProgress(qreal progress)
{
    if (qFuzzyCompare(m_hoverProgress, progress))
        return;

    m_hoverProgress = progress;
    update();
}

void BaseAnimatedButton::setActiveProgress(qreal progress)
{
    if (qFuzzyCompare(m_activeProgress, progress))
        return;

    m_activeProgress = progress;
    update();
}

void BaseAnimatedButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    if (!isEnabled()) {
        return;
    }
    setHovered(true);
}

void BaseAnimatedButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    // Defer reconciliation because hover can change due to overlay/popup creation in the same tick.
    reconcileHoverStateLater(this);
}

void BaseAnimatedButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPressed = true;
        update();
    }
    QPushButton::mousePressEvent(event);
}

void BaseAnimatedButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPressed = false;
        update();
    }
    QPushButton::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton) {
        reconcileHoverStateLater(this);
    }
}

void BaseAnimatedButton::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!isEnabled()) {
            m_isPressed = false;
            m_isHovered = false;
            m_hoverAnimation->stop();
            setHoverProgress(0.0);
        }
        update();
    }
    QPushButton::changeEvent(event);
}

void BaseAnimatedButton::setHoverDuration(int ms)
{
    m_hoverAnimation->setDuration(ms);
}

void BaseAnimatedButton::setActiveDuration(int ms)
{
    m_activeAnimation->setDuration(ms);
}

void BaseAnimatedButton::setHovered(bool hovered)
{
    if (m_isHovered == hovered) {
        return;
    }
    m_isHovered = hovered;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

} // namespace ruwa::ui::widgets
