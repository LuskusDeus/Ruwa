// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T   C O M P O S I T O R
// ==========================================================================

#include "features/canvas/rendering/SmartContentCompositor.h"

#include "features/canvas/rendering/CompositeLayerKeys.h"
#include "features/canvas/rendering/GLCompositor.h"
#include "features/canvas/rendering/GLRenderer.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/SmartContent.h"
#include "features/layers/smart/SmartDocument.h"
#include "features/transform/TransformApplicator.h"
#include "features/transform/TransformGeometry.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileGridClone.h"

#include <QHash>
#include <QRect>
#include <QSize>

#include <cmath>
#include <functional>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aether {
namespace {

using ruwa::core::layers::LayerData;
using ruwa::core::layers::SmartContent;
using ruwa::core::layers::SmartDocument;

void forEachLayerInList(
    const QList<std::shared_ptr<LayerData>>& layers, const std::function<void(LayerData*)>& fn)
{
    for (const auto& layer : layers) {
        if (!layer) {
            continue;
        }
        fn(layer.get());
        forEachLayerInList(layer->children, fn);
    }
}

/**
 * Project every nested smart layer's content into the nested document's space.
 *
 * A smart layer inside a smart object places its own content through
 * `smartTransform`, exactly like one in the main document — where the widget
 * keeps those projections in `m_smartProjectedGrids`. A nested document has no
 * such cache, so the projections are built here, live only for the duration of
 * one composite, and are patched into the stack below.
 *
 * CPU projection on purpose: `GLTransformRenderer` owns ONE source atlas and ONE
 * readback PBO, and this can run while a transform readback is still pending
 * (a mask-transform commit on a smart layer does exactly that). Nested smart
 * layers are rare and this path is not per-frame, so the cheap correctness wins.
 *
 * Content-space filters are baked in first, exactly as
 * `OpenGLCanvasWidget::buildContentSpaceEffectedGrid` does for a layer of the
 * main document: they belong to the content, so they must be in the pixels
 * BEFORE the placement, and `buildOffscreenLayerStack` (via
 * `LayerData::documentSpaceEffects`) has already left them out of the chain the
 * composite runs. The bake is its own compositor batch, which is why it happens
 * here — before `compositeStackIntoGrid` opens the one this stack composites in.
 * @p compositor is null when there is no usable GPU; the layer then projects its
 * unfiltered content rather than nothing at all.
 */
void collectNestedProjections(const QList<std::shared_ptr<LayerData>>& layers,
    QHash<QUuid, TileGrid*>& outProjections, std::vector<std::shared_ptr<TileGrid>>& outOwners,
    GLCompositor* compositor, GLTileRenderer* tileRenderer)
{
    for (const auto& layer : layers) {
        if (!layer) {
            continue;
        }
        collectNestedProjections(
            layer->children, outProjections, outOwners, compositor, tileRenderer);

        if (!layer->isIsolatedPixelLayer()) {
            continue;
        }
        const TileGrid* source = layer->smartGrid();
        if (!source || source->empty()) {
            continue;
        }

        std::shared_ptr<TileGrid> baked;
        const auto contentEffects = layer->contentSpaceEffects();
        if (compositor && tileRenderer && !contentEffects.isEmpty()) {
            baked = cloneGridWithSolids(*source);
            std::vector<TileKey> touched;
            if (compositor->bakeEffectsIntoGrid(
                    *baked, contentEffects, tileRenderer, /*beforeTileWrite=*/nullptr, touched)) {
                // Projection sources stay CPU-backed, like every other grid here.
                for (auto& [key, tile] : baked->tiles()) {
                    if (tile.hasTexture()) {
                        tileRenderer->destroyTileTexture(tile);
                    }
                }
                source = baked.get();
                // Kept alive (and cache-purged) by the caller's owner list even
                // when only the transform below ends up being patched in.
                outOwners.push_back(baked);
            } else {
                baked.reset();
            }
        }

        // Re-anchored to the current content bounds, the same way
        // OpenGLCanvasWidget::buildSmartProjectedGrid and every measurement of a
        // smart layer do it.
        const TransformState state
            = transformStateWithSourceBounds(layer->smartTransform, layer->smartContentBounds());
        if (state.isIdentity()) {
            // Identity placement reads the content grid directly, which is what
            // LayerCompositingBuilder::compositingGridForLayer already returns —
            // unless a filter has just produced different pixels for it.
            if (baked && !baked->empty()) {
                outProjections.insert(layer->id, baked.get());
            }
            continue;
        }

        std::shared_ptr<TileGrid> projected = cloneGridWithSolids(*source);
        TransformApplicator::apply(*projected, state);
        if (projected->empty()) {
            continue;
        }
        outProjections.insert(layer->id, projected.get());
        outOwners.push_back(std::move(projected));
    }
}

/// Tile keys covering @p rect (document space, origin at 0,0).
void addRectTileKeys(const QRect& rect, std::unordered_set<TileKey, TileKeyHash>& outKeys)
{
    const double tileSize = static_cast<double>(TILE_SIZE);
    const int minTx = static_cast<int>(std::floor(static_cast<double>(rect.left()) / tileSize));
    const int minTy = static_cast<int>(std::floor(static_cast<double>(rect.top()) / tileSize));
    const int maxTx = static_cast<int>(std::floor(static_cast<double>(rect.right()) / tileSize));
    const int maxTy = static_cast<int>(std::floor(static_cast<double>(rect.bottom()) / tileSize));
    for (int ty = minTy; ty <= maxTy; ++ty) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            outKeys.insert(TileKey { tx, ty });
        }
    }
}

