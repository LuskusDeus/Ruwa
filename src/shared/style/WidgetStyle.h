// SPDX-License-Identifier: MPL-2.0

// WidgetStyle.h
#ifndef RUWA_UI_CORE_STYLE_WIDGETSTYLE_H
#define RUWA_UI_CORE_STYLE_WIDGETSTYLE_H

#include "LayerConfigs.h"
#include <QString>

namespace ruwa::ui::core {

/**
 * @brief Complete style definition for a widget
 *
 * Contains all layer configurations and metrics.
 * Can be registered with WidgetStyleManager and referenced by name.
 *
 * Usage:
 *   WidgetStyle style = WidgetStyle::defaultButtonStyle();
 *   style.name = "MyButton";
 *   style.hoverGlow.enabled = true;
 *   WidgetStyleManager::instance().registerStyle(style);
 */
struct WidgetStyle {
    QString name; // Unique identifier

    // Metrics
    WidgetMetrics metrics;

    // Layer configurations (in draw order)
    BackgroundLayerConfig background;
    BorderLayerConfig border;
    HoverLayerConfig hover;
    HoverGlowConfig hoverGlow;
    ActiveBackgroundConfig activeBackground;
    ActiveBorderConfig activeBorder;
    PressLayerConfig press;
    ContentLayerConfig content;

    // Animation overrides
    AnimationConfig animations;

    // ========================================================================
    // Factory methods for common base styles
    // ========================================================================

    /**
     * @brief Default button style (matches current ChoiceButton/SidebarButton inactive)
     */
    static WidgetStyle defaultButtonStyle()
    {
        WidgetStyle style;
        style.name = "DefaultButton";

        // Metrics
        style.metrics.baseHeight = 36;
        style.metrics.baseCornerRadius = 8;

        // Background: subtle overlay
        style.background.enabled = true;
        style.background.color = ColorSource::OverlayBase;

        // Border: vertical gradient
        style.border.enabled = true;
        style.border.style = BorderStyle::VerticalGradient;
        style.border.topColor = ColorSource::BorderSubtle;
        style.border.bottomColor = ColorSource::BorderSubtleAlpha50;
        style.border.animateOnHover = true;
        style.border.hoverTopColor = ColorSource::BorderSubtleHover;

        // Hover overlay
        style.hover.enabled = true;
        style.hover.color = ColorSource::SurfaceElevated;

        // No glow by default
        style.hoverGlow.enabled = false;

        // Active: primary color
        style.activeBackground.enabled = true;
        style.activeBackground.color = ColorSource::Primary;
        style.activeBackground.hoverBrightnessBoost = 1.08;
        style.activeBackground.bottomShadow = false;

        // Active border: light gradient
        style.activeBorder.enabled = true;
        style.activeBorder.style = BorderStyle::VerticalGradient;
        style.activeBorder.topBrightness = 7.7;
        style.activeBorder.bottomBrightness = 6.2;

        // Press effect
        style.press.enabled = true;
        style.press.color = ColorSource::Shadow20;
        style.press.activeColor = ColorSource::Shadow25;

        // Content
        style.content.iconPosition = IconPosition::None;
        style.content.textAlignment = ContentAlignment::Center;
        style.content.baseFontSize = 9;
        style.content.textColor = ColorSource::TextMuted;
        style.content.textHoverColor = ColorSource::Text;
        style.content.textActiveColor = ColorSource::TextOnPrimary;

        return style;
    }

    /**
     * @brief Sidebar button style with minimal idle state and soft hover fill
     */
    static WidgetStyle sidebarButtonStyle()
    {
        WidgetStyle style = defaultButtonStyle();
        style.name = "SidebarButton";

        // Minimal idle state: text + icon only, no fill or border until hover.
        style.background.enabled = false;
        style.border.enabled = false;

        // Hover: light white fill only, no outline/glow.
        style.hover.enabled = true;
        style.hover.color = ColorSource::Custom;
        style.hover.customColor = QColor(255, 255, 255);
        style.hover.maxOpacity = 0.08;

        style.hoverGlow.enabled = false;

        // Content with icon
        style.content.iconPosition = IconPosition::Left;
        style.content.textAlignment = ContentAlignment::Left;
        style.content.baseIconSize = 18;
        style.content.colorizeIcon = true;
        style.content.basePadding = { 12, 0, 12, 0 };

        // Active state has bottom shadow
        style.activeBackground.bottomShadow = true;
        style.activeBackground.bottomShadowOpacity = 0.10;

        return style;
    }

