// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   G R I D   R E M A P
// ==========================================================================

#ifndef RUWA_CORE_CANVAS_GRIDREMAP_H
#define RUWA_CORE_CANVAS_GRIDREMAP_H

#include "shared/tiles/TileTypes.h"

#include <vector>

namespace aether {
class TileGrid;
}

namespace ruwa::core::canvas {

/**
 * @brief Remap tile grid when canvas is resized or cropped.
 *
 * Maps source pixel (sx, sy) to destination pixel (sx - offsetX, sy - offsetY),
 * dropping everything that falls outside [0, newWidth) x [0, newHeight).
 *
 * Two fast paths:
 *  - Tile-aligned offset: transfers tiles by key renaming (no pixel copies
 *    for fully-covered tiles; edge tiles get a single masking pass).
 *  - General case: per-dst-tile parallel memcpy row-blit using QtConcurrent.
 *
 * @return true if any content was remapped.
 */
bool remapGridForCanvasRect(
    aether::TileGrid& grid, int offsetX, int offsetY, int newWidth, int newHeight);

/**
 * @brief Enumerate source tiles whose footprint is NOT fully contained
 *        in the keep-rect [offsetX, offsetY, offsetX+newWidth, offsetY+newHeight].
 *
 * These are the only tiles that must be snapshotted to support undo — tiles
 * fully inside the keep-rect are losslessly reconstructed by the inverse remap.
 */
std::vector<aether::TileKey> tilesCroppedByResize(
    const aether::TileGrid& grid, int offsetX, int offsetY, int newWidth, int newHeight);

} // namespace ruwa::core::canvas

#endif // RUWA_CORE_CANVAS_GRIDREMAP_H
