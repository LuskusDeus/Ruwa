// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/TileBrush.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
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
