// SPDX-License-Identifier: MPL-2.0

// WidgetFadeInOverlay.cpp
#include "WidgetFadeInOverlay.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QAbstractAnimation>
#include <QEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QCoreApplication>
namespace ruwa::ui::widgets {

WidgetFadeInOverlay::WidgetFadeInOverlay(
    QWidget* target, const QColor& backgroundColor, QWidget* parent)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_target(target)
    , m_backgroundColor(backgroundColor)
{
    Q_UNUSED(parent);

    if (!m_target) {
        deleteLater();
        return;
    }

    // Top-level popup window flags
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating); // Don't steal focus
    setAttribute(Qt::WA_DeleteOnClose);
    setAutoFillBackground(false);

    // Start fully opaque
    m_opacity = 1.0;

    // Position overlay in SCREEN coordinates over target
    updateGeometry();

    // Track target movement/resize
    m_target->installEventFilter(this);

    // Also track target's window for move events
    if (m_target->window()) {
        m_target->window()->installEventFilter(this);
    }
}

void WidgetFadeInOverlay::showOverlay()
{
    // Update position right before showing
    updateGeometry();

    // Show as popup
    show();

    // Force immediate paint
    repaint();
}

void WidgetFadeInOverlay::startAnimation(
    int durationMs, int delayMs, QEasingCurve::Type easingCurve)
{
    auto doStartAnimation = [this, durationMs, easingCurve]() {
        // Ensure painted
        repaint();

        // Create fade animation
        m_animation = new QPropertyAnimation(this, "opacity", this);
        m_animation->setDuration(durationMs);
        m_animation->setStartValue(1.0);
        m_animation->setEndValue(0.0);
        m_animation->setEasingCurve(easingCurve);

        connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
            emit animationFinished();
            close(); // Will trigger deleteLater via WA_DeleteOnClose
        });

        m_animation->start(QAbstractAnimation::DeleteWhenStopped);
    };

    if (delayMs > 0) {
        QTimer::singleShot(delayMs, this, doStartAnimation);
    } else {
        doStartAnimation();
    }
}

void WidgetFadeInOverlay::setOpacity(qreal opacity)
{
    if (qFuzzyCompare(m_opacity, opacity)) {
        return;
    }

    m_opacity = qBound(0.0, opacity, 1.0);
    update();
}

void WidgetFadeInOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QColor color = m_backgroundColor;
    color.setAlphaF(m_opacity);
    painter.fillRect(rect(), color);
}

bool WidgetFadeInOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Update position when target or its window moves/resizes
    if (watched == m_target || (m_target && watched == m_target->window())) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move
            || event->type() == QEvent::Show) {
            updateGeometry();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void WidgetFadeInOverlay::updateGeometry()
{
    if (!m_target) {
        return;
    }

    // Get target's position in SCREEN coordinates
    QPoint globalPos = m_target->mapToGlobal(QPoint(0, 0));

    // Set geometry in screen coordinates (we're a top-level window)
    setGeometry(globalPos.x(), globalPos.y(), m_target->width(), m_target->height());
}

} // namespace ruwa::ui::widgets
