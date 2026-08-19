// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_PROJECT_RECENTPROJECTPRESETSMANAGER_H
#define RUWA_FEATURES_PROJECT_RECENTPROJECTPRESETSMANAGER_H

#include "shared/tiles/TileFormat.h"

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

namespace ruwa::core::serialization {

/**
 * @brief True when @p name is the placeholder New Project name (any language) or empty.
 *
 * Entries created without a real name store an empty projectName so the UI can label them
 * from the matching built-in preset instead of freezing one language into the settings.
 */
bool isDefaultProjectName(const QString& name);

struct RecentProjectPresetEntry {
    QString id;
    QString projectName; ///< empty ⇒ unnamed, the UI derives a label from the settings
    QSize canvasSize;
    bool infiniteCanvasEnabled = false;
    QString colorMode;
    QColor backgroundColor { Qt::white };
    aether::TilePixelFormat tileFormat = aether::kDefaultTileFormat;
    QDateTime createdAt;
    int useCount = 1; ///< how many projects collapsed into this entry

    bool isValid() const
    {
        return !id.isEmpty() && canvasSize.width() > 0 && canvasSize.height() > 0;
    }

    bool hasCustomName() const { return !isDefaultProjectName(projectName); }

    /// Everything except the name, the timestamp and the use count.
    bool sameConfiguration(const RecentProjectPresetEntry& other) const
    {
        return canvasSize == other.canvasSize
            && infiniteCanvasEnabled == other.infiniteCanvasEnabled && colorMode == other.colorMode
            && backgroundColor.rgba() == other.backgroundColor.rgba()
            && tileFormat == other.tileFormat;
    }
};

/**
 * @brief Persistent creation history used by the New Project preset tabs.
 *
 * Unlike RecentProjectsManager, these entries describe projects immediately
 * after creation and therefore do not require a saved file path.
 *
 * Entries are collapsed by configuration: creating the same kind of project again
 * moves the existing entry back to the front instead of stacking a near-identical
 * card, and the list is capped at maxEntries().
 */
class RecentProjectPresetsManager : public QObject {
    Q_OBJECT

public:
    static RecentProjectPresetsManager& instance();

    static constexpr int maxEntries() { return 12; }

    const QList<RecentProjectPresetEntry>& entries() const { return m_entries; }
    bool isEmpty() const { return m_entries.isEmpty(); }

    /// Id of the entry whose configuration matches these settings ({} when none does).
    QString findMatchingEntryId(const QSize& canvasSize, bool infiniteCanvasEnabled,
        const QString& colorMode, const QColor& backgroundColor,
        aether::TilePixelFormat tileFormat) const;

    void addEntry(const QString& projectName, const QSize& canvasSize, bool infiniteCanvasEnabled,
        const QString& colorMode, const QColor& backgroundColor,
        aether::TilePixelFormat tileFormat);
    void removeEntry(const QString& id);
    void clear();

signals:
    void entriesChanged();

private:
    RecentProjectPresetsManager();
    ~RecentProjectPresetsManager() override = default;

    RecentProjectPresetsManager(const RecentProjectPresetsManager&) = delete;
    RecentProjectPresetsManager& operator=(const RecentProjectPresetsManager&) = delete;

    void load();
    void save() const;

    /// Collapses duplicate configurations and trims to maxEntries(); true when it changed anything.
    bool normalizeEntries();

private:
    QList<RecentProjectPresetEntry> m_entries;
};

} // namespace ruwa::core::serialization

#endif // RUWA_FEATURES_PROJECT_RECENTPROJECTPRESETSMANAGER_H
