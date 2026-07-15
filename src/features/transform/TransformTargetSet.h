// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   T A R G E T   S E T
// ==========================================================================

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMTARGETSET_H
#define RUWA_CORE_TRANSFORM_TRANSFORMTARGETSET_H

#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include "features/transform/TransformState.h"

#include <QSet>
#include <QUuid>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>
#include <algorithm>

namespace aether {

struct TransformQUuidHash {
    size_t operator()(const QUuid& value) const noexcept
    {
        return static_cast<size_t>(qHash(value));
    }
};

struct TransformTargetInfo {
    enum class Kind { Raster, IsolatedPixel, Text };

    QUuid layerId;
    QUuid rootLayerId;
    Kind kind = Kind::Raster;
    Rect contentBounds {};
    bool usesRasterPixels = true;
};

struct TransformPreviewBlock {
    std::vector<QUuid> rootLayerIds;
    std::vector<QUuid> visualLayerIds;
    QUuid topInsertionLayerId;
    Rect sourceBounds {};
};

struct TransformTargetSet {
    std::vector<QUuid> rootLayerIds;
    std::vector<TransformTargetInfo> visualTargets;
    std::vector<TransformPreviewBlock> previewBlocks;
    Rect contentBounds {};

    bool empty() const { return visualTargets.empty(); }
    bool singleVisualTarget() const { return visualTargets.size() == 1; }
    bool containsVisualTarget(const QUuid& layerId) const
    {
        return visualTargetIds.find(layerId) != visualTargetIds.end();
    }
    void clear()
    {
        rootLayerIds.clear();
        visualTargets.clear();
        previewBlocks.clear();
        contentBounds = {};
        rootTargetIds.clear();
        visualTargetIds.clear();
        visualTargetRootIds.clear();
    }

    std::unordered_set<QUuid, TransformQUuidHash> rootTargetIds;
    std::unordered_set<QUuid, TransformQUuidHash> visualTargetIds;
    std::unordered_set<QUuid, TransformQUuidHash> visualTargetRootIds;
};

using TransformContentBoundsResolver
    = std::function<std::optional<Rect>(const ruwa::core::layers::LayerData*)>;

inline bool transformLayerHierarchyEditable(const ruwa::core::layers::LayerData* layer)
{
    for (auto* current = layer; current; current = current->parent) {
        if (!current->visible || current->locked || current->isBackground()) {
            return false;
        }
    }
    return layer != nullptr;
}

inline bool transformIsVisualTarget(const ruwa::core::layers::LayerData* layer)
{
    return layer && (layer->isRaster() || layer->isIsolatedPixelLayer() || layer->isText());
}

inline TransformTargetInfo::Kind transformTargetKind(const ruwa::core::layers::LayerData* layer)
{
    if (layer && layer->isText()) {
        return TransformTargetInfo::Kind::Text;
    }
    if (layer && layer->isIsolatedPixelLayer()) {
        return TransformTargetInfo::Kind::IsolatedPixel;
    }
    return TransformTargetInfo::Kind::Raster;
}

inline Rect unionTransformRects(const Rect& a, const Rect& b)
{
    if (a.width <= 0.0f || a.height <= 0.0f) {
        return b;
    }
    if (b.width <= 0.0f || b.height <= 0.0f) {
        return a;
    }
    const float left = std::min(a.left(), b.left());
    const float top = std::min(a.top(), b.top());
    const float right = std::max(a.right(), b.right());
    const float bottom = std::max(a.bottom(), b.bottom());
    return { left, top, right - left, bottom - top };
}

inline bool hasSelectedAncestor(
    const ruwa::core::layers::LayerData* layer, const QSet<QUuid>& selectedIds)
{
    for (auto* parent = layer ? layer->parent : nullptr; parent; parent = parent->parent) {
        if (selectedIds.contains(parent->id)) {
            return true;
        }
    }
    return false;
}

inline void collectTransformVisualDescendants(const ruwa::core::layers::LayerData* layer,
    const QUuid& rootLayerId, const TransformContentBoundsResolver& boundsResolver,
    std::vector<TransformTargetInfo>& out)
{
    if (!layer || !transformLayerHierarchyEditable(layer)) {
        return;
    }

    if (transformIsVisualTarget(layer)) {
        const std::optional<Rect> bounds = boundsResolver ? boundsResolver(layer) : std::nullopt;
        if (bounds.has_value() && bounds->width > 0.0f && bounds->height > 0.0f) {
            TransformTargetInfo info;
            info.layerId = layer->id;
            info.rootLayerId = rootLayerId;
            info.kind = transformTargetKind(layer);
            info.contentBounds = *bounds;
            info.usesRasterPixels = layer->isRaster();
            out.push_back(info);
        }
    }

    for (const auto& child : layer->children) {
        collectTransformVisualDescendants(child.get(), rootLayerId, boundsResolver, out);
    }
}

inline void collectTransformVisualStack(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers, std::vector<QUuid>& out)
{
    for (int i = layers.size() - 1; i >= 0; --i) {
        const auto& layer = layers[i];
        if (!layer || !layer->visible || layer->isBackground()) {
            continue;
        }
        if (transformIsVisualTarget(layer.get())) {
            out.push_back(layer->id);
        }
        if (layer->hasChildren()) {
            collectTransformVisualStack(layer->children, out);
        }
    }
}

inline TransformTargetSet buildTransformTargetSet(const ruwa::core::layers::LayerModel& layerModel,
    const TransformContentBoundsResolver& boundsResolver)
{
    TransformTargetSet set;
    const QList<ruwa::core::layers::LayerData*> selectedLayers = layerModel.selectedLayers();
    if (selectedLayers.isEmpty()) {
        return set;
    }

    QSet<QUuid> selectedIds;
    for (auto* layer : selectedLayers) {
        if (layer) {
            selectedIds.insert(layer->id);
        }
    }

    for (auto* layer : selectedLayers) {
        if (!layer || hasSelectedAncestor(layer, selectedIds)
            || !transformLayerHierarchyEditable(layer)) {
            continue;
        }

        const size_t beforeCount = set.visualTargets.size();
        collectTransformVisualDescendants(layer, layer->id, boundsResolver, set.visualTargets);
        if (set.visualTargets.size() != beforeCount) {
            set.rootLayerIds.push_back(layer->id);
            set.rootTargetIds.insert(layer->id);
        }
    }

    for (const TransformTargetInfo& target : set.visualTargets) {
        set.contentBounds = unionTransformRects(set.contentBounds, target.contentBounds);
        set.visualTargetIds.insert(target.layerId);
        set.visualTargetRootIds.insert(target.rootLayerId);
    }

    std::vector<QUuid> visualStack;
    collectTransformVisualStack(layerModel.rootLayers(), visualStack);

    TransformPreviewBlock block;
    auto flushBlock = [&]() {
        if (block.visualLayerIds.empty()) {
            return;
        }
        block.topInsertionLayerId = block.visualLayerIds.back();
        set.previewBlocks.push_back(std::move(block));
        block = {};
    };

    for (const QUuid& layerId : visualStack) {
        const bool selected = set.visualTargetIds.find(layerId) != set.visualTargetIds.end();
        if (!selected) {
            flushBlock();
            continue;
        }

        block.visualLayerIds.push_back(layerId);
        for (const TransformTargetInfo& target : set.visualTargets) {
            if (target.layerId != layerId) {
                continue;
            }
            block.sourceBounds = unionTransformRects(block.sourceBounds, target.contentBounds);
            if (std::find(block.rootLayerIds.begin(), block.rootLayerIds.end(), target.rootLayerId)
                == block.rootLayerIds.end()) {
                block.rootLayerIds.push_back(target.rootLayerId);
            }
            break;
        }
    }
    flushBlock();

    return set;
}

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMTARGETSET_H
