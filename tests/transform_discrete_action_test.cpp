// SPDX-License-Identifier: MPL-2.0

#include "features/transform/TransformController.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace aether;

namespace {
bool enterController(TransformController& controller)
{
    return controller.enter(QUuid::createUuid(), Rect { 0.0f, 0.0f, 100.0f, 80.0f });
}
} // namespace

TEST_CASE("rapid transform flips compose from their logical endpoints")
{
    TransformController controller;
    REQUIRE(enterController(controller));

    REQUIRE(controller.animateFlipHorizontal());
    REQUIRE(controller.animatedTargetState().scale.x == Catch::Approx(-1.0f));

    // A second command can arrive before the first rendered frame. It must
    // cancel the logical flip, not mirror an arbitrary intermediate scale.
    REQUIRE(controller.animateFlipHorizontal());
    REQUIRE(controller.animatedTargetState().scale.x == Catch::Approx(1.0f));
    REQUIRE_FALSE(controller.hasPendingDiscreteActionAnimation());
    REQUIRE(controller.state().isIdentity());
}

TEST_CASE("discrete rotation uses the flip duration and OutCubic easing")
{
    TransformController controller;
    REQUIRE(enterController(controller));
    constexpr float halfPi = 1.57079632679489661923f;

    REQUIRE(controller.animateRotationBy(halfPi));
    REQUIRE(controller.updateAnimation(TransformController::ACTION_ANIMATION_DURATION * 0.5f));

    // OutCubic(0.5) = 0.875.
    REQUIRE(controller.state().rotation == Catch::Approx(halfPi * 0.875f).epsilon(0.0001f));
    REQUIRE(controller.hasPendingDiscreteActionAnimation());

    REQUIRE(controller.updateAnimation(TransformController::ACTION_ANIMATION_DURATION * 0.5f));
    REQUIRE(controller.state().rotation == Catch::Approx(halfPi).epsilon(0.0001f));
    REQUIRE_FALSE(controller.hasPendingDiscreteActionAnimation());
}

TEST_CASE("rapid quarter turns accumulate and full turns canonicalize to identity")
{
    TransformController controller;
    REQUIRE(enterController(controller));
    constexpr float halfPi = 1.57079632679489661923f;

    REQUIRE(controller.animateRotationBy(halfPi));
    REQUIRE(controller.animateRotationBy(-halfPi));
    REQUIRE_FALSE(controller.hasPendingDiscreteActionAnimation());
    REQUIRE(controller.state().isIdentity());

    REQUIRE(controller.animateRotationBy(halfPi));
    REQUIRE(controller.animateRotationBy(halfPi));
    REQUIRE(controller.animateRotationBy(halfPi));
    REQUIRE(controller.animateRotationBy(halfPi));
    REQUIRE_FALSE(controller.hasPendingDiscreteActionAnimation());
    REQUIRE(controller.state().rotation == Catch::Approx(0.0f).margin(0.0001f));
    REQUIRE(controller.state().isIdentity());
}
