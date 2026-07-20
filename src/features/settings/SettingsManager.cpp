// SPDX-License-Identifier: MPL-2.0

// SettingsManager.cpp
#include "SettingsManager.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QVariantMap>
#include <QtConcurrent>

namespace ruwa::core {

namespace {
constexpr bool kPersistOnboardingState = true;
constexpr auto kFirstRunIntegrationCompletedKey = "firstRunIntegrationCompleted";
constexpr auto kLegacyInitialSetupCompletedKey = "initialSetupCompleted";
constexpr auto kBrushDisplayColorsGroup = "BrushDisplayColors";
constexpr auto kFavoriteBrushIdsKey = "favoriteBrushIds";
constexpr int kMinBrushDisplayColorIndex = 0;
constexpr int kMaxBrushDisplayColorIndex = 8;

bool welcomeBannerStoredKeysMatch(const QString& a, const QString& b)
{
    if (a == b) {
        return true;
    }
    if (a.startsWith(QLatin1String(":/")) || b.startsWith(QLatin1String(":/"))) {
        return false;
    }
    const QFileInfo fa(a);
    const QFileInfo fb(b);
    const QString ca = fa.canonicalFilePath();
    const QString cb = fb.canonicalFilePath();
    if (!ca.isEmpty() && !cb.isEmpty()) {
        return ca == cb;
    }
    return fa.absoluteFilePath() == fb.absoluteFilePath();
}

bool isFullCropRect(const QRectF& r)
{
    return !r.isValid()
        || (qFuzzyIsNull(r.x()) && qFuzzyIsNull(r.y()) && qFuzzyCompare(r.width(), 1.0)
            && qFuzzyCompare(r.height(), 1.0));
}

/// Serialize crops as a QVariantMap (path -> "x y w h"), skipping full-image entries.
QVariantMap serializeWelcomeBannerCrops(const QHash<QString, QRectF>& crops)
{
    QVariantMap map;
    for (auto it = crops.cbegin(); it != crops.cend(); ++it) {
        if (it.key().isEmpty() || isFullCropRect(it.value())) {
            continue;
        }
        const QRectF& r = it.value();
        map.insert(it.key(),
            QStringLiteral("%1 %2 %3 %4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
    }
    return map;
}

QHash<QString, QRectF> deserializeWelcomeBannerCrops(const QVariantMap& map)
{
    QHash<QString, QRectF> crops;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const QStringList parts = it.value().toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 4) {
            continue;
        }
        bool ok[4] = { false, false, false, false };
        const QRectF r(parts[0].toDouble(&ok[0]), parts[1].toDouble(&ok[1]),
            parts[2].toDouble(&ok[2]), parts[3].toDouble(&ok[3]));
        if (ok[0] && ok[1] && ok[2] && ok[3] && r.isValid() && !isFullCropRect(r)) {
            crops.insert(it.key(), r);
        }
    }
    return crops;
}
} // namespace

SettingsManager& SettingsManager::instance()
{
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
    : m_settings(AppSettings::defaults())
{
    // NOTE: Do NOT call load() here!
    // load() must be called AFTER QCoreApplication::setOrganizationName() in main.cpp
    // Otherwise QSettings will use wrong file path
}

void SettingsManager::load()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    loadAppearance(settings);
    loadEditor(settings);
    loadOnboarding(settings);
    loadPerformance(settings);
    loadWindowState(settings);

    m_loaded = true;
}

const AppSettings& SettingsManager::settings()
{
    if (!m_loaded) {
        load();
    }
    return m_settings;
}

void SettingsManager::save()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    saveAppearance(settings);
    saveEditor(settings);
    saveOnboarding(settings);
    savePerformance(settings);
    saveWindowState(settings);

    settings.sync();
}

