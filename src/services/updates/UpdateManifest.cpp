// SPDX-License-Identifier: MPL-2.0

#include "services/updates/UpdateManifest.h"

#include "services/updates/SemanticVersion.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>

namespace ruwa::services {

namespace {

constexpr qsizetype kMaximumManifestFiles = 4096;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool isSha256(const QString& value)
{
    if (value.size() != 64) {
        return false;
    }
    for (const QChar ch : value) {
        const bool digit = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        const QChar lower = ch.toLower();
        if (!digit && (lower < QLatin1Char('a') || lower > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || path.contains(QLatin1Char('\\')) || path.startsWith(QLatin1Char('/'))
        || path.endsWith(QLatin1Char('/')) || QFileInfo(path).isAbsolute()) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")
            || part.contains(QLatin1Char(':'))) {
            return false;
        }
    }
    return true;
}

std::optional<qint64> exactInteger(const QJsonValue& value, bool allowZero)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    constexpr double kLargestExactlyRepresentableInteger = 9007199254740991.0;
    if (!std::isfinite(number) || number < 0.0 || number > kLargestExactlyRepresentableInteger
        || std::floor(number) != number) {
        return std::nullopt;
    }
    const qint64 integer = static_cast<qint64>(number);
    if (integer < 0 || (!allowZero && integer == 0) || static_cast<double>(integer) != number) {
        return std::nullopt;
    }
    return integer;
}

} // namespace

std::optional<UpdateManifest> UpdateManifest::parse(
    const QByteArray& json, qint64 maximumArchiveBytes, QString* errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("Update manifest is not valid JSON"));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("ruwa-patch-v1")
        || root.value(QStringLiteral("product")).toString() != QLatin1String("Ruwa")) {
        setError(errorMessage, QStringLiteral("Unsupported update manifest format"));
        return std::nullopt;
    }
    if (root.value(QStringLiteral("platform")).toString() != QLatin1String("windows")
        || root.value(QStringLiteral("architecture")).toString() != QLatin1String("x86_64")) {
        setError(errorMessage, QStringLiteral("Update package is not for Windows x64"));
        return std::nullopt;
    }

    UpdateManifest manifest;
    manifest.version = root.value(QStringLiteral("version")).toString().trimmed();
    QString versionError;
    if (!SemanticVersion::parse(manifest.version, &versionError)) {
        setError(errorMessage, QStringLiteral("Invalid update version: %1").arg(versionError));
        return std::nullopt;
    }
    manifest.description = root.value(QStringLiteral("description")).toString().simplified();

    const QJsonObject archive = root.value(QStringLiteral("archive")).toObject();
    manifest.archiveUrl = QUrl(archive.value(QStringLiteral("url")).toString());
    manifest.archiveFileName = archive.value(QStringLiteral("fileName")).toString();
    const auto archiveSize = exactInteger(archive.value(QStringLiteral("size")), false);
    const QString archiveHash = archive.value(QStringLiteral("sha256")).toString().toLower();
    if (!manifest.archiveUrl.isValid() || manifest.archiveUrl.isRelative()
        || manifest.archiveFileName != QFileInfo(manifest.archiveFileName).fileName()
        || !manifest.archiveFileName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
        || !archiveSize || *archiveSize > maximumArchiveBytes || !isSha256(archiveHash)) {
        setError(errorMessage, QStringLiteral("Invalid update archive metadata"));
        return std::nullopt;
    }
    manifest.archiveSize = *archiveSize;
    manifest.archiveSha256 = archiveHash.toLatin1();

    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    if (files.isEmpty() || files.size() > kMaximumManifestFiles) {
        setError(errorMessage, QStringLiteral("Update manifest has an invalid file list"));
        return std::nullopt;
    }

    QSet<QString> sourcePaths;
    QSet<QString> targetPaths;
    bool containsExecutable = false;
    qint64 totalFileBytes = 0;
    manifest.files.reserve(files.size());
    for (const QJsonValue& fileValue : files) {
        if (!fileValue.isObject()) {
            setError(errorMessage, QStringLiteral("Invalid update file entry"));
            return std::nullopt;
        }
        const QJsonObject fileObject = fileValue.toObject();
        UpdateManifestFile file;
        file.sourcePath = fileObject.value(QStringLiteral("source")).toString();
        file.targetPath = fileObject.value(QStringLiteral("target")).toString();
        const auto fileSize = exactInteger(fileObject.value(QStringLiteral("size")), true);
        const QString fileHash = fileObject.value(QStringLiteral("sha256")).toString().toLower();
        const QString sourceKey = file.sourcePath.toCaseFolded();
        const QString targetKey = file.targetPath.toCaseFolded();
        if (!isSafeRelativePath(file.sourcePath) || !isSafeRelativePath(file.targetPath)
            || !fileSize || *fileSize > maximumArchiveBytes
            || totalFileBytes > maximumArchiveBytes - *fileSize || !isSha256(fileHash)
            || sourcePaths.contains(sourceKey) || targetPaths.contains(targetKey)) {
            setError(errorMessage, QStringLiteral("Invalid or duplicate update file entry"));
            return std::nullopt;
        }
        sourcePaths.insert(sourceKey);
        targetPaths.insert(targetKey);
        totalFileBytes += *fileSize;
        file.size = *fileSize;
        file.sha256 = fileHash.toLatin1();
        containsExecutable = containsExecutable
            || file.targetPath.compare(QStringLiteral("Ruwa.exe"), Qt::CaseInsensitive) == 0;
        manifest.files.append(file);
    }
    if (!containsExecutable) {
        setError(errorMessage, QStringLiteral("Update does not contain Ruwa.exe"));
        return std::nullopt;
    }

    const QJsonValue deleteValue = root.value(QStringLiteral("delete"));
    if (!deleteValue.isUndefined() && !deleteValue.isArray()) {
        setError(errorMessage, QStringLiteral("Invalid update deletion list"));
        return std::nullopt;
    }
    const QJsonArray deletePaths = deleteValue.toArray();
    for (const QJsonValue& pathValue : deletePaths) {
        const QString path = pathValue.toString();
        const QString key = path.toCaseFolded();
        if (!isSafeRelativePath(path) || targetPaths.contains(key)) {
            setError(errorMessage, QStringLiteral("Invalid update deletion path"));
            return std::nullopt;
        }
        targetPaths.insert(key);
        manifest.deletePaths.append(path);
    }

    return manifest;
}

} // namespace ruwa::services
