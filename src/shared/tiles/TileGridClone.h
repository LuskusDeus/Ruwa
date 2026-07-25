// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   G R I D   C L O N E
// ==========================================================================

#ifndef RUWA_CORE_TILES_TILEGRIDCLONE_H
#define RUWA_CORE_TILES_TILEGRIDCLONE_H

#include "TileGrid.h"

#include <cstring>
#include <memory>

namespace aether {

/**
 * @brief Deep-copy a grid, preserving format, default fill and solid tiles.
 *
 * TileData's const pixels() hands back a shared zero buffer for a uniform-color
 * ("solid") tile, so a memcpy-only clone silently turns such tiles transparent.
 * Layer masks are full of them (a hide-all background, a selection baked into
 * whole tiles), which is why the clipboard/mask paths clone through here.
 *
 * @p tileOffsetX / @p tileOffsetY move the copy by whole TILES (that is, by
 * tileOffset * TILE_SIZE document pixels). Whole tiles only: the clone stays
 * bit-exact because nothing is resampled.
 */
inline std::unique_ptr<TileGrid> cloneGridWithSolids(
    const TileGrid& source, int tileOffsetX = 0, int tileOffsetY = 0)
{
    auto clone = std::make_unique<TileGrid>();
    clone->setFormat(source.format());
    clone->setDefaultFillPacked(source.defaultFillPacked());

    const size_t tileBytes = tileByteSize(source.format());
    for (const auto& [key, tile] : source.tiles()) {
        const TileKey dstKey { key.x + tileOffsetX, key.y + tileOffsetY };
        TileData& dst = clone->getOrCreateTile(dstKey);
        if (tile.isSolid()) {
            dst.setSolidPacked(tile.solidColorPacked());
        } else {
            std::memcpy(dst.pixels(), tile.pixels(), tileBytes);
        }
        dst.markDirty();
    }

    return clone;
}

} // namespace aether

#endif // RUWA_CORE_TILES_TILEGRIDCLONE_H
