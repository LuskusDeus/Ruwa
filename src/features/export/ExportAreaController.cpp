// SPDX-License-Identifier: MPL-2.0

#include "ExportAreaController.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"

#include <QWidget>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {

ExportAreaController::ExportAreaController(QObject* parent)
    : QObject(parent)
{
}

void ExportAreaController::setGlWidget(aether::OpenGLCanvasWidget* widget)
{
    m_glWidget = widget;
}

void ExportAreaController::setContentWidget(QWidget* widget)
{
    m_contentWidget = widget;
}

void ExportAreaController::setCanvasSize(const QSize& size)
{
    m_canvasSize = QSize(qMax(1, size.width()), qMax(1, size.height()));
    m_exportFrame = normalizedRect(m_exportFrame);
    if (m_active) {
        syncSelectionToFrame();
        updateOverlay();
    }
}

void ExportAreaController::setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode)
{
    m_canvasBoundsMode = mode;
    m_exportFrame = normalizedRect(m_exportFrame);
    if (m_active && !isInteractionActive()) {
        syncSelectionToFrame();
        updateOverlay();
    }
}

void ExportAreaController::setCallbacks(Callbacks callbacks)
{
    m_callbacks = std::move(callbacks);
}

void ExportAreaController::setExportFrame(const QRect& frame)
{
    m_exportFrame = normalizedRect(frame);
    if (m_active && !isInteractionActive()) {
        syncSelectionToFrame();
        updateOverlay();
    }
}

QRect ExportAreaController::exportFrame() const
{
    return normalizedRect(m_exportFrame);
}

void ExportAreaController::enter()
{
    m_active = true;
    m_isSelecting = false;
    m_isMoving = false;
    m_isResizing = false;
    m_dragDetected = false;
    m_activeHandle = Handle::None;
    syncSelectionToFrame();
    updateOverlay();
}

void ExportAreaController::exit()
{
    const QRectF lastRect = m_selectionWorld;
    m_active = false;
    m_isSelecting = false;
    m_isMoving = false;
    m_isResizing = false;
    m_dragDetected = false;
    m_activeHandle = Handle::None;
    m_selectionWorld = QRectF();

    if (m_glWidget) {
        m_glWidget->setCanvasResizeOverlayState(false, lastRect, false);
    }
}

bool ExportAreaController::isActive() const
{
    return m_active;
}

bool ExportAreaController::isSelectingOrMoving() const
{
    return m_isSelecting || m_isMoving;
}

bool ExportAreaController::isInteractionActive() const
{
    return m_isSelecting || m_isMoving || m_isResizing;
}

bool ExportAreaController::handleMousePress(const aether::Vector2& worldPos,
    const QPoint& globalPos, const QPoint& localPos, Qt::MouseButton button)
{
    if (!m_active || button != Qt::LeftButton || !m_glWidget || !m_glWidget->isInitialized()) {
        return false;
    }

    const Handle hitHandle = hitHandleAt(globalPos);
    if (hitHandle != Handle::None) {
        m_isResizing = true;
        m_isMoving = false;
        m_isSelecting = false;
        m_activeHandle = hitHandle;
        m_resizeAnchorWorld = worldPos;
        m_resizeStartRect = m_selectionWorld.normalized();
        updateOverlay();
        return true;
    }

    if (!m_selectionWorld.isEmpty() && containsPoint(globalPos)) {
        m_isMoving = true;
        m_isResizing = false;
        m_isSelecting = false;
        m_moveAnchorWorld = worldPos;
        m_moveStartRect = m_selectionWorld.normalized();
        updateOverlay();
        return true;
    }

    m_pressPos = localPos;
    m_startWorld = worldPos;
    m_isSelecting = true;
    m_isMoving = false;
    m_isResizing = false;
    m_dragDetected = false;
    m_activeHandle = Handle::None;
    m_selectionWorld
        = QRectF(static_cast<qreal>(worldPos.x), static_cast<qreal>(worldPos.y), 0.5, 0.5);
    updateOverlay();
    return true;
}

