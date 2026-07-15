// SPDX-License-Identifier: MPL-2.0

// WidgetStyleManager.h
#ifndef RUWA_UI_CORE_STYLE_WIDGETSTYLEMANAGER_H
#define RUWA_UI_CORE_STYLE_WIDGETSTYLEMANAGER_H

#include "WidgetStyle.h"
#include "features/theme/manager/ThemeManager.h"

#include <QObject>
#include <QHash>
#include <QEasingCurve>

namespace ruwa::ui::core {

/**
 * @brief Global settings for widget animations and effects
 */
struct GlobalStyleSettings {
    // Animation master switch
    bool animationsEnabled = true;

    // Global animation durations (can be overridden per-style)
    int hoverDuration = 200;
    int activeDuration = 250;
    int glowDuration = 350;

    // Global easing curves
    QEasingCurve hoverEasingIn = QEasingCurve::OutCubic;
    QEasingCurve hoverEasingOut = QEasingCurve::OutCubic;
    QEasingCurve activeEasing = QEasingCurve::InOutCubic;
    QEasingCurve glowEasingIn = QEasingCurve::OutCubic;
    QEasingCurve glowEasingOut = QEasingCurve::InCubic;

    // Effect toggles
    bool hoverEffectsEnabled = true;
    bool glowEffectsEnabled = true;
    bool pressEffectsEnabled = true;

    // Global metric overrides (0 = use style defaults)
    int globalCornerRadiusOverride = 0;
};

/**
 * @brief Singleton manager for widget styles
 *
 * Manages:
 * - Style registry (named styles)
 * - Global animation/effect settings
 * - Color resolution from ThemeColors
 *
 * Usage:
 *   auto& mgr = WidgetStyleManager::instance();
 *   mgr.registerStyle(WidgetStyle::sidebarButtonStyle());
 *   const WidgetStyle* style = mgr.style("SidebarButton");
 */
class WidgetStyleManager : public QObject {
    Q_OBJECT

public:
    static WidgetStyleManager& instance();

    // ========================================================================
    // Style Registry
    // ========================================================================

    /// Register a named style
    void registerStyle(const WidgetStyle& style);

    /// Get style by name (returns nullptr if not found)
    const WidgetStyle* style(const QString& name) const;

    /// Check if style exists
    bool hasStyle(const QString& name) const;

    /// Remove a style
    void removeStyle(const QString& name);

    /// Get all registered style names
    QStringList styleNames() const;

    // ========================================================================
    // Global Settings
    // ========================================================================

    /// Get current global settings
    const GlobalStyleSettings& globalSettings() const { return m_globalSettings; }

    /// Set all global settings at once
    void setGlobalSettings(const GlobalStyleSettings& settings);

    // Individual setting accessors
    bool animationsEnabled() const { return m_globalSettings.animationsEnabled; }
    void setAnimationsEnabled(bool enabled);

    bool hoverEffectsEnabled() const { return m_globalSettings.hoverEffectsEnabled; }
    void setHoverEffectsEnabled(bool enabled);

    bool glowEffectsEnabled() const { return m_globalSettings.glowEffectsEnabled; }
    void setGlowEffectsEnabled(bool enabled);

    int hoverDuration() const { return m_globalSettings.hoverDuration; }
    void setHoverDuration(int ms);

    int activeDuration() const { return m_globalSettings.activeDuration; }
    void setActiveDuration(int ms);

    int globalCornerRadius() const { return m_globalSettings.globalCornerRadiusOverride; }
    void setGlobalCornerRadius(int radius);

    // ========================================================================
    // Resolved Values (considering overrides)
    // ========================================================================

    /// Get effective hover duration for a style
    int effectiveHoverDuration(const WidgetStyle& style) const;

    /// Get effective active duration for a style
    int effectiveActiveDuration(const WidgetStyle& style) const;

    /// Get effective corner radius for a style
    int effectiveCornerRadius(const WidgetStyle& style) const;

    /// Get effective easing curve
    QEasingCurve effectiveHoverEasingIn(const WidgetStyle& style) const;
    QEasingCurve effectiveHoverEasingOut(const WidgetStyle& style) const;
    QEasingCurve effectiveActiveEasing(const WidgetStyle& style) const;

    // ========================================================================
    // Color Resolution
    // ========================================================================

    /// Resolve ColorSource to QColor using current theme
    QColor resolveColor(ColorSource source, const QColor& customColor = Qt::transparent) const;

    /// Interpolate between two ColorSources
    QColor interpolateColors(ColorSource from, ColorSource to, qreal progress,
        const QColor& customFrom = Qt::transparent, const QColor& customTo = Qt::transparent) const;

    // ========================================================================
    // Convenience: ThemeManager access
    // ========================================================================

    const ThemeColors& colors() const { return ThemeManager::instance().colors(); }
    int scaled(int value) const { return ThemeManager::instance().scaled(value); }
    qreal scaled(qreal value) const { return ThemeManager::instance().scaled(value); }
    int scaledFontSize(int baseSize) const
    {
        return ThemeManager::instance().scaledFontSize(baseSize);
    }

signals:
    /// Emitted when global settings change (widgets should update)
    void globalSettingsChanged();

    /// Emitted when a style is registered/updated/removed
    void stylesChanged();

private:
    WidgetStyleManager();
    ~WidgetStyleManager() override = default;

    WidgetStyleManager(const WidgetStyleManager&) = delete;
    WidgetStyleManager& operator=(const WidgetStyleManager&) = delete;

    void registerDefaultStyles();

private:
    QHash<QString, WidgetStyle> m_styles;
    GlobalStyleSettings m_globalSettings;
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_STYLE_WIDGETSTYLEMANAGER_H
