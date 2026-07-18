// SPDX-License-Identifier: MPL-2.0

#include "services/updates/UpdateSecurity.h"

#include <QCryptographicHash>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace ruwa::services {

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

QString normalizedFingerprint(QString fingerprint)
{
    fingerprint.remove(QLatin1Char(':'));
    fingerprint.remove(QLatin1Char(' '));
    return fingerprint.trimmed().toLower();
}

} // namespace

bool isAllowedUpdateUrl(const QUrl& url, const QStringList& allowedHosts)
{
    const int port = url.port(-1);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || url.host().isEmpty() || !url.userInfo().isEmpty() || (port != -1 && port != 443)
        || !url.fragment().isEmpty()) {
        return false;
    }
    for (const QString& host : allowedHosts) {
        if (url.host().compare(host.trimmed(), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool isValidCertificateSha256(const QString& fingerprint)
{
    const QString normalized = normalizedFingerprint(fingerprint);
    if (normalized.size() != 64) {
        return false;
    }
    for (const QChar ch : normalized) {
        const bool digit = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        if (!digit && (ch < QLatin1Char('a') || ch > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

bool shouldRejectUpdateArchiveContentLength(
    int httpStatusCode, std::optional<qint64> contentLength, qint64 expectedSize)
{
    return httpStatusCode == 200 && contentLength && *contentLength != expectedSize;
}

bool verifyDetachedUpdateSignature(const QByteArray& content, const QByteArray& signature,
    const QString& pinnedCertificateSha256, QString* errorMessage)
{
    if (content.isEmpty() || signature.isEmpty()
        || !isValidCertificateSha256(pinnedCertificateSha256)) {
        setError(errorMessage, QStringLiteral("Update signature input is invalid"));
        return false;
    }

#if defined(Q_OS_WIN)
    CRYPT_VERIFY_MESSAGE_PARA parameters {};
    parameters.cbSize = sizeof(parameters);
    parameters.dwMsgAndCertEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

    const BYTE* contentPointer = reinterpret_cast<const BYTE*>(content.constData());
    DWORD contentSize = static_cast<DWORD>(content.size());
    PCCERT_CONTEXT signerCertificate = nullptr;
    const BOOL verified = CryptVerifyDetachedMessageSignature(&parameters, 0,
        reinterpret_cast<const BYTE*>(signature.constData()), static_cast<DWORD>(signature.size()),
        1, &contentPointer, &contentSize, &signerCertificate);
    if (!verified || !signerCertificate) {
        if (signerCertificate) {
            CertFreeCertificateContext(signerCertificate);
        }
        setError(errorMessage, QStringLiteral("Update manifest signature is invalid"));
        return false;
    }

    const QByteArray certificateBytes(
        reinterpret_cast<const char*>(signerCertificate->pbCertEncoded),
        static_cast<qsizetype>(signerCertificate->cbCertEncoded));
    CertFreeCertificateContext(signerCertificate);
    const QString actualFingerprint = QString::fromLatin1(
        QCryptographicHash::hash(certificateBytes, QCryptographicHash::Sha256).toHex());
    if (actualFingerprint.compare(
            normalizedFingerprint(pinnedCertificateSha256), Qt::CaseInsensitive)
        != 0) {
        setError(
            errorMessage, QStringLiteral("Update manifest was signed by an unknown publisher"));
        return false;
    }
    return true;
#else
    Q_UNUSED(content)
    Q_UNUSED(signature)
    Q_UNUSED(pinnedCertificateSha256)
    setError(errorMessage, QStringLiteral("Signed self-updates are only supported on Windows"));
    return false;
#endif
}

} // namespace ruwa::services
