// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/DabShapeFalloff.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Custom dab hardness filters interior alpha", "[brush][dab][hardness]")
{
    using aether::dab_shape_falloff::softenAlpha;

    constexpr float sourceAlpha = 0.9f;
    constexpr float softAlpha = 0.3f;
    REQUIRE(softenAlpha(sourceAlpha, softAlpha, 1.0f) == Catch::Approx(sourceAlpha));
    REQUIRE(softenAlpha(sourceAlpha, softAlpha, 0.5f) == Catch::Approx(0.6f));
    REQUIRE(softenAlpha(sourceAlpha, softAlpha, 0.0f) == Catch::Approx(softAlpha));
}
