// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_UPDATES_UPDATESECURITY_H
#define RUWA_SERVICES_UPDATES_UPDATESECURITY_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace ruwa::services {

bool isAllowedUpdateUrl(const QUrl& url, const QStringList& allowedHosts);
bool isValidCertificateSha256(const QString& fingerprint);
bool verifyDetachedUpdateSignature(const QByteArray& content, const QByteArray& signature,
    const QString& pinnedCertificateSha256, QString* errorMessage = nullptr);

} // namespace ruwa::services

#endif // RUWA_SERVICES_UPDATES_UPDATESECURITY_H
