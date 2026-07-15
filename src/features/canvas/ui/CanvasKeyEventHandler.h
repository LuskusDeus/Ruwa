// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   K E Y   E V E N T   H A N D L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASKEYEVENTHANDLER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASKEYEVENTHANDLER_H

class QObject;
class QEvent;

namespace ruwa::ui::workspace {

class CanvasInputHost;
class CanvasPanel;

/**
 * @brief Handles keyboard and app-level events for CanvasPanel (eventFilter).
 *
 * Extracted from CanvasPanel to isolate ~250 lines of key/shortcut logic:
 * ShortcutOverride, KeyPress, KeyRelease, ApplicationDeactivate, ApplicationActivate,
 * temporary tool hold (Space/Alt), Ctrl+T transform, modifier tracking.
 */
class CanvasKeyEventHandler {
public:
    explicit CanvasKeyEventHandler(CanvasPanel* panel);

    /// Process event from CanvasPanel::eventFilter. Return true if event was handled.
    bool handleEvent(QObject* watched, QEvent* event);

private:
    CanvasInputHost* m_host = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASKEYEVENTHANDLER_H
