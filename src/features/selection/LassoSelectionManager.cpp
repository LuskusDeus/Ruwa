// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   S E L E C T I O N   M A N A G E R
// ==========================================================================

#include "features/selection/LassoSelectionManager.h"

#include <algorithm>
#include <cstring>

namespace aether {

namespace {

inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

inline uint32_t floorMod(int32_t a, int32_t b)
{
    int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

bool pointInPolygon(const Vector2& p, const std::vector<Vector2>& poly)
{
    bool inside = false;
    size_t count = poly.size();
    if (count < 3)
        return false;
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vector2& a = poly[i];
        const Vector2& b = poly[j];
        bool intersect = ((a.y > p.y) != (b.y > p.y))
            && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 0.0000001f) + a.x);
        if (intersect)
            inside = !inside;
    }
    return inside;
}

uint8_t maskAlphaAt(
    const TileGrid& grid, int32_t x, int32_t y, uint32_t canvasWidth, uint32_t canvasHeight)
{
    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    if (clipToCanvas) {
        if (x < 0 || y < 0)
            return 0;
        if (x >= static_cast<int32_t>(canvasWidth) || y >= static_cast<int32_t>(canvasHeight))
            return 0;
    }

    int32_t tx = floorDiv(x, static_cast<int32_t>(TILE_SIZE));
    int32_t ty = floorDiv(y, static_cast<int32_t>(TILE_SIZE));
    uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
    uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
    const TileData* tile = grid.getTile(TileKey { tx, ty });
    if (!tile)
        return 0;

    uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return tile->pixels()[idx + 3];
}
} // namespace

void LassoSelectionManager::clear()
{
    m_regions.clear();
    m_mask.clear();
    m_edges.clear();
    setMaskHasSoftAlpha(false);
    invalidateMaskSnapshotCache();
}

std::shared_ptr<const MaskTileSnapshot> LassoSelectionManager::snapshotMask() const
{
    if (m_cachedMaskSnapshot) {
        return m_cachedMaskSnapshot;
    }
    auto snapshot = std::make_shared<MaskTileSnapshot>();
    snapshot->reserve(m_mask.tiles().size());
    for (const auto& [key, tile] : m_mask.tiles()) {
        std::vector<uint8_t> bytes(TILE_BYTE_SIZE);
        std::memcpy(bytes.data(), tile.pixels(), TILE_BYTE_SIZE);
        snapshot->emplace(key, std::move(bytes));
    }
    m_cachedMaskSnapshot = std::move(snapshot);
    return m_cachedMaskSnapshot;
}

void LassoSelectionManager::applyMaskSnapshot(std::shared_ptr<const MaskTileSnapshot> maskTiles,
    std::vector<LassoRegion> regions, bool softAlpha, uint32_t canvasWidth, uint32_t canvasHeight)
{
    m_regions = std::move(regions);
    {
        MaskMutationScope scope(*this);
        // Soft-alpha state is set explicitly below. The snapshot cache is
        // invalidated by the scope, then replaced with maskTiles after it ends.
        scope.disableSoftAlphaInvalidation();
        TileGrid& mask = scope.grid();
        mask.clear();
        if (maskTiles) {
            for (const auto& [key, bytes] : *maskTiles) {
                if (bytes.size() != TILE_BYTE_SIZE)
                    continue;
                TileData& tile = mask.getOrCreateTile(key);
                std::memcpy(tile.pixels(), bytes.data(), TILE_BYTE_SIZE);
                tile.markDirty();
            }
        }
    }
    setMaskHasSoftAlpha(softAlpha);
    // The live mask now exactly equals *maskTiles, so we can adopt it as the
    // cached snapshot — no copy needed for any subsequent capture until the
    // next mutation.
    m_cachedMaskSnapshot
        = maskTiles ? std::move(maskTiles) : std::make_shared<const MaskTileSnapshot>();

    if (m_mask.empty()) {
        m_edges.clear();
    } else {
        rebuildEdges(canvasWidth, canvasHeight);
    }
}

