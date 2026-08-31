// SPDX-License-Identifier: MPL-2.0

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/canvas/clipboard/MergedSelectionExtractor.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TilePixelAccess.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace {

void setMaskCoverage(aether::TileGrid& mask, int worldX, int worldY, float coverage)
{
    constexpr int tileSize = static_cast<int>(aether::TILE_SIZE);
    const auto floorDiv = [](int value, int divisor) {
        const int quotient = value / divisor;
        return quotient - (value % divisor < 0 ? 1 : 0);
    };
    const int tileX = floorDiv(worldX, tileSize);
    const int tileY = floorDiv(worldY, tileSize);
    const uint32_t localX = static_cast<uint32_t>(worldX - tileX * tileSize);
    const uint32_t localY = static_cast<uint32_t>(worldY - tileY * tileSize);
    const float pixel[4] = { coverage, coverage, coverage, coverage };
    aether::writeTilePixelF(mask.getOrCreateTile({ tileX, tileY }), localX, localY, pixel);
}

void fillSurface(ruwa::shared::imaging::PixelSurface& surface, const std::array<float, 4>& pixel)
{
    std::vector<float> row(static_cast<size_t>(surface.width()) * 4u);
    for (int x = 0; x < surface.width(); ++x) {
        std::copy(pixel.begin(), pixel.end(), row.begin() + static_cast<size_t>(x) * 4u);
    }
    for (int y = 0; y < surface.height(); ++y) {
        surface.writeRowFloat(y, row.data());
    }
}

} // namespace

TEST_CASE("Merged selection extraction preserves negative document coordinates and soft coverage")
{
    using ruwa::shared::imaging::PixelAlpha;
    using ruwa::shared::imaging::PixelStorage;
    auto surface = ruwa::shared::imaging::PixelSurface::create(
        3, 2, PixelStorage::UInt8, PixelAlpha::Premultiplied);
    fillSurface(surface, { 1.0f, 0.0f, 0.0f, 1.0f });

    aether::TileGrid mask;
    mask.setFormat(aether::TilePixelFormat::RGBA8);
    setMaskCoverage(mask, -2, -1, 0.5f);
    setMaskCoverage(mask, 0, 0, 1.0f);

    QRect maskBounds;
    REQUIRE(aether::selectionMaskPixelBounds(mask, maskBounds));
    REQUIRE(maskBounds == QRect(QPoint(-2, -1), QPoint(0, 0)));

    auto extracted = aether::extractMergedSelectionPixels(
        surface, QRect(-2, -1, 3, 2), mask, aether::TilePixelFormat::RGBA8);
    REQUIRE(extracted);
    REQUIRE(extracted.contentBounds == QRect(QPoint(-2, -1), QPoint(0, 0)));

    const aether::TileData* softTile = extracted.pixels->getTile({ -1, -1 });
    REQUIRE(softTile);
    float softPixel[4];
    aether::readTilePixelF(*softTile, 254, 255, softPixel);
    REQUIRE(softPixel[0] == Catch::Approx(0.5f).margin(1.0f / 255.0f));
    REQUIRE(softPixel[3] == Catch::Approx(0.5f).margin(1.0f / 255.0f));

    const aether::TileData* opaqueTile = extracted.pixels->getTile({ 0, 0 });
    REQUIRE(opaqueTile);
    float opaquePixel[4];
    aether::readTilePixelF(*opaqueTile, 0, 0, opaquePixel);
    REQUIRE(opaquePixel[0] == Catch::Approx(1.0f));
    REQUIRE(opaquePixel[3] == Catch::Approx(1.0f));
}