    /**
     * @brief Choice button style (compact, for settings)
     */
    static WidgetStyle choiceButtonStyle()
    {
        WidgetStyle style = defaultButtonStyle();
        style.name = "ChoiceButton";

        style.metrics.baseCornerRadius = 6;
        style.content.boldWhenActive = true;

        return style;
    }

    /**
     * @brief Card style (legacy list row metrics; prefer PresetCard for new-project presets)
     */
    static WidgetStyle cardStyle()
    {
        WidgetStyle style = defaultButtonStyle();
        style.name = "Card";

        style.metrics.baseHeight = 64;
        style.metrics.baseWidth = 210;
        style.metrics.fixedWidth = true;

        style.content.iconPosition = IconPosition::Left;
        style.content.textAlignment = ContentAlignment::Left;
        style.content.basePadding = { 12, 12, 8, 12 };

        return style;
    }

    /**
     * @brief New-project preset tiles: barely visible panel fill + thin border + light hover;
     *        primary fill when selected.
     */
    static WidgetStyle presetCardStyle()
    {
        WidgetStyle style = cardStyle();
        style.name = "PresetCard";

        style.background.enabled = true;
        style.background.color = ColorSource::OverlayBase;
        style.background.opacity = 0.3;
        // Inherited from defaultButtonStyle: 1px vertical gradient BorderSubtle (+ hover).
        style.border.enabled = true;

        style.hover.color = ColorSource::SurfaceElevated;
        style.hover.maxOpacity = 90.0 / 255.0;

        return style;
    }

    /**
     * @brief Static panel style (non-interactive, like CanvasThumbnail)
     */
    static WidgetStyle panelStyle()
    {
        WidgetStyle style = defaultButtonStyle();
        style.name = "Panel";

        // No active state for panels
        style.activeBackground.enabled = false;
        style.activeBorder.enabled = false;
        style.press.enabled = false;

        return style;
    }

    /**
     * @brief Panel-like control used as a push button (CommandInputWidget, ColorInputButton, etc.)
     */
    static WidgetStyle panelButtonStyle()
    {
        WidgetStyle style = defaultButtonStyle();
        style.name = "PanelButton";
        return style;
    }

    /**
     * @brief Settings panel style (for BaseSettingsWidget)
     */
    static WidgetStyle settingsPanelStyle()
    {
        WidgetStyle style;
        style.name = "SettingsPanel";

        // Metrics - no fixed size, content determines size
        style.metrics.baseHeight = 0;
        style.metrics.fixedHeight = false;
        style.metrics.fixedWidth = false;
        style.metrics.baseCornerRadius = 8;

        // Background: subtle overlay
        style.background.enabled = true;
        style.background.color = ColorSource::OverlayBase;

        // Border: vertical gradient (static, no hover animation)
        style.border.enabled = true;
        style.border.style = BorderStyle::VerticalGradient;
        style.border.topColor = ColorSource::BorderSubtle;
        style.border.bottomColor = ColorSource::BorderSubtleAlpha50;
        style.border.animateOnHover = false; // Static border for panels

        // No hover overlay for settings panels
        style.hover.enabled = false;

        // No glow
        style.hoverGlow.enabled = false;

        // No active states
        style.activeBackground.enabled = false;
        style.activeBorder.enabled = false;
        style.press.enabled = false;

        // Content padding
        style.content.basePadding = { 16, 12, 16, 12 };
        style.content.baseFontSize = 10;

        return style;
    }