void LassoSelectionManager::invalidateMaskSnapshotCache() const noexcept
{
    m_cachedMaskSnapshot.reset();
}

void LassoSelectionManager::addRegion(const std::vector<Vector2>& polygon, LassoSelectionMode mode)
{
    if (polygon.size() < 3)
        return;
    if (mode == LassoSelectionMode::Replace) {
        m_regions.clear();
        m_edges.clear();
        mode = LassoSelectionMode::Add;
    }
    m_regions.push_back({ polygon, mode });
}

void LassoSelectionManager::rebuildEdgesFromMask(uint32_t canvasWidth, uint32_t canvasHeight)
{
    rebuildEdges(canvasWidth, canvasHeight);
}

bool LassoSelectionManager::maskHasSoftAlpha() const
{
    if (m_maskSoftAlphaKnown) {
        return m_maskHasSoftAlpha;
    }

    bool hasSoftAlpha = false;
    constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
    for (const auto& [key, tile] : m_mask.tiles()) {
        (void) key;
        const uint8_t* px = tile.pixels();
        for (uint32_t i = 0; i < pixelCount; ++i) {
            const uint8_t alpha = px[i * TILE_CHANNELS + 3];
            if (alpha > 0 && alpha < 255) {
                hasSoftAlpha = true;
                break;
            }
        }
        if (hasSoftAlpha) {
            break;
        }
    }

    setMaskHasSoftAlpha(hasSoftAlpha);
    return hasSoftAlpha;
}

void LassoSelectionManager::setMaskHasSoftAlpha(bool hasSoftAlpha) const
{
    m_maskSoftAlphaKnown = true;
    m_maskHasSoftAlpha = hasSoftAlpha;
}

void LassoSelectionManager::markMaskSoftAlphaUnknown() const
{
    m_maskSoftAlphaKnown = false;
}

void LassoSelectionManager::setRegionsOnly(const std::vector<LassoRegion>& regions)
{
    m_regions = regions;
}

void LassoSelectionManager::applyState(
    const std::vector<LassoRegion>& regions, uint32_t canvasWidth, uint32_t canvasHeight)
{
    clear();

    for (const LassoRegion& r : regions) {
        if (r.polygon.size() < 3)
            continue;
        LassoSelectionMode mode = r.mode;
        if (mode == LassoSelectionMode::Replace) {
            mode = LassoSelectionMode::Add;
        }
        if (mode == LassoSelectionMode::Subtract && m_regions.empty())
            continue;
        applySelection(r.polygon, mode, canvasWidth, canvasHeight, 255);
    }
}

