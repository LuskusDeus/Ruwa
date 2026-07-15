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
#include <memory>
#include <unordered_map>
#include <vector>

namespace aether {

/// Per-tile pixel snapshot of the selection mask, suitable for undo restore.
/// Each tile entry is the raw RGBA byte buffer (TILE_BYTE_SIZE long) for a
/// single tile keyed by world TileKey. Empty buffer / absent key = empty tile.
using MaskTileSnapshot = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;

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
     * The first call after a mutation deep-copies all current mask tile pixel
     * buffers into a fresh `MaskTileSnapshot` and returns a shared_ptr to it.
     * Subsequent calls without intervening mutation return the same pointer
     * (cheap copy). MaskMutationScope's destructor invalidates the cache, so
     * the next call after a mutation rebuilds.
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
    void setMaskHasSoftAlpha(bool hasSoftAlpha) const;
    void markMaskSoftAlphaUnknown() const;
    const std::vector<LassoEdgeSegment>& edges() const { return m_edges; }

    void addRegion(const std::vector<Vector2>& polygon, LassoSelectionMode mode);
    void rebuildEdgesFromMask(uint32_t canvasWidth, uint32_t canvasHeight);
    void clearEdges() { m_edges.clear(); }

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
    mutable bool m_maskSoftAlphaKnown = true;
    mutable bool m_maskHasSoftAlpha = false;
    mutable std::shared_ptr<const MaskTileSnapshot> m_cachedMaskSnapshot;
};

} // namespace aether

#endif // RUWA_CORE_SELECTION_LASSOSELECTIONMANAGER_H
