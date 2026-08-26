// SPDX-License-Identifier: MPL-2.0

// AppSettings.h
#ifndef RUWA_CORE_SETTINGS_APPSETTINGS_H
#define RUWA_CORE_SETTINGS_APPSETTINGS_H

#include "shared/resources/ResourceAssetCatalog.h"

#include <QString>
#include <QStringList>
#include <QSize>
#include <QUuid>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <array>
#include <cstdint>

namespace ruwa::core {

inline constexpr std::array<qreal, 5> kUiScaleFactors { 0.85, 1.0, 1.25, 1.75, 2.25 };
inline constexpr int kDefaultUiScaleIndex = 1;

constexpr int normalizedUiScaleIndex(int index)
{
    return index < 0 ? 0
                     : (index >= static_cast<int>(kUiScaleFactors.size())
                             ? static_cast<int>(kUiScaleFactors.size()) - 1
                             : index);
}

/**
 * @brief Structure holding all application settings
 */
struct AppSettings {
    // === ONBOARDING ===
    struct Onboarding {
        /// Controls whether the dedicated first-run integration page should be skipped.
        bool firstRunIntegrationCompleted = false;
    } onboarding;

    // === APPEARANCE ===
    struct Appearance {
        QUuid themeId; // Selected theme ID
    int uiScale = kDefaultUiScaleIndex; // 0=85%, 1=100%, 2=125%, 3=175%, 4=225%
        QString language = "en"; // UI language: "en", "ru", etc.
        /// Tab strip in title bar: 0=left, 1=centered in available space
        int topBarTabAlignment = 0;
        /// Welcome screen banner: built-in resource path or absolute file path when fixed
        QString welcomeBannerFixedKey = ruwa::ui::core::assets::images::defaultWelcomeBannerPath();
        /// If true, pick a random image from built-ins + custom paths on each load
        bool welcomeBannerRandomize = true;
        /// User-added banner images (absolute paths)
        QStringList welcomeBannerCustomPaths;
        /// Per-custom-image crop region, normalized to [0,1] of the source image.
        /// Key = absolute path (matches welcomeBannerCustomPaths). Absent or full
        /// rect (0,0,1,1) means "no crop" (use the whole image, legacy behaviour).
        QHash<QString, QRectF> welcomeBannerCustomCrops;
        /// 0=Basic (auto contrast from image), 1=Inverted (swap light/dark text & buttons)
        int welcomeBannerTextColorMode = 0;
        /// Brush panel row accent color indices by brush id. 0 = default.
        QHash<QString, int> brushDisplayColorIndices;
        /// Brush ids marked as favorites. Stored globally, independent of panel layouts.
        QSet<QString> favoriteBrushIds;
        /// Stable user-defined order for favorite brushes. Empty means the
        /// Brushes Panel should derive its legacy pack order. Membership remains
        /// authoritative in favoriteBrushIds for fast lookups.
        QStringList favoriteBrushOrder;
    } appearance;

    // === EDITOR ===
    struct Editor {
        int autoSaveInterval = 5; // 0=Off, 2=2min, 5=5min, 10=10min
        bool quickshapesEnabled = true; // Hold stroke to morph into line/circle/triangle/square
        bool autoSnapCanvasEnabled = true;
        bool autoSnapLayersEnabled = true;
        bool autoSnapEqualSpacingEnabled = true;
        bool pixelAlignRasterMovesEnabled = true;
    } editor;

    // === PERFORMANCE ===
    struct Performance {
        int undoMemoryLimitMb = 3072; // 300, 1024, 3072, 8192
        int tabletBackend = 2; // 0=WinTab (Qt), 1=Windows Ink, 2=WinTab (Ruwa)
    } performance;

    // === USAGE ===
    struct Usage {
        /// Accumulated wall-clock seconds the application has been running, across all
        /// sessions. Committed periodically while running, so a crash loses at most one
        /// commit interval.
        qint64 totalSeconds = 0;
    } usage;

    // === WINDOW STATE ===
    struct WindowState {
        QSize mainWindowSize = QSize(1280, 800);
        bool isMaximized = false;
        int lastActiveTabIndex = 0;
    } windowState;

    // === DEFAULT VALUES ===
    static AppSettings defaults()
    {
        AppSettings settings;
        // All defaults are already set via in-class initializers
        return settings;
    }
};

} // namespace ruwa::core

#endif // RUWA_CORE_SETTINGS_APPSETTINGS_H