bool ExportAreaController::handleMouseMove(
    const aether::Vector2& worldPos, const QPoint& globalPos, const QPoint& localPos)
{
    Q_UNUSED(globalPos);

    if (!m_active) {
        return false;
    }

    if (m_isResizing) {
        QRectF rect = m_resizeStartRect;
        const qreal dx = static_cast<qreal>(worldPos.x - m_resizeAnchorWorld.x);
        const qreal dy = static_cast<qreal>(worldPos.y - m_resizeAnchorWorld.y);

        switch (m_activeHandle) {
        case Handle::TopLeft:
            rect.setLeft(m_resizeStartRect.left() + dx);
            rect.setTop(m_resizeStartRect.top() + dy);
            break;
        case Handle::Top:
            rect.setTop(m_resizeStartRect.top() + dy);
            break;
        case Handle::TopRight:
            rect.setRight(m_resizeStartRect.right() + dx);
            rect.setTop(m_resizeStartRect.top() + dy);
            break;
        case Handle::Right:
            rect.setRight(m_resizeStartRect.right() + dx);
            break;
        case Handle::BottomRight:
            rect.setRight(m_resizeStartRect.right() + dx);
            rect.setBottom(m_resizeStartRect.bottom() + dy);
            break;
        case Handle::Bottom:
            rect.setBottom(m_resizeStartRect.bottom() + dy);
            break;
        case Handle::BottomLeft:
            rect.setLeft(m_resizeStartRect.left() + dx);
            rect.setBottom(m_resizeStartRect.bottom() + dy);
            break;
        case Handle::Left:
            rect.setLeft(m_resizeStartRect.left() + dx);
            break;
        case Handle::None:
            break;
        }

        m_selectionWorld = rect.normalized();
        commitSelection();
        updateOverlay();
        return true;
    }

    if (m_isMoving) {
        const qreal dx = static_cast<qreal>(worldPos.x - m_moveAnchorWorld.x);
        const qreal dy = static_cast<qreal>(worldPos.y - m_moveAnchorWorld.y);
        m_selectionWorld = m_moveStartRect.translated(dx, dy);
        commitSelection();
        updateOverlay();
        return true;
    }

    if (m_isSelecting) {
        if (!m_dragDetected && (localPos - m_pressPos).manhattanLength() >= kDragThreshold) {
            m_dragDetected = true;
        }

        if (m_dragDetected) {
            const qreal left
                = qMin(static_cast<qreal>(m_startWorld.x), static_cast<qreal>(worldPos.x));
            const qreal top
                = qMin(static_cast<qreal>(m_startWorld.y), static_cast<qreal>(worldPos.y));
            const qreal right
                = qMax(static_cast<qreal>(m_startWorld.x), static_cast<qreal>(worldPos.x));
            const qreal bottom
                = qMax(static_cast<qreal>(m_startWorld.y), static_cast<qreal>(worldPos.y));
            m_selectionWorld = QRectF(QPointF(left, top), QPointF(right, bottom));
            commitSelection();
        }

        updateOverlay();
        return true;
    }

    return false;
}

bool ExportAreaController::handleMouseRelease(
    const aether::Vector2& worldPos, const QPoint& globalPos, Qt::MouseButton button)
{
    Q_UNUSED(worldPos);
    Q_UNUSED(globalPos);

    if (!m_active || button != Qt::LeftButton) {
        return false;
    }

    if (m_isResizing) {
        m_isResizing = false;
        m_activeHandle = Handle::None;
        commitSelection();
        updateOverlay();
        return true;
    }

    if (m_isMoving) {
        m_isMoving = false;
        commitSelection();
        updateOverlay();
        return true;
    }

    if (m_isSelecting) {
        m_isSelecting = false;

        if (!m_dragDetected) {
            syncSelectionToFrame();
        } else {
            m_selectionWorld = m_selectionWorld.normalized();
            commitSelection();
        }

        updateOverlay();
        return true;
    }

    return false;
}

