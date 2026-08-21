// SPDX-License-Identifier: MPL-2.0

#include "features/fill/FloodFill.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

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

TEST_CASE("Soft selection fill replaces the silhouette instead of stacking alpha",
    "[fill][selection][soft-alpha]")
{
    // Content selection: the coverage IS the layer's own soft alpha.
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 64, 32, 16, 128);

    TileGrid selection;
    selection.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 128, 128, 128, 128);

    fillMaskTiles(grid, singlePixelMask(), 255, 0, 0, 255, &selection, false);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    // Alpha stays at the selected coverage (no build-up) and the color is fully
    // replaced: premultiplied red at 128/255.
    REQUIRE(a == 128);
    REQUIRE(r == 128);
    REQUIRE(g == 0);
    REQUIRE(b == 0);
}

TEST_CASE("Soft selection fill blends where the destination is denser than the coverage",
    "[fill][selection][soft-alpha]")
{
    // Antialiased selection rim over opaque content: the fill has to blend, and
    // the opaque pixel keeps its alpha.
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 0, 0, 0, 255);

    TileGrid selection;
    selection.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 128, 128, 128, 128);

    fillMaskTiles(grid, singlePixelMask(), 255, 255, 255, 255, &selection, false);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    REQUIRE(a == 255);
    REQUIRE(r == 128);
    REQUIRE(g == 128);
    REQUIRE(b == 128);
}

TEST_CASE("Fill outside the selection coverage is a no-op", "[fill][selection][soft-alpha]")
{
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 64, 32, 16, 128);

    TileGrid selection;
    selection.getOrCreateTile(TileKey { 0, 0 }).setPixel(1, 0, 255, 255, 255, 255);

    const FloodFillResult result
        = fillMaskTiles(grid, singlePixelMask(), 255, 0, 0, 255, &selection, false);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    REQUIRE(result.pixelsFilled == 0);
    REQUIRE(r == 64);
    REQUIRE(g == 32);
    REQUIRE(b == 16);
    REQUIRE(a == 128);
}

TEST_CASE("Soft selection fill stays continuous when the mask rounds off the content alpha",
    "[fill][selection][soft-alpha]")
{
    // A selection mask is a quantized copy of what it traces, so coverage sits
    // one step either side of the content alpha all along a soft gradient. Both
    // sides must land on the same result, otherwise the fill posterizes into
    // contour bands.
    auto fillPixelAlpha = [](uint8_t contentAlpha, uint8_t coverage) -> uint8_t {
        TileGrid grid;
        grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, contentAlpha, 0, 0, contentAlpha);

        TileGrid selection;
        selection.getOrCreateTile(TileKey { 0, 0 })
            .setPixel(0, 0, coverage, coverage, coverage, coverage);

        fillMaskTiles(grid, singlePixelMask(), 0, 0, 255, 255, &selection, false);

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
        grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);
        return b;
    };

    // Blue channel = how much of the fill actually landed (premultiplied).
    const uint8_t below = fillPixelAlpha(100, 99);
    const uint8_t equal = fillPixelAlpha(100, 100);
    const uint8_t above = fillPixelAlpha(100, 101);

    REQUIRE(std::abs(static_cast<int>(below) - static_cast<int>(equal)) <= 2);
    REQUIRE(std::abs(static_cast<int>(above) - static_cast<int>(equal)) <= 2);
}

TEST_CASE("Fully covered selection pixels composite exactly like an unclipped fill",
    "[fill][selection][soft-alpha]")
{
    // The clamp must not be reachable at coverage 255, otherwise every ordinary
    // selection fill with a partly transparent color would come out too thin.
    TileGrid grid;
    grid.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 0, 0, 0, 128);

    TileGrid selection;
    selection.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 255, 255, 255, 255);

    fillMaskTiles(grid, singlePixelMask(), 255, 255, 255, 128, &selection, false);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    REQUIRE(r == 128);
    REQUIRE(g == 128);
    REQUIRE(b == 128);
    REQUIRE(a == 192);
}

TEST_CASE("A partly transparent fill still fades out along a soft selection edge",
    "[fill][selection][soft-alpha]")
{
    // Coverage 50% with a 50% color on empty pixels must land at 25%: the
    // ceiling carries the fill's own alpha, it is not the raw coverage.
    TileGrid grid;

    TileGrid selection;
    selection.getOrCreateTile(TileKey { 0, 0 }).setPixel(0, 0, 128, 128, 128, 128);

    fillMaskTiles(grid, singlePixelMask(), 255, 255, 255, 128, &selection, false);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    grid.getTile(TileKey { 0, 0 })->getPixel(0, 0, r, g, b, a);

    REQUIRE(a == 64);
    // Premultiplied white: the color itself is unchanged, only its coverage.
    REQUIRE(r == 64);
    REQUIRE(g == 64);
    REQUIRE(b == 64);
}
