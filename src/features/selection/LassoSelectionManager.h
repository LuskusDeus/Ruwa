// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   S E L E C T I O N   M A N A G E R
// ==========================================================================

#ifndef RUWA_CORE_SELECTION_LASSOSELECTIONMANAGER_H
#define RUWA_CORE_SELECTION_LASSOSELECTIONMANAGER_H

#include "shared/types/Types.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileTypes.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aether {

/// Per-tile pixel snapshot of the selection mask, suitable for undo restore.
/// Each tile entry is keyed by world TileKey; an absent key = empty tile.
///
/// A tile entry carries one of two encodings, distinguished by its size:
///   - TILE_BYTE_SIZE bytes: verbatim RGBA8 pixels for the tile.
///   - kMaskTileUniformBytes bytes: a single RGBA value that applies to every
///     pixel of the tile.
/// Any other size is malformed and is skipped by the readers.
///
/// The uniform encoding matters at scale: a large selection is mostly solid
/// interior tiles, so a 6000x6000 selection snapshots to a few MB instead of
/// ~150 MB — and costs a scan instead of 576 heap allocations plus a 150 MB
/// copy on the GUI thread. Producers should emit it via makeUniformMaskTile()
/// whenever a tile is uniform; readers must go through MaskTileView.
using MaskTileSnapshot = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;

inline constexpr size_t kMaskTileUniformBytes = TILE_CHANNELS;

inline std::vector<uint8_t> makeUniformMaskTile(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return std::vector<uint8_t> { r, g, b, a };
}

/// Read-only view over one MaskTileSnapshot entry that hides the encoding.
/// `valid` is false for a malformed entry; the accessors must not be used then.
struct MaskTileView {
    const uint8_t* pixels = nullptr; // null when uniform
    const uint8_t* uniform = nullptr; // 4 RGBA bytes when uniform
    bool valid = false;

    bool isUniform() const noexcept { return uniform != nullptr; }

    /// Alpha at a byte offset produced the usual way:
    /// (localY * TILE_SIZE + localX) * TILE_CHANNELS.
    uint8_t alphaAt(uint32_t byteIndex) const noexcept
    {
        return uniform ? uniform[3] : pixels[byteIndex + 3];
    }

    /// Write the tile's pixels into `dst` (TILE_BYTE_SIZE bytes).
    void expandInto(uint8_t* dst) const
    {
        if (uniform) {
            // Selection coverage always writes the same value to all four
            // channels, so the broadcast is a memset in practice.
            if (uniform[0] == uniform[1] && uniform[1] == uniform[2] && uniform[2] == uniform[3]) {
                std::memset(dst, uniform[0], TILE_BYTE_SIZE);
                return;
            }
            for (size_t i = 0; i < TILE_BYTE_SIZE; i += TILE_CHANNELS) {
                dst[i + 0] = uniform[0];
                dst[i + 1] = uniform[1];
                dst[i + 2] = uniform[2];
                dst[i + 3] = uniform[3];
            }
        } else {
            std::memcpy(dst, pixels, TILE_BYTE_SIZE);
        }
    }
};

inline MaskTileView viewMaskTile(const std::vector<uint8_t>& bytes) noexcept
{
    MaskTileView view;
    if (bytes.size() == TILE_BYTE_SIZE) {
        view.pixels = bytes.data();
        view.valid = true;
    } else if (bytes.size() == kMaskTileUniformBytes) {
        view.uniform = bytes.data();
        view.valid = true;
    }
    return view;
}

enum class LassoSelectionMode { Replace, Add, Subtract };

struct LassoRegion {
    std::vector<Vector2> polygon;
    LassoSelectionMode mode = LassoSelectionMode::Add;

    bool operator==(const LassoRegion& other) const
    {
        return mode == other.mode && polygon == other.polygon;
    }
};

struct LassoEdgeSegment {
    Vector2 a;
    Vector2 b;
};

class LassoSelectionManager {
public:
    LassoSelectionManager()
    {
        // Selection mask is alpha COVERAGE — keep it RGBA8 regardless of the
        // document tile format (see LayerData::ensureMask). This keeps the
        // RGBA8 selection-render temp textures + readback + CPU mask ops valid.
        m_mask.setFormat(TilePixelFormat::RGBA8);
    }

    void clear();
    void applySelection(const std::vector<Vector2>& polygon, LassoSelectionMode mode,
        uint32_t canvasWidth, uint32_t canvasHeight, uint8_t strength = 255);

    const std::vector<LassoRegion>& regions() const { return m_regions; }
    bool hasSelection() const { return !m_regions.empty(); }
    const TileGrid& mask() const { return m_mask; }

    /**
     * @brief RAII access to the mutable mask grid.
     *
     * Construct a scope, call grid() to read/write, let it go out of scope.
     * On destruction, post-conditions are applied:
     *   - cached undo mask snapshot is invalidated (default; opt out only for
     *     access that does not mutate pixel data).
     *   - soft-alpha cache is invalidated (default; opt out via disableSoftAlphaInvalidation()).
     *
     * Callers that change mask shape should additionally invoke
     * rebuildEdgesFromMask() on the manager (kept explicit because edge
     * rebuilding is comparatively expensive and not always wanted, e.g. for
     * GPU-sync-only iterations).
     *
     * The scope also prevents external code from holding a long-lived
     * mutable reference to the mask grid past the mutation point.
     */
    class MaskMutationScope {
    public:
        explicit MaskMutationScope(LassoSelectionManager& mgr) noexcept
            : m_mgr(mgr)
        {
        }

