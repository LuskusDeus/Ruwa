// SPDX-License-Identifier: MPL-2.0

// ThemeColors.h
#ifndef RUWA_UI_CORE_THEME_THEMECOLORS_H
#define RUWA_UI_CORE_THEME_THEMECOLORS_H

#include "shared/resources/FontFamilyNames.h"

#include <QColor>
#include <QFont>
#include <QString>
#include <algorithm>

namespace ruwa::ui::core {

// Forward declaration
struct ThemePreset;

/**
 * @brief Font configuration for a theme
 */
struct ThemeFonts {
    QString uiFont { FontFamilyNames::JetBrainsMono }; ///< Main UI font
    QString codeFont { FontFamilyNames::JetBrainsMono }; ///< Monospace code font
    QString titleFont { FontFamilyNames::IBMPlexSansCondensed }; ///< Large titles font

    int uiSize { 9 }; ///< Default UI font size
    int codeSize { 9 }; ///< Default code font size
    int titleSize { 16 }; ///< Default title font size

    // Helper to create font instances
    QFont getUIFont(int size = -1) const { return QFont(uiFont, size > 0 ? size : uiSize); }

    QFont getCodeFont(int size = -1) const { return QFont(codeFont, size > 0 ? size : codeSize); }

    QFont getTitleFont(int size = -1) const
    {
        QFont f(titleFont, size > 0 ? size : titleSize);
        f.setWeight(QFont::Bold);
        return f;
    }
};

/**
 * @brief Active color palette for the Ruwa UI theme
 *
 * This struct holds the currently active colors and provides
 * computed color methods. It can be populated from a ThemePreset.
 */
struct ThemeColors {
    // === Core Colors (Midnight defaults) ===
    QColor primary { 251, 248, 239 }; ///< Warm white accent
    QColor background { 26, 26, 26 }; ///< Main background
    QColor surface { 40, 40, 40 }; ///< Widget surfaces
    QColor surfaceAlt { 50, 50, 50 }; ///< Alternative surface
    QColor border { 60, 60, 60 }; ///< Borders and separators

    // === Text Colors ===
    QColor text { 251, 248, 239 }; ///< Primary text
    QColor textMuted { 160, 160, 160 }; ///< Secondary/muted text
    QColor _textOnPrimary { 26, 26, 26 }; ///< Text on primary background (storage)

    // === Overlay & Effects ===
    QColor overlayColor { 255, 255, 255 }; ///< Color for overlay effects

    // === Semantic Colors ===
    QColor success { 76, 175, 80 }; ///< Success state
    QColor warning { 255, 193, 7 }; ///< Warning state
    QColor error { 244, 67, 54 }; ///< Error state
    QColor info { 33, 150, 243 }; ///< Info state

    // === Secondary Accent ===
    QColor accent { 124, 92, 252 }; ///< Vivid secondary accent (e.g. #7c5cfc)

    // === Theme Metadata ===
    bool isDark { true }; ///< Dark or light theme

    // === Fonts ===
    ThemeFonts fonts; ///< Font configuration

    // === Computed Color Methods ===

    /// Get text color for use on primary backgrounds
    QColor textOnPrimary() const { return _textOnPrimary; }

    QColor primaryHover() const { return adjustBrightness(primary, isDark ? 0.85 : 1.15); }

    QColor primaryPressed() const { return adjustBrightness(primary, isDark ? 0.70 : 1.30); }

    QColor primaryDisabled() const
    {
        QColor c = primary;
        c.setAlpha(100);
        return c;
    }

    QColor surfaceElevated() const { return adjustBrightness(surface, isDark ? 1.25 : 0.95); }

    QColor surfaceHover() const { return adjustBrightness(surface, isDark ? 1.15 : 0.92); }

    QColor borderLight() const { return adjustBrightness(border, isDark ? 1.4 : 0.7); }

    QColor borderDark() const { return adjustBrightness(border, isDark ? 0.6 : 1.3); }

    QColor textDisabled() const
    {
        QColor c = textMuted;
        c.setAlpha(128);
        return c;
    }

    QColor accentDim(int alpha = 45) const { return withAlpha(accent, alpha); }

    QColor canvas() const { return adjustBrightness(surface, isDark ? 1.5 : 0.85); }

    QColor canvasGrid() const { return adjustBrightness(surface, isDark ? 2.0 : 0.75); }

    QColor shadow(int alpha = 100) const { return QColor(0, 0, 0, alpha); }

    /// Базовый overlay для inactive состояний (очень светлый)
    QColor overlayBase() const { return withAlpha(overlayColor, isDark ? 10 : 15); }

    /// Hover overlay (светлее базового)
    QColor overlayHover() const { return withAlpha(overlayColor, isDark ? 20 : 30); }

    /// Создает overlay с произвольной интенсивностью (0.0 - 1.0)
    QColor overlay(qreal intensity) const
    {
        int alpha = static_cast<int>(std::clamp(intensity, 0.0, 1.0) * 255);
        return withAlpha(overlayColor, alpha);
    }

    /// Тонкая граница для subtle эффектов
    QColor borderSubtle() const { return withAlpha(overlayColor, isDark ? 20 : 30); }

    /// Тонкая граница при hover
    QColor borderSubtleHover() const { return withAlpha(overlayColor, isDark ? 38 : 50); }

    // === Static Helper Methods ===

    static QColor interpolate(const QColor& from, const QColor& to, qreal progress)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        return QColor(static_cast<int>(from.red() + (to.red() - from.red()) * progress),
            static_cast<int>(from.green() + (to.green() - from.green()) * progress),
            static_cast<int>(from.blue() + (to.blue() - from.blue()) * progress),
            static_cast<int>(from.alpha() + (to.alpha() - from.alpha()) * progress));
    }

    static QColor adjustBrightness(const QColor& color, qreal factor)
    {
        factor = std::clamp(factor, 0.0, 2.0);
        int r = std::clamp(static_cast<int>(color.red() * factor), 0, 255);
        int g = std::clamp(static_cast<int>(color.green() * factor), 0, 255);
        int b = std::clamp(static_cast<int>(color.blue() * factor), 0, 255);
        return QColor(r, g, b, color.alpha());
    }

    static QColor withAlpha(const QColor& color, int alpha)
    {
        QColor c = color;
        c.setAlpha(std::clamp(alpha, 0, 255));
        return c;
    }
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_THEME_THEMECOLORS_H