ExportAreaController::Handle ExportAreaController::hitHandleAt(const QPoint& globalPos) const
{
    if (!m_active || m_selectionWorld.isEmpty() || !m_contentWidget) {
        return Handle::None;
    }

    const QRectF realRect = selectionRectInWidget();
    if (realRect.isEmpty()) {
        return Handle::None;
    }

    const QPointF p = m_contentWidget->mapFromGlobal(globalPos);
    const QRectF handleRect = realRect.adjusted(-kHandleFramePaddingPx, -kHandleFramePaddingPx,
        kHandleFramePaddingPx, kHandleFramePaddingPx);

    const QPointF tl = handleRect.topLeft();
    const QPointF tr = handleRect.topRight();
    const QPointF br = handleRect.bottomRight();
    const QPointF bl = handleRect.bottomLeft();

    const QRectF tlBox(
        tl.x() - kHandleHitPx, tl.y() - kHandleHitPx, kHandleHitPx * 2.0, kHandleHitPx * 2.0);
    const QRectF trBox(
        tr.x() - kHandleHitPx, tr.y() - kHandleHitPx, kHandleHitPx * 2.0, kHandleHitPx * 2.0);
    const QRectF brBox(
        br.x() - kHandleHitPx, br.y() - kHandleHitPx, kHandleHitPx * 2.0, kHandleHitPx * 2.0);
    const QRectF blBox(
        bl.x() - kHandleHitPx, bl.y() - kHandleHitPx, kHandleHitPx * 2.0, kHandleHitPx * 2.0);

    if (tlBox.contains(p))
        return Handle::TopLeft;
    if (trBox.contains(p))
        return Handle::TopRight;
    if (brBox.contains(p))
        return Handle::BottomRight;
    if (blBox.contains(p))
        return Handle::BottomLeft;

    const qreal halfSideLen = kSideLengthPx * 0.5;
    const qreal hx = handleRect.center().x();
    const qreal hy = handleRect.center().y();
    const QRectF topSeg(
        hx - halfSideLen, handleRect.top() - kHandleHitPx, kSideLengthPx, kHandleHitPx * 2.0);
    const QRectF bottomSeg(
        hx - halfSideLen, handleRect.bottom() - kHandleHitPx, kSideLengthPx, kHandleHitPx * 2.0);
    const QRectF leftSeg(
        handleRect.left() - kHandleHitPx, hy - halfSideLen, kHandleHitPx * 2.0, kSideLengthPx);
    const QRectF rightSeg(
        handleRect.right() - kHandleHitPx, hy - halfSideLen, kHandleHitPx * 2.0, kSideLengthPx);

    if (topSeg.contains(p))
        return Handle::Top;
    if (rightSeg.contains(p))
        return Handle::Right;
    if (bottomSeg.contains(p))
        return Handle::Bottom;
    if (leftSeg.contains(p))
        return Handle::Left;

    return Handle::None;
}

bool ExportAreaController::containsPoint(const QPoint& globalPos) const
{
    if (!m_active || m_selectionWorld.isEmpty() || !m_contentWidget) {
        return false;
    }
    return selectionRectInWidget().contains(m_contentWidget->mapFromGlobal(globalPos));
}

Qt::CursorShape ExportAreaController::cursorForHandle(Handle handle)
{
    switch (handle) {
    case Handle::TopLeft:
    case Handle::BottomRight:
        return Qt::SizeFDiagCursor;
    case Handle::TopRight:
    case Handle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case Handle::Top:
    case Handle::Bottom:
        return Qt::SizeVerCursor;
    case Handle::Left:
    case Handle::Right:
        return Qt::SizeHorCursor;
    case Handle::None:
    default:
        return Qt::CrossCursor;
    }
}

Qt::CursorShape ExportAreaController::cursorForPosition(const QPoint& globalPos) const
{
    if (!m_active) {
        return Qt::ArrowCursor;
    }
    if (m_isResizing) {
        return cursorForHandle(m_activeHandle);
    }
    if (m_isMoving) {
        return Qt::SizeAllCursor;
    }
    if (m_isSelecting) {
        return Qt::CrossCursor;
    }

    const Handle hitHandle = hitHandleAt(globalPos);
    if (hitHandle != Handle::None) {
        return cursorForHandle(hitHandle);
    }
    if (containsPoint(globalPos)) {
        return Qt::SizeAllCursor;
    }
    return Qt::CrossCursor;
}