void patchProjectionsIntoStack(
    std::vector<CompositeLayerInfo>& stack, const QHash<QUuid, TileGrid*>& projections)
{
    for (CompositeLayerInfo& info : stack) {
        patchProjectionsIntoStack(info.children, projections);
        if (info.isGroup || info.id.isNull()) {
            continue;
        }
        const auto it = projections.constFind(info.id);
        if (it != projections.constEnd()) {
            info.tileGrid = it.value();
        }
    }
}

} // namespace

SmartContentCompositor::SmartContentCompositor(
    GLRenderer* renderer, const LayerCompositingBuilder* stackBuilder)
    : m_renderer(renderer)
    , m_stackBuilder(stackBuilder)
{
}

bool SmartContentCompositor::ensureComposite(SmartContent& content)
{
    QSet<QUuid> activePath;
    return ensureCompositeRecursive(content, 0, activePath);
}

bool SmartContentCompositor::ensureCompositeRecursive(
    SmartContent& content, int depth, QSet<QUuid>& activePath)
{
    if (!m_stackBuilder || content.compositeUpToDate()) {
        return false;
    }
    SmartDocument* document = content.document.get();
    if (!document) {
        // compositeUpToDate() is true without a document, so this is
        // unreachable; keeping the guard means the deref below never has to be
        // re-argued.
        return false;
    }

    if (depth >= kMaxNestingDepth || activePath.contains(content.contentId)) {
        // Nested too deep, or this content (transitively) contains itself. The
        // last valid pixels stay; stamping them as current is what stops the
        // impossible composite from being retried on every single refresh.
        content.markCompositeCurrent();
        return false;
    }

    // Nested contents first: this document composites THEIR composited pixels,
    // so a stale one inside would bake stale content into the parent.
    activePath.insert(content.contentId);
    forEachLayerInList(document->roots, [&](LayerData* layer) {
        if (layer->smartContent && layer->smartContent->document) {
            ensureCompositeRecursive(*layer->smartContent, depth + 1, activePath);
        }
    });
    activePath.remove(content.contentId);

    std::vector<CompositeLayerInfo> stack
        = m_stackBuilder->buildOffscreenLayerStack(document->roots);

    // The grids below are throwaway, and the compositor's cross-batch caches are
    // keyed on grid ADDRESSES: an entry outliving one would be revalidated
    // against whatever allocates that address next. The guard is declared before
    // they are built so every early return below is covered too.
    std::vector<std::shared_ptr<TileGrid>> projectionOwners;
    struct ProjectionCacheGuard {
        GLCompositor* compositor = nullptr;
        const std::vector<std::shared_ptr<TileGrid>>* grids = nullptr;
        ~ProjectionCacheGuard()
        {
            if (!compositor || !grids) {
                return;
            }
            for (const auto& grid : *grids) {
                compositor->dropWholeLayerCacheEntry(grid.get());
            }
        }
    } projectionCacheGuard { m_renderer ? m_renderer->compositor() : nullptr, &projectionOwners };

    // Baking a nested layer's content-space filters needs a live GPU; without
    // one the projection is built from the unfiltered content and the whole
    // composite is left stale anyway (see the renderer check below), so the next
    // sweep redoes it properly.
    const bool canBake = m_renderer && m_renderer->isInitialized();
    QHash<QUuid, TileGrid*> projections;
    collectNestedProjections(document->roots, projections, projectionOwners,
        canBake ? m_renderer->compositor() : nullptr,
        canBake ? m_renderer->tileRenderer() : nullptr);
    if (!projections.isEmpty()) {
        patchProjectionsIntoStack(stack, projections);
    }

    // The background of a nested document is composited as a real solid layer
    // by buildOffscreenLayerStack (there is no separate backdrop pass behind a
    // flattened smart object), so it is not passed as a backdrop colour here —
    // it is only asked about to know that the whole canvas is covered.
    Color backgroundColor = Color::transparent();
    const bool hasBackground = LayerCompositingBuilder::resolveBackgroundColorForLayers(
        document->roots, backgroundColor);

    const QSize canvasSize = document->size;
    const QRect canvasRect = (canvasSize.isValid() && !canvasSize.isEmpty())
        ? QRect(0, 0, canvasSize.width(), canvasSize.height())
        : QRect();

    std::unordered_set<TileKey, TileKeyHash> keys;
    collectCompositeLayerKeys(stack, keys);
    if (hasBackground && !canvasRect.isNull()) {
        // The background is a solid-colour layer with no tiles, so it
        // contributes no coverage of its own — but it fills the whole nested
        // canvas. Without these keys a background-only smart object (or the
        // area of it no layer paints on) would flatten to nothing. It is also
        // the one thing bounded by the canvas rect: an unbounded solid fill has
        // no meaning.
        addRectTileKeys(canvasRect, keys);
    }

    // NOT clipped to the nested canvas. Content space can legitimately extend
    // outside it — converting a layer that was painted past the document edge
    // puts pixels at NEGATIVE content coordinates, and the canvas is anchored at
    // (0,0) because the projection depends on that origin — so clipping here
    // would silently delete them on the first edit. Ruwa keeps off-canvas pixels
    // everywhere else too (they survive in the grid and reappear when the canvas
    // grows); a smart object's contents are no different.
    if (keys.empty()) {
        // An empty document — or one whose every layer is hidden — composites to
        // nothing. Installing that (rather than leaving the previous pixels) is
        // what makes deleting the last layer inside a smart object visible in
        // the parent, and it settles the cache so nothing retries.
        content.setCompositeGrid(std::make_unique<TileGrid>(), document->revision);
        return true;
    }

    if (!m_renderer || !m_renderer->isInitialized()) {
        // No GPU yet (the canvas has never been shown). The content keeps its
        // last valid composite and stays stale, so the next refresh retries.
        return false;
    }

    auto composited = std::make_unique<TileGrid>();
    if (!m_renderer->compositeStackIntoGrid(stack, keys, *composited)) {
        // The GPU had a context and still produced nothing readable. Retrying
        // that on every sweep would spin once per frame for as long as the
        // driver stays unhappy, so the last valid pixels are accepted as this
        // revision's composite; the next real edit bumps the revision and tries
        // again. (A missing renderer is the other, retryable case above.)
        content.markCompositeCurrent();
        return false;
    }

    content.setCompositeGrid(std::move(composited), document->revision);
    return true;
}

} // namespace aether
