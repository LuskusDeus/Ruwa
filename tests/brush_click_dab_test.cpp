// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/TileBrush.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
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

uint8_t strokeAlphaAt(const aether::TileBrush& brush, int worldX, int worldY)
{
    const aether::TileKey key
        = aether::worldToTile(static_cast<float>(worldX) + 0.5f, static_cast<float>(worldY) + 0.5f);
    const auto* tile = brush.strokeBuffer().getTile(key);
    if (!tile)
        return 0;
    float originX = 0.0f;
    float originY = 0.0f;
    aether::tileWorldOrigin(key, originX, originY);
    const uint32_t localX = static_cast<uint32_t>(worldX - static_cast<int>(originX));
    const uint32_t localY = static_cast<uint32_t>(worldY - static_cast<int>(originY));
    uint8_t r = 0, g = 0, b = 0, a = 0;
    tile->getPixel(localX, localY, r, g, b, a);
    return a;
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

TEST_CASE("the built-in dab keeps its conservative radius bound",
    "[brush][stroke][geometry][performance]")
{
    aether::TileBrush brush;
    brush.setDabType(0);
    brush.setDabXScale(0.25f);
    brush.setDabYScale(0.5f);
    brush.setDabRotation(37.0f);

    // Scale, roundness and rotation may shrink the circular footprint but can
    // never extend it beyond the radius. Raster padding is an independent pixel.
    CHECK(brush.dabCoverageExtent(12.0f, 0.4f, 0.2f, 73.0f)
        == Catch::Approx(12.0f));
    CHECK(brush.dabCoverageExtent(12.0f, 0.4f, 0.2f, 73.0f, true)
        == Catch::Approx(13.0f));
    CHECK(brush.dabRotationInvariantCoverageExtent(12.0f, 0.4f, 0.2f)
        == Catch::Approx(12.0f));
}

TEST_CASE(
    "a stretched dab spans the whole gap to the previous dab center", "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    auto renderTwoDabs = [](bool connectDabs) {
        BrushSettingsData settings;
        settings.connectDabs = connectDabs;
        settings.brushFeather = false;
        settings.hardness = 1.0f;

        aether::TileBrush brush;
        brush.setBrushSettings(settings);
        brush.setRadius(4.0f);
        brush.beginStroke();
        brush.recordDabPoint(16.0f, 32.0f);
        brush.recordDabPoint(80.0f, 32.0f);
        brush.rebuildStrokeBufferFromDabs();
        return std::array<uint8_t, 2> { strokeAlphaAt(brush, 48, 32),
            strokeAlphaAt(brush, 48, 38) };
    };

    const auto disconnected = renderTwoDabs(false);
    const auto connected = renderTwoDabs(true);

    CHECK(disconnected[0] == 0);
    CHECK(connected[0] > 0);
    CHECK(connected[1] == 0);
}

TEST_CASE("a stretched dab transforms the opaque content bounds, not transparent padding",
    "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    auto renderTwoDabs = [](bool connectDabs) {
        BrushSettingsData settings;
        settings.connectDabs = connectDabs;
        settings.brushFeather = false;
        settings.hardness = 1.0f;
        settings.dabType = 1;

        // The visible 3x3 square occupies only the center of the 9x9 dab
        // texture. Its canonical content bounds are [-0.25, 0.25], not the
        // texture bounds [-1, 1].
        std::array<uint8_t, 81> alpha {};
        for (int y = 3; y <= 5; ++y) {
            for (int x = 3; x <= 5; ++x) {
                alpha[static_cast<size_t>(y * 9 + x)] = 255;
            }
        }

        aether::TileBrush brush;
        brush.setBrushSettings(settings);
        brush.setDabShapeMask(alpha.data(), 9, 9);
        brush.setRadius(8.0f);
        brush.beginStroke();
        brush.recordDabPoint(16.0f, 32.0f);
        brush.recordDabPoint(48.0f, 32.0f);
        brush.rebuildStrokeBufferFromDabs();

        const auto bounds = brush.dabShapeContentBounds(1.0f);
        return std::pair { bounds, strokeAlphaAt(brush, 22, 33) };
    };

    const auto disconnected = renderTwoDabs(false);
    const auto connected = renderTwoDabs(true);

    CHECK(connected.first.minX == Catch::Approx(-0.25f));
    CHECK(connected.first.minY == Catch::Approx(-0.25f));
    CHECK(connected.first.maxX == Catch::Approx(0.25f));
    CHECK(connected.first.maxY == Catch::Approx(0.25f));
    CHECK(disconnected.second == 0);
    CHECK(connected.second > 0);
}