TEST_CASE("Merged selection bounds handle uniform mask tiles without materializing them")
{
    aether::TileGrid mask;
    mask.setFormat(aether::TilePixelFormat::RGBA8);
    mask.getOrCreateTile({ -2, 1 }).setSolid(255, 255, 255, 128);

    QRect bounds;
    REQUIRE(aether::selectionMaskPixelBounds(mask, bounds));
    REQUIRE(bounds == QRect(-512, 256, 256, 256));
    REQUIRE(mask.getTile({ -2, 1 })->isSolid());

    aether::TileGrid outOfRangeMask;
    outOfRangeMask.setFormat(aether::TilePixelFormat::RGBA8);
    outOfRangeMask.getOrCreateTile({ std::numeric_limits<int>::max(), 0 })
        .setSolid(255, 255, 255, 255);
    REQUIRE_FALSE(aether::selectionMaskPixelBounds(outOfRangeMask, bounds));
    REQUIRE(bounds.isEmpty());
}

TEST_CASE("Merged selection extraction rejects empty, transparent, and mismatched inputs")
{
    using ruwa::shared::imaging::PixelAlpha;
    using ruwa::shared::imaging::PixelStorage;
    auto transparent = ruwa::shared::imaging::PixelSurface::create(
        2, 2, PixelStorage::UInt8, PixelAlpha::Premultiplied);

    aether::TileGrid mask;
    mask.setFormat(aether::TilePixelFormat::RGBA8);
    setMaskCoverage(mask, 0, 0, 1.0f);

    REQUIRE_FALSE(aether::extractMergedSelectionPixels(
        transparent, QRect(0, 0, 1, 1), mask, aether::TilePixelFormat::RGBA8));
    REQUIRE_FALSE(aether::extractMergedSelectionPixels(
        transparent, QRect(0, 0, 2, 2), mask, aether::TilePixelFormat::RGBA8));

    aether::TileGrid emptyMask;
    REQUIRE_FALSE(aether::extractMergedSelectionPixels(
        transparent, QRect(0, 0, 2, 2), emptyMask, aether::TilePixelFormat::RGBA8));

    auto straightAlpha = ruwa::shared::imaging::PixelSurface::create(
        1, 1, PixelStorage::UInt8, PixelAlpha::Straight);
    fillSurface(straightAlpha, { 1.0f, 0.0f, 0.0f, 1.0f });
    REQUIRE_FALSE(aether::extractMergedSelectionPixels(
        straightAlpha, QRect(0, 0, 1, 1), mask, aether::TilePixelFormat::RGBA8));
}

TEST_CASE("Merged selection extraction retains HDR values in deep tile formats")
{
    using ruwa::shared::imaging::PixelAlpha;
    using ruwa::shared::imaging::PixelStorage;
    auto surface = ruwa::shared::imaging::PixelSurface::create(
        1, 1, PixelStorage::Float32, PixelAlpha::Premultiplied);
    fillSurface(surface, { 2.0f, 0.5f, 0.25f, 1.0f });

    aether::TileGrid mask;
    mask.setFormat(aether::TilePixelFormat::RGBA8);
    setMaskCoverage(mask, 0, 0, 0.5f);

    auto extracted = aether::extractMergedSelectionPixels(
        surface, QRect(0, 0, 1, 1), mask, aether::TilePixelFormat::RGBA16F);
    REQUIRE(extracted);
    REQUIRE(extracted.pixels->format() == aether::TilePixelFormat::RGBA16F);

    const aether::TileData* tile = extracted.pixels->getTile({ 0, 0 });
    REQUIRE(tile);
    float pixel[4];
    aether::readTilePixelF(*tile, 0, 0, pixel);
    // The RGBA8 mask quantizes 0.5 to 128/255 before the HDR pixels are multiplied.
    constexpr float maskCoverage = 128.0f / 255.0f;
    REQUIRE(pixel[0] == Catch::Approx(2.0f * maskCoverage).margin(0.001f));
    REQUIRE(pixel[1] == Catch::Approx(0.5f * maskCoverage).margin(0.001f));
    REQUIRE(pixel[3] == Catch::Approx(maskCoverage).margin(0.001f));
}
