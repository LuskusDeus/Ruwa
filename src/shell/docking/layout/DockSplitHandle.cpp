// SPDX-License-Identifier: MPL-2.0

// DockSplitHandle.cpp
#include "DockSplitHandle.h"

#include <QPainter>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPoint>

namespace ruwa::ui::docking {

DockSplitHandle::DockSplitHandle(
    DockSplitNode* splitNode, int handleIndex, SplitDirection direction, QWidget* parent)
    : QWidget(parent)
    , m_splitNode(splitNode)
    , m_handleIndex(handleIndex)
    , m_direction(direction)
{
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    updateCursor();
}

void DockSplitHandle::setDirection(SplitDirection direction)
{
    if (m_direction == direction) {
        return;
    }

    m_direction = direction;
    updateCursor();
    update();
}

void DockSplitHandle::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    // Handles are invisible - always use background color
    // No hover or press highlight - only cursor changes
    m_normalColor = colors.background;
    m_hoverColor = colors.background;
    m_pressColor = colors.background;
    update();
}

void DockSplitHandle::setColors(const QColor& normal, const QColor& hover, const QColor& pressed)
{
    m_normalColor = normal;
    m_hoverColor = hover;
    m_pressColor = pressed;
    update();
}

void DockSplitHandle::paintEvent(QPaintEvent* /*event*/)
{
    // Handles are invisible - just fill with background color
    // No grip dots, no hover/press highlighting
    QPainter painter(this);
    painter.fillRect(rect(), m_normalColor);
}

void DockSplitHandle::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void DockSplitHandle::leaveEvent(QEvent* event)
{
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

void DockSplitHandle::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;

        if (m_direction == SplitDirection::Horizontal) {
            m_lastDragPos = event->globalPosition().toPoint().x();
        } else {
            m_lastDragPos = event->globalPosition().toPoint().y();
        }

        emit dragStarted(m_splitNode, m_handleIndex);
        update();
    }
}

void DockSplitHandle::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        return;
    }

    int currentPos;
    if (m_direction == SplitDirection::Horizontal) {
        currentPos = event->globalPosition().toPoint().x();
    } else {
        currentPos = event->globalPosition().toPoint().y();
    }

    int delta = currentPos - m_lastDragPos;

    if (delta != 0) {
        const int handlePosBeforeDrag = dragAxisGlobalPosition();
        emit dragMoved(m_splitNode, m_handleIndex, delta);

        // Keep the drag baseline tied to the real handle movement, not cursor overshoot.
        const int appliedDelta = dragAxisGlobalPosition() - handlePosBeforeDrag;
        m_lastDragPos += appliedDelta;
    }
}

void DockSplitHandle::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        emit dragFinished(m_splitNode, m_handleIndex);
        update();
    }
}

void DockSplitHandle::updateCursor()
{
    if (m_direction == SplitDirection::Horizontal) {
        setCursor(Qt::SplitHCursor);
    } else {
        setCursor(Qt::SplitVCursor);
    }
}

int DockSplitHandle::dragAxisGlobalPosition() const
{
    const QPoint topLeft = mapToGlobal(rect().topLeft());
    return (m_direction == SplitDirection::Horizontal) ? topLeft.x() : topLeft.y();
}

} // namespace ruwa::ui::docking
