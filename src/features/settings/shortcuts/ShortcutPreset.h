// SPDX-License-Identifier: MPL-2.0

// ShortcutPreset.h
#ifndef RUWA_FEATURES_SETTINGS_SHORTCUTS_SHORTCUTPRESET_H
#define RUWA_FEATURES_SETTINGS_SHORTCUTS_SHORTCUTPRESET_H

#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>
#include <QUuid>

namespace ruwa::features::settings::shortcuts {

/**
 * @brief A named set of command and canvas hold-shortcut overrides.
 *
 * "Default" (built-in) has an empty @ref bindings — i.e. every command uses
 * its compiled-in defaults. Custom presets store only the deltas from the
 * defaults, including the three single-key canvas bindings.
 */
struct ShortcutPreset {
    QUuid id;
    QString name;
    bool isBuiltIn = false;
    QHash<QString, QKeySequence> bindings; // commandId -> overridden sequence
    QHash<QString, int> canvasModifierBindings; // canvas action id -> overridden single key

    QJsonObject toJson() const;
    static ShortcutPreset fromJson(const QJsonObject& obj);
};

} // namespace ruwa::features::settings::shortcuts

#endif
