// SPDX-License-Identifier: MPL-2.0

#include "features/transform/TransformSnapSession.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace aether;

namespace {
SnapScene finiteCanvasScene()
{
    SnapScene scene;
    scene.canvasSize = { 100.0f, 80.0f };
    scene.finiteCanvas = true;
    return scene;
}
} // namespace

TEST_CASE("transform snap canvas exposes center and finite edges")
{
    SnapSettings settings;
    const auto bounded = TransformSnapSolver::pointCandidates(
        finiteCanvasScene(), settings, { 48.0f, 38.0f }, {}, true);
    REQUIRE_FALSE(bounded.empty());
    REQUIRE(std::any_of(bounded.begin(), bounded.end(), [](const SnapRelation& relation) {
        return relation.axis == SnapAxis::X && relation.targetCoordinate == 50.0f;
    }));
    REQUIRE(std::any_of(bounded.begin(), bounded.end(), [](const SnapRelation& relation) {
        return relation.axis == SnapAxis::X && relation.targetCoordinate == 0.0f;
    }));

    SnapScene infinite = finiteCanvasScene();
    infinite.finiteCanvas = false;
    const auto unbounded
        = TransformSnapSolver::pointCandidates(infinite, settings, { 48.0f, 38.0f }, {}, true);
    REQUIRE(std::none_of(unbounded.begin(), unbounded.end(), [](const SnapRelation& relation) {
        return relation.targetCoordinate == 0.0f || relation.targetCoordinate == 100.0f
            || relation.targetCoordinate == 80.0f;
    }));

    SnapScene oddCanvas = finiteCanvasScene();
    oddCanvas.canvasSize = { 101.0f, 81.0f };
    const auto oddCandidates
        = TransformSnapSolver::pointCandidates(oddCanvas, settings, { 50.0f, 40.0f }, {}, true);
    REQUIRE(
        std::any_of(oddCandidates.begin(), oddCandidates.end(), [](const SnapRelation& relation) {
            return relation.axis == SnapAxis::X && relation.targetCoordinate == 50.5f;
        }));
}

TEST_CASE("transform snap hysteresis captures at eight and releases after fourteen screen pixels")
{
    SnapSettings settings;
    TransformSnapSession session(settings, finiteCanvasScene(), SnapCoordinatePolicy::Continuous);

    REQUIRE(session.solvePoint({ 92.0f, 40.0f }, nullptr, 1.0f, true).xRelation.has_value());
    REQUIRE(session.solvePoint({ 87.0f, 40.0f }, nullptr, 1.0f, true).xRelation.has_value());
    REQUIRE_FALSE(session.solvePoint({ 85.0f, 40.0f }, nullptr, 1.0f, true).xRelation.has_value());
}

TEST_CASE("transform snap thresholds are measured through zoomed and rotated viewport")
{
    SnapSettings settings;
    Viewport viewport(400, 300);
    viewport.camera().setZoom(2.0f);
    viewport.camera().setRotation(1.57079632679f);

    TransformSnapSession atThreshold(
        settings, finiteCanvasScene(), SnapCoordinatePolicy::Continuous);
    TransformSnapSession outsideThreshold(
        settings, finiteCanvasScene(), SnapCoordinatePolicy::Continuous);

    REQUIRE(atThreshold.solvePoint({ 46.1f, 40.0f }, &viewport, 2.0f, true).xRelation.has_value());
    REQUIRE_FALSE(
        outsideThreshold.solvePoint({ 45.8f, 40.0f }, &viewport, 2.0f, true).xRelation.has_value());
}

