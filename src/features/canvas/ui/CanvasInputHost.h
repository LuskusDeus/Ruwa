// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   I N P U T   H O S T
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASINPUTHOST_H
#define RUWA_UI_WORKSPACE_CANVASINPUTHOST_H

#include "features/canvas/engine/CanvasEngineSession.h"
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
class QWidget;

namespace ruwa::ui::workspace {

class CanvasCursorManager;

class CanvasInputHost {
public:
    virtual ~CanvasInputHost() = default;

    virtual ToolId currentInputTool() const = 0;
    virtual void setToolMode(ToolId tool) = 0;
    /// True when the engine session exists and has finished initializing.
    /// Input paths gate their renderer access on this exactly where they used
    /// to null-check/initialize-check the concrete renderer (plan 7.6.17).
    virtual bool inputRenderReady() const = 0;
    /// Engine capabilities for input. Non-null whenever the render content
    /// exists; callers check inputRenderReady() before mutating state.
    virtual CanvasViewCapability* inputView() const = 0;
    virtual CanvasPaintingCapability* inputPainting() const = 0;
    virtual CanvasEditingCapability* inputEditing() const = 0;
    virtual CanvasTransformCapability* inputTransform() const = 0;
    virtual CanvasHitTesting* inputHitTesting() const = 0;
    virtual CanvasPresentationCapability* inputPresentation() const = 0;
    /// The generic QWidget viewport host: mapFromGlobal/geometry/cursor
    /// ownership only — never renderer API (plan 7.6.17).
    virtual QWidget* inputViewportHostWidget() const = 0;
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
    virtual void beginTemporaryToolHoldFromButton(Qt::MouseButton heldButton, ToolId tool) = 0;
    virtual void endTemporaryTool() = 0;
    virtual bool finalizeTemporaryToolHoldForKeyRelease(int key) = 0;
    virtual void setPendingTemporaryToolKey(int key, bool alwaysRevert) = 0;
    virtual void clearPendingTemporaryToolKey() = 0;
    virtual std::optional<ToolId> inputToolModeForKey(int key) const = 0;
    virtual std::optional<ToolId> inputToolModeForKeyEvent(const QKeyEvent* event) const = 0;
    virtual QString commandIdForInputToolMode(ToolId mode) const = 0;
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
    virtual void notifyCanvasToolInteractionStarted() = 0;

    virtual aether::Vector2 mapInputToViewportWorld(const QPointF& globalPos) const = 0;
    virtual bool isGlobalOverInputViewport(const QPoint& globalPos) const = 0;
    virtual bool isGlobalOverInputViewport(const QPointF& globalPos) const = 0;

    virtual bool shouldEraseForTool(ToolId tool) const = 0;
    virtual void setEraseMode(bool erase) = 0;
    /// A paint stroke just finished. Applies a tool switch whose brush settings
    /// were deferred because the switch landed in the middle of that stroke.
    virtual void notifyStrokeSessionEnded() = 0;
    virtual void showBlockedDrawMessageForSelectedLayer() = 0;
    virtual void notifyCanvasContentChanged() = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASINPUTHOST_H
