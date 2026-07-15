// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   T Y P E S
// ==========================================================================

#ifndef RUWA_CORE_TILES_TILETYPES_H
#define RUWA_CORE_TILES_TILETYPES_H

#include <cstdint>
#include <cmath>
#include <functional>

namespace aether {

// ==========================================================================
//   C O N S T A N T S
// ==========================================================================

static constexpr uint32_t TILE_SIZE = 256;
static constexpr uint32_t TILE_CHANNELS = 4; // RGBA
static constexpr uint32_t TILE_BYTE_SIZE = TILE_SIZE * TILE_SIZE * TILE_CHANNELS; // 256 KB

// ==========================================================================
//   T I L E   K E Y
// ==========================================================================

struct TileKey {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const TileKey& other) const { return x == other.x && y == other.y; }

    bool operator!=(const TileKey& other) const { return !(*this == other); }
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& key) const
    {
        // Combine two 32-bit ints into a single hash
        auto h1 = std::hash<int32_t> {}(key.x);
        auto h2 = std::hash<int32_t> {}(key.y);
        return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

// ==========================================================================
//   H E L P E R S
// ==========================================================================

/// Convert world pixel coordinate to tile coordinate
inline TileKey worldToTile(float worldX, float worldY)
{
    // floor-division: works correctly for both positive and negative coordinates
    auto floorDiv = [](float val) -> int32_t {
        return static_cast<int32_t>(std::floor(val / static_cast<float>(TILE_SIZE)));
    };
    return { floorDiv(worldX), floorDiv(worldY) };
}

/// World-space origin of a tile (top-left corner)
inline void tileWorldOrigin(const TileKey& key, float& outX, float& outY)
{
    outX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE);
    outY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE);
}

} // namespace aether

#endif // RUWA_CORE_TILES_TILETYPES_H
