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

struct RecentProjectPresetEntry {
    QString id;
    QString projectName;
    QSize canvasSize;
    bool infiniteCanvasEnabled = false;
    QString colorMode;
    QColor backgroundColor { Qt::white };
    aether::TilePixelFormat tileFormat = aether::kDefaultTileFormat;
    QDateTime createdAt;

    bool isValid() const
    {
        return !id.isEmpty() && !projectName.isEmpty() && canvasSize.width() > 0
            && canvasSize.height() > 0;
    }
};

/**
 * @brief Persistent creation history used by the New Project preset tabs.
 *
 * Unlike RecentProjectsManager, these entries describe projects immediately
 * after creation and therefore do not require a saved file path.
 */
class RecentProjectPresetsManager : public QObject {
    Q_OBJECT

public:
    static RecentProjectPresetsManager& instance();

    const QList<RecentProjectPresetEntry>& entries() const { return m_entries; }
    bool isEmpty() const { return m_entries.isEmpty(); }

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

private:
    QList<RecentProjectPresetEntry> m_entries;
};

} // namespace ruwa::core::serialization

#endif // RUWA_FEATURES_PROJECT_RECENTPROJECTPRESETSMANAGER_H
