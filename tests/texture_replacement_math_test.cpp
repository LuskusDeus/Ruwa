// SPDX-License-Identifier: MPL-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "features/canvas/rendering/TextureReplacementMath.h"

TEST_CASE("Texture replacement preserves premultiplied FloodFillResult pixels")
{
    const aether::PremultipliedRgba base { 0.10f, 0.20f, 0.30f, 0.50f };
    const aether::PremultipliedRgba after { 0.32f, 0.08f, 0.16f, 0.40f };

    REQUIRE(aether::replacePremultipliedPixel(base, after, 0.0f) == base);
    REQUIRE(aether::replacePremultipliedPixel(base, after, 1.0f) == after);

    const auto half = aether::replacePremultipliedPixel(base, after, 0.5f);
    REQUIRE(half[0] == Catch::Approx(0.21f));
    REQUIRE(half[1] == Catch::Approx(0.14f));
    REQUIRE(half[2] == Catch::Approx(0.23f));
    REQUIRE(half[3] == Catch::Approx(0.45f));
}

TEST_CASE("Texture replacement clamps coverage without straight-alpha conversion")
{
    const aether::PremultipliedRgba transparent {};
    const aether::PremultipliedRgba translucent { 0.06f, 0.12f, 0.18f, 0.25f };

    REQUIRE(aether::replacePremultipliedPixel(transparent, translucent, 2.0f) == translucent);
    REQUIRE(aether::replacePremultipliedPixel(translucent, transparent, -1.0f) == translucent);
}