void LassoSelectionManager::applySelection(const std::vector<Vector2>& polygon,
    LassoSelectionMode mode, uint32_t canvasWidth, uint32_t canvasHeight, uint8_t strength)
{
    if (polygon.size() < 3)
        return;

    // Mask pixel data will change below; any cached undo snapshot is now stale.
    invalidateMaskSnapshotCache();

    const bool canPreserveSoftAlphaState = (mode != LassoSelectionMode::Replace);
    const bool previousSoftAlpha = canPreserveSoftAlphaState ? maskHasSoftAlpha() : false;

    if (mode == LassoSelectionMode::Replace) {
        m_regions.clear();
        m_mask.clear();
        m_edges.clear();
        setMaskHasSoftAlpha(false);
        mode = LassoSelectionMode::Add;
    }

    if (mode == LassoSelectionMode::Subtract && m_regions.empty()) {
        return;
    }

    m_regions.push_back({ polygon, mode });

    float minX = polygon[0].x;
    float minY = polygon[0].y;
    float maxX = polygon[0].x;
    float maxY = polygon[0].y;
    for (const auto& p : polygon) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    int32_t x0 = static_cast<int32_t>(std::floor(minX));
    int32_t y0 = static_cast<int32_t>(std::floor(minY));
    int32_t x1 = static_cast<int32_t>(std::ceil(maxX));
    int32_t y1 = static_cast<int32_t>(std::ceil(maxY));
    if (clipToCanvas) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(static_cast<int32_t>(canvasWidth) - 1, x1);
        y1 = std::min(static_cast<int32_t>(canvasHeight) - 1, y1);
    }

    if (x1 < x0 || y1 < y0)
        return;

    constexpr int32_t TS = static_cast<int32_t>(TILE_SIZE);
    const size_t count = polygon.size();
    for (int32_t y = y0; y <= y1; ++y) {
        float scanY = static_cast<float>(y) + 0.5f;
        std::vector<float> intersections;
        intersections.reserve(count);

        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            const Vector2& a = polygon[j];
            const Vector2& b = polygon[i];
            if ((a.y <= scanY) == (b.y <= scanY))
                continue;
            float t = (scanY - a.y) / (b.y - a.y);
            float ix = a.x + t * (b.x - a.x);
            intersections.push_back(ix);
        }

        if (intersections.size() < 2)
            continue;
        std::sort(intersections.begin(), intersections.end());

        if (intersections.size() % 2 != 0) {
            intersections.pop_back();
        }

        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            int32_t xa = std::max(static_cast<int32_t>(std::ceil(intersections[k] - 0.5f)), x0);
            int32_t xb
                = std::min(static_cast<int32_t>(std::floor(intersections[k + 1] - 0.5f)), x1);
            if (xb < xa)
                continue;

            for (int32_t x = xa; x <= xb; ++x) {
                int32_t tx = floorDiv(x, TS);
                int32_t ty = floorDiv(y, TS);
                uint32_t localX = floorMod(x, TS);
                uint32_t localY = floorMod(y, TS);
                TileData& tile = m_mask.getOrCreateTile(TileKey { tx, ty });

                uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
                uint8_t current = tile.pixels()[idx + 3];
                uint8_t next = current;
                if (mode == LassoSelectionMode::Add) {
                    next = std::max(current, strength);
                } else if (mode == LassoSelectionMode::Subtract) {
                    next = (strength >= current) ? 0 : static_cast<uint8_t>(current - strength);
                }
                tile.setPixel(localX, localY, next, next, next, next);
            }
        }
    }

    rebuildEdges(canvasWidth, canvasHeight);
    setMaskHasSoftAlpha(previousSoftAlpha || (strength > 0 && strength < 255));
}

