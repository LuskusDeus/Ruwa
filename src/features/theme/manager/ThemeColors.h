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
 * @brief Semantic typography roles used by the application UI.
 *
 * Widget code must request a role instead of embedding a numeric font size.
 * Display is reserved for oversized hero text. H0 is the largest regular
 * heading; H6 is the smallest heading. Subtitle and body roles use the UI
 * family, while Code uses the code family.
 */
enum class ThemeFontRole {
    Display,
    H0,
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,
    Subtitle,
    BodyLarge,
    Label,
    Body,
    Small,
    Caption,
    Micro,
    Code
};

/**
 * @brief Font family selection, independent from the semantic size role.
 *
 * Most widgets use the family implied by ThemeFontRole. Components that need
 * a heading-sized UI font (or a body-sized title font) can explicitly select
 * the family without bypassing the theme typography scale.
 */
enum class ThemeFontFamilyRole { Ui, Title, Code };

/**
 * @brief Theme-owned typography scale.
 *
 * The defaults preserve the existing visual scale anchored at the legacy
 * 16pt title, 9pt UI and 9pt code sizes.
 */
struct ThemeFontSizes {
    int display { 54 };
    int h0 { 32 };
    int h1 { 26 };
    int h2 { 22 };
    int h3 { 18 };
    int h4 { 16 };
    int h5 { 14 };
    int h6 { 12 };
    int subtitle { 13 };
    int bodyLarge { 11 };
    int label { 10 };
    int body { 9 };
    int small { 8 };
    int caption { 7 };
    int micro { 6 };
    int code { 9 };

    /// Convert persisted reference point sizes to DPI-independent UI pixels.
    /// 96 DPI preserves the visual size used when the theme scale was authored.
    static int pixelSize(int referencePointSize)
    {
        return qMax(1, qRound(referencePointSize * (96.0 / 72.0)));
    }

    static ThemeFontSizes fromLegacy(int uiSize, int codeSize, int titleSize)
    {
        ThemeFontSizes sizes;
        sizes.display = titleSize + 38;
        sizes.h0 = titleSize + 16;
        sizes.h1 = titleSize + 10;
        sizes.h2 = titleSize + 6;
        sizes.h3 = titleSize + 2;
        sizes.h4 = titleSize;
        sizes.h5 = titleSize - 2;
        sizes.h6 = titleSize - 4;
        sizes.subtitle = titleSize - 3;
        sizes.bodyLarge = uiSize + 2;
        sizes.label = uiSize + 1;
        sizes.body = uiSize;
        sizes.small = uiSize - 1;
        sizes.caption = uiSize - 2;
        sizes.micro = uiSize - 3;
        sizes.code = codeSize;
        sizes.normalize();
        return sizes;
    }

    int value(ThemeFontRole role) const
    {
        switch (role) {
        case ThemeFontRole::Display:
            return display;
        case ThemeFontRole::H0:
            return h0;
        case ThemeFontRole::H1:
            return h1;
        case ThemeFontRole::H2:
            return h2;
        case ThemeFontRole::H3:
            return h3;
        case ThemeFontRole::H4:
            return h4;
        case ThemeFontRole::H5:
            return h5;
        case ThemeFontRole::H6:
            return h6;
        case ThemeFontRole::Subtitle:
            return subtitle;
        case ThemeFontRole::BodyLarge:
            return bodyLarge;
        case ThemeFontRole::Label:
            return label;
        case ThemeFontRole::Body:
            return body;
        case ThemeFontRole::Small:
            return small;
        case ThemeFontRole::Caption:
            return caption;
        case ThemeFontRole::Micro:
            return micro;
        case ThemeFontRole::Code:
            return code;
        }
        return body;
    }

    void setValue(ThemeFontRole role, int size)
    {
        size = std::clamp(size, MinimumSize, MaximumSize);
        switch (role) {
        case ThemeFontRole::Display:
            display = size;
            break;
        case ThemeFontRole::H0:
            h0 = size;
            break;
        case ThemeFontRole::H1:
            h1 = size;
            break;
        case ThemeFontRole::H2:
            h2 = size;
            break;
        case ThemeFontRole::H3:
            h3 = size;
            break;
        case ThemeFontRole::H4:
            h4 = size;
            break;
        case ThemeFontRole::H5:
            h5 = size;
            break;
        case ThemeFontRole::H6:
            h6 = size;
            break;
        case ThemeFontRole::Subtitle:
            subtitle = size;
            break;
        case ThemeFontRole::BodyLarge:
            bodyLarge = size;
            break;
        case ThemeFontRole::Label:
            label = size;
            break;
        case ThemeFontRole::Body:
            body = size;
            break;
        case ThemeFontRole::Small:
            small = size;
            break;
        case ThemeFontRole::Caption:
            caption = size;
            break;
        case ThemeFontRole::Micro:
            micro = size;
            break;
        case ThemeFontRole::Code:
            code = size;
            break;
        }
    }

