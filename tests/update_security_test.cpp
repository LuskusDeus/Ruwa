// SPDX-License-Identifier: MPL-2.0

#include "services/updates/UpdateSecurity.h"

#include <catch2/catch_test_macros.hpp>

using ruwa::services::isAllowedUpdateUrl;
using ruwa::services::isValidCertificateSha256;
using ruwa::services::shouldRejectUpdateArchiveContentLength;

TEST_CASE("update URLs require an exact allowlisted HTTPS origin", "[updates][security]")
{
    const QStringList hosts { QStringLiteral("github.com"),
        QStringLiteral("release-assets.githubusercontent.com") };

    CHECK(isAllowedUpdateUrl(
        QUrl(QStringLiteral("https://github.com/owner/repo/latest.json")), hosts));
    CHECK(isAllowedUpdateUrl(
        QUrl(QStringLiteral("https://github.com:443/owner/repo/latest.json")), hosts));
    CHECK_FALSE(isAllowedUpdateUrl(
        QUrl(QStringLiteral("http://github.com/owner/repo/latest.json")), hosts));
    CHECK_FALSE(isAllowedUpdateUrl(
        QUrl(QStringLiteral("https://github.com.evil.example/latest.json")), hosts));
    CHECK_FALSE(isAllowedUpdateUrl(
        QUrl(QStringLiteral("https://user@github.com/owner/repo/latest.json")), hosts));
    CHECK_FALSE(isAllowedUpdateUrl(
        QUrl(QStringLiteral("https://github.com:444/owner/repo/latest.json")), hosts));
    CHECK_FALSE(isAllowedUpdateUrl(
        QUrl(QStringLiteral("https://github.com/owner/repo/latest.json#fragment")), hosts));
}

TEST_CASE("trusted certificate fingerprints are strict SHA-256", "[updates][security]")
{
    CHECK(isValidCertificateSha256(QString(64, QLatin1Char('a'))));
    CHECK(isValidCertificateSha256(
        QStringLiteral("AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:"
                       "AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA:AA")));
    CHECK_FALSE(isValidCertificateSha256(QString(63, QLatin1Char('a'))));
    CHECK_FALSE(isValidCertificateSha256(QString(64, QLatin1Char('g'))));
}

TEST_CASE("archive length validation ignores redirects and validates the final response",
    "[updates][security][download]")
{
    constexpr qint64 expectedSize = 21'030'329;

    SECTION("GitHub redirect metadata does not reject the download")
    {
        CHECK_FALSE(shouldRejectUpdateArchiveContentLength(302, qint64(0), expectedSize));
        CHECK_FALSE(
            shouldRejectUpdateArchiveContentLength(307, qint64(expectedSize + 1), expectedSize));
    }

    SECTION("the final response may omit Content-Length")
    {
        CHECK_FALSE(shouldRejectUpdateArchiveContentLength(200, std::nullopt, expectedSize));
    }

    SECTION("the final response must report the signed size when the header is present")
    {
        CHECK_FALSE(
            shouldRejectUpdateArchiveContentLength(200, qint64(expectedSize), expectedSize));
        CHECK(shouldRejectUpdateArchiveContentLength(200, qint64(0), expectedSize));
        CHECK(shouldRejectUpdateArchiveContentLength(200, qint64(expectedSize + 1), expectedSize));
    }
}

TEST_CASE("empty or malformed detached signatures fail closed", "[updates][security]")
{
    QString error;
    CHECK_FALSE(ruwa::services::verifyDetachedUpdateSignature(QByteArrayLiteral("manifest"),
        QByteArrayLiteral("not-a-signature"), QString(64, QLatin1Char('a')), &error));
    CHECK_FALSE(error.isEmpty());
}

