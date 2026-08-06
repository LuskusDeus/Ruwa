// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   D I S P L A Y   P Y R A M I D
// ==========================================================================
//
//   A persistent, incrementally updated pyramid over a composition cache.
//
//   Level 0 IS the composition cache — this class owns levels 1..kMaxLevel.
//   A level-L tile covers 2^L * TILE_SIZE document pixels and is keyed by
//   floor(level0Key / 2^L). Each one is an exact 2:1 box downsample of the 2x2
//   block of level-(L-1) tiles below it, which is why the chain is seam-free:
//   a 2:1 box of a 512x512 region into 256x256 needs no data from outside that
//   region.
//
//   WHY THIS EXISTS. Per-tile display mip chains force one binary decision for
//   the whole visible set every frame — either every visible tile has a fresh
//   chain, or the entire frame drops to aliased level zero. Regenerating chains
//   is deferred during content mutation, so the whole canvas snapped to aliased
//   for the length of a stroke and back a few frames later. The pyramid removes
//   the decision: a tile that missed its rebuild simply draws STALE, and stale
//   is still smooth, so there is never a quality seam. Only mixed QUALITY reads
//   as a grid to the eye; mixed freshness does not.
//
//   It also collapses the draw call count. At zoom 0.1 a 2560x1440 viewport
//   covers ~5600 level-0 tiles; on the level lattice the visible quad count is
//   ~constant (~40) at any zoom.
//
//   APRON. Tiles are TILE_SIZE + 2 texels square. The extra ring exists purely
//   so the display pass's bilinear tail at a tile border samples real neighbour
//   data instead of clamping. It cannot be inherited from the parents' own
//   aprons (an apron of A at level L would need 2A at level L-1), so it is
//   produced by binding the 4x4 block of parents and addressing every one of
//   them in core-texel coordinates. See pyramid_downsample.frag.glsl.
//
//   THREADING / OWNERSHIP. Pure GL objects: one instance belongs to exactly one
//   context and is emptied by shutdown().
//

#ifndef RUWA_CANVAS_RENDERING_DISPLAYPYRAMID_H
#define RUWA_CANVAS_RENDERING_DISPLAYPYRAMID_H

#include "shared/types/Result.h"
#include "shared/tiles/TileFormat.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileTypes.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether {

class GLShaderProgram;

class DisplayPyramid {
public:
    /// Texels of real neighbour content carried outside the 256x256 core.
    static constexpr int kApron = 1;
    static constexpr int kTextureSize = static_cast<int>(TILE_SIZE) + 2 * kApron;
    /// Level 8 tiles span 65536 document pixels — far past any usable zoom-out.
    static constexpr int kMaxLevel = 8;

    explicit DisplayPyramid(QOpenGLFunctions_4_5_Core* gl);
    ~DisplayPyramid();

    DisplayPyramid(const DisplayPyramid&) = delete;
    DisplayPyramid& operator=(const DisplayPyramid&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // ---- Level math -------------------------------------------------------

    /// Document pixels spanned by one level-`level` tile.
    static int32_t levelSpanPixels(int level) { return static_cast<int32_t>(TILE_SIZE) << level; }

    /// Key at `level` containing the given level-0 key.
    static TileKey ancestorKey(const TileKey& level0Key, int level)
    {
        // C++20 onwards defines >> on a negative signed value as an arithmetic
        // shift, i.e. exactly the floor division this lattice needs.
        return { level0Key.x >> level, level0Key.y >> level };
    }

    /// Continuous level for a zoom factor: 0 at 1:1, +1 per halving.
    /// Values below 0 (magnification) are clamped to 0 by the caller.
    static float continuousLevelForZoom(float zoom);

    // ---- Invalidation -----------------------------------------------------

    /// The level-0 tile at `key` changed (or went away). Marks every ancestor.
    void invalidate(const TileKey& level0Key);

    /// Everything the pyramid holds is stale; the next update() re-seeds from
    /// the source grid.
    void invalidateAll();

    /// Drop all GPU storage. Also forces a re-seed.
    void clear();

    // ---- Build ------------------------------------------------------------

    struct UpdateRequest {
        /// Highest level the display will actually sample. Levels up to
        /// topLevel + 1 are built (the display lerps with the next one up).
        int topLevel = 1;
        float worldMinX = 0.0f;
        float worldMinY = 0.0f;
        float worldMaxX = 0.0f;
        float worldMaxY = 0.0f;
        /// Ceiling on tiles rebuilt this call; 0 = unlimited. Whatever is left
        /// over stays dirty and draws stale, which is smooth — the only
        /// artifact is a one-frame content lag in a small region.
        uint32_t budget = 0;
    };

    /// Rebuild, bottom-up, every dirty tile that the request's world region
    /// touches. Returns true when nothing in the region is left dirty.
    bool update(const TileGrid& source, const UpdateRequest& request);

    /// Texture for a built tile, or 0 when the pyramid has none there.
    /// A returned texture may be STALE — that is by design.
    GLuint texture(int level, const TileKey& key) const;

    // ---- Introspection (stage 0 instrumentation) --------------------------

    bool hasPendingWork() const { return m_pendingWork; }
    uint32_t lastBuildCount() const { return m_lastBuildCount; }
    size_t tileCount() const;
    size_t approximateBytes() const;

private:
    struct LevelTile {
        GLuint texture = 0;
    };
    using LevelMap = std::unordered_map<TileKey, LevelTile, TileKeyHash>;
    using KeySet = std::unordered_set<TileKey, TileKeyHash>;

    struct KeyRange {
        int32_t minX = 0;
        int32_t minY = 0;
        int32_t maxX = 0;
        int32_t maxY = 0;
        bool contains(const TileKey& k) const
        {
            return k.x >= minX && k.x <= maxX && k.y >= minY && k.y <= maxY;
        }
    };

    void seedFrom(const TileGrid& source);
    void adoptFormat(TilePixelFormat format);
    KeyRange rangeForLevel(const UpdateRequest& request, int level) const;
    /// Returns false when the tile had no content and was dropped instead.
    bool buildTile(const TileGrid& source, int level, const TileKey& key);
    void releaseTile(int level, const TileKey& key);

    GLuint acquireTexture();
    void recycleTexture(GLuint texture);
    Result<void> ensureTransparentTexture();

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_downsampleProgram;
    GLuint m_fbo = 0;
    GLuint m_emptyVAO = 0;

    // Bound in place of an absent parent so the downsample shader needs no
    // branch for missing neighbours. Sized like a pyramid tile so the same
    // object serves both apron conventions.
    GLuint m_transparentTexture = 0;

    // Index 0 is unused: level 0 is the composition cache itself.
    std::array<LevelMap, kMaxLevel + 1> m_levels {};
    std::array<KeySet, kMaxLevel + 1> m_dirty {};

    // Recycled 258x258 storage. GLTileTexturePool hard-rejects anything that is
    // not TILE_SIZE square, so the pyramid keeps its own free list.
    std::vector<GLuint> m_freeTextures;

    TilePixelFormat m_format = kDefaultTileFormat;
    bool m_needsSeed = true;
    bool m_pendingWork = false;
    uint32_t m_lastBuildCount = 0;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_CANVAS_RENDERING_DISPLAYPYRAMID_H
