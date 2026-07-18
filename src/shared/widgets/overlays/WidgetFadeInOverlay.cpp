// SPDX-License-Identifier: MPL-2.0

// WidgetFadeInOverlay.cpp
#include "WidgetFadeInOverlay.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QAbstractAnimation>
#include <QEvent>
#include <QTimer>

namespace ruwa::ui::widgets {

WidgetFadeInOverlay::WidgetFadeInOverlay(
    QWidget* target, const QColor& backgroundColor, QWidget* parent)
    : QWidget(target)
    , m_target(target)
    , m_backgroundColor(backgroundColor)
{
    Q_UNUSED(parent);

    if (!m_target) {
        deleteLater();
        return;
    }

    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_DeleteOnClose);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);

    // Start fully opaque
    m_opacity = 1.0;

    updateGeometry();

    // The overlay uses target-local coordinates and only needs to follow its size.
    m_target->installEventFilter(this);
}

void WidgetFadeInOverlay::showOverlay()
{
    if (!m_target) {
        return;
    }

    updateGeometry();
    m_targetSnapshot = m_target->grab();
    show();
    raise();

    // Force immediate paint
    repaint();
}

void WidgetFadeInOverlay::startAnimation(
    int durationMs, int delayMs, QEasingCurve::Type easingCurve)
{
    auto doStartAnimation = [this, durationMs, easingCurve]() {
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

    QColor background = m_backgroundColor;
    background.setAlpha(255);
    painter.fillRect(rect(), background);

    if (!m_targetSnapshot.isNull() && m_opacity < 1.0) {
        const qreal snapshotDpr = m_targetSnapshot.devicePixelRatio();
        const QRectF sourceRect(QPointF(0, 0),
            QSizeF(
                m_targetSnapshot.width() / snapshotDpr, m_targetSnapshot.height() / snapshotDpr));
        painter.setOpacity(1.0 - m_opacity);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRectF(rect()), m_targetSnapshot, sourceRect);
    }
}

bool WidgetFadeInOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_target && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateGeometry();
        raise();
    }

    return QWidget::eventFilter(watched, event);
}

void WidgetFadeInOverlay::updateGeometry()
{
    if (!m_target) {
        return;
    }

    setGeometry(m_target->rect());
}

} // namespace ruwa::ui::widgets
