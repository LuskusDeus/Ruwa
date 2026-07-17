// SPDX-License-Identifier: MPL-2.0

// SettingsManager.h
#ifndef RUWA_CORE_SETTINGS_SETTINGSMANAGER_H
#define RUWA_CORE_SETTINGS_SETTINGSMANAGER_H

#include "AppSettings.h"
#include <QAtomicInteger>
#include <QObject>
#include <QSettings>
#include <QStringList>

namespace ruwa::core {

/**
 * @brief Manager for application settings persistence
 *
 * Features:
 * - Load/Save settings using QSettings
 * - Signal on settings change
 * - Singleton pattern for global access
 * - Type-safe settings access
 */
class SettingsManager : public QObject {
    Q_OBJECT

public:
    /// Get singleton instance
    static SettingsManager& instance();

    /// Get current settings (auto-loads if not loaded yet)
    const AppSettings& settings();

    /// Check if settings have been loaded
    bool isLoaded() const { return m_loaded; }

    /// Load settings from disk
    void load();

    /// Save settings to disk (blocking)
    void save();

    /// Save settings to disk on a worker thread; coalesces rapid calls via serial token.
    /// Safe to call from UI thread without blocking.
    void saveAsync();

    /// Reset to defaults
    void resetToDefaults();

    // === APPEARANCE SETTERS ===
    void setThemeId(const QUuid& id);
    void setUiScale(int scale);
    void setLanguage(const QString& code);
    /// 0=left, 1=center (invalid values ignored)
    void setTopBarTabAlignment(int alignment);
    void setWelcomeBannerRandomize(bool randomize);
    void setWelcomeBannerFixedKey(const QString& key);
    /// User picked a thumbnail: show this image (turns off random). Single notify.
    void setWelcomeBannerFixedKeyDisablingRandomize(const QString& key);
    void setWelcomeBannerCustomPaths(const QStringList& paths);
    /// Updates paths + fixed key with a single welcome-banner signal (avoids double reload /
    /// reentrancy).
    void setWelcomeBannerCustomPathsAndFixedKey(const QStringList& paths, const QString& fixedKey);
    /// Store the normalized ([0,1]) crop region for a custom banner image. An invalid or
    /// full rect clears the entry. Does not emit on its own — pair with the path setter.
    void setWelcomeBannerCustomCrop(const QString& path, const QRectF& normRect);
    /// Like setWelcomeBannerCustomCrop but emits welcomeBannerBackgroundSettingsChanged so the
    /// banner reloads (use when changing the crop on its own, e.g. the "Edit crop" action).
    void setWelcomeBannerCustomCropNotifying(const QString& path, const QRectF& normRect);
    /// Normalized crop for a custom image, or the full rect (0,0,1,1) when none is stored.
    QRectF welcomeBannerCropFor(const QString& path) const;
    /// 0=Basic (auto), 1=Inverted; invalid values ignored
    void setWelcomeBannerTextColorMode(int mode);
    /// Random mode: persist the image actually shown as welcomeBannerFixedKey (settings preview +
    /// Fixed fallback). Does not emit welcomeBannerBackgroundSettingsChanged (avoids re-rolling the
    /// banner).
    void syncWelcomeBannerDisplayedImageKey(const QString& key);
    int brushDisplayColorIndex(const QString& brushId);
    void setBrushDisplayColorIndex(const QString& brushId, int colorIndex);

    // === EDITOR SETTERS ===
    void setAutoSaveInterval(int minutes); // 0=Off, 2=2min, 5=5min, 10=10min
    void setQuickshapesEnabled(bool enabled);

    // === PERFORMANCE SETTERS ===
    void setUndoMemoryLimitMb(int megabytes); // 300, 1024, 3072, 8192
    void setTabletBackend(int backend); // 0=WinTab (Qt), 1=Windows Ink, 2=WinTab (Ruwa)

    // === WINDOW STATE SETTERS ===
    void setMainWindowSize(const QSize& size);
    void setIsMaximized(bool maximized);
    void setLastActiveTabIndex(int index);

signals:
    /// Emitted when any setting changes
    void settingsChanged();

    /// Specific change signals
    void themeChanged(const QUuid& themeId);
    void uiScaleChanged(int scale);
    void topBarTabAlignmentChanged(int alignment);
    void autoSaveIntervalChanged(int minutes);
    void undoMemoryLimitChanged(int megabytes);
    void tabletBackendChanged(int backend);
    void welcomeBannerBackgroundSettingsChanged();
    void welcomeBannerDisplayedImageKeyChanged(const QString& key);
    void brushDisplayColorChanged(const QString& brushId, int colorIndex);

private:
    SettingsManager();
    ~SettingsManager() override = default;

    // Prevent copying
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    void loadAppearance(QSettings& settings);
    void loadEditor(QSettings& settings);
    void loadOnboarding(QSettings& settings);
    void loadPerformance(QSettings& settings);
    void loadWindowState(QSettings& settings);

    void saveAppearance(QSettings& settings);
    void saveEditor(QSettings& settings);
    void saveOnboarding(QSettings& settings);
    void savePerformance(QSettings& settings);
    void saveWindowState(QSettings& settings);
    void saveBrushDisplayColorsAsync();

public:
    bool isFirstRunIntegrationCompleted() const;
    void setFirstRunIntegrationCompleted(bool completed);

signals:
    void firstRunIntegrationCompletedChanged(bool completed);

private:
    AppSettings m_settings;
    QAtomicInteger<quint64> m_brushDisplayColorSaveSerial { 0 };
    QAtomicInteger<quint64> m_saveSerial { 0 };
    bool m_loaded { false };
};

} // namespace ruwa::core

#endif // RUWA_CORE_SETTINGS_SETTINGSMANAGER_H
