// SPDX-License-Identifier: MPL-2.0

// ShortcutPresetStore.h
#ifndef RUWA_FEATURES_SETTINGS_SHORTCUTS_SHORTCUTPRESETSTORE_H
#define RUWA_FEATURES_SETTINGS_SHORTCUTS_SHORTCUTPRESETSTORE_H

#include "ShortcutPreset.h"

#include <QObject>
#include <QUuid>
#include <QVector>
#include <optional>

namespace ruwa::features::settings::shortcuts {

/**
 * @brief Persists user-defined shortcut presets via QSettings.
 *
 * Mirrors the DockLayoutPresetStore pattern: a single built-in "Default" preset
 * (no overrides) plus any number of user-created custom presets.
 */
class ShortcutPresetStore : public QObject {
    Q_OBJECT

public:
    static ShortcutPresetStore& instance();

    /// Built-in "Default" preset (empty bindings = use compiled defaults).
    ShortcutPreset defaultPreset() const;

    QVector<ShortcutPreset> customPresets() const { return m_custom; }

    /// Built-ins first, then user presets.
    QVector<ShortcutPreset> allPresets() const;

    std::optional<ShortcutPreset> presetById(const QUuid& id) const;

    QString suggestUniqueName(const QString& base) const;

    void addCustomPreset(const ShortcutPreset& preset);
    void updateCustomPreset(const ShortcutPreset& preset);
    void removeCustomPreset(const QUuid& id);

signals:
    void changed();

private:
    explicit ShortcutPresetStore(QObject* parent = nullptr);

    void load();
    void save() const;

    QVector<ShortcutPreset> m_custom;
};

} // namespace ruwa::features::settings::shortcuts

#endif
