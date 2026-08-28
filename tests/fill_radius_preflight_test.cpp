// SPDX-License-Identifier: MPL-2.0

// Preflight classification of fill radius estimates (plan 7.6.41): the policy
// reports semantic facts only — no UI, no widget dependency.

#include "features/fill/FillProgressivePolicy.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using ruwa::core::canvas::CanvasFillAlgorithm;
using ruwa::core::canvas::CanvasFillRequestStatus;

TEST_CASE("Fill radius preflight accepts estimates below the limit")
{
    const auto result
        = aether::classifyFillRadiusRequest(CanvasFillAlgorithm::Smart, 2999.0f, 3000.0f);

    REQUIRE(result.status == CanvasFillRequestStatus::Accepted);
    REQUIRE_FALSE(result.limit.has_value());
}

TEST_CASE("Fill radius preflight rejects estimates at the limit with radius facts")
{
    const auto result
        = aether::classifyFillRadiusRequest(CanvasFillAlgorithm::Smart, 3123.5f, 3000.0f);

    REQUIRE(result.status == CanvasFillRequestStatus::RejectedRegionTooLarge);
    REQUIRE(result.limit.has_value());
    REQUIRE(result.limit->algorithm == CanvasFillAlgorithm::Smart);
    REQUIRE(result.limit->estimatedRadiusDocumentPx == Catch::Approx(3123.5).margin(0.001));
    REQUIRE(result.limit->radiusLimitDocumentPx == Catch::Approx(3000.0).margin(0.001));
}

TEST_CASE("Fill radius preflight keeps the algorithm tag of the rejected request")
{
    const auto result
        = aether::classifyFillRadiusRequest(CanvasFillAlgorithm::Classic, 8000.0f, 8000.0f);

    REQUIRE(result.status == CanvasFillRequestStatus::RejectedRegionTooLarge);
    REQUIRE(result.limit.has_value());
    REQUIRE(result.limit->algorithm == CanvasFillAlgorithm::Classic);
}
