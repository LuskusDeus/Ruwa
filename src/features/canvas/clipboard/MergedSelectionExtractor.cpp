// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/clipboard/MergedSelectionExtractor.h"

#include "shared/tiles/TilePixelAccess.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace aether {

namespace {

int floorDiv(int value, int divisor)
{
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

} // namespace

bool selectionMaskPixelBounds(const TileGrid& selectionMask, QRect& outBounds)
{
    outBounds = {};
    bool found = false;
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (const auto& [key, tile] : selectionMask.tiles()) {
        if (tile.isEmpty()) {
            continue;
        }
        const qint64 wideOriginX = static_cast<qint64>(key.x) * TILE_SIZE;
        const qint64 wideOriginY = static_cast<qint64>(key.y) * TILE_SIZE;
        if (wideOriginX < std::numeric_limits<int>::min()
            || wideOriginY < std::numeric_limits<int>::min()
            || wideOriginX + TILE_SIZE - 1u > std::numeric_limits<int>::max()
            || wideOriginY + TILE_SIZE - 1u > std::numeric_limits<int>::max()) {
            continue;
        }
        const int tileOriginX = static_cast<int>(wideOriginX);
        const int tileOriginY = static_cast<int>(wideOriginY);
        if (tile.isSolid()) {
            float coverage[4];
            readTilePixelF(tile, 0, 0, coverage);
            if (coverage[3] > 0.0f) {
                minX = std::min(minX, tileOriginX);
                minY = std::min(minY, tileOriginY);
                maxX = std::max(maxX, tileOriginX + static_cast<int>(TILE_SIZE) - 1);
                maxY = std::max(maxY, tileOriginY + static_cast<int>(TILE_SIZE) - 1);
                found = true;
            }
            continue;
        }
        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                float coverage[4];
                readTilePixelF(tile, localX, localY, coverage);
                if (coverage[3] <= 0.0f) {
                    continue;
                }
                const int worldX = tileOriginX + static_cast<int>(localX);
                const int worldY = tileOriginY + static_cast<int>(localY);
                minX = std::min(minX, worldX);
                minY = std::min(minY, worldY);
                maxX = std::max(maxX, worldX);
                maxY = std::max(maxY, worldY);
                found = true;
            }
        }
    }

    if (!found) {
        return false;
    }
    outBounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    return true;
}

MergedSelectionExtraction extractMergedSelectionPixels(
    const ruwa::shared::imaging::PixelSurface& composite, const QRect& surfaceBounds,
    const TileGrid& selectionMask, TilePixelFormat outputFormat)
{
    MergedSelectionExtraction result;
    if (composite.isNull()
        || composite.alphaMode() != ruwa::shared::imaging::PixelAlpha::Premultiplied
        || surfaceBounds.isEmpty() || composite.size() != surfaceBounds.size()
        || selectionMask.empty()) {
        return result;
    }

    std::unique_ptr<TileGrid> copied;
    std::vector<float> compositeRow;
    const size_t rowWidth = static_cast<size_t>(surfaceBounds.width());
    if (rowWidth > compositeRow.max_size() / 4u) {
        return result;
    }
    try {
        copied = std::make_unique<TileGrid>();
        compositeRow.resize(rowWidth * 4u);
    } catch (const std::bad_alloc&) {
        return result;
    } catch (const std::length_error&) {
        return result;
    }
    copied->setFormat(outputFormat);

    constexpr int tileSize = static_cast<int>(TILE_SIZE);
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (int sourceY = 0; sourceY < composite.height(); ++sourceY) {
        const int worldY = surfaceBounds.top() + sourceY;
        composite.readRowFloat(sourceY, compositeRow.data());
        const int tileY = floorDiv(worldY, tileSize);
        const uint32_t localY = static_cast<uint32_t>(worldY - tileY * tileSize);

        int worldX = surfaceBounds.left();
        while (worldX <= surfaceBounds.right()) {
            const int tileX = floorDiv(worldX, tileSize);
            const int tileOriginX = tileX * tileSize;
            const int segmentRight = std::min(surfaceBounds.right(), tileOriginX + tileSize - 1);
            const TileKey key { tileX, tileY };
            const TileData* maskTile = selectionMask.getTile(key);
            TileData* destinationTile = nullptr;

            if (maskTile) {
                for (int x = worldX; x <= segmentRight; ++x) {
                    const uint32_t localX = static_cast<uint32_t>(x - tileOriginX);
                    float coverage[4];
                    readTilePixelF(*maskTile, localX, localY, coverage);
                    if (coverage[3] <= 0.0f) {
                        continue;
                    }

                    const size_t sourceOffset = static_cast<size_t>(x - surfaceBounds.left()) * 4u;
                    const float* source = compositeRow.data() + sourceOffset;
                    const float output[4] = { source[0] * coverage[3], source[1] * coverage[3],
                        source[2] * coverage[3], source[3] * coverage[3] };
                    if (output[3] <= 0.0f) {
                        continue;
                    }

                    try {
                        if (!destinationTile) {
                            destinationTile = &copied->getOrCreateTile(key);
                        }
                        writeTilePixelF(*destinationTile, localX, localY, output);
                    } catch (const std::bad_alloc&) {
                        return {};
                    } catch (const std::length_error&) {
                        return {};
                    }
                    minX = std::min(minX, x);
                    minY = std::min(minY, worldY);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, worldY);
                }
            }
            if (segmentRight == surfaceBounds.right()) {
                break;
            }
            worldX = segmentRight + 1;
        }
    }

    copied->pruneEmpty();
    if (copied->empty() || minX > maxX || minY > maxY) {
        return result;
    }

    result.contentBounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    result.pixels = std::move(copied);
    return result;
}

} // namespace aether
