// SPDX-License-Identifier: MPL-2.0

#include "shared/tiles/TileBrush.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace {

using Pixel = std::array<uint8_t, 4>;
using ruwa::core::layers::BlendMode;
constexpr aether::TileKey kKey { -1, 2 };
constexpr Pixel kBrushColor { 128, 64, 32, 255 };

void prepareStroke(aether::TileBrush& brush, BlendMode mode, uint8_t opacity = 255)
{
    brush.setStrokeBlendMode(mode);
    brush.setColor(kBrushColor[0], kBrushColor[1], kBrushColor[2], opacity);
    brush.beginStroke();
    // Supply a known premultiplied stroke pixel to exercise production flattening
    // independently of dab shape, pressure dynamics and antialiasing.
    brush.strokeBuffer().getOrCreateTile(kKey).setPixel(
        0, 0, kBrushColor[0], kBrushColor[1], kBrushColor[2], kBrushColor[3]);
}

Pixel readPixel(const aether::TileGrid& grid, uint32_t x = 0)
{
    const auto* tile = grid.getTile(kKey);
    REQUIRE(tile != nullptr);
    Pixel result {};
    tile->getPixel(x, 0, result[0], result[1], result[2], result[3]);
    return result;
}

} // namespace

TEST_CASE(
    "Brush blend modes preserve paint color on a transparent layer", "[brush][blend][regression]")
{
    for (int mode = static_cast<int>(BlendMode::Normal);
        mode <= static_cast<int>(BlendMode::Luminosity); ++mode) {
        DYNAMIC_SECTION("mode " << mode)
        {
            aether::TileBrush brush;
            aether::TileGrid layer;
            prepareStroke(brush, static_cast<BlendMode>(mode));

            const auto affected = brush.endStroke(layer);

            CHECK(affected.count(kKey) == 1);
            CHECK(readPixel(layer) == kBrushColor);
            CHECK(readPixel(layer, 1) == Pixel { 0, 0, 0, 0 });
            CHECK(brush.strokeBuffer().empty());
        }
    }
}

TEST_CASE(
    "Multiply brush blends with existing pixels of its own layer", "[brush][blend][regression]")
{
    aether::TileBrush brush;
    aether::TileGrid layer;
    auto& tile = layer.getOrCreateTile(kKey);
    tile.setPixel(0, 0, 64, 32, 16, 128);
    tile.setPixel(1, 0, 20, 40, 60, 80);
    prepareStroke(brush, BlendMode::Multiply);

    brush.endStroke(layer);

    // Half-covered brown: Multiply affects covered paint; the transparent
    // fraction receives the brush color without any background contribution.
    CHECK(readPixel(layer) == Pixel { 96, 40, 18, 255 });
    CHECK(readPixel(layer, 1) == Pixel { 20, 40, 60, 80 });
}

TEST_CASE("Multiply brush uses previously committed strokes on the same layer",
    "[brush][blend][regression]")
{
    aether::TileBrush brush;
    aether::TileGrid layer;
    prepareStroke(brush, BlendMode::Multiply);
    brush.endStroke(layer);
    REQUIRE(readPixel(layer) == kBrushColor);

    prepareStroke(brush, BlendMode::Multiply);
    brush.endStroke(layer);

    CHECK(readPixel(layer) == Pixel { 64, 16, 4, 255 });
}

TEST_CASE(
    "Multiply brush respects stroke opacity and selection gating", "[brush][blend][regression]")
{
    aether::TileBrush brush;
    aether::TileGrid layer;
    layer.getOrCreateTile(kKey).setPixel(0, 0, 128, 64, 32, 255);

    SECTION("stroke opacity")
    {
        prepareStroke(brush, BlendMode::Multiply, 128);
        brush.endStroke(layer);
        CHECK(readPixel(layer) == Pixel { 96, 40, 18, 255 });
    }
    SECTION("selection coverage")
    {
        aether::TileGrid selection;
        selection.getOrCreateTile(kKey).setPixel(0, 0, 0, 0, 0, 128);
        prepareStroke(brush, BlendMode::Multiply);
        brush.endStroke(layer, false, &selection);
        CHECK(readPixel(layer) == Pixel { 96, 40, 18, 255 });
    }
}

TEST_CASE("Alpha-locked Multiply blends paint color without filling transparency",
    "[brush][blend][alpha-lock][regression]")
{
    aether::TileBrush brush;
    aether::TileGrid layer;
    layer.getOrCreateTile(kKey).setPixel(0, 0, 64, 32, 16, 128);
    prepareStroke(brush, BlendMode::Multiply);
    brush.strokeBuffer().getOrCreateTile(kKey).setPixel(1, 0, 128, 64, 32, 255);

    brush.endStroke(layer, true);

    CHECK(readPixel(layer) == Pixel { 32, 8, 2, 128 });
    CHECK(readPixel(layer, 1) == Pixel { 0, 0, 0, 0 });
}
