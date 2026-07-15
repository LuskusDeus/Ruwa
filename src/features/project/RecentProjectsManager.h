// SPDX-License-Identifier: MPL-2.0

// RecentProjectsManager.h
#ifndef RUWA_CORE_SERIALIZATION_RECENTPROJECTSMANAGER_H
#define RUWA_CORE_SERIALIZATION_RECENTPROJECTSMANAGER_H

#include <QObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QSize>
#include <QDateTime>

namespace ruwa::core::serialization {

// ============================================================================
// Recent project entry
// ============================================================================

struct RecentProjectEntry {
    QString filePath;
    QString projectName;
    QString tabIconAlias;
    QDateTime lastOpened;
    QSize canvasSize;
    bool previewEnabled = true;

    bool isValid() const { return !filePath.isEmpty() && !projectName.isEmpty(); }
};

// ============================================================================
// RecentProjectsManager
// ============================================================================

/**
 * @brief Manages the list of recently opened/saved projects
 *
 * Persists to QSettings under the "RecentProjects" group.
 * Automatically deduplicates by file path and keeps the list
 * trimmed to maxEntries().
 */
class RecentProjectsManager : public QObject {
    Q_OBJECT

public:
    static RecentProjectsManager& instance();

    // === Access ===

    const QList<RecentProjectEntry>& entries() const { return m_entries; }
    int count() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }

    int maxEntries() const { return m_maxEntries; }
    void setMaxEntries(int max);

    // === Modification ===

    /// Add or update an entry (moves it to the top)
    void addEntry(const RecentProjectEntry& entry);

    /// Add from minimal info
    void addEntry(const QString& filePath, const QString& projectName,
        const QSize& canvasSize = QSize(), const QString& tabIconAlias = QString());

    /// Remove entry by file path
    void removeEntry(const QString& filePath);

    /// Hide entry from recent projects until it is added again.
    void forgetEntry(const QString& filePath);

    /// Update editable metadata for an existing entry.
    void updateEntryMetadata(
        const QString& filePath, const QString& projectName, bool previewEnabled);

    /// Return a copy of the entry for the given path, or an invalid entry when absent.
    RecentProjectEntry entryForPath(const QString& filePath) const;

    /// Check whether preview is enabled for the given file path.
    bool isPreviewEnabled(const QString& filePath) const;

    /// Clear all entries
    void clear();

    /// Remove entries whose files no longer exist on disk
    void pruneInvalid();

    // === Persistence ===

    void load();
    void save();

    /// Emit entriesChanged so UI reloads previews (e.g. after ThumbnailCache writes a new image).
    void notifyThumbnailsUpdated();

signals:
    void entriesChanged();

private:
    RecentProjectsManager();
    ~RecentProjectsManager() override = default;

    RecentProjectsManager(const RecentProjectsManager&) = delete;
    RecentProjectsManager& operator=(const RecentProjectsManager&) = delete;

    int indexByPath(const QString& filePath) const;
    static QString normalizePath(const QString& filePath);

private:
    QList<RecentProjectEntry> m_entries;
    QSet<QString> m_forgottenPaths;
    QSet<QString> m_previewDisabledPaths;
    int m_maxEntries = 20;
    bool m_loaded = false;

    static constexpr int MAX_ENTRIES_LIMIT = 50;
};

} // namespace ruwa::core::serialization

#endif // RUWA_CORE_SERIALIZATION_RECENTPROJECTSMANAGER_H