    /**
     * @brief Theme preview card style
     */
    static WidgetStyle themePreviewStyle()
    {
        WidgetStyle style = defaultButtonStyle();
        style.name = "ThemePreview";

        style.metrics.baseWidth = 120;
        style.metrics.baseHeight = 95;
        style.metrics.fixedWidth = true;
        style.metrics.fixedHeight = true;
        style.metrics.baseCornerRadius = 6;

        // Active border is thicker for theme preview
        style.activeBorder.width = 2.0;

        return style;
    }

    /**
     * @brief Welcome banner image preview tile (identical size to ThemePreview; no title strip)
     */
    static WidgetStyle welcomeBannerPreviewStyle()
    {
        WidgetStyle style = themePreviewStyle();
        style.name = "WelcomeBannerPreview";
        return style;
    }

    /**
     * @brief Toggle switch style
     */
    static WidgetStyle toggleSwitchStyle()
    {
        WidgetStyle style;
        style.name = "ToggleSwitch";

        // Metrics for the switch track
        style.metrics.baseWidth = 52;
        style.metrics.baseHeight = 28;
        style.metrics.fixedWidth = true;
        style.metrics.fixedHeight = true;
        style.metrics.baseCornerRadius = 14; // Fully rounded

        // Background (inactive track)
        style.background.enabled = true;
        style.background.color = ColorSource::OverlayBase;

        // Border
        style.border.enabled = true;
        style.border.style = BorderStyle::VerticalGradient;
        style.border.topColor = ColorSource::BorderSubtle;
        style.border.bottomColor = ColorSource::BorderSubtleAlpha50;
        style.border.animateOnHover = true;
        style.border.hoverTopColor = ColorSource::BorderSubtleHover;

        // Hover
        style.hover.enabled = true;
        style.hover.color = ColorSource::OverlayHover;

        // No glow
        style.hoverGlow.enabled = false;

        // Active (on state)
        style.activeBackground.enabled = true;
        style.activeBackground.color = ColorSource::Primary;
        style.activeBackground.hoverBrightnessBoost = 1.08;
        style.activeBackground.bottomShadow = false;

        // Active border
        style.activeBorder.enabled = true;
        style.activeBorder.style = BorderStyle::VerticalGradient;
        style.activeBorder.topBrightness = 7.7;
        style.activeBorder.bottomBrightness = 6.2;

        // No press effect (toggle changes state instead)
        style.press.enabled = false;

        // No text content
        style.content.iconPosition = IconPosition::None;

        return style;
    }

    /**
     * @brief Canvas thumbnail style (for dimension preview)
     */
    static WidgetStyle canvasThumbnailStyle()
    {
        WidgetStyle style;
        style.name = "CanvasThumbnail";

        // Size set dynamically, but defaults provided (never fixedWidth: ctor must expand in
        // layout)
        style.metrics.baseWidth = 120;
        style.metrics.baseHeight = 140;
        style.metrics.fixedWidth = false;
        style.metrics.fixedHeight = false; // Min height
        style.metrics.baseCornerRadius = 8;

        // Background
        style.background.enabled = true;
        style.background.color = ColorSource::OverlayBase;

        // Border with hover animation
        style.border.enabled = true;
        style.border.style = BorderStyle::VerticalGradient;
        style.border.topColor = ColorSource::BorderSubtle;
        style.border.bottomColor = ColorSource::BorderSubtleAlpha50;
        style.border.animateOnHover = true;
        style.border.hoverTopColor = ColorSource::BorderSubtleHover;

        // Hover overlay
        style.hover.enabled = true;
        style.hover.color = ColorSource::SurfaceElevated;

        // No active states (it's a display panel)
        style.activeBackground.enabled = false;
        style.activeBorder.enabled = false;
        style.press.enabled = false;
        style.hoverGlow.enabled = false;

        // Content padding
        style.content.basePadding = { 10, 10, 10, 10 };

        return style;
    }
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_STYLE_WIDGETSTYLE_H