void SettingsManager::saveAsync()
{
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    const AppSettings snapshot = m_settings;
    const quint64 serial = m_saveSerial.fetchAndAddRelaxed(1) + 1;

    (void) QtConcurrent::run([organization, application, snapshot, serial]() {
        if (serial != SettingsManager::instance().m_saveSerial.loadAcquire()) {
            return; // Coalesced — a newer save is queued
        }

        QSettings settings(organization, application);

        // Appearance
        settings.beginGroup("Appearance");
        settings.setValue("themeId", snapshot.appearance.themeId.toString());
        settings.setValue("uiScale", snapshot.appearance.uiScale);
        settings.setValue("language", snapshot.appearance.language);
        settings.setValue("topBarTabAlignment", snapshot.appearance.topBarTabAlignment);
        settings.setValue("welcomeBannerRandomize", snapshot.appearance.welcomeBannerRandomize);
        settings.setValue("welcomeBannerFixedKey", snapshot.appearance.welcomeBannerFixedKey);
        settings.setValue("welcomeBannerCustomPaths", snapshot.appearance.welcomeBannerCustomPaths);
        settings.setValue("welcomeBannerCustomCrops",
            serializeWelcomeBannerCrops(snapshot.appearance.welcomeBannerCustomCrops));
        settings.setValue(
            "welcomeBannerTextColorMode", snapshot.appearance.welcomeBannerTextColorMode);
        settings.remove(QLatin1String(kBrushDisplayColorsGroup));
        settings.beginGroup(QLatin1String(kBrushDisplayColorsGroup));
        for (auto it = snapshot.appearance.brushDisplayColorIndices.cbegin();
            it != snapshot.appearance.brushDisplayColorIndices.cend(); ++it) {
            if (!it.key().isEmpty() && it.value() > kMinBrushDisplayColorIndex
                && it.value() <= kMaxBrushDisplayColorIndex) {
                settings.setValue(it.key(), it.value());
            }
        }
        settings.endGroup();
        QStringList favoriteBrushIds = snapshot.appearance.favoriteBrushIds.values();
        favoriteBrushIds.sort();
        settings.setValue(QLatin1String(kFavoriteBrushIdsKey), favoriteBrushIds);
        settings.endGroup();

        // Editor
        settings.beginGroup("Editor");
        settings.setValue("autoSaveInterval", snapshot.editor.autoSaveInterval);
        settings.setValue("quickshapesEnabled", snapshot.editor.quickshapesEnabled);
        settings.endGroup();

        // Onboarding
        if (kPersistOnboardingState) {
            settings.beginGroup("Onboarding");
            settings.setValue(
                kFirstRunIntegrationCompletedKey, snapshot.onboarding.firstRunIntegrationCompleted);
            settings.endGroup();
        }

        // Performance
        settings.beginGroup("Performance");
        settings.setValue("undoMemoryLimitMb", snapshot.performance.undoMemoryLimitMb);
        settings.setValue("tabletBackend", snapshot.performance.tabletBackend);
        settings.endGroup();

        // Window state
        settings.beginGroup("WindowState");
        settings.setValue("mainWindowSize", snapshot.windowState.mainWindowSize);
        settings.setValue("isMaximized", snapshot.windowState.isMaximized);
        settings.setValue("lastActiveTabIndex", snapshot.windowState.lastActiveTabIndex);
        settings.endGroup();

        settings.sync();
    });
}

void SettingsManager::resetToDefaults()
{
    const bool onboardingChanged = m_settings.onboarding.firstRunIntegrationCompleted
        != AppSettings::defaults().onboarding.firstRunIntegrationCompleted;
    const QSet<QString> previousFavoriteBrushIds = m_settings.appearance.favoriteBrushIds;
    m_settings = AppSettings::defaults();
    save();
    saveBrushFavoritesAsync();
    if (onboardingChanged) {
        emit firstRunIntegrationCompletedChanged(
            m_settings.onboarding.firstRunIntegrationCompleted);
    }
    for (const QString& brushId : previousFavoriteBrushIds) {
        emit brushFavoriteChanged(brushId, false);
    }
    emit welcomeBannerBackgroundSettingsChanged();
    emit settingsChanged();
}

// === APPEARANCE ===

void SettingsManager::loadAppearance(QSettings& settings)
{
    settings.beginGroup("Appearance");

    QString themeIdStr = settings.value("themeId", "").toString();
    if (!themeIdStr.isEmpty()) {
        m_settings.appearance.themeId = QUuid::fromString(themeIdStr);
    }

    m_settings.appearance.uiScale = settings.value("uiScale", 1).toInt();
    m_settings.appearance.language = settings.value("language", "en").toString();
    const int tabAlign = settings.value("topBarTabAlignment", 0).toInt();
    m_settings.appearance.topBarTabAlignment = (tabAlign == 1) ? 1 : 0;

    m_settings.appearance.welcomeBannerRandomize
        = settings.value("welcomeBannerRandomize", true).toBool();
    const QString fixedKey = settings.value("welcomeBannerFixedKey", QString()).toString();
    if (!fixedKey.isEmpty()) {
        m_settings.appearance.welcomeBannerFixedKey = fixedKey;
    }
    m_settings.appearance.welcomeBannerCustomPaths
        = settings.value("welcomeBannerCustomPaths").toStringList();

    // Crops, pruned to paths still present (orphans accumulate otherwise when a custom image is
    // removed).
    QHash<QString, QRectF> loadedCrops
        = deserializeWelcomeBannerCrops(settings.value("welcomeBannerCustomCrops").toMap());
    m_settings.appearance.welcomeBannerCustomCrops.clear();
    for (const QString& path : m_settings.appearance.welcomeBannerCustomPaths) {
        auto it = loadedCrops.constFind(path);
        if (it != loadedCrops.constEnd()) {
            m_settings.appearance.welcomeBannerCustomCrops.insert(path, it.value());
        }
    }

    const int textMode = settings.value("welcomeBannerTextColorMode", 0).toInt();
    m_settings.appearance.welcomeBannerTextColorMode = (textMode == 1) ? 1 : 0;

    m_settings.appearance.brushDisplayColorIndices.clear();
    settings.beginGroup(QLatin1String(kBrushDisplayColorsGroup));
    const QStringList brushColorKeys = settings.childKeys();
    for (const QString& brushId : brushColorKeys) {
        const int colorIndex = settings.value(brushId, 0).toInt();
        if (colorIndex > kMinBrushDisplayColorIndex && colorIndex <= kMaxBrushDisplayColorIndex) {
            m_settings.appearance.brushDisplayColorIndices.insert(brushId, colorIndex);
        }
    }
    settings.endGroup();

    m_settings.appearance.favoriteBrushIds.clear();
    const QStringList favoriteBrushIds
        = settings.value(QLatin1String(kFavoriteBrushIdsKey)).toStringList();
    for (const QString& brushId : favoriteBrushIds) {
        if (!brushId.isEmpty()) {
            m_settings.appearance.favoriteBrushIds.insert(brushId);
        }
    }

    settings.endGroup();
}

void SettingsManager::saveAppearance(QSettings& settings)
{
    settings.beginGroup("Appearance");

    settings.setValue("themeId", m_settings.appearance.themeId.toString());
    settings.setValue("uiScale", m_settings.appearance.uiScale);
    settings.setValue("language", m_settings.appearance.language);
    settings.setValue("topBarTabAlignment", m_settings.appearance.topBarTabAlignment);
    settings.setValue("welcomeBannerRandomize", m_settings.appearance.welcomeBannerRandomize);
    settings.setValue("welcomeBannerFixedKey", m_settings.appearance.welcomeBannerFixedKey);
    settings.setValue("welcomeBannerCustomPaths", m_settings.appearance.welcomeBannerCustomPaths);
    settings.setValue("welcomeBannerCustomCrops",
        serializeWelcomeBannerCrops(m_settings.appearance.welcomeBannerCustomCrops));
    settings.setValue(
        "welcomeBannerTextColorMode", m_settings.appearance.welcomeBannerTextColorMode);

    settings.remove(QLatin1String(kBrushDisplayColorsGroup));
    settings.beginGroup(QLatin1String(kBrushDisplayColorsGroup));
    for (auto it = m_settings.appearance.brushDisplayColorIndices.cbegin();
        it != m_settings.appearance.brushDisplayColorIndices.cend(); ++it) {
        if (!it.key().isEmpty() && it.value() > kMinBrushDisplayColorIndex
            && it.value() <= kMaxBrushDisplayColorIndex) {
            settings.setValue(it.key(), it.value());
        }
    }
    settings.endGroup();

    // Favorite brushes are intentionally excluded from this blocking save path.
    // Their dedicated writer and saveAsync() always call QSettings::sync() on a worker thread.
    settings.endGroup();
}

void SettingsManager::setThemeId(const QUuid& id)
{
    if (m_settings.appearance.themeId != id) {
        m_settings.appearance.themeId = id;
        emit themeChanged(id);
        emit settingsChanged();
    }
}

void SettingsManager::setUiScale(int scale)
{
    if (m_settings.appearance.uiScale != scale) {
        m_settings.appearance.uiScale = scale;
        emit uiScaleChanged(scale);
        emit settingsChanged();
    }
}

void SettingsManager::setLanguage(const QString& code)
{
    if (m_settings.appearance.language != code) {
        m_settings.appearance.language = code;
        emit settingsChanged();
    }
}

void SettingsManager::setTopBarTabAlignment(int alignment)
{
    if (alignment != 0 && alignment != 1) {
        return;
    }
    if (m_settings.appearance.topBarTabAlignment != alignment) {
        m_settings.appearance.topBarTabAlignment = alignment;
        emit topBarTabAlignmentChanged(alignment);
        emit settingsChanged();
    }
}

void SettingsManager::setWelcomeBannerRandomize(bool randomize)
{
    if (m_settings.appearance.welcomeBannerRandomize != randomize) {
        m_settings.appearance.welcomeBannerRandomize = randomize;
        emit welcomeBannerBackgroundSettingsChanged();
        emit settingsChanged();
    }
}

void SettingsManager::setWelcomeBannerFixedKey(const QString& key)
{
    if (m_settings.appearance.welcomeBannerFixedKey != key) {
        m_settings.appearance.welcomeBannerFixedKey = key;
        emit welcomeBannerBackgroundSettingsChanged();
        emit settingsChanged();
    }
}

void SettingsManager::setWelcomeBannerFixedKeyDisablingRandomize(const QString& key)
{
    const bool wasRandom = m_settings.appearance.welcomeBannerRandomize;
    const bool keyChanged = (m_settings.appearance.welcomeBannerFixedKey != key);
    if (!wasRandom && !keyChanged) {
        return;
    }
    m_settings.appearance.welcomeBannerRandomize = false;
    m_settings.appearance.welcomeBannerFixedKey = key;
    emit welcomeBannerBackgroundSettingsChanged();
    emit settingsChanged();
}

void SettingsManager::setWelcomeBannerCustomPaths(const QStringList& paths)
{
    if (m_settings.appearance.welcomeBannerCustomPaths != paths) {
        m_settings.appearance.welcomeBannerCustomPaths = paths;
        emit welcomeBannerBackgroundSettingsChanged();
        emit settingsChanged();
    }
}

void SettingsManager::setWelcomeBannerCustomPathsAndFixedKey(
    const QStringList& paths, const QString& fixedKey)
{
    const bool pathsChanged = m_settings.appearance.welcomeBannerCustomPaths != paths;
    const bool keyChanged = m_settings.appearance.welcomeBannerFixedKey != fixedKey;
    if (!pathsChanged && !keyChanged) {
        return;
    }
    if (pathsChanged) {
        m_settings.appearance.welcomeBannerCustomPaths = paths;
    }
    if (keyChanged) {
        m_settings.appearance.welcomeBannerFixedKey = fixedKey;
    }
    emit welcomeBannerBackgroundSettingsChanged();
    emit settingsChanged();
}

void SettingsManager::setWelcomeBannerCustomCrop(const QString& path, const QRectF& normRect)
{
    if (path.isEmpty()) {
        return;
    }
    // Intentionally does not emit: callers pair this with a path/fixed-key setter
    // so the banner reload happens once. Full/invalid rect clears any stored crop.
    if (isFullCropRect(normRect)) {
        m_settings.appearance.welcomeBannerCustomCrops.remove(path);
    } else {
        m_settings.appearance.welcomeBannerCustomCrops.insert(path, normRect);
    }
}

void SettingsManager::setWelcomeBannerCustomCropNotifying(
    const QString& path, const QRectF& normRect)
{
    setWelcomeBannerCustomCrop(path, normRect);
    emit welcomeBannerBackgroundSettingsChanged();
    emit settingsChanged();
}

QRectF SettingsManager::welcomeBannerCropFor(const QString& path) const
{
    const auto it = m_settings.appearance.welcomeBannerCustomCrops.constFind(path);
    if (it != m_settings.appearance.welcomeBannerCustomCrops.constEnd() && it.value().isValid()) {
        return it.value();
    }
    return QRectF(0.0, 0.0, 1.0, 1.0);
}

void SettingsManager::setWelcomeBannerTextColorMode(int mode)
{
    if (mode != 0 && mode != 1) {
        return;
    }
    if (m_settings.appearance.welcomeBannerTextColorMode != mode) {
        m_settings.appearance.welcomeBannerTextColorMode = mode;
        emit welcomeBannerBackgroundSettingsChanged();
        emit settingsChanged();
    }
}

void SettingsManager::syncWelcomeBannerDisplayedImageKey(const QString& key)
{
    if (welcomeBannerStoredKeysMatch(m_settings.appearance.welcomeBannerFixedKey, key)) {
        return;
    }
    m_settings.appearance.welcomeBannerFixedKey = key;
    save();
    emit welcomeBannerDisplayedImageKeyChanged(key);
    emit settingsChanged();
}

int SettingsManager::brushDisplayColorIndex(const QString& brushId)
{
    if (!m_loaded) {
        load();
    }
    if (brushId.isEmpty()) {
        return kMinBrushDisplayColorIndex;
    }

    return qBound(kMinBrushDisplayColorIndex,
        m_settings.appearance.brushDisplayColorIndices.value(brushId, kMinBrushDisplayColorIndex),
        kMaxBrushDisplayColorIndex);
}

void SettingsManager::setBrushDisplayColorIndex(const QString& brushId, int colorIndex)
{
    if (brushId.isEmpty()) {
        return;
    }

    colorIndex = qBound(kMinBrushDisplayColorIndex, colorIndex, kMaxBrushDisplayColorIndex);
    if (brushDisplayColorIndex(brushId) == colorIndex) {
        return;
    }

    if (colorIndex == kMinBrushDisplayColorIndex) {
        m_settings.appearance.brushDisplayColorIndices.remove(brushId);
    } else {
        m_settings.appearance.brushDisplayColorIndices.insert(brushId, colorIndex);
    }

    saveBrushDisplayColorsAsync();
    emit brushDisplayColorChanged(brushId, colorIndex);
    emit settingsChanged();
}

void SettingsManager::saveBrushDisplayColorsAsync()
{
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    const QHash<QString, int> colorIndices = m_settings.appearance.brushDisplayColorIndices;
    const quint64 serial = m_brushDisplayColorSaveSerial.fetchAndAddRelaxed(1) + 1;

    (void) QtConcurrent::run([organization, application, colorIndices, serial]() {
        if (serial != SettingsManager::instance().m_brushDisplayColorSaveSerial.loadAcquire()) {
            return;
        }

        QSettings settings(organization, application);
        settings.beginGroup(QStringLiteral("Appearance"));
        settings.remove(QLatin1String(kBrushDisplayColorsGroup));
        settings.beginGroup(QLatin1String(kBrushDisplayColorsGroup));
        for (auto it = colorIndices.cbegin(); it != colorIndices.cend(); ++it) {
            if (!it.key().isEmpty() && it.value() > kMinBrushDisplayColorIndex
                && it.value() <= kMaxBrushDisplayColorIndex) {
                settings.setValue(it.key(), it.value());
            }
        }
        settings.endGroup();
        settings.endGroup();
        settings.sync();
    });
}

bool SettingsManager::isBrushFavorite(const QString& brushId)
{
    if (!m_loaded) {
        load();
    }
    return !brushId.isEmpty() && m_settings.appearance.favoriteBrushIds.contains(brushId);
}

QSet<QString> SettingsManager::favoriteBrushIds()
{
    if (!m_loaded) {
        load();
    }
    return m_settings.appearance.favoriteBrushIds;
}

void SettingsManager::setBrushFavorite(const QString& brushId, bool favorite)
{
    if (brushId.isEmpty() || isBrushFavorite(brushId) == favorite) {
        return;
    }

    if (favorite) {
        m_settings.appearance.favoriteBrushIds.insert(brushId);
    } else {
        m_settings.appearance.favoriteBrushIds.remove(brushId);
    }

    saveBrushFavoritesAsync();
    emit brushFavoriteChanged(brushId, favorite);
    emit settingsChanged();
}

void SettingsManager::saveBrushFavoritesAsync()
{
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    QStringList favoriteBrushIds = m_settings.appearance.favoriteBrushIds.values();
    favoriteBrushIds.sort();
    const quint64 serial = m_brushFavoriteSaveSerial.fetchAndAddRelaxed(1) + 1;

    (void) QtConcurrent::run([organization, application, favoriteBrushIds, serial]() {
        if (serial != SettingsManager::instance().m_brushFavoriteSaveSerial.loadAcquire()) {
            return;
        }

        QSettings settings(organization, application);
        settings.beginGroup(QStringLiteral("Appearance"));
        settings.setValue(QLatin1String(kFavoriteBrushIdsKey), favoriteBrushIds);
        settings.endGroup();
        settings.sync();
    });
}

// === EDITOR ===

void SettingsManager::loadEditor(QSettings& settings)
{
    settings.beginGroup("Editor");

    // Migration: old autoSave (bool) + autoSaveInterval -> single autoSaveInterval (0=off,
    // 2/5/10=minutes)
    const bool legacyAutoSave = settings.value("autoSave", true).toBool();
    int interval = legacyAutoSave ? settings.value("autoSaveInterval", 5).toInt() : 0;
    // Map legacy 20min to 10min; valid values: 0, 2, 5, 10
    if (interval == 20)
        interval = 10;
    else if (interval != 0 && interval != 2 && interval != 5 && interval != 10)
        interval = 5;
    m_settings.editor.autoSaveInterval = interval;

    m_settings.editor.quickshapesEnabled = settings.value("quickshapesEnabled", true).toBool();

    settings.endGroup();
}

void SettingsManager::loadOnboarding(QSettings& settings)
{
    settings.beginGroup("Onboarding");

    if (kPersistOnboardingState) {
        m_settings.onboarding.firstRunIntegrationCompleted
            = settings
                  .value(kFirstRunIntegrationCompletedKey,
                      settings.value(kLegacyInitialSetupCompletedKey, false))
                  .toBool();
    } else {
        m_settings.onboarding.firstRunIntegrationCompleted = false;
    }

    settings.endGroup();
}

void SettingsManager::saveEditor(QSettings& settings)
{
    settings.beginGroup("Editor");

    settings.setValue("autoSaveInterval", m_settings.editor.autoSaveInterval);
    settings.setValue("quickshapesEnabled", m_settings.editor.quickshapesEnabled);

    settings.endGroup();
}

void SettingsManager::saveOnboarding(QSettings& settings)
{
    if (!kPersistOnboardingState) {
        return;
    }

    settings.beginGroup("Onboarding");
    settings.setValue(
        kFirstRunIntegrationCompletedKey, m_settings.onboarding.firstRunIntegrationCompleted);
    settings.endGroup();
}

void SettingsManager::setAutoSaveInterval(int minutes)
{
    if (m_settings.editor.autoSaveInterval != minutes) {
        m_settings.editor.autoSaveInterval = minutes;
        emit autoSaveIntervalChanged(minutes);
        emit settingsChanged();
    }
}

