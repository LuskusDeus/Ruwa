// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/TileBrush.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("pen-down records a dab before direction is available", "[brush][input][dynamics]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.angle = 37.0f;
    auto& directionBinding = settings.dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeAngle)
                                 .binding(BrushInputSourceKey::StrokeDirection);
    directionBinding.enabled = true;
    directionBinding.curve.points = {
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 360.0f, 1.0f },
    };
    directionBinding.curve.normalize(directionBinding.setting, directionBinding.mode);

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
