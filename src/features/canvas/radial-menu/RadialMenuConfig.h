// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   R A D I A L   M E N U
// ======================================================================================
//   File        : RadialMenuConfig.h
//   Description : Persistence for the user-configurable radial menu layout.
// ======================================================================================

#ifndef RUWA_UI_CANVAS_RADIALMENUCONFIG_H
#define RUWA_UI_CANVAS_RADIALMENUCONFIG_H

#include "RadialMenuModel.h"

#include <QObject>

namespace ruwa::ui::widgets {

/**
 * @brief Single source of truth for the radial menu layout.
 *
 * The layout is a per-user preference rather than per-document or per-panel
 * state, so it lives in QSettings next to the shortcut overrides instead of in
 * the workspace layout file. Writes go through a worker thread for the same
 * reason ShortcutManager does it: QSettings::sync() can stall the UI for
 * seconds on Windows once the INI grows.
 */
class RadialMenuConfig : public QObject {
    Q_OBJECT

public:
    static RadialMenuConfig& instance();

    /// Current layout; loads from settings on first access.
    const RadialMenuLayout& layout();

    /// Replace the layout and persist it. Emits layoutChanged().
    void setLayout(const RadialMenuLayout& layout);

    /// Replace a single page (see RadialMenuLayout::setPage) and persist.
    void setPage(const RadialMenuPage& page);

    /// Drop user customization and go back to RadialMenuLayout::defaults().
    void resetToDefaults();

signals:
    /// The layout changed — open menus should rebuild from it.
    void layoutChanged();

private:
    RadialMenuConfig();
    ~RadialMenuConfig() override = default;

    Q_DISABLE_COPY_MOVE(RadialMenuConfig)

    void ensureLoaded();
    void save() const;

    RadialMenuLayout m_layout;
    bool m_loaded = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_CANVAS_RADIALMENUCONFIG_H
