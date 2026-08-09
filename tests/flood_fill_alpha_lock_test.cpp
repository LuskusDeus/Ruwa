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

uint8_t rawMaskAlphaAt(const FloodFillResult::RawTileMap& mask, uint32_t x, uint32_t y)
{
    const TileKey key { static_cast<int32_t>(x / TILE_SIZE), static_cast<int32_t>(y / TILE_SIZE) };
    const auto it = mask.find(key);
    if (it == mask.end()) {
        return 0;
    }
    const uint32_t localX = x % TILE_SIZE;
    const uint32_t localY = y % TILE_SIZE;
    return it->second[(localY * TILE_SIZE + localX) * TILE_CHANNELS + 3];
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

TEST_CASE("Alpha-locked masked fill cannot create opaque content", "[fill][lasso][alpha-lock]")
{
    TileGrid grid;

    const FloodFillResult result
        = fillMaskTiles(grid, singlePixelMask(), 255, 0, 0, 255, nullptr, true);

    REQUIRE(result.pixelsFilled == 0);
    REQUIRE(grid.empty());
}

TEST_CASE("Magic Wand mask selects only the connected matching-color region",
    "[fill][selection][magic-wand]")
{
    TileGrid grid;
    TileData& tile = grid.getOrCreateTile(TileKey { 0, 0 });
    tile.setPixel(0, 0, 255, 0, 0, 255);
    tile.setPixel(1, 0, 255, 0, 0, 255);
    tile.setPixel(2, 0, 0, 0, 255, 255);

    const auto mask = buildMagicWandSelectionMask(grid, 0, 0, 3, 1);

    REQUIRE(rawMaskAlphaAt(mask, 0, 0) == 255);
    REQUIRE(rawMaskAlphaAt(mask, 1, 0) == 255);
    REQUIRE(rawMaskAlphaAt(mask, 2, 0) == 0);

    const auto snapshotMask
        = buildMagicWandSelectionMask(snapshotContentTiles(grid), 0, 0, 3, 1, grid.format());
    REQUIRE(snapshotMask == mask);

    const auto classicFillResult = classicFloodFillRawTiles(
        snapshotContentTiles(grid), 0, 0, 0, 0, 0, 254, {}, 3, 1, grid.format());
    REQUIRE(classicFillResult.fillMaskTiles == mask);
}

TEST_CASE("Magic Wand uses exact classic fill matching instead of smart fill tolerance",
    "[fill][selection][magic-wand][classic]")
{
    TileGrid grid;
    TileData& tile = grid.getOrCreateTile(TileKey { 0, 0 });
    tile.setPixel(0, 0, 255, 0, 0, 255);
    tile.setPixel(1, 0, 254, 0, 0, 255);
    tile.setPixel(2, 0, 255, 0, 0, 255);

    const auto mask = buildMagicWandSelectionMask(grid, 0, 0, 3, 1);

    REQUIRE(rawMaskAlphaAt(mask, 0, 0) == 255);
    REQUIRE(rawMaskAlphaAt(mask, 1, 0) == 0);
    REQUIRE(rawMaskAlphaAt(mask, 2, 0) == 0);
}

TEST_CASE("Magic Wand mask can select transparent space around opaque content",
    "[fill][selection][magic-wand][transparent]")
{
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(1, 1, 0, 0, 0, 255);

    const auto mask = buildMagicWandSelectionMask(grid, 0, 0, 3, 3);

    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 3; ++x) {
            const uint8_t expected = (x == 1 && y == 1) ? 0 : 255;
            REQUIRE(rawMaskAlphaAt(mask, x, y) == expected);
        }
    }
}
