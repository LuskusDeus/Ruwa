// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   CONTEXT MENU PROVIDER INTERFACE
// ======================================================================================

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_ICONTEXTMENUPROVIDER_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_ICONTEXTMENUPROVIDER_H

#include "ContextMenuTypes.h"
#include <QVariantMap>
#include <QtGlobal>

namespace ruwa::ui::widgets {

/**
 * @brief Interface for widgets that provide context menus
 *
 * Widgets implementing this interface can specify what type of context menu
 * they want to show and provide context data for menu items.
 */
class IContextMenuProvider {
public:
    virtual ~IContextMenuProvider() = default;

    /**
     * @brief Get the type of context menu to show
     * @return ContextMenuType or ContextMenuType::None if no menu
     */
    virtual ContextMenuType contextMenuType() const = 0;

    /**
     * @brief Get context data for the menu (e.g., tab ID, layer index, etc.)
     * @return
     * QVariantMap with context information
     */
    virtual QVariantMap contextMenuContext() const { return {}; }

    /**
     * @brief Handle a SimpleActions menu action triggered for this provider.
     *
     *
     * Providers that expose ContextMenuType::SimpleActions can override this to
     * keep the
     * action routing local instead of being hardcoded in
     * ContextMenuSystem.
     */
    virtual void handleContextMenuAction(int actionId) { Q_UNUSED(actionId); }
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_ICONTEXTMENUPROVIDER_H
