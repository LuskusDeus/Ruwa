// SPDX-License-Identifier: MPL-2.0

// DockStateSerializer.h
#ifndef RUWA_UI_DOCKING_STATE_DOCKSTATESERIALIZER_H
#define RUWA_UI_DOCKING_STATE_DOCKSTATESERIALIZER_H

#include "shell/docking/DockTypes.h"
#include "DockLayoutPreset.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>

namespace ruwa::ui::docking {

class DockManager;
class DockContainerWidget;
class DockPanel;

/**
 * @brief Handles serialization and deserialization of dock layouts
 *
 * Supports:
 * - Saving/loading complete layout state to JSON
 * - Applying layout presets
 * - Exporting/importing layouts as files
 */
class DockStateSerializer {
public:
    explicit DockStateSerializer(DockManager* manager);

    // === Save/Load State ===

    /// Save current layout to JSON
    QJsonObject saveState() const;

    /// Restore layout from JSON
    bool restoreState(const QJsonObject& state);

    /// Save to byte array (for QSettings)
    QByteArray saveToByteArray() const;

    /// Restore from byte array
    bool restoreFromByteArray(const QByteArray& data);

    // === Presets ===

    /// Apply a layout preset
    bool applyPreset(const DockLayoutPreset& preset);

    /// Create preset from current layout (layoutTree + floating when using new layout system).
    DockLayoutPreset createPresetFromCurrent(const QString& name) const;

    // === Version ===

    static constexpr int StateVersion = 1;

private:
    // Serialization helpers
    QJsonObject serializePanel(DockPanel* panel) const;
    QJsonArray serializeFloatingContainers() const;

    // Deserialization helpers
    bool deserializeFloatingContainers(const QJsonArray& arr);

    // Preset helpers
    DockPanel* findPanelForPlacement(const PanelPlacement& placement) const;

private:
    DockManager* m_manager;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_STATE_DOCKSTATESERIALIZER_H
