// SPDX-License-Identifier: MPL-2.0

#include "services/updates/UpdateManifest.h"

#include <catch2/catch_test_macros.hpp>

using ruwa::services::UpdateManifest;

namespace {

QByteArray validManifest()
{
    const QByteArray hash(64, 'a');
    return QByteArrayLiteral(R"({
        "format":"ruwa-patch-v1",
        "product":"Ruwa",
        "version":"0.2.10",
        "description":"Security update",
        "platform":"windows",
        "architecture":"x86_64",
        "archive":{
            "url":"https://github.com/LuskusDeus/Ruwa-releases/releases/download/v0.2.10/Ruwa-0.2.10-win64.patch.zip",
            "fileName":"Ruwa-0.2.10-win64.patch.zip",
            "size":15273335,
            "sha256":")")
        + hash + QByteArrayLiteral(R"("
        },
        "files":[{
            "source":"Main/Ruwa.exe",
            "target":"Ruwa.exe",
            "size":28515455,
            "sha256":")")
        + hash + QByteArrayLiteral(R"("
        }],
        "delete":[]
    })");
}

} // namespace

TEST_CASE("signed update manifest schema accepts a bounded Windows patch", "[updates][manifest]")
{
    QString error;
    const auto manifest = UpdateManifest::parse(validManifest(), 64 * 1024 * 1024, &error);
    INFO(error.toStdString());
    REQUIRE(manifest.has_value());
    CHECK(manifest->version == QStringLiteral("0.2.10"));
    CHECK(manifest->archiveSize == 15273335);
    REQUIRE(manifest->files.size() == 1);
    CHECK(manifest->files.constFirst().targetPath == QStringLiteral("Ruwa.exe"));
}

TEST_CASE("update manifest rejects archives above the configured limit", "[updates][manifest]")
{
    CHECK_FALSE(UpdateManifest::parse(validManifest(), 1024).has_value());
}

TEST_CASE("update manifest rejects traversal and duplicate destinations", "[updates][manifest]")
{
    QByteArray traversal = validManifest();
    traversal.replace("Main/Ruwa.exe", "../Ruwa.exe");
    CHECK_FALSE(UpdateManifest::parse(traversal, 64 * 1024 * 1024).has_value());

    QByteArray duplicate = validManifest();
    const QByteArray fileEntry = QByteArrayLiteral(R"(,
        {"source":"Shaders/a.glsl","target":"RUWA.EXE","size":1,"sha256":")")
        + QByteArray(64, 'b') + QByteArrayLiteral(R"("})");
    duplicate.replace(
        QByteArrayLiteral("        }],\n        \"delete\""),
        QByteArrayLiteral("        }") + fileEntry
            + QByteArrayLiteral("],\n        \"delete\""));
    CHECK_FALSE(UpdateManifest::parse(duplicate, 64 * 1024 * 1024).has_value());
}

TEST_CASE("update manifest rejects wrong product platform and invalid version", "[updates][manifest]")
{
    QByteArray wrongProduct = validManifest();
    wrongProduct.replace("\"product\":\"Ruwa\"", "\"product\":\"Other\"");
    CHECK_FALSE(UpdateManifest::parse(wrongProduct, 64 * 1024 * 1024).has_value());

    QByteArray wrongPlatform = validManifest();
    wrongPlatform.replace("\"architecture\":\"x86_64\"", "\"architecture\":\"arm64\"");
    CHECK_FALSE(UpdateManifest::parse(wrongPlatform, 64 * 1024 * 1024).has_value());

    QByteArray invalidVersion = validManifest();
    invalidVersion.replace("\"version\":\"0.2.10\"", "\"version\":\"0.02.10\"");
    CHECK_FALSE(UpdateManifest::parse(invalidVersion, 64 * 1024 * 1024).has_value());
}
