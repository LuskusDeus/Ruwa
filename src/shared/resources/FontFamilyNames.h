// SPDX-License-Identifier: MPL-2.0

// FontFamilyNames.h
#ifndef RUWA_UI_CORE_RESOURCES_FONTFAMILYNAMES_H
#define RUWA_UI_CORE_RESOURCES_FONTFAMILYNAMES_H

#include <QString>

namespace ruwa::ui::core {

/// Central font family name constants — single source of truth.
/// Must match names returned by QFontDatabase when loading TTF files.
namespace FontFamilyNames {

// === Obsidian & Graphite themes ===
inline const QString InstrumentSerif = QStringLiteral("Instrument Serif");
inline const QString DMSans18pt = QStringLiteral("DM Sans 18pt");

// === Other theme fonts ===
inline const QString JetBrainsMono = QStringLiteral("JetBrains Mono");
inline const QString Comfortaa = QStringLiteral("Comfortaa");
inline const QString Manrope = QStringLiteral("Manrope");
inline const QString IBMPlexSansCondensed = QStringLiteral("IBM Plex Sans Condensed");

// Theme presets persisted before the open-source font migration may use these
// family names. Preserve their intent without retaining the removed font files.
inline QString migrateLegacyFamilyName(const QString& family)
{
    if (family == QStringLiteral("Avenir LT") || family == QStringLiteral("Avenir LT 55 Roman")) {
        return Manrope;
    }
    if (family == QStringLiteral("Lucian Schoenschrift")
        || family == QStringLiteral("Lucien Schoenschriftv CAT")) {
        return IBMPlexSansCondensed;
    }
    if (family == QStringLiteral("IBM Plex Sans")) {
        return IBMPlexSansCondensed;
    }
    return family;
}

// === System fallbacks ===
inline const QString SegoeUI = QStringLiteral("Segoe UI");
inline const QString Consolas = QStringLiteral("Consolas");
inline const QString FiraCode = QStringLiteral("Fira Code");
inline const QString SFMono = QStringLiteral("SF Mono");
inline const QString Monaco = QStringLiteral("Monaco");
inline const QString CourierNew = QStringLiteral("Courier New");
inline const QString Arial = QStringLiteral("Arial");
inline const QString Impact = QStringLiteral("Impact");
inline const QString ArialBlack = QStringLiteral("Arial Black");
inline const QString HelveticaNeue = QStringLiteral("Helvetica Neue");

} // namespace FontFamilyNames
} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_RESOURCES_FONTFAMILYNAMES_H