void LassoSelectionManager::rebuildEdges(uint32_t canvasWidth, uint32_t canvasHeight)
{
    m_edges.clear();
    if (m_mask.empty())
        return;
    constexpr uint32_t TS = TILE_SIZE;
    constexpr uint32_t TC = TILE_CHANNELS;
    (void) canvasWidth;
    (void) canvasHeight;

    // ---- Step 1: Collect raw unit-length edges with cached tile lookups ----
    // HEdge: horizontal from (a, b) to (a+1, b)
    // VEdge: vertical   from (a, b) to (a, b+1)
    struct RawEdge {
        int32_t a, b;
    };
    std::vector<RawEdge> hRaw;
    std::vector<RawEdge> vRaw;

    for (const auto& [key, tile] : m_mask.tiles()) {
        const int32_t baseX = key.x * static_cast<int32_t>(TS);
        const int32_t baseY = key.y * static_cast<int32_t>(TS);
        const uint8_t* px = tile.pixels();

        // Cache neighbor tile pointers (avoids hash lookup per boundary pixel)
        const TileData* tL = m_mask.getTile({ key.x - 1, key.y });
        const TileData* tR = m_mask.getTile({ key.x + 1, key.y });
        const TileData* tU = m_mask.getTile({ key.x, key.y - 1 });
        const TileData* tD = m_mask.getTile({ key.x, key.y + 1 });

        for (uint32_t ly = 0; ly < TS; ++ly) {
            for (uint32_t lx = 0; lx < TS; ++lx) {
                if (px[(ly * TS + lx) * TC + 3] == 0)
                    continue;

                const int32_t x = baseX + static_cast<int32_t>(lx);
                const int32_t y = baseY + static_cast<int32_t>(ly);

                // Left neighbor (x-1, y) → vertical edge at x
                {
                    uint8_t na = 0;
                    if (lx > 0)
                        na = px[(ly * TS + lx - 1) * TC + 3];
                    else if (tL)
                        na = tL->pixels()[(ly * TS + TS - 1) * TC + 3];
                    if (na == 0)
                        vRaw.push_back({ x, y });
                }

                // Right neighbor (x+1, y) → vertical edge at x+1
                {
                    uint8_t na = 0;
                    if (lx + 1 < TS)
                        na = px[(ly * TS + lx + 1) * TC + 3];
                    else if (tR)
                        na = tR->pixels()[(ly * TS + 0) * TC + 3];
                    if (na == 0)
                        vRaw.push_back({ x + 1, y });
                }

                // Top neighbor (x, y-1) → horizontal edge at y
                {
                    uint8_t na = 0;
                    if (ly > 0)
                        na = px[((ly - 1) * TS + lx) * TC + 3];
                    else if (tU)
                        na = tU->pixels()[((TS - 1) * TS + lx) * TC + 3];
                    if (na == 0)
                        hRaw.push_back({ x, y });
                }

                // Bottom neighbor (x, y+1) → horizontal edge at y+1
                {
                    uint8_t na = 0;
                    if (ly + 1 < TS)
                        na = px[((ly + 1) * TS + lx) * TC + 3];
                    else if (tD)
                        na = tD->pixels()[(lx) *TC + 3];
                    if (na == 0)
                        hRaw.push_back({ x, y + 1 });
                }
            }
        }
    }

    // ---- Step 2: Sort, deduplicate, and merge collinear edges ----

    auto dedup = [](std::vector<RawEdge>& v) {
        size_t out = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0 && v[i].a == v[i - 1].a && v[i].b == v[i - 1].b)
                continue;
            v[out++] = v[i];
        }
        v.resize(out);
    };

    // Horizontal edges: sort by (y, x), deduplicate, merge consecutive x at same y
    std::sort(hRaw.begin(), hRaw.end(),
        [](const RawEdge& a, const RawEdge& b) { return a.b < b.b || (a.b == b.b && a.a < b.a); });
    dedup(hRaw);

    m_edges.reserve(hRaw.size() / 4 + vRaw.size() / 4);

    for (size_t i = 0; i < hRaw.size();) {
        const int32_t row = hRaw[i].b;
        int32_t startX = hRaw[i].a;
        int32_t endX = startX + 1;
        size_t j = i + 1;
        while (j < hRaw.size() && hRaw[j].b == row && hRaw[j].a == endX) {
            ++endX;
            ++j;
        }
        m_edges.push_back({ { static_cast<float>(startX), static_cast<float>(row) },
            { static_cast<float>(endX), static_cast<float>(row) } });
        i = j;
    }

    // Vertical edges: sort by (x, y), deduplicate, merge consecutive y at same x
    std::sort(vRaw.begin(), vRaw.end(),
        [](const RawEdge& a, const RawEdge& b) { return a.a < b.a || (a.a == b.a && a.b < b.b); });
    dedup(vRaw);

    for (size_t i = 0; i < vRaw.size();) {
        const int32_t col = vRaw[i].a;
        int32_t startY = vRaw[i].b;
        int32_t endY = startY + 1;
        size_t j = i + 1;
        while (j < vRaw.size() && vRaw[j].a == col && vRaw[j].b == endY) {
            ++endY;
            ++j;
        }
        m_edges.push_back({ { static_cast<float>(col), static_cast<float>(startY) },
            { static_cast<float>(col), static_cast<float>(endY) } });
        i = j;
    }
}

} // namespace aether
