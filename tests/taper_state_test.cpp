// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/StrokeTaperState.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("Stroke taper state calculates endpoint ranges", "[brush][taper]")
{
    const auto state = aether::stroke_taper::makeState(1000, 0.2f, 0.1f, true);

    REQUIRE(state.applicable);
    REQUIRE(state.dabCount == 1000);
    REQUIRE(state.startDabCount == 300);
    REQUIRE(state.endDabCount == 150);
    REQUIRE(state.endRangeStart() == 850);
    REQUIRE_FALSE(state.touchesWholeStroke());

    const auto overlapping = aether::stroke_taper::makeState(1000, 0.5f, 0.5f, true);
    REQUIRE(overlapping.touchesWholeStroke());
}

TEST_CASE("Dynamic taper replay remains stable across zero", "[brush][taper]")
{
    using aether::stroke_taper::requiresReplay;

    REQUIRE(requiresReplay(true, false, 0.0f, true, false));
    REQUIRE_FALSE(requiresReplay(true, true, 0.0f, true, false));
    REQUIRE(requiresReplay(true, true, 0.0f, false, true));
    REQUIRE_FALSE(requiresReplay(false, false, 0.0f, true, true));
}

TEST_CASE("A zero taper state restores the base scale", "[brush][taper]")
{
    const auto zeroState = aether::stroke_taper::makeState(4, 0.0f, 0.0f, true);

    REQUIRE_FALSE(zeroState.hasEffect());
    REQUIRE(zeroState.endRangeStart() == 4);
    for (std::size_t i = 0; i < zeroState.dabCount; ++i) {
        REQUIRE(aether::stroke_taper::scaleForDab(zeroState, i) == Catch::Approx(1.0f));
    }
}

TEST_CASE("Taper range update starts at new dabs or the moving tail", "[brush][taper]")
{
    using aether::stroke_taper::makeState;
    using aether::stroke_taper::updateRangeStart;

    const auto startOnly = makeState(1001, 0.2f, 0.0f, true);
    REQUIRE(updateRangeStart(startOnly, 1000, 1000) == 1000);

    const auto movingEnd = makeState(1001, 0.0f, 0.1f, true);
    REQUIRE(updateRangeStart(movingEnd, 1000, 850) == 850);

    REQUIRE(updateRangeStart(startOnly, 0, std::numeric_limits<std::size_t>::max()) == 0);
}
