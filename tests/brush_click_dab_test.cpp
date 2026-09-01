// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/TileBrush.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

void enableBinding(ruwa::core::brushes::BrushSettingsData& settings,
    ruwa::core::brushes::BrushDynamicsSettingKey setting,
    ruwa::core::brushes::BrushInputSourceKey source)
{
    auto& binding = settings.dynamics.slotForSetting(setting).binding(source);
    binding.enabled = true;
    binding.curve.points = {
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
    };
    binding.curve.normalize(binding.setting, binding.mode);
}

void enableStrokeDirectionAngle(ruwa::core::brushes::BrushSettingsData& settings)
{
    using namespace ruwa::core::brushes;

    auto& binding = settings.dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeAngle)
                        .binding(BrushInputSourceKey::StrokeDirection);
    binding.enabled = true;
    binding.mode = BrushDynamicsBlendMode::Override;
    binding.curve.points = {
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 360.0f, 1.0f },
    };
    binding.curve.normalize(binding.setting, binding.mode);
}

} // namespace

TEST_CASE(
    "direct dab recording remains available to non-interactive callers", "[brush][input][dynamics]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.angle = 37.0f;
    enableStrokeDirectionAngle(settings);

    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    brush.beginStroke();

    const auto dab = brush.recordDabPoint(12.0f, 24.0f);

    REQUIRE(dab.alpha > 0);
    REQUIRE(brush.strokeDabs().size() == 1);
    CHECK(brush.strokeDabs().front().worldX == Catch::Approx(12.0f));
    CHECK(brush.strokeDabs().front().worldY == Catch::Approx(24.0f));
    // Direction is unknowable at pen-down, so the established dynamics
    // fallback keeps the base angle until a real segment supplies direction.
    CHECK(brush.strokeDabs().front().angleDegrees == Catch::Approx(settings.angle));
}

TEST_CASE(
    "only segment-derived dynamics defer the interactive first dab", "[brush][input][dynamics]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    CHECK_FALSE(brush.requiresMotionBeforeFirstDab());

    enableBinding(
        settings, BrushDynamicsSettingKey::ShapeAngle, BrushInputSourceKey::StrokeDirection);
    brush.setBrushSettings(settings);
    CHECK(brush.requiresMotionBeforeFirstDab());

    settings = {};
    enableBinding(
        settings, BrushDynamicsSettingKey::ShapeAngle, BrushInputSourceKey::StrokeDirection);
    settings.dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeAngle)
        .binding(BrushInputSourceKey::StrokeDirection)
        .enabled
        = false;
    brush.setBrushSettings(settings);
    CHECK_FALSE(brush.requiresMotionBeforeFirstDab());

    settings = {};
    enableBinding(
        settings, BrushDynamicsSettingKey::RadiusMultiplier, BrushInputSourceKey::StrokeSpeed);
    brush.setBrushSettings(settings);
    CHECK(brush.requiresMotionBeforeFirstDab());

    settings = {};
    enableBinding(settings, BrushDynamicsSettingKey::ShapeAngle, BrushInputSourceKey::PenTilt);
    brush.setBrushSettings(settings);
    CHECK_FALSE(brush.requiresMotionBeforeFirstDab());
}

TEST_CASE("segmentless dab recording preserves an established stroke direction",
    "[brush][input][dynamics][direction]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.spacing = 0.125f; // 8 px default radius -> 1 px dab step.
    enableStrokeDirectionAngle(settings);

    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    brush.beginStroke();

    std::vector<aether::TileBrush::DabPoint> movingDabs;
    brush.appendInterpolatedStrokeDabs(0.0f, 0.0f, 0.0f, 8.0f, 1.0f, 1.0f, movingDabs);
    REQUIRE(brush.hasInitializedStrokeDirection());
    REQUIRE_FALSE(movingDabs.empty());

    const auto stationaryDab = brush.recordDabPoint(0.0f, 8.0f);
    CHECK(stationaryDab.angleDegrees == Catch::Approx(90.0f).margin(0.01f));
}

TEST_CASE("host-stabilized direction affects dynamics without changing dab geometry",
    "[brush][input][dynamics][direction]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.spacing = 0.125f; // 8 px default radius -> 1 px dab step.
    enableStrokeDirectionAngle(settings);

    BrushInputDynamics stabilizedDirection;
    stabilizedDirection.strokeDirection = 0.25f; // 90 degrees.
    stabilizedDirection.strokeDirectionAvailable = true;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    brush.beginStroke();

    std::vector<aether::TileBrush::DabPoint> dabs;
    brush.appendInterpolatedStrokeDabs(0.0f, 0.0f, 4.0f, 0.0f, 1.0f, 1.0f, dabs, 0.0f, 0.01f, true,
        stabilizedDirection, stabilizedDirection);

    REQUIRE_FALSE(dabs.empty());
    for (const auto& dab : dabs) {
        CHECK(dab.worldY == Catch::Approx(0.0f));
        CHECK(dab.angleDegrees == Catch::Approx(90.0f).margin(0.01f));
    }

    // A segmentless re-evaluation and the cursor preview read the same stable
    // parameter, rather than falling back to the raw horizontal segment.
    const auto stationaryDab = brush.recordDabPoint(4.0f, 0.0f);
    CHECK(stationaryDab.angleDegrees == Catch::Approx(90.0f).margin(0.01f));
    CHECK(brush.previewDabRotationDeltaRadians() == Catch::Approx(1.57079632679f).margin(0.001f));
}

TEST_CASE("host-stabilized direction still uses the brush direction accumulator",
    "[brush][input][dynamics][direction]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.spacing = 0.125f; // 8 px default radius -> 1 px dab step.
    enableStrokeDirectionAngle(settings);

    BrushInputDynamics horizontal;
    horizontal.strokeDirection = 0.0f;
    horizontal.strokeDirectionAvailable = true;
    BrushInputDynamics vertical = horizontal;
    vertical.strokeDirection = 0.25f;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    brush.beginStroke();

    std::vector<aether::TileBrush::DabPoint> dabs;
    brush.appendInterpolatedStrokeDabs(
        0.0f, 0.0f, 8.0f, 0.0f, 1.0f, 1.0f, dabs, 0.0f, 0.01f, true, horizontal, horizontal);
    dabs.clear();

    // One residual direction change from the private stabilizer must enter the
    // established spatial accumulator, not override the next dab by 90 degrees.
    brush.appendInterpolatedStrokeDabs(
        8.0f, 0.0f, 9.0f, 0.0f, 1.0f, 1.0f, dabs, 0.01f, 0.02f, true, horizontal, vertical);

    REQUIRE(dabs.size() == 1);
    CHECK(dabs.front().angleDegrees < 20.0f);
}

TEST_CASE("stroke direction rejects isolated reverse jitter without weakening hard corners",
    "[brush][input][dynamics][direction]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.spacing = 0.125f; // 8 px default radius -> 1 px dab step.
    enableStrokeDirectionAngle(settings);

    SECTION("sub-pixel reverse packet cannot flip a drawable dab")
    {
        aether::TileBrush brush;
        brush.setBrushSettings(settings);
        brush.beginStroke();

        std::vector<aether::TileBrush::DabPoint> dabs;
        brush.appendInterpolatedStrokeDabs(0.0f, 0.0f, 8.0f, 0.0f, 1.0f, 1.0f, dabs);
        dabs.clear();
        // Leave the spacing carry just below one drawable interval.
        brush.appendInterpolatedStrokeDabs(8.0f, 0.0f, 8.95f, 0.0f, 1.0f, 1.0f, dabs);
        REQUIRE(dabs.empty());

        // The old hard-corner path treated this single reverse packet as a
        // decisive 180-degree turn and emitted a backward-facing dab.
        brush.appendInterpolatedStrokeDabs(8.95f, 0.0f, 8.85f, 0.0f, 1.0f, 1.0f, dabs);
        REQUIRE(dabs.size() == 1);
        CHECK(dabs.front().angleDegrees == Catch::Approx(0.0f).margin(1.0f));
    }

    SECTION("decisive corner still snaps within its first segment")
    {
        aether::TileBrush brush;
        brush.setBrushSettings(settings);
        brush.beginStroke();

        std::vector<aether::TileBrush::DabPoint> dabs;
        brush.appendInterpolatedStrokeDabs(0.0f, 0.0f, 8.0f, 0.0f, 1.0f, 1.0f, dabs);
        dabs.clear();
        brush.appendInterpolatedStrokeDabs(8.0f, 0.0f, 8.0f, 6.0f, 1.0f, 1.0f, dabs);

        REQUIRE_FALSE(dabs.empty());
        for (const auto& dab : dabs) {
            CHECK(dab.angleDegrees == Catch::Approx(90.0f).margin(0.01f));
        }
    }

    SECTION("short coherent corner confirms within one drawable interval")
    {
        aether::TileBrush brush;
        brush.setBrushSettings(settings);
        brush.beginStroke();

        std::vector<aether::TileBrush::DabPoint> dabs;
        brush.appendInterpolatedStrokeDabs(0.0f, 0.0f, 8.0f, 0.0f, 1.0f, 1.0f, dabs);
        dabs.clear();
        brush.appendInterpolatedStrokeDabs(8.0f, 0.0f, 8.0f, 0.6f, 1.0f, 1.0f, dabs);
        REQUIRE(dabs.empty());
        brush.appendInterpolatedStrokeDabs(8.0f, 0.6f, 8.0f, 1.2f, 1.0f, 1.0f, dabs);

        REQUIRE(dabs.size() == 1);
        CHECK(dabs.front().angleDegrees == Catch::Approx(90.0f).margin(0.01f));
    }
}
