// SPDX-License-Identifier: MPL-2.0

// ColorSource.h
#ifndef RUWA_UI_CORE_STYLE_COLORSOURCE_H
#define RUWA_UI_CORE_STYLE_COLORSOURCE_H

#include "features/theme/manager/ThemeColors.h"

namespace ruwa::ui::core {

/**
 * @brief Enum referencing colors from ThemeColors
 *
 * Used in layer configurations to specify which color to use.
 * Resolved at runtime through WidgetStyleManager::resolveColor().
 */
enum class ColorSource {
    // Core colors
    Primary,
    PrimaryHover,
    PrimaryPressed,
    Background,
    Surface,
    SurfaceAlt,
    SurfaceElevated,
    SurfaceHover,
    Border,
    BorderLight,
    BorderDark,

    // Text colors
    Text,
    TextMuted,
    TextOnPrimary,
    TextDisabled,

    // Semantic (toasts, confirmations, destructive actions)
    Success,
    Warning,
    Error,
    Info,

    // Overlay colors (white/black with alpha)
    OverlayBase, // Very subtle overlay
    OverlayHover, // Hover state overlay
    Overlay10, // 10% opacity
    Overlay20, // 20% opacity
    Overlay40, // 40% opacity

    // Border subtle (for gradient borders)
    BorderSubtle,
    BorderSubtleHover,
    BorderSubtleAlpha50, // borderSubtle with 50% alpha

    // Shadows
    Shadow20,
    Shadow25,
    Shadow100,

    // Special
    Transparent,
    Custom // Use customColor field in config
};

/**
 * @brief Resolve ColorSource to actual QColor
 *
 * @param source The color source enum
 * @param colors Current theme colors
 * @param customColor Custom color to use if source == Custom
 * @return Resolved QColor
 */
inline QColor resolveColor(
    ColorSource source, const ThemeColors& colors, const QColor& customColor = Qt::transparent)
{
    switch (source) {
    case ColorSource::Primary:
        return colors.primary;
    case ColorSource::PrimaryHover:
        return colors.primaryHover();
    case ColorSource::PrimaryPressed:
        return colors.primaryPressed();
    case ColorSource::Background:
        return colors.background;
    case ColorSource::Surface:
        return colors.surface;
    case ColorSource::SurfaceAlt:
        return colors.surfaceAlt;
    case ColorSource::SurfaceElevated:
        return colors.surfaceElevated();
    case ColorSource::SurfaceHover:
        return colors.surfaceHover();
    case ColorSource::Border:
        return colors.border;
    case ColorSource::BorderLight:
        return colors.borderLight();
    case ColorSource::BorderDark:
        return colors.borderDark();

    case ColorSource::Text:
        return colors.text;
    case ColorSource::TextMuted:
        return colors.textMuted;
    case ColorSource::TextOnPrimary:
        return colors.textOnPrimary();
    case ColorSource::TextDisabled:
        return colors.textDisabled();

    case ColorSource::Success:
        return colors.success;
    case ColorSource::Warning:
        return colors.warning;
    case ColorSource::Error:
        return colors.error;
    case ColorSource::Info:
        return colors.info;

    case ColorSource::OverlayBase:
        return colors.overlayBase();
    case ColorSource::OverlayHover:
        return colors.overlayHover();
    case ColorSource::Overlay10:
        return colors.overlay(0.10);
    case ColorSource::Overlay20:
        return colors.overlay(0.20);
    case ColorSource::Overlay40:
        return colors.overlay(0.40);

    case ColorSource::BorderSubtle:
        return colors.borderSubtle();
    case ColorSource::BorderSubtleHover:
        return colors.borderSubtleHover();
    case ColorSource::BorderSubtleAlpha50: {
        QColor c = colors.borderSubtle();
        c.setAlpha(c.alpha() / 2);
        return c;
    }

    case ColorSource::Shadow20:
        return colors.shadow(20);
    case ColorSource::Shadow25:
        return colors.shadow(25);
    case ColorSource::Shadow100:
        return colors.shadow(100);

    case ColorSource::Transparent:
        return Qt::transparent;
    case ColorSource::Custom:
        return customColor;
    }

    return Qt::transparent;
}

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_STYLE_COLORSOURCE_H