TEST_CASE("a stretched dab moves its trailing corners onto the previous dab's leading ones",
    "[brush][stroke][geometry]")
{
    aether::TileBrush brush;
    brush.setRoundness(0.5f);

    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;
    previous.roundness = 0.5f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;

    aether::TileBrush::DabQuad stretchQuad;
    REQUIRE(brush.dabStretchedQuad(previous, current, stretchQuad));
    CHECK(stretchQuad[0].x == Catch::Approx(14.0f));
    CHECK(stretchQuad[0].y == Catch::Approx(18.0f));
    CHECK(stretchQuad[1].x == Catch::Approx(34.0f));
    CHECK(stretchQuad[1].y == Catch::Approx(18.0f));
    CHECK(stretchQuad[2].x == Catch::Approx(34.0f));
    CHECK(stretchQuad[2].y == Catch::Approx(22.0f));
    CHECK(stretchQuad[3].x == Catch::Approx(14.0f));
    CHECK(stretchQuad[3].y == Catch::Approx(22.0f));
}

TEST_CASE("a stretched dab keeps corner identity when the shape is rotated across the travel",
    "[brush][stroke][geometry]")
{
    aether::TileBrush brush;
    brush.setRoundness(0.5f);

    // The dab is turned 90 degrees, so the stroke travels along the shape's own
    // y axis: the trailing pair is now the (maxX,maxY)/(minX,maxY) corners, and
    // both land on the previous dab's leading corners on their own side.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;
    previous.roundness = 0.5f;
    previous.angleDegrees = 90.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;

    aether::TileBrush::DabQuad stretchQuad;
    REQUIRE(brush.dabStretchedQuad(previous, current, stretchQuad));
    CHECK(stretchQuad[0].x == Catch::Approx(32.0f));
    CHECK(stretchQuad[0].y == Catch::Approx(16.0f));
    CHECK(stretchQuad[1].x == Catch::Approx(32.0f));
    CHECK(stretchQuad[1].y == Catch::Approx(24.0f));
    CHECK(stretchQuad[2].x == Catch::Approx(12.0f));
    CHECK(stretchQuad[2].y == Catch::Approx(24.0f));
    CHECK(stretchQuad[3].x == Catch::Approx(12.0f));
    CHECK(stretchQuad[3].y == Catch::Approx(16.0f));
}

TEST_CASE("a dab that rotated since its predecessor still anchors on that dab's leading edge",
    "[brush][stroke][geometry]")
{
    aether::TileBrush brush;

    // A sharp turn with the angle driven by stroke direction: the two dabs are
    // rotated 90 degrees apart, so reading the previous dab's corners through
    // the current dab's axes would anchor on a side edge of it and fold the
    // stretched shape. The joint has to sit on the previous dab's own leading
    // edge (x = 14) and stay parallel to the current dab's cross-section.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;
    previous.angleDegrees = 0.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;
    current.angleDegrees = 90.0f;

    aether::TileBrush::DabQuad stretchQuad;
    REQUIRE(brush.dabStretchedQuad(previous, current, stretchQuad));
    CHECK(stretchQuad[0].x == Catch::Approx(34.0f));
    CHECK(stretchQuad[0].y == Catch::Approx(16.0f));
    CHECK(stretchQuad[1].x == Catch::Approx(34.0f));
    CHECK(stretchQuad[1].y == Catch::Approx(24.0f));
    CHECK(stretchQuad[2].x == Catch::Approx(14.0f));
    CHECK(stretchQuad[2].y == Catch::Approx(24.0f));
    CHECK(stretchQuad[3].x == Catch::Approx(14.0f));
    CHECK(stretchQuad[3].y == Catch::Approx(16.0f));
}

TEST_CASE("a flat dab leads with its short edge, not with the axis it leans on",
    "[brush][stroke][geometry]")
{
    aether::TileBrush brush;

    // Half extents 8 x 2, travelling at 60 degrees: the dab leans more on its
    // y axis (sin 60 > cos 60), but its x edge still reaches 8 * cos 60 = 4
    // ahead against 2 * sin 60 = 1.7, so the joint belongs on the x edge. The
    // long top edge is a side edge here, and anchoring on it folds the ribbon.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 20.0f;
    previous.worldY = 20.0f;
    previous.radius = 8.0f;
    previous.roundness = 0.25f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;
    current.worldY = 20.0f + 10.0f * std::sqrt(3.0f);

    aether::TileBrush::DabQuad stretchQuad;
    REQUIRE(brush.dabStretchedQuad(previous, current, stretchQuad));
    CHECK(stretchQuad[0].x == Catch::Approx(28.0f));
    CHECK(stretchQuad[0].y == Catch::Approx(18.0f));
    CHECK(stretchQuad[3].x == Catch::Approx(28.0f));
    CHECK(stretchQuad[3].y == Catch::Approx(22.0f));
    CHECK(stretchQuad[1].x == Catch::Approx(38.0f));
    CHECK(stretchQuad[2].x == Catch::Approx(38.0f));
}

TEST_CASE("a refined joint sits midway between both dabs' edges", "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.refineDabJoints = true;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);

    // Three dabs on a straight line, half extent 4: the middle one's own quad
    // runs from x = 26 to x = 34, its predecessor leads at x = 14 and its
    // successor trails at x = 46. Both joints land halfway.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;
    aether::TileBrush::DabPoint next = previous;
    next.worldX = 50.0f;

    aether::TileBrush::DabQuad refined;
    REQUIRE(brush.dabStretchedQuad(previous, current, &next, refined));
    CHECK(refined[0].x == Catch::Approx(20.0f));
    CHECK(refined[3].x == Catch::Approx(20.0f));
    CHECK(refined[1].x == Catch::Approx(40.0f));
    CHECK(refined[2].x == Catch::Approx(40.0f));

    // Without refinement the same three dabs keep the previous behaviour: the
    // joint IS the predecessor's leading edge and the leading edge is the dab's
    // own, successor or not.
    settings.refineDabJoints = false;
    brush.setBrushSettings(settings);
    aether::TileBrush::DabQuad plain;
    REQUIRE(brush.dabStretchedQuad(previous, current, &next, plain));
    CHECK(plain[0].x == Catch::Approx(14.0f));
    CHECK(plain[1].x == Catch::Approx(34.0f));
}

TEST_CASE("a refined joint takes half of each dab's rotation", "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.refineDabJoints = true;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);

    // The current dab stands on a corner (45 degrees) while its successor is
    // upright, so their shared edge is what a sharp turn folds: the current
    // dab's leading edge runs (-4*sqrt2, 4*sqrt2) and the successor's trailing
    // edge runs (0, 8). The refined joint is the average of the two, so the
    // ribbon twists half as much at each of its two joints instead of taking
    // the whole turn on one of them.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;
    current.angleDegrees = 45.0f;
    aether::TileBrush::DabPoint next = previous;
    next.worldX = 50.0f;

    aether::TileBrush::DabQuad refined;
    REQUIRE(brush.dabStretchedQuad(previous, current, &next, refined));
    const float half = 2.0f * std::sqrt(2.0f);
    CHECK(refined[2].x - refined[1].x == Catch::Approx(-half).margin(0.001f));
    CHECK(refined[2].y - refined[1].y == Catch::Approx(half + 4.0f).margin(0.001f));

    // Unrefined, that same edge is the dab's own and carries the whole turn.
    settings.refineDabJoints = false;
    brush.setBrushSettings(settings);
    aether::TileBrush::DabQuad plain;
    REQUIRE(brush.dabStretchedQuad(previous, current, &next, plain));
    CHECK(plain[2].x - plain[1].x == Catch::Approx(-2.0f * half).margin(0.001f));
    CHECK(plain[2].y - plain[1].y == Catch::Approx(2.0f * half).margin(0.001f));
}

