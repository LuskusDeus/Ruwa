// SPDX-License-Identifier: MPL-2.0

// ThemeManager.h
#ifndef RUWA_UI_CORE_THEME_THEMEMANAGER_H
#define RUWA_UI_CORE_THEME_THEMEMANAGER_H

#include "ThemeColors.h"
#include "ThemePreset.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <QUuid>
#include <QSize>
#include <QMargins>
#include <QPointer>

#include <functional>

class QTimer;
class QWidget;
class QPalette;

namespace ruwa::ui::core {

// Forward declarations
class ResourceManager;
class IconProvider;
class FontManager;

/**
 * @brief Singleton manager for application theming
 *
 * Manages theme presets, generates QSS, and applies themes.
 * Supports built-in and custom themes.
 *
 * Usage:
 *   ThemeManager::instance().initialize();
 *   ThemeManager::instance().applyPreset(presetId);
 */
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    /// Initialize theme system (must be called before use)
    void initialize();

    // === Color Access ===

    /// Get current active color palette. During a synchronous preview render,
    /// returns that render's local palette without changing the application.
    const ThemeColors& colors() const { return s_colorOverride ? *s_colorOverride : m_colors; }

    /// Build the runtime color set used by widgets from an editable preset.
    static ThemeColors colorsForPreset(const ThemePreset& preset);

    /// Build the QPalette counterpart of a runtime color set.
    static QPalette paletteForColors(const ThemeColors& colors, QPalette basePalette);

    /**
     * @brief Run a synchronous widget render against local theme colors.
     *
     * Custom-painted widgets often read ThemeManager::colors() directly. This
     * scope lets a passive preview render those real widgets with an edited
     * theme without applying it to the application or emitting theme signals.
     */
    void withColorOverride(const ThemeColors& colors, const std::function<void()>& render);

    // === Preset Management ===

    /// Get all available presets (built-in + custom)
    QVector<ThemePreset> allPresets() const { return m_presets; }

    /// Get built-in presets only
    QVector<ThemePreset> builtInPresets() const;

    /// Get custom presets only
    QVector<ThemePreset> customPresets() const;

    /// Get current preset
    const ThemePreset* currentPreset() const;

    /// Get current preset ID
    QUuid currentPresetId() const { return m_currentPresetId; }

    /// Apply a preset by ID
    bool applyPreset(const QUuid& id);

    /// Apply a preset directly
    bool applyPreset(const ThemePreset& preset);

    /// Apply current theme (re-apply stylesheet) - for backwards compatibility
    void applyTheme();

    // === UI Scale ===

    /// Scale factor indices: 0=75%, 1=100%, 2=125%, 3=175%, 4=225%
    enum class ScalePreset { Percent75 = 0, Percent100, Percent125, Percent175, Percent225 };

    /// Get current scale factor (0.75, 1.0, 1.25, 1.75, or 2.25)
    qreal scale() const { return m_scaleFactor; }

    /// Get current scale index (0 through 4)
    int scaleIndex() const { return m_scaleIndex; }

    /// Set scale by index (0=75%, 1=100%, 2=125%, 3=175%, 4=225%)
    void setScaleIndex(int index);

    /// Scale an integer value
    int scaled(int value) const { return qRound(value * m_scaleFactor); }

    /// Scale a qreal value
    qreal scaled(qreal value) const { return value * m_scaleFactor; }

    /// Scale a QSize
    QSize scaled(const QSize& size) const
    {
        return QSize(scaled(size.width()), scaled(size.height()));
    }

    /// Scale a QMargins
    QMargins scaled(const QMargins& margins) const
    {
        return QMargins(scaled(margins.left()), scaled(margins.top()), scaled(margins.right()),
            scaled(margins.bottom()));
    }

    /// Get scaled font size (base size will be multiplied by scale)
    int scaledFontSize(int baseSize) const { return qMax(6, scaled(baseSize)); }

    /// Resolve a semantic theme font size with the current UI scale.
    int fontSize(ThemeFontRole role) const
    {
        return scaledFontSize(colors().fonts.sizes.value(role));
    }

