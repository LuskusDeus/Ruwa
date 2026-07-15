// SPDX-License-Identifier: MPL-2.0

// Action ids for SimpleActions context menus on canvas HUD widgets (brush overlay, tool strip,
// joystick). Kept in sync with CanvasPanel::onSimpleContextAction.

#ifndef RUWA_UI_CANVAS_CANVASOVERLAYCONTEXTACTIONS_H
#define RUWA_UI_CANVAS_CANVASOVERLAYCONTEXTACTIONS_H

namespace ruwa::ui::canvas_overlay {

enum class CanvasOverlayContextActionId : int {
    HideBrushControl = 100,
    HideToolStateOverlay = 101,
    HideStylusJoystick = 102,
};

} // namespace ruwa::ui::canvas_overlay

#endif
