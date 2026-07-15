// SPDX-License-Identifier: MPL-2.0

// DockLayoutPresetStore.h
#ifndef RUWA_UI_DOCKING_STATE_DOCKLAYOUTPRESETSTORE_H
#define RUWA_UI_DOCKING_STATE_DOCKLAYOUTPRESETSTORE_H

#include "DockLayoutPreset.h"

#include <QObject>
#include <QUuid>
#include <optional>

namespace ruwa::ui::docking {

/**
 * Persists user-defined dock layout presets (QSettings), similar to ThemeManager custom themes.
 */
class DockLayoutPresetStore : public QObject {
    Q_OBJECT

public:
    static DockLayoutPresetStore& instance();

    QVector<DockLayoutPreset> customPresets() const { return m_custom; }

    /// Built-in presets first, then custom (same order as theme list pattern).
    QVector<DockLayoutPreset> allPresets() const;

    std::optional<DockLayoutPreset> presetById(const QUuid& id) const;

    QString suggestUniqueName(const QString& base) const;

    void addCustomPreset(const DockLayoutPreset& preset);
    void updateCustomPreset(const DockLayoutPreset& preset);
    void removeCustomPreset(const QUuid& id);

signals:
    void changed();

private:
    explicit DockLayoutPresetStore(QObject* parent = nullptr);

    void load();
    void save();

    QVector<DockLayoutPreset> m_custom;
};

} // namespace ruwa::ui::docking

#endif
