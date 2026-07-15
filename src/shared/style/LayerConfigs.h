// SPDX-License-Identifier: MPL-2.0

// LayerConfigs.h
// Widget paint layer configs (background, border, hover, glow, etc.).
// Not to be confused with canvas layers (LayerModel).
#ifndef RUWA_UI_CORE_STYLE_LAYERCONFIGS_H
#define RUWA_UI_CORE_STYLE_LAYERCONFIGS_H

#include "ColorSource.h"
#include <QEasingCurve>
#include <QMargins>
#include <optional>

namespace ruwa::ui::core {

/**
 * @brief Border rendering style
 */
enum class BorderStyle {
    None,
    Solid,
    VerticalGradient, // Top to bottom gradient (common in Ruwa)
    HorizontalGradient
};

/**
 * @brief Glow/highlight shape for hover effects
 */
enum class GlowShape {
    None,
    RadialFromTop, // Radial gradient from top center
    RadialFromCenter,
    LinearFromTop
};

/**
 * @brief Icon position relative to text
 */
enum class IconPosition {
    None,
    Left,
    Right,
    Top,
    Center // Icon only, no text
};

/**
 * @brief Text alignment within content area
 */
enum class ContentAlignment { Left, Center, Right };

// ============================================================================
// Layer 0: Background
// ============================================================================

struct BackgroundLayerConfig {
    bool enabled = true;
    ColorSource color = ColorSource::OverlayBase;
    QColor customColor = Qt::transparent;
    /// Multiplier for resolved color alpha (1 = full strength from theme).
    qreal opacity = 1.0;
    // Corner radius is taken from WidgetStyle::metrics
};

// ============================================================================
// Layer 1: Border (inactive state)
// ============================================================================

struct BorderLayerConfig {
    bool enabled = true;
    BorderStyle style = BorderStyle::VerticalGradient;

    // For solid border
    ColorSource color = ColorSource::BorderSubtle;
    QColor customColor = Qt::transparent;

    // For gradient border
    ColorSource topColor = ColorSource::BorderSubtle;
    ColorSource bottomColor = ColorSource::BorderSubtleAlpha50;
    QColor customTopColor = Qt::transparent;
    QColor customBottomColor = Qt::transparent;

    qreal width = 1.0;

    // Hover interpolation
    bool animateOnHover = true;
    ColorSource hoverTopColor = ColorSource::BorderSubtleHover;
    ColorSource hoverBottomColor = ColorSource::BorderSubtleAlpha50;
};

// ============================================================================
// Layer 2: Hover overlay
// ============================================================================

struct HoverLayerConfig {
    bool enabled = true;
    ColorSource color = ColorSource::SurfaceElevated;
    QColor customColor = Qt::transparent;
    qreal maxOpacity = 1.0; // Multiplied by hoverProgress
};

// ============================================================================
// Layer 3: Hover glow (radial gradient effect)
// ============================================================================

struct HoverGlowConfig {
    bool enabled = false; // Off by default, SidebarButton enables it
    GlowShape shape = GlowShape::RadialFromTop;
    ColorSource color = ColorSource::Custom;
    QColor customColor = Qt::transparent; // Will use overlay with calculated alpha

    qreal minRadius = 0.2; // Multiplier of widget height
    qreal maxRadius = 2.2; // Multiplier of widget height
    qreal stretchFactor = 1.5; // Horizontal stretch
    qreal maxStretch = 3.0; // Maximum stretch limit
    qreal maxOpacity = 0.08; // Peak opacity at center

    // Animation
    bool animateSize = true;
    int sizeDuration = 350;
    QEasingCurve sizeEasingIn = QEasingCurve::OutCubic;
    QEasingCurve sizeEasingOut = QEasingCurve::InCubic;
};

// ============================================================================
// Layer 4: Active background
// ============================================================================

struct ActiveBackgroundConfig {
    bool enabled = true;
    ColorSource color = ColorSource::Primary;
    QColor customColor = Qt::transparent;

    // Brightness adjustment on hover (1.0 = no change)
    qreal hoverBrightnessBoost = 1.08;

    // Bottom shadow gradient (darkening at bottom)
    bool bottomShadow = true;
    qreal bottomShadowOpacity = 0.10;
    qreal bottomShadowExtent = 0.4; // How far up the shadow extends (0-1)
};

// ============================================================================
// Layer 5: Active border
// ============================================================================

struct ActiveBorderConfig {
    bool enabled = true;
    BorderStyle style = BorderStyle::VerticalGradient;

    // These use brightness adjustment from background color
    qreal topBrightness = 7.7;
    qreal bottomBrightness = 6.2;

    // Or use explicit colors
    bool useExplicitColors = false;
    ColorSource topColor = ColorSource::Custom;
    ColorSource bottomColor = ColorSource::Custom;
    QColor customTopColor = Qt::transparent;
    QColor customBottomColor = Qt::transparent;

    qreal width = 1.0;
};

// ============================================================================
// Layer 6: Press overlay
// ============================================================================

struct PressLayerConfig {
    bool enabled = true;
    ColorSource color = ColorSource::Shadow20;
    QColor customColor = Qt::transparent;

    // Different shadow for active state
    ColorSource activeColor = ColorSource::Shadow25;
};

// ============================================================================
// Layer 7: Content (text, icon)
// ============================================================================

struct ContentLayerConfig {
    // Icon settings
    IconPosition iconPosition = IconPosition::Left;
    int baseIconSize = 18;
    bool colorizeIcon = true; // Tint icon to match text color

    // Text settings
    ContentAlignment textAlignment = ContentAlignment::Left;
    int baseFontSize = 9;
    bool boldWhenActive = false;

    // Spacing
    int baseIconTextGap = 6;
    QMargins basePadding = { 12, 8, 12, 8 };

    // Colors (interpolated based on hover/active progress)
    ColorSource textColor = ColorSource::TextMuted;
    ColorSource textHoverColor = ColorSource::Text;
    ColorSource textActiveColor = ColorSource::TextOnPrimary;

    // For active state secondary text (like dimensions in ProjectPresetCard)
    ColorSource secondaryTextColor = ColorSource::TextMuted;
    ColorSource secondaryTextActiveColor = ColorSource::Custom;
    qreal secondaryActiveBrightness = 2.5; // Brightness from background
};

// ============================================================================
// Animation settings (can override global)
// ============================================================================

struct AnimationConfig {
    // Hover animation
    std::optional<int> hoverDuration; // nullopt = use global
    std::optional<QEasingCurve> hoverEasingIn;
    std::optional<QEasingCurve> hoverEasingOut;

    // Active state animation
    std::optional<int> activeDuration;
    std::optional<QEasingCurve> activeEasing;

    // Press (instant by default)
    bool animatePress = false;
    int pressDuration = 50;
};

// ============================================================================
// Widget metrics
// ============================================================================

struct WidgetMetrics {
    int baseHeight = 36;
    int baseWidth = 0; // 0 = auto/expanding
    int baseCornerRadius = 8;
    int baseMinWidth = 0;
    int baseMaxWidth = 0; // 0 = no limit

    // Content area (actual padding comes from ContentLayerConfig)
    bool fixedHeight = true;
    bool fixedWidth = false;
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_STYLE_LAYERCONFIGS_H