TEST_CASE("a refined joint stays shared through a right-angle turn", "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.refineDabJoints = true;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);

    // A 90 degree corner: the incoming joint claims the dab's minX edge and the
    // outgoing one its maxY edge, and those two share a corner. Whatever the
    // quad does about that, both dabs have to meet on the SAME two points -
    // dropping the outgoing refinement there tore the ribbon at every corner of
    // a zigzag while a smooth preview stroke never hit the case.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;
    aether::TileBrush::DabPoint next = current;
    next.worldY = 40.0f;

    const aether::TileBrush::DabJoint joint = brush.dabJointEdge(current, next);
    REQUIRE(joint.valid);

    aether::TileBrush::DabQuad leaving;
    aether::TileBrush::DabQuad arriving;
    REQUIRE(brush.dabStretchedQuad(previous, current, &next, leaving));
    REQUIRE(brush.dabStretchedQuad(current, next, nullptr, arriving));

    const auto carriesPoint = [](const aether::TileBrush::DabQuad& quad,
                                  const aether::Vector2& point) {
        for (const aether::Vector2& corner : quad) {
            if (std::abs(corner.x - point.x) < 0.001f && std::abs(corner.y - point.y) < 0.001f) {
                return true;
            }
        }
        return false;
    };
    for (const aether::Vector2& point : joint.points) {
        CHECK(carriesPoint(leaving, point));
        CHECK(carriesPoint(arriving, point));
    }

    // Cubic transform rails also share the derivative at that joint. They are
    // the exact cubic representation of adjacent quadratic dab-spline spans.
    aether::TileBrush::DabPoint following = next;
    following.worldY = 60.0f;
    aether::TileBrush::DabTransform leavingTransform;
    aether::TileBrush::DabTransform arrivingTransform;
    REQUIRE(brush.dabStretchedTransform(previous, current, &next, leavingTransform));
    REQUIRE(brush.dabStretchedTransform(current, next, &following, arrivingTransform));
    for (int side = 0; side < 2; ++side) {
        const int leavingEnd = side == 0 ? 1 : 2;
        const int arrivingStart = side == 0 ? 0 : 3;
        CHECK(leavingTransform.guide[leavingEnd].x
            == Catch::Approx(arrivingTransform.guide[arrivingStart].x).margin(0.001f));
        CHECK(leavingTransform.guide[leavingEnd].y
            == Catch::Approx(arrivingTransform.guide[arrivingStart].y).margin(0.001f));
        const aether::Vector2 leavingDerivative { leavingTransform.guide[leavingEnd].x
                - leavingTransform.endControls[side].x,
            leavingTransform.guide[leavingEnd].y - leavingTransform.endControls[side].y };
        const aether::Vector2 arrivingDerivative { arrivingTransform.startControls[side].x
                - arrivingTransform.guide[arrivingStart].x,
            arrivingTransform.startControls[side].y - arrivingTransform.guide[arrivingStart].y };
        CHECK(leavingDerivative.x == Catch::Approx(arrivingDerivative.x).margin(0.001f));
        CHECK(leavingDerivative.y == Catch::Approx(arrivingDerivative.y).margin(0.001f));
    }
}

