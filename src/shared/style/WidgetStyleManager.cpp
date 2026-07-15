// SPDX-License-Identifier: MPL-2.0

// WidgetStyleManager.cpp
#include "WidgetStyleManager.h"
namespace ruwa::ui::core {

WidgetStyleManager::WidgetStyleManager()
    : QObject(nullptr)
{
    registerDefaultStyles();
}

WidgetStyleManager& WidgetStyleManager::instance()
{
    static WidgetStyleManager instance;
    return instance;
}

void WidgetStyleManager::registerDefaultStyles()
{
    // Register built-in styles
    registerStyle(WidgetStyle::defaultButtonStyle());
    registerStyle(WidgetStyle::sidebarButtonStyle());
    registerStyle(WidgetStyle::choiceButtonStyle());
    registerStyle(WidgetStyle::cardStyle());
    registerStyle(WidgetStyle::presetCardStyle());
    registerStyle(WidgetStyle::panelStyle());
    registerStyle(WidgetStyle::panelButtonStyle());
    registerStyle(WidgetStyle::settingsPanelStyle());
    registerStyle(WidgetStyle::themePreviewStyle());
    registerStyle(WidgetStyle::welcomeBannerPreviewStyle());
    registerStyle(WidgetStyle::toggleSwitchStyle());
    registerStyle(WidgetStyle::canvasThumbnailStyle());
}

// ============================================================================
// Style Registry
// ============================================================================

void WidgetStyleManager::registerStyle(const WidgetStyle& style)
{
    if (style.name.isEmpty()) {
        return;
    }

    bool isUpdate = m_styles.contains(style.name);
    m_styles[style.name] = style;

    emit stylesChanged();
}

const WidgetStyle* WidgetStyleManager::style(const QString& name) const
{
    auto it = m_styles.constFind(name);
    return (it != m_styles.constEnd()) ? &(*it) : nullptr;
}

bool WidgetStyleManager::hasStyle(const QString& name) const
{
    return m_styles.contains(name);
}

void WidgetStyleManager::removeStyle(const QString& name)
{
    if (m_styles.remove(name)) {
        emit stylesChanged();
    }
}

QStringList WidgetStyleManager::styleNames() const
{
    return m_styles.keys();
}

// ============================================================================
// Global Settings
// ============================================================================

void WidgetStyleManager::setGlobalSettings(const GlobalStyleSettings& settings)
{
    m_globalSettings = settings;
    emit globalSettingsChanged();
}

void WidgetStyleManager::setAnimationsEnabled(bool enabled)
{
    if (m_globalSettings.animationsEnabled != enabled) {
        m_globalSettings.animationsEnabled = enabled;
        emit globalSettingsChanged();
    }
}

void WidgetStyleManager::setHoverEffectsEnabled(bool enabled)
{
    if (m_globalSettings.hoverEffectsEnabled != enabled) {
        m_globalSettings.hoverEffectsEnabled = enabled;
        emit globalSettingsChanged();
    }
}

void WidgetStyleManager::setGlowEffectsEnabled(bool enabled)
{
    if (m_globalSettings.glowEffectsEnabled != enabled) {
        m_globalSettings.glowEffectsEnabled = enabled;
        emit globalSettingsChanged();
    }
}

void WidgetStyleManager::setHoverDuration(int ms)
{
    ms = qBound(0, ms, 2000);
    if (m_globalSettings.hoverDuration != ms) {
        m_globalSettings.hoverDuration = ms;
        emit globalSettingsChanged();
    }
}

void WidgetStyleManager::setActiveDuration(int ms)
{
    ms = qBound(0, ms, 2000);
    if (m_globalSettings.activeDuration != ms) {
        m_globalSettings.activeDuration = ms;
        emit globalSettingsChanged();
    }
}

void WidgetStyleManager::setGlobalCornerRadius(int radius)
{
    radius = qBound(0, radius, 32);
    if (m_globalSettings.globalCornerRadiusOverride != radius) {
        m_globalSettings.globalCornerRadiusOverride = radius;
        emit globalSettingsChanged();
    }
}

// ============================================================================
// Resolved Values
// ============================================================================

int WidgetStyleManager::effectiveHoverDuration(const WidgetStyle& style) const
{
    if (!m_globalSettings.animationsEnabled) {
        return 0;
    }
    return style.animations.hoverDuration.value_or(m_globalSettings.hoverDuration);
}

int WidgetStyleManager::effectiveActiveDuration(const WidgetStyle& style) const
{
    if (!m_globalSettings.animationsEnabled) {
        return 0;
    }
    return style.animations.activeDuration.value_or(m_globalSettings.activeDuration);
}

int WidgetStyleManager::effectiveCornerRadius(const WidgetStyle& style) const
{
    if (m_globalSettings.globalCornerRadiusOverride > 0) {
        return m_globalSettings.globalCornerRadiusOverride;
    }
    return style.metrics.baseCornerRadius;
}

QEasingCurve WidgetStyleManager::effectiveHoverEasingIn(const WidgetStyle& style) const
{
    return style.animations.hoverEasingIn.value_or(m_globalSettings.hoverEasingIn);
}

QEasingCurve WidgetStyleManager::effectiveHoverEasingOut(const WidgetStyle& style) const
{
    return style.animations.hoverEasingOut.value_or(m_globalSettings.hoverEasingOut);
}

QEasingCurve WidgetStyleManager::effectiveActiveEasing(const WidgetStyle& style) const
{
    return style.animations.activeEasing.value_or(m_globalSettings.activeEasing);
}

// ============================================================================
// Color Resolution
// ============================================================================

QColor WidgetStyleManager::resolveColor(ColorSource source, const QColor& customColor) const
{
    return ruwa::ui::core::resolveColor(source, colors(), customColor);
}

QColor WidgetStyleManager::interpolateColors(ColorSource from, ColorSource to, qreal progress,
    const QColor& customFrom, const QColor& customTo) const
{
    QColor colorFrom = resolveColor(from, customFrom);
    QColor colorTo = resolveColor(to, customTo);
    return ThemeColors::interpolate(colorFrom, colorTo, progress);
}

} // namespace ruwa::ui::core
