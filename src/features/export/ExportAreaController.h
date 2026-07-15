// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_EXPORTAREACONTROLLER_H
#define RUWA_UI_WORKSPACE_EXPORTAREACONTROLLER_H

#include "shared/types/Types.h"
#include "features/canvas/CanvasBoundsMode.h"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <functional>

class QWidget;

namespace aether {
class OpenGLCanvasWidget;
}

namespace ruwa::ui::workspace {

class ExportAreaController : public QObject {
    Q_OBJECT

public:
    enum class Handle {
        None,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left
    };

    struct Callbacks {
        std::function<void(const QRect&)> setExportFrame;
        std::function<void()> requestRender;
    };

    explicit ExportAreaController(QObject* parent = nullptr);

    void setGlWidget(aether::OpenGLCanvasWidget* widget);
    void setContentWidget(QWidget* widget);
    void setCanvasSize(const QSize& size);
    void setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode);
    void setCallbacks(Callbacks callbacks);

    void setExportFrame(const QRect& frame);
    QRect exportFrame() const;

    void enter();
    void exit();

    bool isActive() const;
    bool isSelectingOrMoving() const;
    bool isInteractionActive() const;

    bool handleMousePress(const aether::Vector2& worldPos, const QPoint& globalPos,
        const QPoint& localPos, Qt::MouseButton button);
    bool handleMouseMove(
        const aether::Vector2& worldPos, const QPoint& globalPos, const QPoint& localPos);
    bool handleMouseRelease(
        const aether::Vector2& worldPos, const QPoint& globalPos, Qt::MouseButton button);

    Handle hitHandleAt(const QPoint& globalPos) const;
    bool containsPoint(const QPoint& globalPos) const;
    static Qt::CursorShape cursorForHandle(Handle handle);
    Qt::CursorShape cursorForPosition(const QPoint& globalPos) const;

    QRectF selectionRectInWidget() const;
    void updateOverlay();

private:
    QRect normalizedRect(const QRect& rect) const;
    QRect rectFromWorldSelection() const;
    void syncSelectionToFrame();
    void commitSelection();

    aether::OpenGLCanvasWidget* m_glWidget = nullptr;
    QWidget* m_contentWidget = nullptr;
    QSize m_canvasSize;
    ruwa::core::canvas::CanvasBoundsMode m_canvasBoundsMode
        = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    Callbacks m_callbacks;

    bool m_active = false;
    bool m_isSelecting = false;
    bool m_isMoving = false;
    bool m_isResizing = false;
    bool m_dragDetected = false;
    Handle m_activeHandle = Handle::None;

    QPoint m_pressPos;
    aether::Vector2 m_startWorld { 0.0f, 0.0f };
    aether::Vector2 m_moveAnchorWorld { 0.0f, 0.0f };
    aether::Vector2 m_resizeAnchorWorld { 0.0f, 0.0f };
    QRectF m_selectionWorld;
    QRectF m_moveStartRect;
    QRectF m_resizeStartRect;
    QRect m_exportFrame;

    static constexpr int kDragThreshold = 4;
    static constexpr qreal kHandleFramePaddingPx = 10.0;
    static constexpr qreal kHandleHitPx = 8.0;
    static constexpr qreal kSideLengthPx = 60.0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EXPORTAREACONTROLLER_H