#if defined(Q_OS_WIN)
TEST_CASE("detached update signature accepts only the pinned publisher", "[updates][security]")
{
    const QByteArray content = QByteArray::fromBase64(
        QByteArrayLiteral("eyJmaXh0dXJlIjoicnV3YS11cGRhdGUtc2lnbmF0dXJlIn0="));
    const QByteArray signature = QByteArray::fromBase64(
        QByteArrayLiteral("MIIEYAYJKoZIhvcNAQcCoIIEUTCCBE0CAQExDzANBglghkgBZQMEAgEFADALBgkqhkiG9w0B"
                          "BwGgggLKMIICxjCCAa6gAwIBAgIIeh0KY+LJpkcwDQYJKoZIhvcNAQELBQAwIzEhMB8GA1UE"
                          "AxMYUnV3YSBVcGRhdGUgVGVzdCBGaXh0dXJlMB4XDTI2MDcxNDE4MDUyNVoXDTI3MDcxNTE4"
                          "MDUyNVowIzEhMB8GA1UEAxMYUnV3YSBVcGRhdGUgVGVzdCBGaXh0dXJlMIIBIjANBgkqhkiG"
                          "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsmBuq0zgAyMsJ4HEo9C7W68fYuwZi3UJbmzBPJ0QlNOS"
                          "GoGZIUHw6rB29umumdS0jITqAl/ublth99VY0iQZYs5gviJzaMLknFAD4Ann09fmHWgy6M2L"
                          "LNlIxByQBCyRWrcuyPCEmwnKzr0+t+h/u5RTuXFESbiXFXIvMndEgCbzpgR1kDHjeZ5rjZEJ"
                          "d6fKPhUSEtYBqG5y5nTDug1E6jqzINCcwYAe/3Hl7wvijiDtdE89di/qUolpGBNb0oIQ5YPe"
                          "tJvxE2jl9dHaKOLOW2iGCrwCdUzoaisyX0rbu2zSHPIJeAF4Vkiw6h7MjMSLh75VlBc+i2GY"
                          "jjHPND783QIDAQABMA0GCSqGSIb3DQEBCwUAA4IBAQBl1ZVto+v7HndjZPW32bCGUq5B66zD"
                          "/eZGPYk9ALMXzK/AkJzwkjgTrQ2FPGqKLN2NW5trsGcpoPNcIt0jaqYD95QCw7rOssmQ8MT"
                          "w5Gf23ywlcozWiGnxWR5wnBt4b9kHkZ2HORqmzo01RW1OzzkYiU4vW30zunVbd5fc/b8pE7"
                          "VFqZ1akUmSJhCOBR7YxTDVZZtxIyJSwpWDRpmLh/CO6A5VRCjFDHAW+wkL31FtdTyhM1O+d"
                          "4LCIfE2zUiAEqG2AZotiwq97PZJEMSVmupIjxNUcWwzAeG8kMtP4pdiUfzYFmKMt73fh05u"
                          "ps1Qtnk7pztpHV0U+0E12HtclN14MYIBWjCCAVYCAQEwLzAjMSEwHwYDVQQDExhSdXdhIFV"
                          "wZGF0ZSBUZXN0IEZpeHR1cmUCCHodCmPiyaZHMA0GCWCGSAFlAwQCAQUAMA0GCSqGSIb3DQE"
                          "BAQUABIIBAEObBev9DIzOfZyrLWbBR6nNkBcD54y/G9d6dApyU0LbU4IAhZZmQBsBI+vs6T"
                          "pI0w7b3bn831YHprjGUcmY7uz4X3rS1n3vPvBuUMyjdFN6LQcl62uDaj7S/EK518U1dyXXs"
                          "KpkPRT9CHdySKZSEjJqwMitGLKwOOQBc9YO57UZ2hh4FaeOaWnt+DTdfYb+YdRnQC9GwkFpN"
                          "pAfJYAUx9oZ+hZkhEqdwLEGStQWQ2XK4d8SQ9gle6fxQiBv+kk4pVaMOBYi4quMFQSy1/VD"
                          "wN0jJ+D97yTYxhVitnimaEQbx6BqqGngK7nzMWAC4j5RRZ2XbS0aTbuzC2DUgZsio/s="));
    const QString fingerprint
        = QStringLiteral("4cd005823588778c069c6fe713756afefdac25fb9f0f580bdc42a9395af6aa50");

    QString error;
    CHECK(ruwa::services::verifyDetachedUpdateSignature(content, signature, fingerprint, &error));

    QByteArray modifiedContent = content;
    modifiedContent.append('!');
    CHECK_FALSE(ruwa::services::verifyDetachedUpdateSignature(
        modifiedContent, signature, fingerprint, &error));
    CHECK_FALSE(ruwa::services::verifyDetachedUpdateSignature(
        content, signature, QString(64, QLatin1Char('a')), &error));
}
#endif
