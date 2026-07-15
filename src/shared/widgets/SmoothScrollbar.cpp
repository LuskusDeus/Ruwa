// SPDX-License-Identifier: MPL-2.0

// SmoothScrollBar.cpp
#include "SmoothScrollBar.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QTimer>

namespace ruwa::ui::widgets {

SmoothScrollBar::SmoothScrollBar(Qt::Orientation orientation, QWidget* parent)
    : QScrollBar(orientation, parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAutoFillBackground(false);

    // Setup animations
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress");
    m_hoverAnimation->setDuration(200);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_visibilityAnimation = new QPropertyAnimation(this, "visibilityProgress");
    // Slightly shorter than the reserve-column slide (see SmoothScrollArea,
    // kReserveAnimationMs = 220) so the fade has finished by the time the slide ends.
    // OutCubic (matching the slide) makes opacity change from the very first frame —
    // InOutCubic's slow start left the bar near full opacity for most of the slide,
    // so the fade looked like it only began once the bar had stopped moving.
    m_visibilityAnimation->setDuration(200);
    m_visibilityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    // Once a fade-out completes, actually take the widget out of the layout so it
    // stops eating hover/paint work. Fade-in keeps it shown throughout.
    connect(m_visibilityAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_visibilityProgress <= 0.0) {
            QScrollBar::hide();
        }
    });

    // Scroll repeat timer for arrow buttons (starts after initial delay)
    m_scrollRepeatTimer = new QTimer(this);
    m_scrollRepeatTimer->setInterval(50); // Repeat every 50ms when held
    connect(m_scrollRepeatTimer, &QTimer::timeout, this, [this]() {
        if (m_upButtonPressed) {
            emit stepScrollRequested(-singleStep());
        } else if (m_downButtonPressed) {
            emit stepScrollRequested(singleStep());
        }
    });

    // Initial delay timer — fires once, then starts the repeat timer
    m_scrollInitialDelayTimer = new QTimer(this);
    m_scrollInitialDelayTimer->setSingleShot(true);
    m_scrollInitialDelayTimer->setInterval(350); // Wait 350ms before repeating
    connect(m_scrollInitialDelayTimer, &QTimer::timeout, this,
        [this]() { m_scrollRepeatTimer->start(); });

    // Style
    if (orientation == Qt::Vertical) {
        setFixedWidth(12);
    } else {
        setFixedHeight(12);
    }
}

SmoothScrollBar::~SmoothScrollBar()
{
    if (m_scrollRepeatTimer) {
        m_scrollRepeatTimer->stop();
    }
    if (m_scrollInitialDelayTimer) {
        m_scrollInitialDelayTimer->stop();
    }
    delete m_hoverAnimation;
    delete m_visibilityAnimation;
}

void SmoothScrollBar::setHoverProgress(qreal progress)
{
    m_hoverProgress = progress;
    requestRepaint();
}

void SmoothScrollBar::setVisibilityProgress(qreal progress)
{
    m_visibilityProgress = progress;
    requestRepaint();
}

void SmoothScrollBar::setTransparentTrack(bool transparent)
{
    if (m_transparentTrack == transparent) {
        return;
    }
    m_transparentTrack = transparent;
    if (transparent) {
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAttribute(Qt::WA_NoSystemBackground, true);
        // For child widgets on Windows this can produce black backing artifacts.
        // We only need "non-opaque + no system background" for transparent track.
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_StyledBackground, false);
    } else {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAttribute(Qt::WA_NoSystemBackground, false);
        setAttribute(Qt::WA_TranslucentBackground, false);
    }
    setAutoFillBackground(false);
    update();
}

void SmoothScrollBar::requestRepaint()
{
    // Force parent to repaint the region behind us first,
    // otherwise semi-transparent frames stack (ghosting)
    // on frameless windows with WA_TranslucentBackground.
    if (parentWidget()) {
        parentWidget()->update(geometry());
    }
    update();
}

void SmoothScrollBar::showAnimated()
{
    // Make sure we are actually mapped before fading in, otherwise the animation
    // runs on a hidden widget and the bar just pops in when finally shown.
    if (isHidden()) {
        QScrollBar::show();
    }

    if (m_visibilityAnimation->state() == QAbstractAnimation::Stopped
        && m_visibilityProgress >= 1.0) {
        return; // Already fully visible.
    }

    m_visibilityAnimation->stop();
    m_visibilityAnimation->setStartValue(m_visibilityProgress);
    m_visibilityAnimation->setEndValue(1.0);
    m_visibilityAnimation->start();
}

