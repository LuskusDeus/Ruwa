// SPDX-License-Identifier: MPL-2.0

// RecentColorsPersistence.h
#ifndef RUWA_FEATURES_COLOR_RECENTCOLORSPERSISTENCE_H
#define RUWA_FEATURES_COLOR_RECENTCOLORSPERSISTENCE_H

#include <QColor>
#include <QVector>

namespace ruwa::ui {

/// Global persistence for recent colors (QSettings).
/// Load on project create, save when user draws.
class RecentColorsPersistence {
public:
    static QVector<QColor> load();
    static void save(const QVector<QColor>& colors);
};

} // namespace ruwa::ui

#endif // RUWA_FEATURES_COLOR_RECENTCOLORSPERSISTENCE_H
