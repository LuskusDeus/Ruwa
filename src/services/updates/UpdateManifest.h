// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_UPDATES_UPDATEMANIFEST_H
#define RUWA_SERVICES_UPDATES_UPDATEMANIFEST_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace ruwa::services {

struct UpdateManifestFile {
    QString sourcePath;
    QString targetPath;
    qint64 size = 0;
    QByteArray sha256;
};

struct UpdateManifest {
    static std::optional<UpdateManifest> parse(
        const QByteArray& json, qint64 maximumArchiveBytes, QString* errorMessage = nullptr);

    QString version;
    QString description;
    QUrl archiveUrl;
    QString archiveFileName;
    qint64 archiveSize = 0;
    QByteArray archiveSha256;
    QList<UpdateManifestFile> files;
    QStringList deletePaths;
};

} // namespace ruwa::services

#endif // RUWA_SERVICES_UPDATES_UPDATEMANIFEST_H