void SmoothScrollBar::hideAnimated()
{
    if (isHidden()) {
        return;
    }

    if (m_visibilityAnimation->state() == QAbstractAnimation::Stopped
        && m_visibilityProgress <= 0.0) {
        QScrollBar::hide();
        return;
    }

    // Keep the widget visible while it fades; the finished handler hides it.
    m_visibilityAnimation->stop();
    m_visibilityAnimation->setStartValue(m_visibilityProgress);
    m_visibilityAnimation->setEndValue(0.0);
    m_visibilityAnimation->start();
}

QRect SmoothScrollBar::getHandleRect() const
{
    if (maximum() == 0) {
        return QRect();
    }

    int range = maximum() - minimum();
    if (range <= 0) {
        return QRect();
    }

    int handleSize = getHandleSize();

    if (orientation() == Qt::Vertical) {
        int trackHeight = height() - 24; // 12px top + 12px bottom for arrows
        int maxHandlePos = trackHeight - handleSize;
        if (maxHandlePos < 0)
            maxHandlePos = 0;

        int handlePos = 12 + (value() * maxHandlePos) / range; // Start at 12 (after top button)

        int width = 12;
        int baseWidth = 4; // Narrow base: 4px
        int maxWidth = 8; // Max width on hover: 8px (only 2x increase)
        int actualWidth = baseWidth + int((maxWidth - baseWidth) * m_hoverProgress);
        int x = (12 - actualWidth) / 2;

        return QRect(x, handlePos, actualWidth, handleSize);
    } else {
        int trackWidth = width() - 24;
        int maxHandlePos = trackWidth - handleSize;
        if (maxHandlePos < 0)
            maxHandlePos = 0;

        int handlePos = 12 + (value() * maxHandlePos) / range;

        int height = 12;
        int baseHeight = 4;
        int maxHeight = 8;
        int actualHeight = baseHeight + int((maxHeight - baseHeight) * m_hoverProgress);
        int y = (12 - actualHeight) / 2;

        return QRect(handlePos, y, handleSize, actualHeight);
    }
}

int SmoothScrollBar::getHandleSize() const
{
    if (maximum() == 0) {
        return 0;
    }

    int range = maximum() - minimum() + pageStep();

    if (orientation() == Qt::Vertical) {
        int trackHeight = height() - 24; // Reserve 12px top + 12px bottom for arrows
        int handleSize = (pageStep() * trackHeight) / range;
        return qMax(20, qMin(handleSize, trackHeight));
    } else {
        int trackWidth = width() - 24;
        int handleSize = (pageStep() * trackWidth) / range;
        return qMax(20, qMin(handleSize, trackWidth));
    }
}

QRect SmoothScrollBar::getUpButtonRect() const
{
    if (orientation() == Qt::Vertical) {
        return QRect(0, 0, 12, 12);
    } else {
        return QRect(0, 0, 12, 12);
    }
}

QRect SmoothScrollBar::getDownButtonRect() const
{
    if (orientation() == Qt::Vertical) {
        return QRect(0, height() - 12, 12, 12);
    } else {
        return QRect(width() - 12, 0, 12, 12);
    }
}

void SmoothScrollBar::drawArrowButton(
    QPainter& painter, const QRect& rect, bool isUp, bool isPressed)
{
    // Only draw arrows when hovering (no circles)
    if (m_hoverProgress <= 0.3) {
        return;
    }

    painter.save();

    // Draw arrow directly - no circle background
    qreal arrowOpacity = (m_hoverProgress - 0.3) / 0.7; // Fade in from 0.3 to 1.0
    if (isPressed) {
        arrowOpacity = qMin(1.0, arrowOpacity * 1.5);
    }

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    painter.setPen(QPen(colors.overlay(arrowOpacity * 0.71), 1.5));
    painter.setRenderHint(QPainter::Antialiasing);

    QPointF center = rect.center();
    qreal arrowSize = 3.5;

    if (isUp) {
        // Up arrow
        QPointF tip(center.x(), center.y() - arrowSize);
        QPointF left(center.x() - arrowSize, center.y() + arrowSize * 0.5);
        QPointF right(center.x() + arrowSize, center.y() + arrowSize * 0.5);

        painter.drawLine(left, tip);
        painter.drawLine(tip, right);
    } else {
        // Down arrow
        QPointF tip(center.x(), center.y() + arrowSize);
        QPointF left(center.x() - arrowSize, center.y() - arrowSize * 0.5);
        QPointF right(center.x() + arrowSize, center.y() - arrowSize * 0.5);

        painter.drawLine(left, tip);
        painter.drawLine(tip, right);
    }

    painter.restore();
}

void SmoothScrollBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor backgroundColor = parentWidget()
        ? parentWidget()->palette().color(QPalette::Window)
        : palette().color(QPalette::Window);

    // Opaque mode paints our background; transparent mode leaves parent pixels visible.
    if (!m_transparentTrack) {
        painter.fillRect(rect(), backgroundColor);
    }

    if (m_visibilityProgress <= 0.0) {
        return;
    }

    painter.setOpacity(m_visibilityProgress);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // While there is a live range, recompute the handle and remember it. When the
    // range has collapsed to 0 (e.g. content shrank) keep drawing the last handle so
    // the fade-out actually plays instead of the bar snapping away.
    QRect handleRect;
    if (maximum() > 0) {
        handleRect = getHandleRect();
        if (!handleRect.isEmpty()) {
            m_cachedHandleRect = handleRect;
        }

        // Arrow buttons only make sense while scrolling is possible.
        drawArrowButton(painter, getUpButtonRect(), true, m_upButtonPressed);
        drawArrowButton(painter, getDownButtonRect(), false, m_downButtonPressed);
    } else {
        handleRect = m_cachedHandleRect;
    }

    // Draw handle
    if (!handleRect.isEmpty()) {
        // Handle color with hover
        QColor handleColor = colors.overlay(0.31 + m_hoverProgress * 0.31);

        painter.setPen(Qt::NoPen);
        painter.setBrush(handleColor);
        painter.drawRoundedRect(handleRect, 3, 3);
    }
}

void SmoothScrollBar::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);

    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();

    // Don't auto-hide - keep visible
    showAnimated();
}

void SmoothScrollBar::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);

    if (!m_isDragging) {
        m_hoverAnimation->stop();
        m_hoverAnimation->setStartValue(m_hoverProgress);
        m_hoverAnimation->setEndValue(0.0);
        m_hoverAnimation->start();

        // Don't auto-hide on leave
    }
}

void SmoothScrollBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QRect upButton = getUpButtonRect();
        QRect downButton = getDownButtonRect();
        QRect handleRect = getHandleRect();

        if (upButton.contains(event->pos())) {
            // Clicked on up button — request smooth scroll step
            m_upButtonPressed = true;
            emit stepScrollRequested(-singleStep());
            m_scrollInitialDelayTimer->start();
            update();
            event->accept();
            return;
        } else if (downButton.contains(event->pos())) {
            // Clicked on down button — request smooth scroll step
            m_downButtonPressed = true;
            emit stepScrollRequested(singleStep());
            m_scrollInitialDelayTimer->start();
            update();
            event->accept();
            return;
        } else if (handleRect.contains(event->pos())) {
            // Dragging handle
            m_isDragging = true;
            m_dragStartPos = (orientation() == Qt::Vertical) ? event->pos().y() : event->pos().x();
            m_dragStartValue = value();
            event->accept();
            return;
        } else {
            // Click on track - jump to position
            int pos = (orientation() == Qt::Vertical) ? event->pos().y() : event->pos().x();
            int trackSize = (orientation() == Qt::Vertical) ? height() - 24 : width() - 24;
            int handleSize = getHandleSize();

            int divisor = trackSize - handleSize;
            if (divisor > 0) {
                int newValue = ((pos - 12 - handleSize / 2) * maximum()) / divisor;
                setValue(qBound(minimum(), newValue, maximum()));
            }
        }
    }

    QScrollBar::mousePressEvent(event);
}

void SmoothScrollBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_upButtonPressed || m_downButtonPressed) {
            m_upButtonPressed = false;
            m_downButtonPressed = false;
            m_scrollInitialDelayTimer->stop();
            m_scrollRepeatTimer->stop();
            update();
        }

        if (m_isDragging) {
            m_isDragging = false;

            if (!underMouse()) {
                m_hoverAnimation->stop();
                m_hoverAnimation->setStartValue(m_hoverProgress);
                m_hoverAnimation->setEndValue(0.0);
                m_hoverAnimation->start();
            }
        }
    }

    QScrollBar::mouseReleaseEvent(event);
}

void SmoothScrollBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        int currentPos = (orientation() == Qt::Vertical) ? event->pos().y() : event->pos().x();
        int delta = currentPos - m_dragStartPos;

        int trackSize = (orientation() == Qt::Vertical) ? height() - 24 : width() - 24;
        int handleSize = getHandleSize();
        int maxTravel = trackSize - handleSize;

        if (maxTravel > 0) {
            int valueDelta = (delta * maximum()) / maxTravel;
            setValue(qBound(minimum(), m_dragStartValue + valueDelta, maximum()));
        }

        event->accept();
        return;
    }

    QScrollBar::mouseMoveEvent(event);
}

} // namespace ruwa::ui::widgets
