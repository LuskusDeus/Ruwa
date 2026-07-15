// SPDX-License-Identifier: MPL-2.0

// RecentColorsPersistence.cpp
#include "RecentColorsPersistence.h"
#include <QCoreApplication>
#include <QSettings>

namespace ruwa::ui {

QVector<QColor> RecentColorsPersistence::load()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("Color");
    const int count = settings.value("recentColorsCount", 0).toInt();
    QVector<QColor> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        const quint32 rgba = settings.value(QString("recentColor%1").arg(i), 0u).toUInt();
        if (rgba != 0) {
            result.append(QColor::fromRgba(rgba));
        }
    }
    settings.endGroup();
    return result;
}

void RecentColorsPersistence::save(const QVector<QColor>& colors)
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("Color");
    settings.setValue("recentColorsCount", colors.size());
    for (int i = 0; i < colors.size(); ++i) {
        settings.setValue(QString("recentColor%1").arg(i), colors[i].rgba());
    }
    settings.endGroup();
    settings.sync();
}

} // namespace ruwa::ui
