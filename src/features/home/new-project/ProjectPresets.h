// SPDX-License-Identifier: MPL-2.0

// ProjectPresets.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETS_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETS_H

#include <QString>
#include <QSize>
#include <QList>

namespace ruwa::ui::widgets {

struct Preset {
    QString name;
    QSize size;
};

struct PresetCategory {
    QString name;
    QList<Preset> presets;
};

class ProjectPresets {
public:
    static QList<PresetCategory> categories();
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETS_H
