// SPDX-License-Identifier: MPL-2.0

#include "features/fill/FloodFill.h"

#include <catch2/catch_test_macros.hpp>

namespace {

using namespace aether;

FloodFillResult::RawTileMap singlePixelMask()
{
    FloodFillResult::RawTileMap maskTiles;
    std::vector<uint8_t>& tile = maskTiles[TileKey { 0, 0 }];
    tile.assign(TILE_BYTE_SIZE, 0);
    tile[0] = 255;
    tile[1] = 255;
    tile[2] = 255;
    tile[3] = 255;
    return maskTiles;
}

} // namespace

TEST_CASE("Masked fill preserves destination alpha when alpha lock is enabled",
    "[fill][lasso][alpha-lock]")
{
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 32, 64, 96, 128);

    const FloodFillResult result
        = fillMaskTiles(grid, singlePixelMask(), 200, 100, 50, 128, nullptr, true);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    REQUIRE(result.pixelsFilled == 1);
    REQUIRE(r == 66);
    REQUIRE(g == 57);
    REQUIRE(b == 60);
    REQUIRE(a == 128);
}

TEST_CASE("Alpha-locked masked fill scales color coverage by the selection",
    "[fill][lasso][alpha-lock][selection]")
{
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 32, 64, 96, 128);

    TileGrid selection;
    selection.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 128, 128, 128, 128);

    fillMaskTiles(grid, singlePixelMask(), 200, 100, 50, 128, &selection, true);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    REQUIRE(r == 49);
    REQUIRE(g == 61);
    REQUIRE(b == 78);
    REQUIRE(a == 128);
}

TEST_CASE("Alpha-locked masked fill cannot create opaque content",
    "[fill][lasso][alpha-lock]")
{
    TileGrid grid;

    const FloodFillResult result
        = fillMaskTiles(grid, singlePixelMask(), 255, 0, 0, 255, nullptr, true);

    REQUIRE(result.pixelsFilled == 0);
    REQUIRE(grid.empty());
}
