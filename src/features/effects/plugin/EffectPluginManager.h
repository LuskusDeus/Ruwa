// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTPLUGINMANAGER_H
#define RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTPLUGINMANAGER_H

// Discovers, negotiates and registers effect plugin DLLs. All standard effects
// eventually ship as plugins loaded through here; there is no second internal
// registration path. Registration is transactional: an entire plugin is
// validated before any of its effects touch the registries, and a mid-commit
// failure rolls back cleanly.

#include "features/effects/LayerEffectTypes.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

class QLibrary;
struct RuwaEffectPluginApi;
struct RuwaEffectDescriptor;

namespace ruwa::core::effects::plugin {

class EffectPluginManager {
public:
    static EffectPluginManager& instance();
    ~EffectPluginManager();

    /// Load standard (application) and user effect plugins. Call once at startup,
    /// BEFORE the effects UI and the first GLLayerEffectRenderer initialisation.
    /// Safe to call when no plugin directories exist (loads nothing).
    void loadStandardAndUserPlugins();

    /// Load every *.dll in `directory`. Returns the number of plugins loaded.
    int loadDirectory(const QString& directory);

    /// Load a single plugin DLL. Returns true on success (fully registered).
    bool loadPluginFile(const QString& filePath);

    /// Apply the plugin's sequential schema migrations to a loaded state, in
    /// place, up to the effect's current version. No-op for effects without a
    /// migration callback or already at the current version.
    void migrateState(ruwa::core::effects::LayerEffectState& state) const;

    QStringList loadedPluginIds() const;

private:
    EffectPluginManager() = default;

    struct LoadedPlugin {
        QLibrary* library = nullptr;
        const RuwaEffectPluginApi* api = nullptr;
        QString path;
        QStringList typeIds;
    };

    std::vector<LoadedPlugin> m_plugins;
    QSet<QString> m_pluginIds;
    QSet<QString> m_effectDirsScanned;
    // typeId -> plugin descriptor, for migrateState().
    QHash<QString, const RuwaEffectDescriptor*> m_effectsByType;
};

} // namespace ruwa::core::effects::plugin

#endif // RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTPLUGINMANAGER_H
