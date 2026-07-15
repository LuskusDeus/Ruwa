// SPDX-License-Identifier: MPL-2.0

// ThumbnailCache.cpp
#include "ThumbnailCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCryptographicHash>

namespace ruwa::core::serialization {

ThumbnailCache& ThumbnailCache::instance()
{
    static ThumbnailCache inst;
    return inst;
}

QString ThumbnailCache::cachePathFor(const QString& projectFilePath) const
{
    const QByteArray hash
        = QCryptographicHash::hash(projectFilePath.toUtf8(), QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(hash.toHex().left(32));

    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::tempPath() + QLatin1String("/ruwa-cache");
    }
    const QString thumbDir = baseDir + QLatin1String("/thumbnails");
    QDir().mkpath(thumbDir);
    return thumbDir + QLatin1Char('/') + hex + QLatin1String(".png");
}

void ThumbnailCache::save(const QString& projectFilePath, const QImage& image, int maxSize)
{
    if (projectFilePath.isEmpty() || image.isNull())
        return;

    QImage scaled = image;
    if (image.width() > maxSize || image.height() > maxSize) {
        scaled = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    const QString path = cachePathFor(projectFilePath);
    const QImage existing(path);
    if (!existing.isNull()) {
        const int existingMaxEdge = qMax(existing.width(), existing.height());
        const int newMaxEdge = qMax(scaled.width(), scaled.height());
        if (newMaxEdge < existingMaxEdge) {
            return;
        }
    }

    if (!scaled.save(path, "PNG")) { }
}

void ThumbnailCache::save(const QString& projectFilePath, const QPixmap& pixmap, int maxSize)
{
    if (pixmap.isNull())
        return;
    save(projectFilePath, pixmap.toImage(), maxSize);
}

QPixmap ThumbnailCache::load(const QString& projectFilePath) const
{
    if (projectFilePath.isEmpty())
        return QPixmap();

    const QString path = cachePathFor(projectFilePath);
    if (!QFileInfo::exists(path))
        return QPixmap();

    QPixmap pix;
    if (!pix.load(path)) {
        return QPixmap();
    }
    return pix;
}

void ThumbnailCache::remove(const QString& projectFilePath)
{
    if (projectFilePath.isEmpty())
        return;
    const QString path = cachePathFor(projectFilePath);
    if (QFileInfo::exists(path)) {
        QFile::remove(path);
    }
}

} // namespace ruwa::core::serialization
