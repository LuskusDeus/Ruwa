// SPDX-License-Identifier: MPL-2.0

// UnsavedChangesHelper.h
#ifndef RUWA_UI_WIDGETS_TOPBAR_UNSAVEDCHANGESHELPER_H
#define RUWA_UI_WIDGETS_TOPBAR_UNSAVEDCHANGESHELPER_H

#include <QWidget>

namespace ruwa::ui::tabs {
class WorkspaceTab;
}

namespace ruwa::ui::widgets {

/**
 * @brief Helper for "Save changes before closing?" flow
 *
 * Shows MessagePopup when closing a modified WorkspaceTab.
 * Returns true if close can proceed, false if user cancelled.
 */
bool prepareWorkspaceTabForClose(ruwa::ui::tabs::WorkspaceTab* wsTab, QWidget* context);

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TOPBAR_UNSAVEDCHANGESHELPER_H