TEST_CASE("a refined sharp turn keeps the quad corners in cyclic order",
    "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.refineDabJoints = true;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);

    // At 120 degrees the old nearest-point match preferred swapping the two
    // free corners. That shorter correspondence crossed edges 0-1 and 2-3,
    // producing the visible X exactly when the preference changed. The
    // topological match keeps each end of the outgoing joint on the same side
    // of the original cyclic quad.
    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;

    aether::TileBrush::DabPoint next = current;
    constexpr float kTurnDegrees = 120.0f;
    constexpr float kPi = 3.14159265358979323846f;
    const float turnRadians = kTurnDegrees * kPi / 180.0f;
    next.worldX += 20.0f * std::cos(turnRadians);
    next.worldY += 20.0f * std::sin(turnRadians);
    next.angleDegrees = kTurnDegrees;

    aether::TileBrush::DabQuad refined;
    REQUIRE(brush.dabStretchedQuad(previous, current, &next, refined));

    const auto side = [](const aether::Vector2& a, const aether::Vector2& b,
                          const aether::Vector2& point) {
        return (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
    };
    const auto properlyCross = [&side](const aether::Vector2& a, const aether::Vector2& b,
                                   const aether::Vector2& c, const aether::Vector2& d) {
        const float abC = side(a, b, c);
        const float abD = side(a, b, d);
        const float cdA = side(c, d, a);
        const float cdB = side(c, d, b);
        return abC * abD < 0.0f && cdA * cdB < 0.0f;
    };

    CHECK_FALSE(properlyCross(refined[0], refined[1], refined[2], refined[3]));
    CHECK_FALSE(properlyCross(refined[1], refined[2], refined[3], refined[0]));
    CHECK(refined[1].x > refined[2].x);
}

TEST_CASE("transform segments follow a smooth rotated rail without adding dabs",
    "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.transformSegments = 4;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);

    aether::TileBrush::DabPoint previous;
    previous.worldX = 10.0f;
    previous.worldY = 20.0f;
    previous.radius = 4.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 30.0f;
    current.angleDegrees = 90.0f;

    aether::TileBrush::DabTransform transform;
    REQUIRE(brush.dabStretchedTransform(previous, current, nullptr, transform));
    CHECK(brush.transformSegments() == 4);

    std::array<aether::TileBrush::DabQuad, 4> patches;
    for (int segment = 0; segment < 4; ++segment) {
        patches[segment] = aether::TileBrush::dabTransformSegmentQuad(transform, segment, 4);
    }
    for (int segment = 0; segment < 3; ++segment) {
        CHECK(patches[segment][1].x == Catch::Approx(patches[segment + 1][0].x));
        CHECK(patches[segment][1].y == Catch::Approx(patches[segment + 1][0].y));
        CHECK(patches[segment][2].x == Catch::Approx(patches[segment + 1][3].x));
        CHECK(patches[segment][2].y == Catch::Approx(patches[segment + 1][3].y));
    }

    // The cubic controls are real intermediate transforms, not a linear split
    // of the original quad. Consequently increasing the segment count improves
    // the curve approximation without recording another dab.
    const aether::Vector2 linearFirstControl { transform.guide[0].x
            + (transform.guide[1].x - transform.guide[0].x) / 3.0f,
        transform.guide[0].y + (transform.guide[1].y - transform.guide[0].y) / 3.0f };
    CHECK(std::hypot(transform.startControls[0].x - linearFirstControl.x,
              transform.startControls[0].y - linearFirstControl.y)
        > 0.01f);
    const aether::Vector2 linearMidpoint { (transform.guide[0].x + transform.guide[1].x) * 0.5f,
        (transform.guide[0].y + transform.guide[1].y) * 0.5f };
    CHECK(
        std::hypot(patches[1][1].x - linearMidpoint.x, patches[1][1].y - linearMidpoint.y) > 0.01f);

    brush.setConnectDabs(false);
    CHECK(brush.transformSegments() == 1);
}

TEST_CASE(
    "refined transform segments converge on the curved dab spline", "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.refineDabJoints = true;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);

    aether::TileBrush::DabPoint previous;
    previous.worldX = 0.0f;
    previous.worldY = 0.0f;
    previous.radius = 4.0f;

    aether::TileBrush::DabPoint current = previous;
    current.worldX = 20.0f;

    aether::TileBrush::DabPoint next = current;
    next.worldY = 20.0f;
    next.angleDegrees = 90.0f;

    aether::TileBrush::DabTransform transform;
    REQUIRE(brush.dabStretchedTransform(previous, current, &next, transform));

    constexpr float sampleT = 0.25f;
    const aether::Vector2 exact = aether::TileBrush::cubicRailPoint(transform.guide[0],
        transform.startControls[0], transform.endControls[0], transform.guide[1], sampleT);
    const auto approximateAt = [&](int segmentCount) {
        const float scaled = sampleT * static_cast<float>(segmentCount);
        const int segment = std::min(static_cast<int>(scaled), segmentCount - 1);
        const float local = scaled - static_cast<float>(segment);
        const aether::TileBrush::DabQuad patch
            = aether::TileBrush::dabTransformSegmentQuad(transform, segment, segmentCount);
        return aether::Vector2 { patch[0].x + (patch[1].x - patch[0].x) * local,
            patch[0].y + (patch[1].y - patch[0].y) * local };
    };
    const aether::Vector2 withTwo = approximateAt(2);
    const aether::Vector2 withTen = approximateAt(10);
    const float twoError = std::hypot(withTwo.x - exact.x, withTwo.y - exact.y);
    const float tenError = std::hypot(withTen.x - exact.x, withTen.y - exact.y);

    CHECK(twoError > 0.01f);
    CHECK(tenError < twoError);
}

TEST_CASE("segmented dab coordinates stay continuous along the transformed ribbon",
    "[brush][stroke][geometry]")
{
    aether::TileBrush::DabTransform transform;
    float progress = 0.0f;
    float canonicalX = 0.0f;
    float canonicalY = 0.0f;
    aether::TileBrush::dabTransformCoordinates(
        transform, 1, 4, 0.5f, 0.25f, progress, canonicalX, canonicalY);

    CHECK(progress == Catch::Approx(0.375f));
    CHECK(canonicalX == Catch::Approx(0.375f));
    CHECK(canonicalY == Catch::Approx(0.25f));
}

TEST_CASE(
    "joint refinement holds the newest dab back until the stroke ends", "[brush][stroke][geometry]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.refineDabJoints = true;
    settings.brushFeather = false;
    settings.hardness = 1.0f;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    brush.setRadius(4.0f);
    brush.beginStroke();
    brush.recordDabPoint(16.0f, 32.0f);
    brush.recordDabPoint(48.0f, 32.0f);
    brush.recordDabPoint(80.0f, 32.0f);
    brush.rebuildStrokeBufferFromDabs();

    // The last dab's leading edge is the joint with a dab that does not exist
    // yet, so it stays out of the stroke buffer.
    CHECK(brush.hasUnstampedStrokeDabs());
    CHECK(strokeAlphaAt(brush, 80, 32) == 0);
    CHECK(strokeAlphaAt(brush, 48, 32) > 0);

    aether::TileGrid grid;
    brush.endStroke(grid);

    const aether::TileKey key = aether::worldToTile(80.5f, 32.5f);
    const auto* tile = grid.getTile(key);
    REQUIRE(tile != nullptr);
    float originX = 0.0f;
    float originY = 0.0f;
    aether::tileWorldOrigin(key, originX, originY);
    uint8_t r = 0, g = 0, b = 0, a = 0;
    tile->getPixel(static_cast<uint32_t>(80 - static_cast<int>(originX)),
        static_cast<uint32_t>(32 - static_cast<int>(originY)), r, g, b, a);
    CHECK(a > 0);
}

TEST_CASE("connected dab range reports tiles touched only by the stretch back to the previous dab",
    "[brush][stroke][geometry][invalidation]")
{
    using namespace ruwa::core::brushes;

    BrushSettingsData settings;
    settings.connectDabs = true;
    settings.hardness = 1.0f;

    aether::TileBrush brush;
    brush.setBrushSettings(settings);
    brush.setRadius(4.0f);
    brush.beginStroke();
    brush.recordDabPoint(16.0f, 32.0f);
    brush.recordDabPoint(528.0f, 32.0f);

    std::unordered_set<aether::TileKey, aether::TileKeyHash> coveredTiles;
    brush.collectStrokeDabRangeCoveredTiles(1, 1, coveredTiles);

    // The second dab itself is in tile 2. Tile 1 is touched exclusively by
    // the stretchQuad from the preceding dab, so omitting it leaves the GPU
    // pixels hidden in the realtime composition until another dab dirties it.
    CHECK(coveredTiles.contains(aether::TileKey { 1, 0 }));
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
