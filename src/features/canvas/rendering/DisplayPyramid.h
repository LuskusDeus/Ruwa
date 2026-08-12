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
//   STALENESS. Rebuilds are driven by an invalidation feed (CompositionCache
//   reports every position whose pixels changed) plus a dirty mark that walks
//   UP the ancestor chain, because that is the only way to keep the per-frame
//   cost proportional to what actually moved. Both halves can go wrong quietly,
//   and unlike level zero — which draws straight out of the grid, so a removed
//   tile simply stops being drawn — the pyramid draws out of its own storage,
//   where anything missed keeps showing old pixels indefinitely:
//
//     - a cache-mutation path that forgets to report itself;
//     - a rebuild that cannot reach its own ancestors. Marking walks upwards
//       ONCE, from level zero. Rebuilding a level tile does not re-mark the
//       tiles above it, so an ancestor that was already clean — built at some
//       earlier, further-out zoom — has no way to learn that what it was
//       derived from has moved.
//
//   Which is why a tile keeps its dirt for as long as ANYTHING it samples is
//   unsettled, even when nothing this frame can do will settle it. Dirt is a
//   standing request to reconsider, not a promise of work: what it costs while
//   nothing moves is a version comparison, and what it buys is that the only way
//   to lose track of a stale tile is to lose track of the dirt itself.
//
//   So neither is the source of truth. Every tile records the versions of the
//   16 parents it was built from and carries its own, and auditLevels() walks
//   the lattice comparing them on frames where the feed has nothing left to
//   say. That turns both failures into a frame of lag instead of a ghost.
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

    /// The level-0 tile at `key` changed (or went away). Marks every ancestor,
    /// unconditionally — see invalidateLevelTile.
    void invalidate(const TileKey& level0Key);

    /// Everything the pyramid holds is stale; the next update() re-seeds from
    /// the source grid.
    void invalidateAll();

    /// Marks a level tile and every ancestor above it.
    ///
    /// The whole chain, always. It is tempting to stop at the first level that
    /// is already dirty — a level-0 change marks a chain, so surely everything
    /// above it is marked too — but a dirty mark does not only come from below
    /// any more. A tile can hold its dirt because it is waiting on the
    /// compositor, or because a budget deferred it, or because the audit put it
    /// there, and in each of those cases the levels above it may well be clean.
    /// Stopping early on one of those left the tiles above stale until the audit
    /// happened to reach them.
    void invalidateLevelTile(int level, const TileKey& key);

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
        /// Ceiling on rebuilds of tiles that ALREADY hold content; 0 =
        /// unlimited. What is left over stays dirty and draws STALE, which is
        /// smooth — the only artifact is a frame or two of content lag in a
        /// small region.
        ///
        /// A tile with no texture yet ignores this budget entirely. Deferring
        /// one would not leave a stale patch, it would leave a HOLE: the
        /// display pass has nothing to bind and skips the quad, so the
        /// background shows through where content should be.
        uint32_t deferrableBudget = 0;
        /// Document-space point the rebuild radiates out from, so anything that
        /// does end up a frame behind is far from where the user is working.
        /// Unset = the centre of the requested region.
        bool hasFocusPoint = false;
        float focusX = 0.0f;
        float focusY = 0.0f;
        /// Level-0 positions the compositor still owes a recomposite, i.e. the
        /// cache's dirty set. What the cache holds there is KNOWN to be wrong,
        /// so a level tile derived from one must not be stamped current.
        ///
        /// This is not a detail. Compositing is culled to the viewport while the
        /// pyramid's build range is the viewport plus a ring, and both of them
        /// are wider than the viewport was when the edit happened — so there is
        /// always a band where the pyramid can reach a position the compositor
        /// has not caught up with yet. Null = assume nothing is owed.
        const std::unordered_set<TileKey, TileKeyHash>* pendingSourcePositions = nullptr;
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
    /// Parents in the 4x4 block, row-major from (-1,-1) to (2,2).
    static constexpr size_t kParentBlockSize = 16;

    /// Level-1 tiles the audit re-derives per pass. One sweep is spread over as
    /// many catch-up frames as it takes, so this only has to be small enough
    /// that a pass disappears into a frame — 16 hash lookups per tile.
    static constexpr size_t kAuditTilesPerPass = 256;

    struct LevelTile {
        GLuint texture = 0;
        /// Bumped on every successful build, so a CHILD can tell whether this
        /// is still the parent it derived from. Rebuilding a tile does not
        /// re-mark its ancestors — nothing in the lattice walks upwards — so
        /// without this a child that was already clean has no way to find out
        /// its parent moved under it.
        uint64_t version = 0;
        /// What this was built from, 0 for a parent that was absent: cache tile
        /// content versions at level 1, parent LevelTile::version above it. The
        /// ring is stored as well as the core — the ring feeds the apron, and a
        /// stale apron is a visible seam between two level tiles.
        std::array<uint64_t, kParentBlockSize> parentVersions {};
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
        bool operator==(const KeyRange& other) const
        {
            return minX == other.minX && minY == other.minY && maxX == other.maxX
                && maxY == other.maxY;
        }
    };

    void seedFrom(const TileGrid& source);
    void adoptFormat(TilePixelFormat format);
    KeyRange rangeForLevel(const UpdateRequest& request, int level) const;
    /// Versions of the 4x4 parent block, in the order buildTile binds them.
    /// Absent, or present-without-a-texture, is 0 — buildTile treats both as
    /// "no parent", so the audit has to agree.
    ///
    /// `outUnstampedParent`, when given, reports that some parent has content
    /// but no version to identify it by (a cache tile with a texture that was
    /// never stamped). Such a parent is indistinguishable from an absent one, so
    /// every caller that compares versions has to read this as "unknown, assume
    /// it moved" rather than trust the comparison.
    std::array<uint64_t, kParentBlockSize> sampleParentVersions(const TileGrid& source, int level,
        const TileKey& key, bool* outUnstampedParent = nullptr) const;
    /// Whether a rebuild right now would put different pixels on screen than the
    /// tile already holds, i.e. whether anything it samples has moved since it
    /// was last built. A tile with no texture always counts as moved.
    ///
    /// This is what makes it safe for a tile to keep its dirt indefinitely.
    /// Dirt is a request to reconsider, not a promise that a rebuild will
    /// achieve something: a tile waiting on a recomposite the viewport cull may
    /// never perform would otherwise redraw itself every frame, for ever, for no
    /// pixel change.
    bool sampledSourceMoved(const TileGrid& source, int level, const TileKey& key) const;
    /// Re-derives staleness for the tiles inside the request's range by
    /// comparing what each was built from against what is there now, and marks
    /// whatever disagrees. Returns how many tiles that was.
    ///
    /// This is the pyramid's safety net, not its engine: it runs only on a frame
    /// where the invalidation feed has left nothing to do, so it costs nothing
    /// while the user is working. What it buys is that neither of the two ways
    /// this design can silently go stale — a cache mutation that forgets to
    /// report itself, and a rebuild that cannot reach the ancestors it just
    /// invalidated — survives longer than a frame of lag.
    uint32_t auditLevels(const TileGrid& source, const UpdateRequest& request, int topLevel);
    /// Returns false when the tile had no content and was dropped instead.
    bool buildTile(const TileGrid& source, int level, const TileKey& key);
    struct ParentDirt {
        /// Some parent this tile samples is dirty, so whatever it builds now is
        /// provisional and it must keep its own dirt. Range does not enter into
        /// it: this is a statement about the pixels, not about the schedule.
        bool any = false;
        /// ...and at least one of those parents is inside the parent level's key
        /// range, i.e. THIS request is going to rebuild it, so a catch-up frame
        /// can finish the job. An out-of-range dirty parent is nobody's job until
        /// the camera moves, so waiting on one must not claim pending work — that
        /// would spin the catch-up repaint at the frame rate.
        bool resolvable = false;
    };
    /// Whether a build right now would produce content that something is going
    /// to make obsolete, and whether this request is what will do it.
    ///
    /// The range pad is one tile PER LEVEL, so range(L) reaches twice as far in
    /// document pixels as range(L-1): a level tile near the border of the padded
    /// region routinely has core parents outside its parent level's range. Those
    /// used to be ignored outright, and ignoring them is what stranded coarse
    /// tiles for good — the child built over an absent parent, dropped its dirt,
    /// and when the camera later brought the parent into range and built it,
    /// nothing re-marked the child, because marking only ever walks up from level
    /// zero. That is the stage-4 trap in its most reachable form.
    ParentDirt sampledParentsDirt(int level, const TileKey& key, const KeyRange& parentRange) const;
    /// Whether what this tile derives from is out of date for a reason THIS
    /// request cannot resolve: at level 1, a sampled position the compositor
    /// still owes; above it, a parent recorded as stalled for any such reason.
    ///
    /// Stalled is not deferred. A deferred tile is work this request chose to
    /// postpone, so it claims pending work and a catch-up frame finishes it. A
    /// stalled tile is waiting on something outside the pyramid — a recomposite
    /// the viewport cull skipped, or a parent no request will touch until the
    /// camera moves — and claiming pending work for that would spin the catch-up
    /// repaint at the frame rate for as long as the camera sat still. It simply
    /// keeps its dirt until whatever it waits on lands.
    ///
    /// Which is why the stall has to be INHERITED rather than re-derived: a
    /// grandchild that could see only "my parent is dirty and in range" would
    /// conclude the dirt is finishable and ask for a frame that changes nothing.
    ///
    /// What a stall must NOT do is hold the rebuild back. Compositing is culled
    /// to the viewport, so a level tile at the edge of the frame routinely
    /// samples positions the compositor is never going to revisit at this
    /// camera: every level tile straddling the viewport border has parents
    /// outside it, and the outer ring of every one of them is outside it.
    /// Refusing to rebuild those froze the visible half of the tile — showing
    /// pre-undo content next to a level-zero tap that had already updated — for
    /// as long as the camera sat still. The parents that DID recomposite are
    /// real, current content; fold them in and keep the dirt for the rest.
    bool sampledSourceStalled(int level, const TileKey& key, const KeyRange& parentRange,
        const UpdateRequest& request) const;
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
    // Per-update scratch: tiles that kept their dirt for a reason this request
    // cannot resolve, so the level above can tell that kind of dirt apart from
    // work a catch-up frame would finish. See sampledSourceStalled.
    std::array<KeySet, kMaxLevel + 1> m_stalled {};

    // Recycled 258x258 storage. GLTileTexturePool hard-rejects anything that is
    // not TILE_SIZE square, so the pyramid keeps its own free list.
    std::vector<GLuint> m_freeTextures;

    TilePixelFormat m_format = kDefaultTileFormat;
    // Where the next audit pass resumes (level, then offset within that level's
    // map) and whether a sweep is owed at all. Content changing arms it, and so
    // does the request's range changing (see m_auditedScope); a completed sweep
    // disarms it.
    int m_auditLevel = 1;
    size_t m_auditCursor = 0;
    /// Tile count of m_auditLevel when the cursor was last advanced. A change
    /// means the map may have rehashed under the cursor, so the level restarts.
    size_t m_auditLevelTileCount = 0;
    bool m_auditPending = false;
    // The request shape the last COMPLETED sweep answered for. A sweep consumes
    // out-of-range tiles without checking them, so "everything agrees" only ever
    // holds for the range it saw; a camera move brings unchecked tiles into view
    // and has to arm it again. Content is not the only thing that can turn a
    // tile the pyramid holds into a tile the pyramid SHOWS.
    bool m_auditedScopeValid = false;
    int m_auditedTopLevel = 0;
    KeyRange m_auditedScope {};
    /// Source of LevelTile::version. Monotonic for the life of the pyramid, so
    /// a recycled texture can never look like the tile it used to hold.
    uint64_t m_nextTileVersion = 0;
    bool m_needsSeed = true;
    bool m_pendingWork = false;
    uint32_t m_lastBuildCount = 0;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_CANVAS_RENDERING_DISPLAYPYRAMID_H
