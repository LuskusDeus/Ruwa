// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_RETAINEDRENDERPAYLOAD_H
#define RUWA_FEATURES_CANVAS_RENDERING_RETAINEDRENDERPAYLOAD_H

#include "features/transform/TransformState.h"
#include "shared/types/Types.h"
#include "shared/tiles/TileTypes.h"

#include <QRawFont>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace aether {

enum class RetainedPrimitiveType { FilledPolygon, ImageQuad, GlyphRun };

struct RetainedGlyphRun {
    std::string fontFamily;
    float fontSize = 0.0f;
    QRawFont rawFont;
    TransformState transform;
    Color color {};
    Rect sourceBounds {};
    Rect worldBounds {};
    std::vector<uint32_t> glyphIndexes;
    std::vector<Vector2> sourcePositions;
    std::vector<Vector2> worldPositions;

    bool empty() const { return glyphIndexes.empty() || sourcePositions.empty(); }
};

struct RetainedPrimitive {
    RetainedPrimitiveType type = RetainedPrimitiveType::FilledPolygon;
    Rect worldBounds {};
    std::vector<Vector2> points;
    Color color {};
    std::vector<RetainedGlyphRun> glyphRuns;

    bool isEmpty() const
    {
        if (type == RetainedPrimitiveType::FilledPolygon) {
            return points.size() < 3;
        }
        if (type == RetainedPrimitiveType::GlyphRun) {
            if (glyphRuns.empty()) {
                return true;
            }
            for (const RetainedGlyphRun& run : glyphRuns) {
                if (!run.empty()) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
};

struct RetainedRenderPayload {
    uint64_t revision = 0;
    Rect sourceBounds {};
    Rect worldBounds {};
    std::vector<RetainedPrimitive> primitives;

    bool empty() const
    {
        if (primitives.empty()) {
            return true;
        }
        for (const RetainedPrimitive& primitive : primitives) {
            if (!primitive.isEmpty()) {
                return false;
            }
        }
        return true;
    }
};

enum class LayerVisualBackend { RasterTiles, RetainedSimpleForms };

inline Rect retainedPolygonBounds(const std::vector<Vector2>& polygon)
{
    if (polygon.empty()) {
        return {};
    }

    float minX = polygon.front().x;
    float minY = polygon.front().y;
    float maxX = polygon.front().x;
    float maxY = polygon.front().y;
    for (const Vector2& point : polygon) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    return Rect { minX, minY, maxX - minX, maxY - minY };
}

inline std::unordered_set<TileKey, TileKeyHash> retainedCoverageTileKeys(const Rect& bounds)
{
    std::unordered_set<TileKey, TileKeyHash> keys;
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return keys;
    }

    const float maxX = bounds.x + bounds.width;
    const float maxY = bounds.y + bounds.height;
    const TileKey minKey = worldToTile(bounds.x, bounds.y);
    const TileKey maxKey
        = worldToTile(std::nextafter(maxX, bounds.x), std::nextafter(maxY, bounds.y));

    for (int32_t y = minKey.y; y <= maxKey.y; ++y) {
        for (int32_t x = minKey.x; x <= maxKey.x; ++x) {
            keys.insert(TileKey { x, y });
        }
    }
    return keys;
}

inline bool retainedPayloadIntersectsTile(const RetainedRenderPayload& payload, const TileKey& key)
{
    if (payload.empty()) {
        return false;
    }

    float tileX = 0.0f;
    float tileY = 0.0f;
    tileWorldOrigin(key, tileX, tileY);
    const Rect tileBounds { tileX, tileY, static_cast<float>(TILE_SIZE),
        static_cast<float>(TILE_SIZE) };
    return payload.worldBounds.intersects(tileBounds);
}

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_RETAINEDRENDERPAYLOAD_H
