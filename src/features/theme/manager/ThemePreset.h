// SPDX-License-Identifier: MPL-2.0

// ThemePreset.h
#ifndef RUWA_UI_CORE_THEME_THEMEPRESET_H
#define RUWA_UI_CORE_THEME_THEMEPRESET_H

#include "ThemeColors.h"
#include "shared/resources/FontFamilyNames.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QColor>
#include <QVector>
#include <QUuid>

namespace ruwa::ui::core {

struct ThemePreset {
    // === Identification ===
    QUuid id;
    QString name;
    QString description;
    bool isBuiltIn { true };
    bool isDark { true };
    bool isFavorite { false }; // Show in ThemeSelectorWidget

    // === Fonts ===
    ThemeFonts fonts;

    // === Core Colors ===
    QColor primary;
    QColor background;
    QColor surface;
    QColor surfaceAlt;
    QColor border;

    // === Secondary Accent ===
    QColor accent; // Bright vivid accent color

    // === Text Colors ===
    QColor text;
    QColor textMuted;
    QColor textOnPrimary;

    // === Overlay ===
    QColor overlayColor;

    // === Semantic Colors ===
    QColor success;
    QColor warning;
    QColor error;
    QColor info;

    // === Computed Colors ===
    QColor primaryHover() const { return adjustBrightness(primary, isDark ? 0.85 : 1.15); }
    QColor primaryPressed() const { return adjustBrightness(primary, isDark ? 0.7 : 1.3); }
    QColor surfaceHover() const { return adjustBrightness(surface, isDark ? 1.2 : 0.95); }
    QColor borderLight() const { return adjustBrightness(border, isDark ? 1.4 : 0.8); }

    QColor textDisabled() const { return withAlpha(textMuted, 128); }

    // === Helper Methods ===
    static QColor adjustBrightness(const QColor& color, qreal factor)
    {
        int r = qBound(0, static_cast<int>(color.red() * factor), 255);
        int g = qBound(0, static_cast<int>(color.green() * factor), 255);
        int b = qBound(0, static_cast<int>(color.blue() * factor), 255);
        return QColor(r, g, b, color.alpha());
    }

    static QColor interpolate(const QColor& from, const QColor& to, qreal t)
    {
        t = qBound(0.0, t, 1.0);
        return QColor(from.red() + (to.red() - from.red()) * t,
            from.green() + (to.green() - from.green()) * t,
            from.blue() + (to.blue() - from.blue()) * t,
            from.alpha() + (to.alpha() - from.alpha()) * t);
    }

    static QColor withAlpha(const QColor& color, int alpha)
    {
        QColor c = color;
        c.setAlpha(qBound(0, alpha, 255));
        return c;
    }

    // === THEMES ===

    // 1. OBSIDIAN (Нейтральная, Высокий контраст)
    static ThemePreset obsidian()
    {
        ThemePreset t;
        t.id = QUuid("{a1b2c3d4-2222-4000-8000-000000000002}");
        t.name = "Obsidian";
        t.description = "Deep black theme with calibrated contrast";
        t.isBuiltIn = true;
        t.isDark = true;
        t.isFavorite = true;

        // Fonts: Instrument Serif (titles), DM Sans 18pt (body)
        t.fonts.uiFont = FontFamilyNames::DMSans18pt;
        t.fonts.codeFont = FontFamilyNames::JetBrainsMono;
        t.fonts.titleFont = FontFamilyNames::InstrumentSerif;

        t.background = QColor(10, 10, 10);
        t.surface = QColor(18, 18, 18);
        t.surfaceAlt = QColor(26, 26, 26);
        t.border = QColor(40, 40, 40);

        t.primary = QColor(220, 220, 220);
        t.accent = QColor(124, 92, 252); // #7c5cfc — vivid purple
        t.text = QColor(225, 225, 225);
        t.textMuted = QColor(130, 130, 130);
        t.textOnPrimary = QColor(10, 10, 10);

        t.overlayColor = QColor(255, 255, 255);
        t.success = QColor(90, 180, 95);
        t.warning = QColor(230, 180, 20);
        t.error = QColor(230, 80, 80);
        t.info = QColor(50, 160, 230);

        return t;
    }

    // 2. GRAPHITE (Нейтральная, Мягкая)
    static ThemePreset graphite()
    {
        ThemePreset t;
        t.id = QUuid("{a1b2c3d4-1111-4000-8000-000000000001}");
        t.name = "Graphite";
        t.description = "Matte charcoal tones, low eye strain";
        t.isBuiltIn = true;
        t.isDark = true;
        t.isFavorite = true;

        // Fonts: Instrument Serif (titles), DM Sans 18pt (body)
        t.fonts.uiFont = FontFamilyNames::DMSans18pt;
        t.fonts.codeFont = FontFamilyNames::JetBrainsMono;
        t.fonts.titleFont = FontFamilyNames::InstrumentSerif;

        t.background = QColor(35, 35, 35);
        t.surface = QColor(48, 48, 48);
        t.surfaceAlt = QColor(58, 58, 58);
        t.border = QColor(70, 70, 70);

        t.primary = QColor(180, 180, 180);
        t.accent = QColor(64, 156, 255); // #409cff — bright blue
        t.text = QColor(225, 225, 225);
        t.textMuted = QColor(140, 140, 140);
        t.textOnPrimary = QColor(35, 35, 35);

        t.overlayColor = QColor(255, 255, 255);
        t.success = QColor(100, 160, 110);
        t.warning = QColor(200, 180, 100);
        t.error = QColor(200, 90, 90);
        t.info = QColor(100, 140, 180);
        return t;
    }