    void normalize()
    {
        display = std::clamp(display, MinimumSize, MaximumSize);
        h0 = std::clamp(h0, MinimumSize, MaximumSize);
        h1 = std::clamp(h1, MinimumSize, MaximumSize);
        h2 = std::clamp(h2, MinimumSize, MaximumSize);
        h3 = std::clamp(h3, MinimumSize, MaximumSize);
        h4 = std::clamp(h4, MinimumSize, MaximumSize);
        h5 = std::clamp(h5, MinimumSize, MaximumSize);
        h6 = std::clamp(h6, MinimumSize, MaximumSize);
        subtitle = std::clamp(subtitle, MinimumSize, MaximumSize);
        bodyLarge = std::clamp(bodyLarge, MinimumSize, MaximumSize);
        label = std::clamp(label, MinimumSize, MaximumSize);
        body = std::clamp(body, MinimumSize, MaximumSize);
        small = std::clamp(small, MinimumSize, MaximumSize);
        caption = std::clamp(caption, MinimumSize, MaximumSize);
        micro = std::clamp(micro, MinimumSize, MaximumSize);
        code = std::clamp(code, MinimumSize, MaximumSize);
    }

    bool operator==(const ThemeFontSizes& other) const
    {
        return display == other.display && h0 == other.h0 && h1 == other.h1 && h2 == other.h2
            && h3 == other.h3 && h4 == other.h4 && h5 == other.h5 && h6 == other.h6
            && subtitle == other.subtitle && bodyLarge == other.bodyLarge && label == other.label
            && body == other.body && small == other.small && caption == other.caption
            && micro == other.micro && code == other.code;
    }

    bool operator!=(const ThemeFontSizes& other) const { return !(*this == other); }

private:
    static constexpr int MinimumSize = 6;
    static constexpr int MaximumSize = 96;
};

/**
 * @brief Font configuration for a theme
 */
struct ThemeFonts {
    QString uiFont { FontFamilyNames::JetBrainsMono }; ///< Main UI font
    QString codeFont { FontFamilyNames::JetBrainsMono }; ///< Monospace code font
    QString titleFont { FontFamilyNames::IBMPlexSansCondensed }; ///< Large titles font

    ThemeFontSizes sizes;

    QString family(ThemeFontRole role) const
    {
        switch (role) {
        case ThemeFontRole::Display:
        case ThemeFontRole::H0:
        case ThemeFontRole::H1:
        case ThemeFontRole::H2:
        case ThemeFontRole::H3:
        case ThemeFontRole::H4:
        case ThemeFontRole::H5:
        case ThemeFontRole::H6:
            return titleFont;
        case ThemeFontRole::Code:
            return codeFont;
        default:
            return uiFont;
        }
    }

    QString family(ThemeFontFamilyRole role) const
    {
        switch (role) {
        case ThemeFontFamilyRole::Ui:
            return uiFont;
        case ThemeFontFamilyRole::Title:
            return titleFont;
        case ThemeFontFamilyRole::Code:
            return codeFont;
        }
        return uiFont;
    }

    QFont getFont(ThemeFontRole role, int size = -1) const
    {
        QFont font(family(role));
        font.setPixelSize(ThemeFontSizes::pixelSize(size > 0 ? size : sizes.value(role)));
        return font;
    }

    QFont getFont(ThemeFontRole sizeRole, ThemeFontFamilyRole familyRole, int size = -1) const
    {
        QFont font(family(familyRole));
        font.setPixelSize(ThemeFontSizes::pixelSize(size > 0 ? size : sizes.value(sizeRole)));
        return font;
    }

    // Helper to create font instances
    QFont getUIFont(int size = -1) const
    {
        QFont font(uiFont);
        font.setPixelSize(ThemeFontSizes::pixelSize(size > 0 ? size : sizes.body));
        return font;
    }

    QFont getCodeFont(int size = -1) const
    {
        QFont font(codeFont);
        font.setPixelSize(ThemeFontSizes::pixelSize(size > 0 ? size : sizes.code));
        return font;
    }

    QFont getTitleFont(int size = -1) const
    {
        QFont f(titleFont);
        f.setPixelSize(ThemeFontSizes::pixelSize(size > 0 ? size : sizes.h4));
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
