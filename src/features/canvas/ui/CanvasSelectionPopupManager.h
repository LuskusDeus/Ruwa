// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S E L E C T I O N   P O P U P   M A N A G E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASSELECTIONPOPUPMANAGER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASSELECTIONPOPUPMANAGER_H

namespace ruwa::ui::workspace {

class CanvasPanel;

/**
 * @brief Manages selection action popup and confirmation popup for CanvasPanel.
 *
 * Extracted from CanvasPanel to isolate ~150 lines of popup creation, positioning,
 * and update logic. Handles SelectionActionPopup (fill, transform, delete) and
 * ConfirmationPopup (apply/cancel for transform and canvas resize).
 */
class CanvasSelectionPopupManager {
public:
    explicit CanvasSelectionPopupManager(CanvasPanel* panel);

    void ensureSelectionActionPopup();
    void ensureConfirmationPopup();
    void updateSelectionActionPopup(bool forceShow = false);
    void updateConfirmationPopup();
    void dismissSelectionActionPopupUntilSelectionReset();

private:
    CanvasPanel* m_panel = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASSELECTIONPOPUPMANAGER_H
