// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHMANAGER_H
#define RUWA_CORE_BRUSHES_BRUSHMANAGER_H

#include "features/brush/engine/BrushModule.h"
#include "BrushSettings.h"

#include <QObject>
#include <QFuture>
#include <QVector>
#include <QString>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <optional>

namespace ruwa::core::brushes {

struct BrushPresetData {
    QString id;
    QString name;
    QString iconPath;
};

struct BrushData {
    QString id;
    QString name;
    QString presetId;
    QString iconPath;
    BrushEngineId engineId = QLatin1String(kPixelBrushEngineId);
    int engineVersion = kPixelBrushEngineVersion;
    QVariantMap engineSettings;
    BrushSettingsData settings;

    // Transient: starred ("fav") setting keys parsed from an imported brush
    // file. Not persisted to QSettings as part of BrushData — it is consumed by
    // importBrushesInto*()/importBrushesAsPreset() to restore the author's
    // pinned settings under the freshly assigned brush id. The flag
    // distinguishes "file carried an explicit (possibly empty) set" from "file
    // had no starred info" (in which case the local defaults apply).
    QStringList importedStarredKeys;
    bool hasImportedStarred = false;
};

struct BrushImportResult {
    QVector<BrushData> brushes;
    QString errorMessage;
    bool success = false;
};

class BrushManager : public QObject {
    Q_OBJECT

public:
    static BrushManager& instance();

    const QVector<BrushPresetData>& presets();
    QVector<BrushData> brushesForPreset(const QString& presetId);

    QString createPreset();
    bool removePreset(const QString& presetId);
    bool renamePreset(const QString& presetId, const QString& newName);

    QString createBrush(const QString& presetId);
    bool removeBrush(const QString& brushId);
    bool renameBrush(const QString& brushId, const QString& newName);
    bool updateBrushSettings(const QString& brushId, const BrushSettingsData& settings);
    bool exportBrushesToFile(const QString& filePath, const QVector<BrushData>& brushes,
        const QString& packName, QString* errorMessage = nullptr) const;
    static BrushImportResult readBrushFileForImport(const QString& filePath);
    bool importBrushesIntoPreset(const QString& filePath, const QString& presetId,
        QStringList* importedBrushIds = nullptr, QString* errorMessage = nullptr);
    bool importBrushesIntoPreset(const QVector<BrushData>& brushes, const QString& presetId,
        QStringList* importedBrushIds = nullptr, QString* errorMessage = nullptr);
    QString importBrushFileAsPreset(const QString& filePath, const QString& presetName,
        QStringList* importedBrushIds = nullptr, QString* errorMessage = nullptr);
    QString importBrushesAsPreset(const QVector<BrushData>& brushes, const QString& presetName,
        QStringList* importedBrushIds = nullptr, QString* errorMessage = nullptr);
    std::optional<BrushData> brushData(const QString& brushId);
    std::optional<BrushSettingsData> brushSettings(const QString& brushId);
    QVector<BrushData> recentBrushes(int limit = 10);
    void markBrushAsRecentlyUsed(const QString& brushId);

    /// Returns preset ID that contains the given brush, or empty string if not found
    QString presetIdForBrush(const QString& brushId);

    /// Reset all brush packs to default (delete custom, restore the standard
    /// packs shipped in resources, see loadDefaults())
    void resetToDefaults();

    // --- Starred settings (UI preference, stored per brush) ---
    QSet<QString> starredSettings(const QString& brushId);
    bool isSettingStarred(const QString& brushId, const QString& key);
    void setSettingStarred(const QString& brushId, const QString& key, bool starred);

signals:
    void presetCreated(const QString& presetId);
    void presetRemoved(const QString& presetId);
    void presetRenamed(const QString& presetId, const QString& newName);
    void brushCreated(const QString& presetId, const QString& brushId);
    void brushRemoved(const QString& presetId, const QString& brushId);
    void brushRenamed(const QString& brushId, const QString& newName);
    void brushSettingsUpdated(
        const QString& presetId, const QString& brushId, const BrushSettingsData& settings);
    void starredSettingsChanged(const QString& brushId);

    /// Emitted after resetToDefaults() — UI should reload brush data
    void dataReset();

private:
    BrushManager();
    void ensureLoaded();
    void load();
    void save() const;
    void saveAsync() const;
    void waitForAsyncSave() const;
    void loadDefaults();
    void loadStarredSettings();
    void saveStarredSettings() const;
    // Stages an imported brush's starred-setting set into the in-memory map
    // under its new id. Returns true if anything was staged. Callers must
    // saveStarredSettings() and emit starredSettingsChanged() afterwards.
    bool stageImportedStarredSettings(const QString& brushId, const BrushData& source);
    void ensureRecentBrushesLoaded();
    void loadRecentBrushes();
    void saveRecentBrushes() const;
    void scheduleDeferredSave();
    void flushDeferredSave();

    QVector<BrushPresetData> m_presets;
    QHash<QString, QVector<BrushData>> m_brushesByPreset;
    QMap<QString, QSet<QString>> m_starredSettingsByBrush;
    QVector<QString> m_recentBrushIds;
    QTimer m_deferredSaveTimer;
    mutable QFuture<void> m_asyncSaveFuture;
    bool m_deferredSavePending = false;
    bool m_loaded = false;
    bool m_starredLoaded = false;
    bool m_recentBrushesLoaded = false;
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHMANAGER_H
