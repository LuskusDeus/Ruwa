// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   CONTEXT MENU TYPES
// ======================================================================================

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_CONTEXTMENUTYPES_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_CONTEXTMENUTYPES_H

namespace ruwa::ui::widgets {

/**
 * @brief Types of context menus available in the application
 */
enum class ContextMenuType {
    None = 0,
    TabBar,
    DockPanelTitle,
    LayerItem,
    Canvas,
    Sidebar,
    Generic,
    /// Short menus (2–4 actions), payload in context \c simpleActions
    SimpleActions
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_CONTEXTMENUTYPES_H
