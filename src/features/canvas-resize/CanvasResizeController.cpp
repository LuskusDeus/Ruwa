// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   R E S I Z E   C O N T R O L L E R
// ==========================================================================

#include "CanvasResizeController.h"
#include "CanvasResizeCommand.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/layers/model/LayerModel.h"

#include <QWidget>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <Qt>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {

namespace {

bool approximatelyMatchesCanvasRect(const QRectF& rect, const QSize& canvasSize)
{
    const QRectF normalized = rect.normalized();
    constexpr qreal epsilon = 0.5;
    return std::abs(normalized.left()) <= epsilon && std::abs(normalized.top()) <= epsilon
        && std::abs(normalized.width() - static_cast<qreal>(canvasSize.width())) <= epsilon
        && std::abs(normalized.height() - static_cast<qreal>(canvasSize.height())) <= epsilon;
}

} // namespace

CanvasResizeController::CanvasResizeController(QObject* parent)
    : QObject(parent)
{
}

CanvasResizeController::~CanvasResizeController()
{
    if (m_rectAnim) {
        m_rectAnim->stop();
        m_rectAnim->deleteLater();
        m_rectAnim = nullptr;
    }
}

void CanvasResizeController::setGlWidget(aether::OpenGLCanvasWidget* widget)
{
    m_glWidget = widget;
}

void CanvasResizeController::setContentWidget(QWidget* widget)
{
    m_contentWidget = widget;
}

void CanvasResizeController::setLayerModel(ruwa::core::layers::LayerModel* model)
{
    m_layerModel = model;
}

void CanvasResizeController::setCanvasSize(const QSize& size)
{
    m_canvasSize = size;
}

void CanvasResizeController::setCallbacks(Callbacks callbacks)
{
    m_callbacks = std::move(callbacks);
}

void CanvasResizeController::setupRectAnimation()
{
    if (m_rectAnim) {
        return;
    }
    m_rectAnim = new QVariantAnimation(this);
    m_rectAnim->setDuration(130);
    m_rectAnim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_rectAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (!value.canConvert<QRectF>()) {
            return;
        }
        m_selectionWorld = value.toRectF();
        if (m_overlayActive) {
            updateOverlay();
        }
    });
}

bool CanvasResizeController::isActive() const
{
    return m_overlayActive;
}

bool CanvasResizeController::isSelectingOrMoving() const
{
    return m_isSelecting || m_isMoving;
}

bool CanvasResizeController::handleMousePress(const aether::Vector2& worldPos,
    const QPoint& globalPos, const QPoint& localPos, Qt::MouseButton button)
{
    if (!m_enabled) {
        return false;
    }
    if (button != Qt::LeftButton || !m_glWidget || !m_glWidget->isInitialized()) {
        return false;
    }

    if (m_overlayActive) {
        const Handle hitHandle = hitHandleAt(globalPos);
        if (hitHandle != Handle::None) {
            m_isResizing = true;
            m_isMoving = false;
            m_isSelecting = false;
            m_activeHandle = hitHandle;
            m_resizeAnchorWorld = worldPos;
            m_resizeStartRect = m_selectionWorld.normalized();
            return true;
        }
    }

    if (m_overlayActive && !m_selectionWorld.isEmpty() && containsPoint(globalPos)
        && !approximatelyMatchesCanvasRect(m_selectionWorld, m_canvasSize)) {
        m_isMoving = true;
        m_isResizing = false;
        m_moveAnchorWorld = worldPos;
        m_moveStartRect = m_selectionWorld;
        return true;
    }

    if (m_rectAnim) {
        m_rectAnim->stop();
    }

    m_pressPos = localPos;
    m_startWorld = worldPos;
    m_isSelecting = true;
    m_isMoving = false;
    m_isResizing = false;
    m_activeHandle = Handle::None;
    const bool wasActive = m_overlayActive;
    m_overlayActive = true;
    m_dragDetected = false;
    if (!wasActive) {
        m_selectionWorld
            = QRectF(static_cast<qreal>(worldPos.x), static_cast<qreal>(worldPos.y), 0.5, 0.5);
        startOverlayFadeIn();
    }
    updateOverlay();
    return true;
}

bool CanvasResizeController::handleMouseMove(
    const aether::Vector2& worldPos, const QPoint& globalPos, const QPoint& localPos)
{
    if (!m_enabled) {
        return false;
    }
    Q_UNUSED(globalPos);

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
        updateOverlay();
        return true;
    }

    if (m_isMoving) {
        const qreal dx = static_cast<qreal>(worldPos.x - m_moveAnchorWorld.x);
        const qreal dy = static_cast<qreal>(worldPos.y - m_moveAnchorWorld.y);
        m_selectionWorld = m_moveStartRect.translated(dx, dy);
        updateOverlay();
        return true;
    }

    if (m_isSelecting) {
        if (!m_dragDetected && (localPos - m_pressPos).manhattanLength() >= kDragThreshold) {
            m_dragDetected = true;
            if (m_rectAnim) {
                m_rectAnim->stop();
            }
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
        }
        updateOverlay();
        return true;
    }

    return false;
}

bool CanvasResizeController::handleMouseRelease(
    const aether::Vector2& worldPos, const QPoint& globalPos, Qt::MouseButton button)
{
    if (!m_enabled) {
        return false;
    }
    Q_UNUSED(worldPos);
    Q_UNUSED(globalPos);

    if (button != Qt::LeftButton) {
        return false;
    }

    if (m_isResizing) {
        m_isResizing = false;
        m_activeHandle = Handle::None;
        return true;
    }

    if (m_isMoving) {
        m_isMoving = false;
        return true;
    }

    if (m_isSelecting) {
        m_isSelecting = false;

        if (!m_dragDetected) {
            const QRectF targetCanvasRect(0.0, 0.0, static_cast<qreal>(m_canvasSize.width()),
                static_cast<qreal>(m_canvasSize.height()));
            QRectF fromRect = m_selectionWorld;
            if (fromRect.isEmpty()) {
                fromRect = QRectF(static_cast<qreal>(m_startWorld.x),
                    static_cast<qreal>(m_startWorld.y), 0.5, 0.5);
            }
            startRectTransition(fromRect, targetCanvasRect, 120);
        } else {
            m_selectionWorld = m_selectionWorld.normalized();
        }

        m_overlayActive = true;
        updateOverlay();
        return true;
    }

    return false;
}