TEST_CASE("latched transform relation does not switch to a nearby target")
{
    SnapSettings settings;
    settings.canvasEnabled = false;
    settings.equalSpacingEnabled = false;
    const QUuid firstId = QUuid::createUuid();
    SnapScene scene;
    scene.targets = {
        { { 100.0f, 0.0f, 10.0f, 10.0f }, firstId, {}, SnapTargetType::Raster },
        { { 104.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession session(settings, scene, SnapCoordinatePolicy::Continuous);

    const SnapResult captured = session.solvePoint({ 92.0f, 5.0f }, nullptr, 1.0f);
    REQUIRE(captured.xRelation.has_value());
    REQUIRE(captured.xRelation->targetId == firstId);
    const SnapResult held = session.solvePoint({ 95.0f, 5.0f }, nullptr, 1.0f);
    REQUIRE(held.xRelation.has_value());
    REQUIRE(held.xRelation->targetId == firstId);
}

TEST_CASE("same-parent targets win deterministic transform snap ties")
{
    SnapSettings settings;
    settings.canvasEnabled = false;
    settings.equalSpacingEnabled = false;
    const QUuid parentId = QUuid::createUuid();
    const QUuid preferredId = QUuid::createUuid();
    SnapScene scene;
    scene.targets = {
        { { 100.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), QUuid::createUuid(),
            SnapTargetType::Raster },
        { { 100.0f, 0.0f, 10.0f, 10.0f }, preferredId, parentId, SnapTargetType::Raster },
    };
    TransformSnapSession session(settings, scene, SnapCoordinatePolicy::Continuous, parentId);

    const SnapResult result = session.solvePoint({ 94.0f, 5.0f }, nullptr, 1.0f);
    REQUIRE(result.xRelation.has_value());
    REQUIRE(result.xRelation->targetId == preferredId);
    REQUIRE(result.xRelations.size() >= 2);
}

TEST_CASE("exact snap relations override pixel-aligned free movement")
{
    SnapSettings settings;
    TransformSnapSession pixel(settings, finiteCanvasScene(), SnapCoordinatePolicy::PixelAligned);

    // An odd-width aggregate bounds, typical for a multi-layer selection, has a half-pixel
    // center. It must still align exactly to the even-width canvas center.
    const Rect multiLayerBounds { 39.0f, 30.0f, 21.0f, 20.0f };
    const SnapResult result = pixel.solveMove(multiLayerBounds, nullptr, 1.0f);
    REQUIRE(result.xRelation.has_value());
    REQUIRE(result.xRelation->sourceAnchor == SnapAnchor::Center);
    REQUIRE(result.xRelation->targetAnchor == SnapAnchor::Center);
    REQUIRE(result.correction.x == Catch::Approx(0.5f));
    REQUIRE(multiLayerBounds.center().x + result.correction.x == Catch::Approx(50.0f));

    // Pixel alignment remains a free-movement policy. Point-based resize and deform handles are
    // continuous as before.
    REQUIRE(pixel.solvePoint({ 99.5f, 40.0f }, nullptr, 1.0f, true).xRelation.has_value());
}

TEST_CASE("equal spacing prefers a two-sided symmetric candidate")
{
    SnapSettings settings;
    SnapScene scene;
    scene.targets = {
        { { 0.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 40.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession session(settings, scene, SnapCoordinatePolicy::Continuous);
    const SnapResult result = session.solveMove({ 24.0f, 0.0f, 10.0f, 10.0f }, nullptr, 1.0f);

    REQUIRE(result.xRelation.has_value());
    REQUIRE(result.xRelation->type == SnapRelationType::EqualSpacing);
    REQUIRE(result.xRelation->confirmationCount >= 2);
    REQUIRE(result.correction.x == Catch::Approx(-4.0f));
    REQUIRE(result.visualState.spacingDimensions.size() == 2);
    REQUIRE_FALSE(result.visualState.labels.empty());
    REQUIRE(result.visualState.labels.front().text == QStringLiteral("10 px"));
}

TEST_CASE("spacing metric trims fractional trailing zeros")
{
    SnapSettings settings;
    SnapScene scene;
    scene.targets = {
        { { 0.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 41.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession session(settings, scene, SnapCoordinatePolicy::Continuous);
    const SnapResult result = session.solveMove({ 24.5f, 0.0f, 10.0f, 10.0f }, nullptr, 1.0f);

    REQUIRE(result.xRelation.has_value());
    REQUIRE(result.xRelation->type == SnapRelationType::EqualSpacing);
    REQUIRE_FALSE(result.visualState.labels.empty());
    REQUIRE(result.visualState.labels.front().text == QStringLiteral("10.5 px"));
}

TEST_CASE("equal spacing recognizes a repeated horizontal and vertical row gap")
{
    SnapSettings settings;
    SnapScene horizontal;
    horizontal.targets = {
        { { 0.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 20.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 40.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession horizontalSession(settings, horizontal, SnapCoordinatePolicy::Continuous);
    const SnapResult horizontalResult
        = horizontalSession.solveMove({ 62.0f, 0.0f, 10.0f, 10.0f }, nullptr, 1.0f);
    REQUIRE(horizontalResult.xRelation.has_value());
    REQUIRE(horizontalResult.xRelation->type == SnapRelationType::EqualSpacing);
    REQUIRE(horizontalResult.xRelation->confirmationCount >= 2);
    REQUIRE(horizontalResult.correction.x == Catch::Approx(-2.0f));

    SnapScene vertical;
    vertical.targets = {
        { { 0.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 0.0f, 20.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 0.0f, 40.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession verticalSession(settings, vertical, SnapCoordinatePolicy::Continuous);
    const SnapResult verticalResult
        = verticalSession.solveMove({ 0.0f, 62.0f, 10.0f, 10.0f }, nullptr, 1.0f);
    REQUIRE(verticalResult.yRelation.has_value());
    REQUIRE(verticalResult.yRelation->type == SnapRelationType::EqualSpacing);
    REQUIRE(verticalResult.yRelation->confirmationCount >= 2);
    REQUIRE(verticalResult.correction.y == Catch::Approx(-2.0f));
}

TEST_CASE("equal spacing visualizes every gap in the snapped chain")
{
    SnapSettings settings;
    SnapScene scene;
    scene.targets = {
        { { 0.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 30.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 60.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession session(settings, scene, SnapCoordinatePolicy::Continuous);
    const SnapResult result = session.solveMove({ 92.0f, 0.0f, 10.0f, 10.0f }, nullptr, 1.0f);

    REQUIRE(result.xRelation.has_value());
    REQUIRE(result.xRelation->type == SnapRelationType::EqualSpacing);
    REQUIRE(result.correction.x == Catch::Approx(-2.0f));
    REQUIRE(result.visualState.spacingDimensions.size() == 3);
    REQUIRE(result.visualState.labels.size() == 3);
    for (const SnapSpacingDimension& dimension : result.visualState.spacingDimensions) {
        REQUIRE(dimension.value == Catch::Approx(20.0f));
    }
    for (const SnapMetricLabel& label : result.visualState.labels) {
        REQUIRE(label.text == QStringLiteral("20 px"));
    }
}

TEST_CASE("zero equal spacing has no dimension or metric label")
{
    SnapSettings settings;
    SnapScene scene;
    scene.targets = {
        { { 0.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
        { { 20.0f, 0.0f, 10.0f, 10.0f }, QUuid::createUuid(), {}, SnapTargetType::Raster },
    };
    TransformSnapSession session(settings, scene, SnapCoordinatePolicy::Continuous);
    const SnapResult result = session.solveMove({ 10.0f, 0.0f, 10.0f, 10.0f }, nullptr, 1.0f);

    REQUIRE(result.correction.x == Catch::Approx(0.0f));
    REQUIRE(result.visualState.spacingDimensions.empty());
    REQUIRE(result.visualState.labels.empty());
}
