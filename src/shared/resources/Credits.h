// SPDX-License-Identifier: MPL-2.0

// Credits.h
#ifndef RUWA_UI_CORE_RESOURCES_CREDITS_H
#define RUWA_UI_CORE_RESOURCES_CREDITS_H

#include <QString>
#include <QStringList>

namespace ruwa::ui::core {

/// Who made Ruwa — single source of truth, shared by the About page and the
/// splash screen so the two can never list different people.
namespace Credits {

inline const QString Developer = QStringLiteral("Luskus Deus");

inline const QStringList& testers()
{
    static const QStringList names = { QStringLiteral("kaixxxy"), QStringLiteral("Lozar"),
        QStringLiteral("Mikko_el"), QStringLiteral("HipaaaH!~"), QStringLiteral("Ayami"),
        QStringLiteral("Dgan"), QStringLiteral("KrOl"), QStringLiteral("KAMENOV PLUS"),
        QStringLiteral("Enum Nektovse"), QStringLiteral("kira."), QStringLiteral("medomij") };
    return names;
}

} // namespace Credits

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_RESOURCES_CREDITS_H