CanvasResizeController::Handle CanvasResizeController::hitHandleAt(const QPoint& globalPos) const
{
    if (!m_overlayActive || m_selectionWorld.isEmpty() || !m_contentWidget) {
        return Handle::None;
    }
    if (!m_enabled) {
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

bool CanvasResizeController::containsPoint(const QPoint& globalPos) const
{
    if (!m_overlayActive || m_selectionWorld.isEmpty() || !m_contentWidget) {
        return false;
    }
    return selectionRectInWidget().contains(m_contentWidget->mapFromGlobal(globalPos));
}

Qt::CursorShape CanvasResizeController::cursorForPosition(const QPoint& globalPos) const
{
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

Qt::CursorShape CanvasResizeController::cursorForHandle(Handle handle)
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

QRectF CanvasResizeController::selectionRectInWidget() const
{
    if (!m_glWidget || m_selectionWorld.isEmpty()) {
        return {};
    }

    const aether::Vector2 tl
        = m_glWidget->screenFromDocumentWorld({ static_cast<float>(m_selectionWorld.left()),
            static_cast<float>(m_selectionWorld.top()) });
    const aether::Vector2 br
        = m_glWidget->screenFromDocumentWorld({ static_cast<float>(m_selectionWorld.right()),
            static_cast<float>(m_selectionWorld.bottom()) });

    const QPoint glTopLeft = m_glWidget->geometry().topLeft();
    const QPointF topLeft(static_cast<qreal>(glTopLeft.x()) + static_cast<qreal>(tl.x),
        static_cast<qreal>(glTopLeft.y()) + static_cast<qreal>(tl.y));
    const QPointF bottomRight(static_cast<qreal>(glTopLeft.x()) + static_cast<qreal>(br.x),
        static_cast<qreal>(glTopLeft.y()) + static_cast<qreal>(br.y));
    return QRectF(topLeft, bottomRight).normalized();
}

QRectF CanvasResizeController::activeRectInWidget() const
{
    const bool showing = m_overlayActive || m_isSelecting || m_isMoving || m_isResizing;
    if (!m_glWidget || !showing || m_selectionWorld.isEmpty()) {
        return QRectF();
    }
    const QRectF& wr = m_selectionWorld;
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

void CanvasResizeController::clearOverlay(bool animated)
{
    Q_UNUSED(animated);
    const QRectF lastRect = m_selectionWorld;
    m_isSelecting = false;
    m_isMoving = false;
    m_isResizing = false;
    m_overlayActive = false;
    m_dragDetected = false;
    m_activeHandle = Handle::None;
    if (m_rectAnim) {
        m_rectAnim->stop();
    }
    if (m_glWidget) {
        m_glWidget->setCanvasResizeOverlayState(false, lastRect, false);
    }
    m_selectionWorld = QRectF();
    emit previewSizeChanged(m_canvasSize);
    emit overlayStateChanged();
}

void CanvasResizeController::updateOverlay()
{
    if (!m_glWidget) {
        return;
    }

    const bool overlayActive = m_overlayActive || m_isSelecting || m_isMoving || m_isResizing;
    m_glWidget->setCanvasResizeOverlayState(
        overlayActive, m_selectionWorld, m_isSelecting || m_isMoving || m_isResizing);

    if (overlayActive && m_callbacks.updateSelectionActionPopup) {
        m_callbacks.updateSelectionActionPopup();
    }
    emit previewSizeChanged(targetCanvasSize());
    emit overlayStateChanged();
}

void CanvasResizeController::startOverlayFadeIn()
{
    if (!m_glWidget) {
        return;
    }
    m_glWidget->setCanvasResizeOverlayState(
        true, m_selectionWorld, m_isSelecting || m_isMoving || m_isResizing);
}

void CanvasResizeController::startRectTransition(
    const QRectF& from, const QRectF& to, int durationMs)
{
    if (!m_rectAnim) {
        m_selectionWorld = to;
        emit previewSizeChanged(targetCanvasSize());
        emit overlayStateChanged();
        return;
    }

    m_rectAnim->stop();
    m_rectAnim->setDuration(qMax(60, durationMs));
    m_rectAnim->setStartValue(from.normalized());
    m_rectAnim->setEndValue(to.normalized());
    m_rectAnim->start();
}

QSize CanvasResizeController::targetCanvasSize() const
{
    const QRectF rect = m_selectionWorld.normalized();
    if (rect.isEmpty()) {
        return m_canvasSize;
    }

    return QSize(qMax(1, static_cast<int>(std::lround(rect.width()))),
        qMax(1, static_cast<int>(std::lround(rect.height()))));
}

void CanvasResizeController::applySelection()
{
    if (!m_enabled) {
        return;
    }
    if (!m_glWidget || !m_overlayActive) {
        return;
    }

    const QRectF rect = m_selectionWorld.normalized();
    if (rect.isEmpty()) {
        clearOverlay();
        if (m_callbacks.updateToolCursor) {
            m_callbacks.updateToolCursor();
        }
        return;
    }

    const int newWidth = qMax(1, static_cast<int>(std::lround(rect.width())));
    const int newHeight = qMax(1, static_cast<int>(std::lround(rect.height())));
    const int offsetX = static_cast<int>(std::lround(rect.left()));
    const int offsetY = static_cast<int>(std::lround(rect.top()));
    const QSize newSize(newWidth, newHeight);
    const QSize oldSize = m_canvasSize;
    const bool geometryChanged = (newSize != oldSize) || (offsetX != 0 || offsetY != 0);

    if (geometryChanged && m_layerModel) {
        if (m_callbacks.beforeDocumentMutation) {
            m_callbacks.beforeDocumentMutation();
        }
        aether::CanvasResizeCommand::Hooks hooks {
            m_callbacks.setCanvasSize,
            m_callbacks.requestRender,
            m_callbacks.onContentChanged,
        };

        auto snapshot = aether::CanvasResizeCommand::applyResize(
            m_glWidget, m_layerModel, oldSize, offsetX, offsetY, newSize, hooks);

        m_selectionWorld
            = QRectF(0.0, 0.0, static_cast<qreal>(newWidth), static_cast<qreal>(newHeight));

        auto cmd = std::make_unique<aether::CanvasResizeCommand>(m_glWidget, m_layerModel, oldSize,
            offsetX, offsetY, newSize, std::move(snapshot), std::move(hooks));
        m_glWidget->canvas().undoManager().push(std::move(cmd));
    }

    clearOverlay();
    if (m_callbacks.updateToolCursor) {
        m_callbacks.updateToolCursor();
    }
}

void CanvasResizeController::translateSelection(qreal dx, qreal dy)
{
    if (!m_isSelecting) {
        return;
    }
    m_startWorld.x += static_cast<float>(dx);
    m_startWorld.y += static_cast<float>(dy);
    m_selectionWorld.translate(dx, dy);
    updateOverlay();
}

void CanvasResizeController::resetInteractionState()
{
    m_isSelecting = false;
    m_isMoving = false;
    m_isResizing = false;
}

} // namespace ruwa::ui::workspace