    /// Build a font for a semantic UI role. Weight remains a widget concern.
    QFont font(ThemeFontRole role, QFont::Weight weight = QFont::Normal) const
    {
        QFont resolved = colors().fonts.getFont(role, fontSize(role));
        resolved.setWeight(weight);
        return resolved;
    }

    /// Build a font using a semantic size and an explicitly selected family.
    QFont font(ThemeFontRole sizeRole, ThemeFontFamilyRole familyRole,
        QFont::Weight weight = QFont::Normal) const
    {
        QFont resolved = colors().fonts.getFont(sizeRole, familyRole, fontSize(sizeRole));
        resolved.setWeight(weight);
        return resolved;
    }

    // === Custom Theme Management ===

    /// Add a custom theme preset
    void addCustomPreset(const ThemePreset& preset);

    /// Update an existing custom preset
    void updateCustomPreset(const ThemePreset& preset);

    /// Remove a custom preset
    void removeCustomPreset(const QUuid& id);

    /// Update favorite state for any preset (built-in or custom)
    void updatePresetFavorite(const QUuid& id, bool isFavorite);

    // === Resource Access ===

    ResourceManager& resources();
    const ResourceManager& resources() const;

    IconProvider& icons();
    const IconProvider& icons() const;

    FontManager& fonts();
    const FontManager& fonts() const;

    // === Per-widget theme handlers (visibility-gated) ===
    //
    // Heavy theme handlers (panel content that rebuilds widgets / re-applies
    // stylesheets) should register here instead of connecting directly to
    // themeChanged. On a theme change the handler runs immediately only if the
    // widget is currently visible; for a widget in a background (hidden) tab the
    // call is DEFERRED and re-run later via flushThemeHandlers(root) — which the
    // owning WorkspaceTab calls from its deferred theme refresh, behind the
    // loading overlay. This keeps a theme change from doing the expensive
    // per-panel work for every open project at once.
    void registerThemeHandler(QWidget* widget, std::function<void()> handler);

    /// Re-run any handlers under @p root that were deferred while hidden.
    void flushThemeHandlers(QWidget* root);

signals:
    /// Emitted when a full theme apply has been requested.
    void themeApplyStarted();

    /// Emitted when theme colors change (lightweight, before full themeChanged)
    void colorsChanged();

    /// Emitted when theme fully applied (colors + icons + fonts)
    void themeChanged();

    /// Emitted when preset list changes
    void presetsChanged();

private:
    ThemeManager();
    ~ThemeManager() override;

    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void loadBuiltInPresets();
    void loadCustomPresets();
    void saveCustomPresets();
    void applyColorsFromPreset(const ThemePreset& preset);
    void generateStyleSheet();

    /// Apply QPalette for base widget colors (fast, native)
    void applyPalette();

    /// Apply scoped QSS for specific widget types
    void applyStyleSheet();

    /// Execute the pending coalesced theme apply
    void executePendingApply();

    /// Dispatch registered per-widget handlers (visible -> run, hidden -> defer).
    void dispatchThemeHandlers();

private:
    static thread_local const ThemeColors* s_colorOverride;

    ThemeColors m_colors; ///< Current active colors
    QString m_styleSheet; ///< Generated QSS
    QVector<ThemePreset> m_presets; ///< All available presets
    QUuid m_currentPresetId; ///< Currently active preset
    bool m_initialized { false };

    // UI Scale
    qreal m_scaleFactor { 1.0 }; ///< Current scale multiplier
    int m_scaleIndex { 1 }; ///< Current scale index (0 through 4)

    // Coalescing — multiple rapid applies collapse into one
    QTimer* m_coalesceTimer { nullptr };
    ThemePreset m_pendingPreset; ///< Preset waiting to be applied
    bool m_hasPendingApply { false };

    // Visibility-gated per-widget theme handlers
    struct ThemeHandlerEntry {
        QPointer<QWidget> widget;
        std::function<void()> handler;
        bool dirty = false; ///< deferred while hidden, awaiting flush
    };
    QVector<ThemeHandlerEntry> m_themeHandlers;
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_THEME_THEMEMANAGER_H