    // 3. LUXURY
    static ThemePreset luxury()
    {
        ThemePreset t;
        t.id = QUuid("{a1b2c3d4-9999-4000-8000-000000000009}");
        t.name = "Luxury";
        t.description = "Refined dark luxury with muted brass accents";
        t.isBuiltIn = true;
        t.isDark = true;
        t.isFavorite = true;

        // Fonts: Elegant, luxurious
        t.fonts.uiFont = FontFamilyNames::Manrope;
        t.fonts.codeFont = FontFamilyNames::JetBrainsMono;
        t.fonts.titleFont = FontFamilyNames::Manrope;

        t.background = QColor(14, 13, 12);
        t.surface = QColor(24, 22, 20);
        t.surfaceAlt = QColor(32, 29, 26);
        t.border = QColor(45, 42, 38);

        t.primary = QColor(170, 145, 80);
        t.accent = QColor(212, 160, 23); // #d4a017 — warm gold
        t.text = QColor(235, 232, 225);
        t.textMuted = QColor(150, 145, 135);
        t.textOnPrimary = QColor(18, 16, 14);

        t.overlayColor = QColor(255, 248, 235);
        t.success = QColor(140, 170, 130);
        t.warning = QColor(185, 160, 95);
        t.error = QColor(165, 85, 80);
        t.info = QColor(130, 150, 170);

        return t;
    }

    // 4. NEBULA
    static ThemePreset nebula()
    {
        ThemePreset t;
        t.id = QUuid("{a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d}");
        t.name = "Nebula";
        t.description = "Deep slate aesthetics for focus and clarity";
        t.isBuiltIn = true;
        t.isDark = true;
        t.isFavorite = true;

        // Fonts: Technical, modern
        t.fonts.uiFont = FontFamilyNames::IBMPlexSansCondensed;
        t.fonts.codeFont = FontFamilyNames::JetBrainsMono;
        t.fonts.titleFont = FontFamilyNames::IBMPlexSansCondensed;

        t.background = QColor(23, 27, 33);
        t.surface = QColor(33, 37, 43);
        t.surfaceAlt = QColor(40, 44, 52);
        t.border = QColor(58, 62, 75);

        t.primary = QColor(86, 182, 194);
        t.accent = QColor(0, 200, 155); // #00c89b — vivid teal
        t.text = QColor(220, 223, 228);
        t.textMuted = QColor(105, 115, 130);
        t.textOnPrimary = QColor(33, 37, 43);

        t.overlayColor = QColor(220, 240, 255);
        t.success = QColor(152, 195, 121);
        t.warning = QColor(229, 192, 123);
        t.error = QColor(224, 108, 117);
        t.info = QColor(97, 175, 239);

        return t;
    }

    // 5. SAKURA (НОВАЯ ТЕМА)
    static ThemePreset sakura()
    {
        ThemePreset t;
        t.id = QUuid("{a1b2c3d4-7777-4000-8000-000000000007}");
        t.name = "Sakura";
        t.description = "Deep violet luxury with muted blossom accents";
        t.isBuiltIn = true;
        t.isDark = true;
        t.isFavorite = true;

        // Fonts: Soft, elegant, comfortable
        t.fonts.uiFont = FontFamilyNames::Comfortaa;
        t.fonts.codeFont = FontFamilyNames::JetBrainsMono;
        t.fonts.titleFont = FontFamilyNames::Comfortaa;

        // === Backgrounds (Luxury Style) ===
        // Очень глубокий темный, почти черный, с едва заметным фиолетовым подтоном.
        // Luxury: (14, 13, 12) -> Sakura: (15, 13, 16)
        t.background = QColor(15, 13, 16);
        t.surface = QColor(25, 22, 26);
        t.surfaceAlt = QColor(34, 30, 35);
        t.border = QColor(50, 42, 52); // Темная, сдержанная граница

        // === Accent ===
        // #8d7690 (RGB: 141, 118, 144) - Muted Mauve
        t.primary = QColor(141, 90, 147);
        t.accent = QColor(232, 84, 138); // #e8548a — vivid pink-rose

        // === Text ===
        // Светлый платиновый оттенок, не режущий глаз белый
        t.text = QColor(238, 232, 236);
        t.textMuted = QColor(145, 135, 145);
        // Темный текст на акцентной кнопке для читаемости
        t.textOnPrimary = QColor(20, 16, 22);

        // === Overlay ===
        t.overlayColor = QColor(255, 240, 250);

        // === Semantics (Desaturated / Premium) ===
        // Приглушенные цвета, чтобы не нарушать "дорогую" атмосферу
        t.success = QColor(130, 165, 135); // Muted Sage
        t.warning = QColor(185, 165, 120); // Champagne Gold
        t.error = QColor(175, 95, 105); // Dried Rose
        t.info = QColor(135, 145, 175); // Dusty Blue

        return t;
    }

    /// Returns translated display name for built-in themes, or original name for custom
    static QString translatedDisplayName(const ThemePreset& p)
    {
        if (!p.isBuiltIn)
            return p.name;
        const QByteArray utf8 = p.name.toUtf8();
        return QCoreApplication::translate("ThemePreset", utf8.constData());
    }

    static QVector<ThemePreset> builtInThemes()
    {
        return {
            obsidian(), // Standard Dark
            graphite(), // Soft Grey
            luxury(), // Darkest + Gold
            nebula(), // Slate + Cyan
            sakura() // Purple + Pink
        };
    }
};

} // namespace ruwa::ui::core

#endif
