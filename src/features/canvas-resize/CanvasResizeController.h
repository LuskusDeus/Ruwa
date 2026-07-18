// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   R E S I Z E   C O N T R O L L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASRESIZECONTROLLER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASRESIZECONTROLLER_H

#include "shared/types/Types.h"

#include <QObject>
#include <QRectF>
#include <QPoint>
#include <QSize>
#include <functional>

class QWidget;
class QVariantAnimation;
class QMouseEvent;

namespace aether {
class OpenGLCanvasWidget;
}

namespace ruwa::core::layers {
class LayerModel;
}

namespace ruwa::ui::workspace {

/**
 * @brief Handles canvas resize overlay interaction: selection rect, handles, apply.
 *
 * Extracted from CanvasPanel to isolate ~200 lines of canvas-resize-specific logic.
 * Owns state and implements hit-testing, drag handling, and grid remap on apply.
 */
class CanvasResizeController : public QObject {
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
        std::function<void(QSize)> setCanvasSize;
        std::function<void()> requestRender;
        std::function<void()> onContentChanged;
        std::function<void()> updateToolCursor;
        std::function<void()> updateSelectionActionPopup;
        std::function<void()> beforeDocumentMutation;
    };

    explicit CanvasResizeController(QObject* parent = nullptr);
    ~CanvasResizeController() override;

    void setGlWidget(aether::OpenGLCanvasWidget* widget);
    void setContentWidget(QWidget* widget);
    void setLayerModel(ruwa::core::layers::LayerModel* model);
    void setCanvasSize(const QSize& size);
    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setCallbacks(Callbacks callbacks);

    /// Initialize rect animation (call from createContent, after contentWidget exists)
    void setupRectAnimation();

    bool isActive() const;
    bool isSelectingOrMoving() const;
    bool isInteractionActive() const { return isSelectingOrMoving() || m_isResizing; }

    /// Mouse handling — return true if event was consumed
    bool handleMousePress(const aether::Vector2& worldPos, const QPoint& globalPos,
        const QPoint& localPos, Qt::MouseButton button);
    bool handleMouseMove(
        const aether::Vector2& worldPos, const QPoint& globalPos, const QPoint& localPos);
    bool handleMouseRelease(
        const aether::Vector2& worldPos, const QPoint& globalPos, Qt::MouseButton button);

    Handle hitHandleAt(const QPoint& globalPos) const;
    bool containsPoint(const QPoint& globalPos) const;
    static Qt::CursorShape cursorForHandle(Handle handle);

    /// Cursor for given global position (resize handle, move, or cross)
    Qt::CursorShape cursorForPosition(const QPoint& globalPos) const;

    QRectF selectionRectWorld() const { return m_selectionWorld; }
    QSize targetCanvasSize() const;
    QRectF selectionRectInWidget() const;
    QRectF activeRectInWidget() const;

    void clearOverlay(bool animated = false);
    void updateOverlay();
    void startOverlayFadeIn();
    void startRectTransition(const QRectF& from, const QRectF& to, int durationMs);
    void applySelection();

    /// Translate selection (e.g. during Space+drag pan of selection)
    void translateSelection(qreal dx, qreal dy);

    void resetInteractionState();

signals:
    void overlayStateChanged();
    void previewSizeChanged(const QSize& size);

private:
    aether::OpenGLCanvasWidget* m_glWidget = nullptr;
    QWidget* m_contentWidget = nullptr;
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    QSize m_canvasSize;
    Callbacks m_callbacks;
    bool m_enabled = true;

    bool m_isSelecting = false;
    bool m_isMoving = false;
    bool m_isResizing = false;
    bool m_overlayActive = false;
    bool m_dragDetected = false;
    Handle m_activeHandle = Handle::None;

    QPoint m_pressPos;
    aether::Vector2 m_startWorld { 0.0f, 0.0f };
    aether::Vector2 m_moveAnchorWorld { 0.0f, 0.0f };
    aether::Vector2 m_resizeAnchorWorld { 0.0f, 0.0f };
    QRectF m_selectionWorld;
    QRectF m_moveStartRect;
    QRectF m_resizeStartRect;

    QVariantAnimation* m_rectAnim = nullptr;

    static constexpr int kDragThreshold = 4;
    static constexpr qreal kHandleFramePaddingPx = 10.0;
    static constexpr qreal kHandleHitPx = 8.0;
    static constexpr qreal kSideLengthPx = 60.0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASRESIZECONTROLLER_H
