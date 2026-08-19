// SPDX-License-Identifier: MPL-2.0

// ProjectPresets.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETS_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETS_H

#include <QList>
#include <QSize>
#include <QString>

namespace ruwa::ui::widgets {

struct Preset {
    QString nameKey;
    QSize size;
};

struct PresetCategory {
    QString nameKey;
    QList<Preset> presets;
};

/**
 * @brief Built-in New Project presets (single source of truth).
 *
 * Names are stable English keys; the UI translates them through
 * QCoreApplication::translate() in the NewProjectContent context.
 */
class ProjectPresets {
public:
    static const QList<PresetCategory>& categories();

    /// Stable key of the first built-in preset with exactly this size ({} when none matches).
    static QString matchingNameKey(const QSize& size);

    /// Translate a stable preset/category key for display.
    static QString translated(const QString& nameKey);
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETS_H
