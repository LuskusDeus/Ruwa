// SPDX-License-Identifier: MPL-2.0

// ThumbnailCache.h
#ifndef RUWA_CORE_SERIALIZATION_THUMBNAILCACHE_H
#define RUWA_CORE_SERIALIZATION_THUMBNAILCACHE_H

#include <QString>
#include <QPixmap>
#include <QImage>

namespace ruwa::core::serialization {

/**
 * @brief Cache for project thumbnails (recent projects list preview)
 *
 * Stores thumbnails in app cache directory, keyed by project file path.
 * Used when saving/opening projects to capture canvas preview.
 */
class ThumbnailCache {
public:
    static ThumbnailCache& instance();

    /// Save thumbnail for project file path (PNG, max dimension = size)
    void save(const QString& projectFilePath, const QImage& image, int maxSize = 256);

    /// Save from QPixmap
    void save(const QString& projectFilePath, const QPixmap& pixmap, int maxSize = 256);

    /// Load thumbnail for project file path (returns null if not found)
    QPixmap load(const QString& projectFilePath) const;

    /// Remove thumbnail for path (e.g. when project is removed from recent)
    void remove(const QString& projectFilePath);

private:
    ThumbnailCache() = default;
    ~ThumbnailCache() = default;

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    QString cachePathFor(const QString& projectFilePath) const;
};

} // namespace ruwa::core::serialization

#endif // RUWA_CORE_SERIALIZATION_THUMBNAILCACHE_H
