// SPDX-License-Identifier: MPL-2.0

#include "services/updates/SemanticVersion.h"

#include <catch2/catch_test_macros.hpp>

using ruwa::services::SemanticVersion;

namespace {

int compare(const QString& left, const QString& right)
{
    const auto leftVersion = SemanticVersion::parse(left);
    const auto rightVersion = SemanticVersion::parse(right);
    REQUIRE(leftVersion.has_value());
    REQUIRE(rightVersion.has_value());
    return leftVersion->comparePrecedence(*rightVersion);
}

} // namespace

TEST_CASE("semantic versions use segment numeric precedence", "[updates][semver]")
{
    CHECK(compare(QStringLiteral("0.2.10"), QStringLiteral("0.2.3")) > 0);
    CHECK(compare(QStringLiteral("0.1.8"), QStringLiteral("0.1.75")) < 0);
    CHECK(compare(QStringLiteral("v1.20.0"), QStringLiteral("1.3.999")) > 0);
}

TEST_CASE("semantic versions implement prerelease precedence", "[updates][semver]")
{
    CHECK(compare(QStringLiteral("0.2.3"), QStringLiteral("0.2.3-alpha")) > 0);
    CHECK(compare(QStringLiteral("0.2.3-alpha.10"), QStringLiteral("0.2.3-alpha.2")) > 0);
    CHECK(compare(QStringLiteral("1.0.0-alpha"), QStringLiteral("1.0.0-beta")) < 0);
    CHECK(compare(QStringLiteral("1.0.0-beta"), QStringLiteral("1.0.0-beta.2")) < 0);
    CHECK(compare(QStringLiteral("1.0.0-beta.11"), QStringLiteral("1.0.0-rc.1")) < 0);
}

TEST_CASE("semantic version build metadata does not affect precedence", "[updates][semver]")
{
    CHECK(compare(QStringLiteral("1.2.3+build.1"), QStringLiteral("1.2.3+build.999")) == 0);
}

TEST_CASE("semantic version parser rejects malformed input", "[updates][semver]")
{
    const QStringList invalidVersions { QString(), QStringLiteral("1"), QStringLiteral("1.2"),
        QStringLiteral("1.2.3.4"), QStringLiteral("01.2.3"), QStringLiteral("1.02.3"),
        QStringLiteral("1.2.03"), QStringLiteral("1.2.3-"), QStringLiteral("1.2.3-alpha..1"),
        QStringLiteral("1.2.3-alpha.01"), QStringLiteral("1.2.3+"),
        QStringLiteral("1.2.3+build_1"), QStringLiteral("garbage-1.2.3") };
    for (const QString& version : invalidVersions) {
        CAPTURE(version.toStdString());
        CHECK_FALSE(SemanticVersion::parse(version).has_value());
    }
}

TEST_CASE("semantic version comparison does not overflow fixed integers", "[updates][semver]")
{
    CHECK(compare(QStringLiteral("999999999999999999999999.0.0"),
              QStringLiteral("999999999999999999999998.999.999"))
        > 0);
}