void SettingsManager::setQuickshapesEnabled(bool enabled)
{
    if (m_settings.editor.quickshapesEnabled != enabled) {
        m_settings.editor.quickshapesEnabled = enabled;
        emit settingsChanged();
    }
}

bool SettingsManager::isFirstRunIntegrationCompleted() const
{
    return m_settings.onboarding.firstRunIntegrationCompleted;
}

void SettingsManager::setFirstRunIntegrationCompleted(bool completed)
{
    if (m_settings.onboarding.firstRunIntegrationCompleted == completed) {
        return;
    }

    m_settings.onboarding.firstRunIntegrationCompleted = completed;
    save();
    emit firstRunIntegrationCompletedChanged(completed);
    emit settingsChanged();
}

// === PERFORMANCE ===

void SettingsManager::loadPerformance(QSettings& settings)
{
    settings.beginGroup("Performance");

    const int value = settings.value("undoMemoryLimitMb", 3072).toInt();
    if (value == 300 || value == 1024 || value == 3072 || value == 8192) {
        m_settings.performance.undoMemoryLimitMb = value;
    } else {
        m_settings.performance.undoMemoryLimitMb = 3072;
    }

    const int backend = settings.value("tabletBackend", 2).toInt();
    m_settings.performance.tabletBackend = (backend >= 0 && backend <= 2) ? backend : 2;

    settings.endGroup();
}

void SettingsManager::savePerformance(QSettings& settings)
{
    settings.beginGroup("Performance");

    settings.setValue("undoMemoryLimitMb", m_settings.performance.undoMemoryLimitMb);
    settings.setValue("tabletBackend", m_settings.performance.tabletBackend);

    settings.endGroup();
}

void SettingsManager::setUndoMemoryLimitMb(int megabytes)
{
    if (megabytes != 300 && megabytes != 1024 && megabytes != 3072 && megabytes != 8192) {
        return;
    }

    if (m_settings.performance.undoMemoryLimitMb != megabytes) {
        m_settings.performance.undoMemoryLimitMb = megabytes;
        emit undoMemoryLimitChanged(megabytes);
        emit settingsChanged();
    }
}

void SettingsManager::setTabletBackend(int backend)
{
    if (backend < 0 || backend > 2) {
        return;
    }

    if (m_settings.performance.tabletBackend != backend) {
        m_settings.performance.tabletBackend = backend;
        emit tabletBackendChanged(backend);
        emit settingsChanged();
    }
}

// === WINDOW STATE ===

void SettingsManager::loadWindowState(QSettings& settings)
{
    settings.beginGroup("WindowState");

    m_settings.windowState.mainWindowSize
        = settings.value("mainWindowSize", QSize(1280, 800)).toSize();
    m_settings.windowState.isMaximized = settings.value("isMaximized", false).toBool();
    m_settings.windowState.lastActiveTabIndex = settings.value("lastActiveTabIndex", 0).toInt();

    settings.endGroup();
}

void SettingsManager::saveWindowState(QSettings& settings)
{
    settings.beginGroup("WindowState");

    settings.setValue("mainWindowSize", m_settings.windowState.mainWindowSize);
    settings.setValue("isMaximized", m_settings.windowState.isMaximized);
    settings.setValue("lastActiveTabIndex", m_settings.windowState.lastActiveTabIndex);

    settings.endGroup();
}

void SettingsManager::setMainWindowSize(const QSize& size)
{
    if (m_settings.windowState.mainWindowSize != size) {
        m_settings.windowState.mainWindowSize = size;
        // Don't emit settingsChanged for window state (too frequent)
    }
}

void SettingsManager::setIsMaximized(bool maximized)
{
    if (m_settings.windowState.isMaximized != maximized) {
        m_settings.windowState.isMaximized = maximized;
        // Don't emit settingsChanged for window state
    }
}

void SettingsManager::setLastActiveTabIndex(int index)
{
    if (m_settings.windowState.lastActiveTabIndex != index) {
        m_settings.windowState.lastActiveTabIndex = index;
        // Don't emit settingsChanged for window state
    }
}

} // namespace ruwa::core
