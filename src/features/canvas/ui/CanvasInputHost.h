// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   I N P U T   H O S T
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASINPUTHOST_H
#define RUWA_UI_WORKSPACE_CANVASINPUTHOST_H

#include "features/canvas/ui/CanvasPanelTypes.h"
#include "shared/types/Types.h"

#include <QPoint>
#include <QPointF>
#include <QString>
#include <Qt>

#include <optional>

class QKeyEvent;
class QMouseEvent;
class QTabletEvent;

namespace aether {
class OpenGLCanvasWidget;
}

namespace ruwa::ui::workspace {

class CanvasCursorManager;

class CanvasInputHost {
public:
    using ToolMode = CanvasToolMode;

    virtual ~CanvasInputHost() = default;

    virtual ToolMode currentInputTool() const = 0;
    virtual void setToolMode(ToolMode tool) = 0;
    virtual aether::OpenGLCanvasWidget* inputGlWidget() const = 0;
    virtual CanvasCursorManager* inputCursorManager() const = 0;

    virtual bool hasInputFocus() const = 0;
    virtual bool hasInputFocusOrCursorOverCanvas() const = 0;
    virtual bool isCursorOverCanvas() const = 0;
    virtual bool isTransformInputActive() const = 0;

    virtual bool isDrawingActive() const = 0;
    virtual bool isInputDrawingActive() const = 0;
    virtual void setInputDrawingActive(bool active) = 0;
    virtual bool isInputPanningActive() const = 0;
    virtual Qt::MouseButton inputPanButton() const = 0;
    virtual bool isInputTabletActive() const = 0;
    virtual void setInputTabletActive(bool active) = 0;
    virtual Qt::MouseButtons previousTabletButtons() const = 0;
    virtual void setPreviousTabletButtons(Qt::MouseButtons buttons) = 0;

    virtual bool isAnySelectionInteractionActive() const = 0;
    virtual bool isSpaceSelectionMoveActive() const = 0;
    virtual bool isSpaceStrokeMoveActive() const = 0;
    virtual void beginSpaceSelectionMove() = 0;
    virtual void endSpaceSelectionMove() = 0;
    virtual void beginSpaceStrokeMove() = 0;
    virtual void moveActiveStrokeWithSpace(const QPoint& globalPos) = 0;
    virtual void endSpaceStrokeMove() = 0;

    virtual bool temporaryToolHoldActive() const = 0;
    virtual bool temporaryToolHeldKeyIs(int key) const = 0;
    virtual bool temporaryToolHeldButtonIs(Qt::MouseButton button) const = 0;
    virtual bool temporaryToolShiftSpaceCombo() const = 0;
    virtual void setTemporaryToolShiftSpaceCombo(bool enabled) = 0;
    virtual void markTemporaryToolUsed() = 0;
    virtual void beginTemporaryToolHoldFromButton(Qt::MouseButton heldButton, ToolMode tool) = 0;
    virtual void endTemporaryTool() = 0;
    virtual bool finalizeTemporaryToolHoldForKeyRelease(int key) = 0;
    virtual void setPendingTemporaryToolKey(int key, bool alwaysRevert) = 0;
    virtual void clearPendingTemporaryToolKey() = 0;
    virtual std::optional<ToolMode> inputToolModeForKey(int key) const = 0;
    virtual std::optional<ToolMode> inputToolModeForKeyEvent(const QKeyEvent* event) const = 0;
    virtual QString commandIdForInputToolMode(ToolMode mode) const = 0;
    virtual void noteUndoForTemporaryMoveTool() = 0;

    virtual void setCtrlModifierPressed(bool pressed) = 0;
    virtual void setAltModifierPressed(bool pressed) = 0;
    virtual void endDrawingOnAppDeactivate() = 0;

    virtual void updateSelectionActionPopup(bool forceShow = false) = 0;
    virtual void updateInputCursorPosition(const QPoint& globalPos) = 0;
    virtual void updateToolCursor() = 0;

    virtual bool shouldIgnoreTabletInputForOverlay(
        const QPointF& globalPos, bool activeTabletStroke) const
        = 0;
    virtual bool routeTabletInputToStylusJoystick(QTabletEvent* event, const QPointF& globalPos,
        Qt::MouseButton effectiveButton, bool activeTabletStroke)
        = 0;
    virtual void hideBrushPackOverlayIfNotUserMoved() = 0;

    virtual void dispatchSyntheticMousePress(QMouseEvent* event) = 0;
    virtual void dispatchSyntheticMouseMove(QMouseEvent* event) = 0;
    virtual void dispatchSyntheticMouseRelease(QMouseEvent* event) = 0;

    virtual aether::Vector2 mapInputToViewportWorld(const QPointF& globalPos) const = 0;
    virtual bool isGlobalOverInputViewport(const QPoint& globalPos) const = 0;
    virtual bool isGlobalOverInputViewport(const QPointF& globalPos) const = 0;

    virtual bool shouldEraseForTool(ToolMode tool) const = 0;
    virtual void setEraseMode(bool erase) = 0;
    virtual void showBlockedDrawMessageForSelectedLayer() = 0;
    virtual void notifyCanvasContentChanged() = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASINPUTHOST_H
