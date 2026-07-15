// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   T A B L E T   H A N D L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASTABLETHANDLER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASTABLETHANDLER_H

#include "shared/types/Types.h"

#include <QPointF>
#include <Qt>
#include <QtGlobal>

class QTabletEvent;
class QWidget;

namespace ruwa::ui::workspace {

class CanvasInputHost;
class CanvasPanel;

/**
 * @brief Handles tablet (stylus) input routing for CanvasPanel.
 *
 * Extracted from CanvasPanel to isolate ~320 lines of tablet-specific logic.
 * Routes events to overlays (brush, joystick, popups), forwards to mouse
 * handlers for transform/non-drawing tools, and handles Brush/Eraser strokes.
 */
class CanvasTabletHandler {
public:
    explicit CanvasTabletHandler(CanvasPanel* panel);

    void handleTabletEvent(QTabletEvent* event);

private:
    static bool globalPosOverGlViewport(const CanvasInputHost* host, const QPointF& globalPos);

    CanvasInputHost* m_host = nullptr;
    CanvasPanel* m_panel = nullptr;

    // Part A of the stabilizer fix: feed the stabilizer the pen's real sample
    // time, not the GUI-thread delivery time. QTabletEvent::timestamp() is the
    // OS event clock (≈ when the pen was sampled); reading a local timer at
    // delivery instead made all moves in one delivered cluster share a stamp
    // (clamped to the monotonic-nudge floor) and dumped the real elapsed gap
    // onto one sample — a bimodal dt that the time-domain filter turned into a
    // sawtooth. Base is captured at TabletPress; elapsed = (ts − base)/1000.
    quint64 m_strokeTimestampBaseMs = 0;
    float m_lastTabletElapsedSec = 0.0f;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASTABLETHANDLER_H