        ~MaskMutationScope()
        {
            if (m_invalidateSnapshotCache) {
                m_mgr.invalidateMaskSnapshotCache();
            }
            if (m_invalidateSoftAlpha) {
                m_mgr.markMaskSoftAlphaUnknown();
            }
        }

        MaskMutationScope(const MaskMutationScope&) = delete;
        MaskMutationScope& operator=(const MaskMutationScope&) = delete;
        MaskMutationScope(MaskMutationScope&&) = delete;
        MaskMutationScope& operator=(MaskMutationScope&&) = delete;

        TileGrid& grid() noexcept { return m_mgr.m_mask; }

        /// Keep the caller-provided soft-alpha state authoritative. This does
        /// not suppress undo-snapshot invalidation: callers that mutate mask
        /// pixels still need the next capture to produce a fresh snapshot.
        void disableSoftAlphaInvalidation() noexcept { m_invalidateSoftAlpha = false; }

        /// Use together with disableSoftAlphaInvalidation() only for read-only
        /// or GPU-upload access that does not change mask pixels.
        void disableSnapshotInvalidation() noexcept { m_invalidateSnapshotCache = false; }

    private:
        LassoSelectionManager& m_mgr;
        bool m_invalidateSoftAlpha = true;
        bool m_invalidateSnapshotCache = true;
    };

    bool maskHasSoftAlpha() const;

    /**
     * @brief Cached deep-copy snapshot of the current mask tiles for undo capture.
     *
     * The first call after a mutation copies all current mask tile pixel
     * buffers into a fresh `MaskTileSnapshot` and returns a shared_ptr to it.
     * Uniform tiles are stored in the compact encoding instead of being copied
     * (see MaskTileSnapshot). Subsequent calls without intervening mutation
     * return the same pointer (cheap copy). MaskMutationScope's destructor
     * invalidates the cache, so the next call after a mutation rebuilds.
     *
     * This means typical undo patterns (capture before-state, run an op that
     * mutates only layer pixels — not the selection mask, capture after-state)
     * cost a single deep copy: both before and after share the same pointer.
     * Operations that actually change the mask cost one extra copy.
     */
    std::shared_ptr<const MaskTileSnapshot> snapshotMask() const;

    /**
     * @brief Restore mask pixel state directly from a tile snapshot.
     *
     * Used by undo to revert mask mutations precisely, even when the live
     * regions do not faithfully reproduce the soft-alpha mask (e.g. selections
     * built from layer content via selectActiveLayerContent, where the polygon
     * is a full-canvas placeholder but the actual mask carries soft alpha).
     *
     * `regions` and `softAlpha` are restored verbatim. `maskTiles` may be
     * empty/null to clear the mask entirely.
     */
    void applyMaskSnapshot(std::shared_ptr<const MaskTileSnapshot> maskTiles,
        std::vector<LassoRegion> regions, bool softAlpha, uint32_t canvasWidth,
        uint32_t canvasHeight);

    /// Combine an already-rasterized RGBA8 selection mask with the current
    /// selection. Used by non-polygon selection tools such as Magic Wand.
    void applyRasterSelectionMask(const MaskTileSnapshot& maskTiles, LassoSelectionMode mode,
        uint32_t canvasWidth, uint32_t canvasHeight);
    void setMaskHasSoftAlpha(bool hasSoftAlpha) const;
    void markMaskSoftAlphaUnknown() const;
    const std::vector<LassoEdgeSegment>& edges() const { return m_edges; }
    /// Monotonic invalidation token for renderer-side edge geometry caches.
    uint64_t edgesRevision() const { return m_edgesRevision; }

    void addRegion(const std::vector<Vector2>& polygon, LassoSelectionMode mode);
    void rebuildEdgesFromMask(uint32_t canvasWidth, uint32_t canvasHeight);
    void clearEdges()
    {
        m_edges.clear();
        ++m_edgesRevision;
    }

    /**
     * @brief Restore selection from saved state (for undo/redo).
     */
    void applyState(
        const std::vector<LassoRegion>& regions, uint32_t canvasWidth, uint32_t canvasHeight);

    /**
     * @brief Set regions only (mask and edges unchanged).
     * Use when mask was transformed externally and regions must stay in sync for capture.
     */
    void setRegionsOnly(const std::vector<LassoRegion>& regions);

private:
    void rebuildEdges(uint32_t canvasWidth, uint32_t canvasHeight);
    void invalidateMaskSnapshotCache() const noexcept;

    std::vector<LassoRegion> m_regions;
    TileGrid m_mask;
    std::vector<LassoEdgeSegment> m_edges;
    uint64_t m_edgesRevision = 1;
    mutable bool m_maskSoftAlphaKnown = true;
    mutable bool m_maskHasSoftAlpha = false;
    mutable std::shared_ptr<const MaskTileSnapshot> m_cachedMaskSnapshot;
};

} // namespace aether

#endif // RUWA_CORE_SELECTION_LASSOSELECTIONMANAGER_H
