// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_UPDATES_UPDATESECURITY_H
#define RUWA_SERVICES_UPDATES_UPDATESECURITY_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace ruwa::services {

bool isAllowedUpdateUrl(const QUrl& url, const QStringList& allowedHosts);
bool isValidCertificateSha256(const QString& fingerprint);
// Redirect responses describe their own empty body, not the eventual archive.
// Only a final 200 response can contradict the archive size in the signed manifest.
bool shouldRejectUpdateArchiveContentLength(
    int httpStatusCode, std::optional<qint64> contentLength, qint64 expectedSize);
bool verifyDetachedUpdateSignature(const QByteArray& content, const QByteArray& signature,
    const QString& pinnedCertificateSha256, QString* errorMessage = nullptr);

} // namespace ruwa::services

#endif // RUWA_SERVICES_UPDATES_UPDATESECURITY_H