QRectF ExportAreaController::selectionRectInWidget() const
{
    if (!m_glWidget || m_selectionWorld.isEmpty()) {
        return {};
    }

    const QRectF wr = m_selectionWorld.normalized();
    const aether::Vector2 v1 = m_glWidget->screenFromDocumentWorld(
        { static_cast<float>(wr.left()), static_cast<float>(wr.top()) });
    const aether::Vector2 v2 = m_glWidget->screenFromDocumentWorld(
        { static_cast<float>(wr.right()), static_cast<float>(wr.top()) });
    const aether::Vector2 v3 = m_glWidget->screenFromDocumentWorld(
        { static_cast<float>(wr.right()), static_cast<float>(wr.bottom()) });
    const aether::Vector2 v4 = m_glWidget->screenFromDocumentWorld(
        { static_cast<float>(wr.left()), static_cast<float>(wr.bottom()) });

    const QPoint glTopLeft = m_glWidget->geometry().topLeft();
    const QPointF p1f(glTopLeft.x() + v1.x, glTopLeft.y() + v1.y);
    const QPointF p2f(glTopLeft.x() + v2.x, glTopLeft.y() + v2.y);
    const QPointF p3f(glTopLeft.x() + v3.x, glTopLeft.y() + v3.y);
    const QPointF p4f(glTopLeft.x() + v4.x, glTopLeft.y() + v4.y);

    const qreal minX = qMin(qMin(p1f.x(), p2f.x()), qMin(p3f.x(), p4f.x()));
    const qreal maxX = qMax(qMax(p1f.x(), p2f.x()), qMax(p3f.x(), p4f.x()));
    const qreal minY = qMin(qMin(p1f.y(), p2f.y()), qMin(p3f.y(), p4f.y()));
    const qreal maxY = qMax(qMax(p1f.y(), p2f.y()), qMax(p3f.y(), p4f.y()));
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

void ExportAreaController::updateOverlay()
{
    if (!m_glWidget) {
        return;
    }

    const bool selecting = m_isSelecting || m_isMoving || m_isResizing;
    m_glWidget->setCanvasResizeOverlayState(m_active, m_selectionWorld, selecting);

    if (m_callbacks.requestRender) {
        m_callbacks.requestRender();
    }
}

QRect ExportAreaController::normalizedRect(const QRect& rect) const
{
    if (ruwa::core::canvas::isInfiniteCanvas(m_canvasBoundsMode)) {
        QRect normalized = rect.normalized();
        if (normalized.width() <= 0 || normalized.height() <= 0) {
            normalized = QRect(0, 0, qMax(1, m_canvasSize.width()), qMax(1, m_canvasSize.height()));
        }
        return normalized;
    }

    const int canvasWidth = qMax(1, m_canvasSize.width());
    const int canvasHeight = qMax(1, m_canvasSize.height());
    QRect normalized = rect.normalized();

    if (normalized.width() <= 0 || normalized.height() <= 0) {
        normalized = QRect(0, 0, canvasWidth, canvasHeight);
    }

    const int x = std::clamp(normalized.x(), 0, canvasWidth - 1);
    const int y = std::clamp(normalized.y(), 0, canvasHeight - 1);
    const int maxWidth = canvasWidth - x;
    const int maxHeight = canvasHeight - y;
    const int width = std::clamp(normalized.width(), 1, maxWidth);
    const int height = std::clamp(normalized.height(), 1, maxHeight);
    return QRect(x, y, width, height);
}

QRect ExportAreaController::rectFromWorldSelection() const
{
    const QRectF rect = m_selectionWorld.normalized();
    if (rect.isEmpty()) {
        return exportFrame();
    }

    const int left = static_cast<int>(std::floor(rect.left()));
    const int top = static_cast<int>(std::floor(rect.top()));
    const int width = qMax(1, static_cast<int>(std::ceil(rect.right()) - std::floor(rect.left())));
    const int height = qMax(1, static_cast<int>(std::ceil(rect.bottom()) - std::floor(rect.top())));
    return normalizedRect(QRect(left, top, width, height));
}

void ExportAreaController::syncSelectionToFrame()
{
    const QRect frame = exportFrame();
    m_selectionWorld = QRectF(frame);
}

void ExportAreaController::commitSelection()
{
    const QRect rect = rectFromWorldSelection();
    m_exportFrame = rect;
    m_selectionWorld = QRectF(rect);

    if (m_callbacks.setExportFrame) {
        m_callbacks.setExportFrame(rect);
    }
}

} // namespace ruwa::ui::workspace
