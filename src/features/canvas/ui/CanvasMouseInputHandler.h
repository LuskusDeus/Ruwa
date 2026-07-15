// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   M O U S E   I N P U T   H A N D L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASMOUSEINPUTHANDLER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASMOUSEINPUTHANDLER_H

#include <functional>
#include <QPoint>
#include <QUuid>

#include "shared/types/Types.h"

class QMouseEvent;
class QPointF;

namespace ruwa::ui::workspace {

class CanvasInputHost;
class CanvasPanel;

/**
 * @brief Handles mouse input routing for CanvasPanel (press, move, release, double-click).
 *
 * Extracted from CanvasPanel to isolate ~500 lines of mouse-specific logic.
 * Routes events by tool mode: Hand, Zoom, RotateView, Eyedropper, Fill, Lasso,
 * SquareSelection, CircleSelection, Brush, Eraser, CanvasResize, Transform.
 */
class CanvasMouseInputHandler {
public:
    explicit CanvasMouseInputHandler(CanvasPanel* panel);

    /// Return true if event was fully handled (caller should not pass to base)
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleMouseRelease(QMouseEvent* event);
    bool handleMouseDoubleClick(QMouseEvent* event);

private:
    /// Recovered WM_MOUSEMOVE samples + current event (same idea as brush continueStroke).
    void dispatchUncoalescedWorldMoves(
        QMouseEvent* event, const std::function<void(float, float)>& applyWorld);
    void beginPendingRightClick(QMouseEvent* event);
    void updatePendingRightClick(QMouseEvent* event);
    bool consumePendingRightClick(QMouseEvent* event);
    void cancelPendingRightClick();
    void handleTextToolPress(const aether::Vector2& worldPos);
    void clearPendingMoveToolContentHit();

    bool isPaintingLikeTool() const;
    bool beginBrushSizeAdjust(QMouseEvent* event);
    void updateBrushSizeAdjust(const QPoint& globalPos);
    void endBrushSizeAdjust();
    void applyBrushSizeAdjustOverlay();

    CanvasInputHost* m_host = nullptr;
    CanvasPanel* m_panel = nullptr;
    bool m_pendingRightClick = false;
    QPoint m_pendingRightClickPressPos;
    QPoint m_pendingRightClickLastGlobalPos;
    bool m_textSelecting = false;
    bool m_pendingMoveToolContentHit = false;
    QUuid m_pendingMoveToolContentLayerId;
    QPoint m_pendingMoveToolContentPressGlobalPos;
    aether::Vector2 m_pendingMoveToolContentPressWorldPos;

    // Pressure of the previous *real* (non-recovered) pointer sample of the
    // current stroke, plus its stroke-elapsed time. Recovered WM_MOUSEMOVE
    // positions carry no pressure of their own; we interpolate across this
    // gap so pressure does not collapse into a per-event step function
    // (the "staircase" along stroke width).
    bool m_lastRealStrokeSampleValid = false;
    float m_lastRealStrokePressure = 1.0f;
    float m_lastRealStrokeElapsedSec = 0.0f;

    // Shift+click brush-size resize (brush/eraser/blur/smudge).
    bool m_brushSizeAdjust = false;
    QPoint m_brushSizeAnchorGlobal;
    float m_brushSizeAnchorVx = 0.0f;
    float m_brushSizeAnchorVy = 0.0f;
    float m_brushSizeCursorScale = 1.0f;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASMOUSEINPUTHANDLER_H
