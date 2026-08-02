// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/scene/Camera2D.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float radians(float degrees)
{
    return degrees * kPi / 180.0f;
}

} // namespace

TEST_CASE("camera rotation smoothly snaps to a nearby increment")
{
    aether::Camera2D camera;
    camera.setRotation(radians(88.0f));

    REQUIRE(camera.snapRotationSmooth(radians(90.0f), radians(2.5f)));
    REQUIRE(camera.isAnimating());
    CHECK(camera.rotation() == radians(88.0f));

    camera.update(1.0f / 60.0f);
    CHECK(camera.rotation() > radians(88.0f));
    CHECK(camera.rotation() < radians(90.0f));

    for (int frame = 0; frame < 120; ++frame)
        camera.update(1.0f / 60.0f);

    CHECK_FALSE(camera.isAnimating());
    CHECK(std::abs(camera.rotation() - radians(90.0f)) < 0.0001f);
}

TEST_CASE("camera rotation remains free outside the snap capture distance")
{
    aether::Camera2D camera;
    camera.setRotation(radians(87.0f));

    CHECK_FALSE(camera.snapRotationSmooth(radians(90.0f), radians(2.5f)));
    CHECK_FALSE(camera.isAnimating());
    CHECK(camera.rotation() == radians(87.0f));
}

TEST_CASE("camera rotation snap crosses the zero angle by the shortest path")
{
    aether::Camera2D camera;
    camera.setRotation(radians(358.0f));

    REQUIRE(camera.snapRotationSmooth(radians(90.0f), radians(2.5f)));
    camera.update(1.0f / 60.0f);

    CHECK(camera.rotation() > radians(358.0f));
}
