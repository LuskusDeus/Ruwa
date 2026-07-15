// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S P A C E   M O V E   H A N D L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASSPACEMOVEHANDLER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASSPACEMOVEHANDLER_H

#include <QPoint>

namespace ruwa::ui::workspace {

class CanvasPanel;

/**
 * @brief Handles Space-key panning during selection or stroke for CanvasPanel.
 *
 * Extracted from CanvasPanel to isolate ~100 lines: beginSpaceSelectionMove,
 * moveActiveSelectionWithSpace, endSpaceSelectionMove, beginSpaceStrokeMove,
 * moveActiveStrokeWithSpace, endSpaceStrokeMove.
 */
class CanvasSpaceMoveHandler {
public:
    explicit CanvasSpaceMoveHandler(CanvasPanel* panel);

    void beginSpaceSelectionMove();
    void moveActiveSelectionWithSpace(const QPoint& globalPos);
    void endSpaceSelectionMove();
    void beginSpaceStrokeMove();
    void moveActiveStrokeWithSpace(const QPoint& globalPos);
    void endSpaceStrokeMove();

private:
    CanvasPanel* m_panel = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASSPACEMOVEHANDLER_H
