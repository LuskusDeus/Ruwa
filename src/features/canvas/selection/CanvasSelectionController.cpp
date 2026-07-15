// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S E L E C T I O N   C O N T R O L L E R
// ==========================================================================

#include "CanvasSelectionController.h"
#include "PolygonClipUtils.h"
#include "features/canvas/scene/Canvas.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/selection/GLSelectionRenderer.h"
#include "features/canvas/rendering/GLRenderer.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileTypes.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kCircleSegmentCount = 64;
constexpr float kTwoPi = 6.283185307179586f;

bool selectionUsesFiniteDocumentBounds(const aether::Canvas& canvas)
{
    return canvas.hasFiniteDocumentBounds();
}

uint32_t selectionDocumentWidth(const aether::Canvas& canvas)
{
    return selectionUsesFiniteDocumentBounds(canvas) ? canvas.width() : 0;
}

uint32_t selectionDocumentHeight(const aether::Canvas& canvas)
{
    return selectionUsesFiniteDocumentBounds(canvas) ? canvas.height() : 0;
}

} // namespace

namespace aether {

CanvasSelectionController::CanvasSelectionController(const CanvasSelectionContext& ctx)
    : m_ctx(ctx)
{
}

CanvasSelectionController::~CanvasSelectionController()
{
    if (m_ctx.getSelectionRenderer && m_pendingSelectionReadback.active) {
        auto* sr = m_ctx.getSelectionRenderer();
        if (sr) {
            sr->deleteFence(m_pendingSelectionReadback.fence);
        }
    }
}

void CanvasSelectionController::beginLasso(
    float worldX, float worldY, bool addSelection, bool subtractSelection)
{
    m_isLassoActive = true;
    m_selectionWillReplace = !addSelection && !subtractSelection;
    m_selectionIsAdd = addSelection;
    m_selectionIsSubtract = subtractSelection;
    m_lassoPoints.clear();
    m_lassoPoints.emplace_back(worldX, worldY);
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::updateLasso(float worldX, float worldY)
{
    if (!m_isLassoActive)
        return;
    if (m_lassoPoints.empty()) {
        m_lassoPoints.emplace_back(worldX, worldY);
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }
    const Vector2& last = m_lassoPoints.back();
    float dx = worldX - last.x;
    float dy = worldY - last.y;
    float zoom = m_ctx.getZoom ? m_ctx.getZoom() : 1.0f;
    float minDist = 2.0f / zoom;
    if ((dx * dx + dy * dy) < (minDist * minDist))
        return;

    m_lassoPoints.emplace_back(worldX, worldY);
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::commitPolygonSelection(
    std::vector<Vector2> clipped, LassoSelectionMode mode)
{
    if (clipped.size() < 3)
        return;

    m_contentSelectionSourceLayerId = QUuid();

    auto* tileRenderer = m_ctx.getTileRenderer ? m_ctx.getTileRenderer() : nullptr;
    auto* selectionRenderer = m_ctx.getSelectionRenderer ? m_ctx.getSelectionRenderer() : nullptr;

    if (mode == LassoSelectionMode::Replace && tileRenderer) {
        LassoSelectionManager::MaskMutationScope scope(m_lassoSelection);
        scope.disableSoftAlphaInvalidation(); // applySelection() below resets soft-alpha state
                                              // authoritatively
        TileGrid& maskGrid = scope.grid();
        for (auto& [key, tile] : maskGrid.tiles()) {
            if (tile.hasTexture()) {
                tileRenderer->destroyTileTexture(tile);
            }
        }
        maskGrid.clear();
    }

    if (selectionRenderer && m_pendingSelectionReadback.active) {
        selectionRenderer->deleteFence(m_pendingSelectionReadback.fence);
        m_pendingSelectionReadback = {};
    }
    m_pendingSelectionJob = {};

    if (!m_ctx.getCanvas) {
        return;
    }
    const Canvas& canvas = m_ctx.getCanvas();
    const uint32_t cw = selectionDocumentWidth(canvas);
    const uint32_t ch = selectionDocumentHeight(canvas);

    // Scanline polygon fill (same core algorithm as lasso fill / previewFillPolygonMask), not GPU
    // ear-clip triangulation — avoids jagged self-intersecting strokes.
    m_lassoSelection.applySelection(std::move(clipped), mode, cw, ch, 255);

    if (tileRenderer) {
        LassoSelectionManager::MaskMutationScope scope(m_lassoSelection);
        scope.disableSoftAlphaInvalidation(); // GPU upload only — pixel data not mutated here
        for (auto& [key, tile] : scope.grid().tiles()) {
            if (!tile.isDirty()) {
                continue;
            }
            tileRenderer->ensureTileTexture(tile);
            tileRenderer->uploadTileData(tile);
        }
        scope.disableSnapshotInvalidation();
    }

    if (m_ctx.requestRender) {
        m_ctx.requestRender();
    }
}

void CanvasSelectionController::clearSelectionInternal()
{
    auto* selectionRenderer = m_ctx.getSelectionRenderer ? m_ctx.getSelectionRenderer() : nullptr;
    if (selectionRenderer && m_pendingSelectionReadback.active) {
        selectionRenderer->deleteFence(m_pendingSelectionReadback.fence);
    }
    m_pendingSelectionReadback = {};
    m_pendingSelectionJob = {};

    auto* tileRenderer = m_ctx.getTileRenderer ? m_ctx.getTileRenderer() : nullptr;
    if (tileRenderer) {
        LassoSelectionManager::MaskMutationScope scope(m_lassoSelection);
        scope.disableSoftAlphaInvalidation(); // clear() below resets soft-alpha state
                                              // authoritatively
        for (auto& [key, tile] : scope.grid().tiles()) {
            if (tile.hasTexture()) {
                tileRenderer->destroyTileTexture(tile);
            }
        }
    }
    m_lassoSelection.clear();
    m_contentSelectionSourceLayerId = QUuid();
}

void CanvasSelectionController::endLasso(bool addSelection, bool subtractSelection)
{
    if (!m_isLassoActive)
        return;
    m_isLassoActive = false;
    m_selectionWillReplace = false;
    m_selectionIsAdd = false;
    m_selectionIsSubtract = false;

    if (m_lassoPoints.size() < 3) {
        if (!addSelection && !subtractSelection) {
            clearSelectionInternal();
        }
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    const Vector2& first = m_lassoPoints.front();
    const Vector2& last = m_lassoPoints.back();
    float dx = first.x - last.x;
    float dy = first.y - last.y;
    if ((dx * dx + dy * dy) > 0.01f) {
        m_lassoPoints.push_back(first);
    }

    LassoSelectionMode mode = LassoSelectionMode::Replace;
    if (subtractSelection)
        mode = LassoSelectionMode::Subtract;
    else if (addSelection)
        mode = LassoSelectionMode::Add;

    if (mode == LassoSelectionMode::Subtract && m_lassoSelection.mask().empty()) {
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    bool clipToCanvas = false;
    float cw = 0.0f, ch = 0.0f;
    if (m_ctx.getCanvas) {
        const Canvas& canvas = m_ctx.getCanvas();
        clipToCanvas = selectionUsesFiniteDocumentBounds(canvas);
        if (clipToCanvas) {
            cw = static_cast<float>(canvas.width());
            ch = static_cast<float>(canvas.height());
        }
    }
    std::vector<Vector2> clipped
        = clipToCanvas ? clipPolygonToCanvas(m_lassoPoints, cw, ch) : m_lassoPoints;
    if (clipped.size() < 3) {
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    commitPolygonSelection(std::move(clipped), mode);
    m_lassoPoints.clear();
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::beginRectSelection(
    float worldX, float worldY, bool addSelection, bool subtractSelection)
{
    m_isRectSelectionActive = true;
    m_selectionWillReplace = !addSelection && !subtractSelection;
    m_selectionIsAdd = addSelection;
    m_selectionIsSubtract = subtractSelection;
    m_rectStartX = worldX;
    m_rectStartY = worldY;
    m_lassoPoints.clear();
    m_lassoPoints.emplace_back(worldX, worldY);
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::updateRectSelection(float worldX, float worldY)
{
    if (!m_isRectSelectionActive)
        return;
    m_lassoPoints.clear();
    float x0 = std::min(m_rectStartX, worldX);
    float x1 = std::max(m_rectStartX, worldX);
    float y0 = std::min(m_rectStartY, worldY);
    float y1 = std::max(m_rectStartY, worldY);
    m_lassoPoints.emplace_back(x0, y0);
    m_lassoPoints.emplace_back(x1, y0);
    m_lassoPoints.emplace_back(x1, y1);
    m_lassoPoints.emplace_back(x0, y1);
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

bool CanvasSelectionController::liveRectBoundsWorld(QRectF& out) const
{
    if (!m_isRectSelectionActive || m_lassoPoints.size() < 4)
        return false;
    // updateRectSelection stores the corners normalized as (x0,y0),(x1,y0),(x1,y1),(x0,y1).
    const float x0 = m_lassoPoints[0].x;
    const float y0 = m_lassoPoints[0].y;
    const float x1 = m_lassoPoints[2].x;
    const float y1 = m_lassoPoints[2].y;
    out = QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized();
    return true;
}

void CanvasSelectionController::endRectSelection(bool addSelection, bool subtractSelection)
{
    if (!m_isRectSelectionActive)
        return;
    m_isRectSelectionActive = false;
    m_selectionWillReplace = false;
    m_selectionIsAdd = false;
    m_selectionIsSubtract = false;

    if (m_lassoPoints.size() < 3) {
        if (!addSelection && !subtractSelection) {
            clearSelectionInternal();
        }
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    LassoSelectionMode mode = LassoSelectionMode::Replace;
    if (subtractSelection)
        mode = LassoSelectionMode::Subtract;
    else if (addSelection)
        mode = LassoSelectionMode::Add;

    if (mode == LassoSelectionMode::Subtract && m_lassoSelection.mask().empty()) {
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    bool clipToCanvas = false;
    float cw = 0.0f, ch = 0.0f;
    if (m_ctx.getCanvas) {
        const Canvas& canvas = m_ctx.getCanvas();
        clipToCanvas = selectionUsesFiniteDocumentBounds(canvas);
        if (clipToCanvas) {
            cw = static_cast<float>(canvas.width());
            ch = static_cast<float>(canvas.height());
        }
    }
    std::vector<Vector2> clipped
        = clipToCanvas ? clipPolygonToCanvas(m_lassoPoints, cw, ch) : m_lassoPoints;
    if (clipped.size() < 3) {
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    commitPolygonSelection(std::move(clipped), mode);
    m_lassoPoints.clear();
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::beginCircleSelection(
    float worldX, float worldY, bool addSelection, bool subtractSelection)
{
    m_isCircleSelectionActive = true;
    m_selectionWillReplace = !addSelection && !subtractSelection;
    m_selectionIsAdd = addSelection;
    m_selectionIsSubtract = subtractSelection;
    m_circleStartX = worldX;
    m_circleStartY = worldY;
    m_lassoPoints.clear();
    m_lassoPoints.emplace_back(worldX, worldY);
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::updateCircleSelection(float worldX, float worldY)
{
    if (!m_isCircleSelectionActive)
        return;
    float x0 = std::min(m_circleStartX, worldX);
    float x1 = std::max(m_circleStartX, worldX);
    float y0 = std::min(m_circleStartY, worldY);
    float y1 = std::max(m_circleStartY, worldY);
    float semiA = (x1 - x0) * 0.5f;
    float semiB = (y1 - y0) * 0.5f;
    float centerX = (x0 + x1) * 0.5f;
    float centerY = (y0 + y1) * 0.5f;
    if (semiA < 0.5f && semiB < 0.5f) {
        m_lassoPoints.clear();
        m_lassoPoints.emplace_back(centerX, centerY);
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }
    m_lassoPoints.clear();
    m_lassoPoints.reserve(kCircleSegmentCount);
    for (int i = 0; i < kCircleSegmentCount; ++i) {
        float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kCircleSegmentCount);
        m_lassoPoints.emplace_back(
            centerX + semiA * std::cos(angle), centerY + semiB * std::sin(angle));
    }
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::translateActiveSelection(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f)
        return;
    if (!m_isLassoActive && !m_isRectSelectionActive && !m_isCircleSelectionActive)
        return;

    for (auto& point : m_lassoPoints) {
        point.x += dx;
        point.y += dy;
    }
    if (m_isRectSelectionActive) {
        m_rectStartX += dx;
        m_rectStartY += dy;
    }
    if (m_isCircleSelectionActive) {
        m_circleStartX += dx;
        m_circleStartY += dy;
    }
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::endCircleSelection(bool addSelection, bool subtractSelection)
{
    if (!m_isCircleSelectionActive)
        return;
    m_isCircleSelectionActive = false;
    m_selectionWillReplace = false;
    m_selectionIsAdd = false;
    m_selectionIsSubtract = false;

    if (m_lassoPoints.size() < 3) {
        if (!addSelection && !subtractSelection) {
            clearSelectionInternal();
        }
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    LassoSelectionMode mode = LassoSelectionMode::Replace;
    if (subtractSelection)
        mode = LassoSelectionMode::Subtract;
    else if (addSelection)
        mode = LassoSelectionMode::Add;

    if (mode == LassoSelectionMode::Subtract && m_lassoSelection.mask().empty()) {
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    bool clipToCanvas = false;
    float cw = 0.0f, ch = 0.0f;
    if (m_ctx.getCanvas) {
        const Canvas& canvas = m_ctx.getCanvas();
        clipToCanvas = selectionUsesFiniteDocumentBounds(canvas);
        if (clipToCanvas) {
            cw = static_cast<float>(canvas.width());
            ch = static_cast<float>(canvas.height());
        }
    }
    std::vector<Vector2> clipped
        = clipToCanvas ? clipPolygonToCanvas(m_lassoPoints, cw, ch) : m_lassoPoints;
    if (clipped.size() < 3) {
        m_lassoPoints.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    commitPolygonSelection(std::move(clipped), mode);
    m_lassoPoints.clear();
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::clearSelectionMask()
{
    m_isLassoActive = false;
    m_isRectSelectionActive = false;
    m_isCircleSelectionActive = false;
    m_lassoPoints.clear();
    clearSelectionInternal();
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

void CanvasSelectionController::selectActiveLayerContent()
{
    clearSelectionMask();

    auto* layer = m_ctx.getActiveLayer ? m_ctx.getActiveLayer() : nullptr;
    if (!layer)
        return;

    if (!m_ctx.getCanvas)
        return;
    const Canvas& canvas = m_ctx.getCanvas();
    const int canvasW = static_cast<int>(canvas.width());
    const int canvasH = static_cast<int>(canvas.height());
    if (canvasW <= 0 || canvasH <= 0)
        return;

    LassoSelectionManager::MaskMutationScope maskScope(m_lassoSelection);
    // Soft-alpha state is set explicitly below via setMaskHasSoftAlpha(); avoid
    // the scope's destructor overwriting it with markMaskSoftAlphaUnknown().
    maskScope.disableSoftAlphaInvalidation();
    TileGrid& maskGrid = maskScope.grid();
    bool hasMaskContent = false;
    bool hasSoftAlpha = false;

    // Prefer the effect-processed shape so distortion/blur effects that reshape
    // the visible silhouette are traced by the selection. The baked grid is a
    // throwaway clone we must keep alive for the whole read loop below; when the
    // layer has no bakeable effects this stays null and we read raw content.
    std::shared_ptr<TileGrid> effectShapedGrid
        = m_ctx.getEffectShapedGrid ? m_ctx.getEffectShapedGrid(layer) : nullptr;
    const TileGrid* compositingGrid = effectShapedGrid
        ? effectShapedGrid.get()
        : (m_ctx.getCompositingGridForLayer ? m_ctx.getCompositingGridForLayer(layer) : nullptr);

    if (layer->isBackground() && !layer->backgroundTransparent) {
        const uint8_t bgAlpha
            = static_cast<uint8_t>(qBound(0, layer->backgroundColor.alpha(), 255));
        if (bgAlpha == 0)
            return;
        hasSoftAlpha = (bgAlpha < 255);
        for (int y = 0; y < canvasH; ++y) {
            const int tileY = y / static_cast<int>(TILE_SIZE);
            const uint32_t localY = static_cast<uint32_t>(y % static_cast<int>(TILE_SIZE));
            for (int x = 0; x < canvasW; ++x) {
                const int tileX = x / static_cast<int>(TILE_SIZE);
                const uint32_t localX = static_cast<uint32_t>(x % static_cast<int>(TILE_SIZE));
                TileData& dstTile = maskGrid.getOrCreateTile(TileKey { tileX, tileY });
                dstTile.setPixel(localX, localY, bgAlpha, bgAlpha, bgAlpha, bgAlpha);
                hasMaskContent = true;
            }
        }
    } else if (compositingGrid && !compositingGrid->empty()) {
        for (const auto& [key, srcTile] : compositingGrid->tiles()) {
            const int baseX = key.x * static_cast<int>(TILE_SIZE);
            const int baseY = key.y * static_cast<int>(TILE_SIZE);
            const uint8_t* srcPixels = srcTile.pixels();

            TileData* dstTile = nullptr;
            for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
                const int y = baseY + static_cast<int>(localY);
                if (y < 0 || y >= canvasH)
                    continue;
                for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                    const int x = baseX + static_cast<int>(localX);
                    if (x < 0 || x >= canvasW)
                        continue;
                    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
                    const uint8_t alpha = srcPixels[idx + 3];
                    if (alpha == 0)
                        continue;
                    if (alpha < 255)
                        hasSoftAlpha = true;
                    if (!dstTile)
                        dstTile = &maskGrid.getOrCreateTile(key);
                    dstTile->setPixel(localX, localY, alpha, alpha, alpha, alpha);
                    hasMaskContent = true;
                }
            }
        }
    } else {
        return;
    }

    if (!hasMaskContent) {
        m_lassoSelection.clear();
        if (m_ctx.requestRender)
            m_ctx.requestRender();
        return;
    }

    std::vector<Vector2> fullCanvasPolygon;
    fullCanvasPolygon.emplace_back(0.0f, 0.0f);
    fullCanvasPolygon.emplace_back(static_cast<float>(canvasW), 0.0f);
    fullCanvasPolygon.emplace_back(static_cast<float>(canvasW), static_cast<float>(canvasH));
    fullCanvasPolygon.emplace_back(0.0f, static_cast<float>(canvasH));
    m_lassoSelection.addRegion(fullCanvasPolygon, LassoSelectionMode::Replace);
    m_contentSelectionSourceLayerId = layer->id;
    m_lassoSelection.setMaskHasSoftAlpha(hasSoftAlpha);
    m_lassoSelection.rebuildEdgesFromMask(
        selectionDocumentWidth(canvas), selectionDocumentHeight(canvas));
    if (m_ctx.requestRender)
        m_ctx.requestRender();
}

bool CanvasSelectionController::hasSelectionMask() const
{
    return m_lassoSelection.hasSelection() && !m_lassoSelection.mask().empty();
}

bool CanvasSelectionController::selectionBoundsWorld(QRectF& outBounds) const
{
    outBounds = QRectF();
    if (!hasSelectionMask())
        return false;
    if (!m_ctx.getCanvas)
        return false;

    const Canvas& canvas = m_ctx.getCanvas();
    const bool clipToCanvas = selectionUsesFiniteDocumentBounds(canvas);
    bool hasAny = false;
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

    const auto& edges = m_lassoSelection.edges();
    if (!edges.empty()) {
        for (const auto& edge : edges) {
            const float xs[2] = { edge.a.x, edge.b.x };
            const float ys[2] = { edge.a.y, edge.b.y };
            for (int i = 0; i < 2; ++i) {
                const float x = clipToCanvas
                    ? qBound(0.0f, xs[i], static_cast<float>(canvas.width()))
                    : xs[i];
                const float y = clipToCanvas
                    ? qBound(0.0f, ys[i], static_cast<float>(canvas.height()))
                    : ys[i];
                if (!hasAny) {
                    minX = maxX = x;
                    minY = maxY = y;
                    hasAny = true;
                } else {
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }
        if (hasAny) {
            outBounds = QRectF(static_cast<qreal>(minX), static_cast<qreal>(minY),
                static_cast<qreal>(maxX - minX), static_cast<qreal>(maxY - minY));
            return !outBounds.isEmpty();
        }
    }

    int pxMinX = 0, pxMinY = 0, pxMaxX = 0, pxMaxY = 0;
    for (const auto& [key, tile] : m_lassoSelection.mask().tiles()) {
        const int baseX = key.x * static_cast<int>(TILE_SIZE);
        const int baseY = key.y * static_cast<int>(TILE_SIZE);
        const uint8_t* maskPixels = tile.pixels();
        for (int localY = 0; localY < static_cast<int>(TILE_SIZE); ++localY) {
            const int worldY = baseY + localY;
            if (clipToCanvas && (worldY < 0 || worldY >= static_cast<int>(canvas.height())))
                continue;
            for (int localX = 0; localX < static_cast<int>(TILE_SIZE); ++localX) {
                const int worldX = baseX + localX;
                if (clipToCanvas && (worldX < 0 || worldX >= static_cast<int>(canvas.width())))
                    continue;
                const uint32_t idx
                    = (static_cast<uint32_t>(localY) * TILE_SIZE + static_cast<uint32_t>(localX))
                    * TILE_CHANNELS;
                if (maskPixels[idx + 3] == 0)
                    continue;
                if (!hasAny) {
                    pxMinX = pxMaxX = worldX;
                    pxMinY = pxMaxY = worldY;
                    hasAny = true;
                } else {
                    pxMinX = std::min(pxMinX, worldX);
                    pxMinY = std::min(pxMinY, worldY);
                    pxMaxX = std::max(pxMaxX, worldX);
                    pxMaxY = std::max(pxMaxY, worldY);
                }
            }
        }
    }
    if (!hasAny)
        return false;
    outBounds = QRectF(static_cast<qreal>(pxMinX), static_cast<qreal>(pxMinY),
        static_cast<qreal>(pxMaxX - pxMinX + 1), static_cast<qreal>(pxMaxY - pxMinY + 1));
    return true;
}

bool CanvasSelectionController::fillSelectionWithColor(const QColor& color)
{
    if (m_ctx.isTransformActive && m_ctx.isTransformActive())
        return false;
    if (!hasSelectionMask())
        return false;
    if (m_ctx.executeFillWithColor)
        return m_ctx.executeFillWithColor(color);
    return false;
}

bool CanvasSelectionController::clearSelectionContent()
{
    if (m_ctx.isTransformActive && m_ctx.isTransformActive())
        return false;
    if (!hasSelectionMask())
        return false;
    if (m_ctx.executeClearSelectionContent)
        return m_ctx.executeClearSelectionContent();
    return false;
}

bool CanvasSelectionController::processSelectionReadbackFrame()
{
    auto* selectionRenderer = m_ctx.getSelectionRenderer ? m_ctx.getSelectionRenderer() : nullptr;
    auto* renderer = m_ctx.getRenderer ? m_ctx.getRenderer() : nullptr;

    if (selectionRenderer && m_pendingSelectionJob.active && renderer && renderer->tileRenderer()) {
        constexpr size_t kMaxTilesPerFrame = 32;
        // GPU-side polygon batching mutates mask tiles via tileRenderer / FBO writes.
        // Soft-alpha cache will be invalidated explicitly after CPU readback below
        // (markMaskSoftAlphaUnknown), so this scope only needs raw access.
        LassoSelectionManager::MaskMutationScope scope(m_lassoSelection);
        scope.disableSoftAlphaInvalidation();
        TileGrid& maskGrid = scope.grid();
        m_pendingSelectionJob.nextTile = selectionRenderer->applyPolygonBatch(maskGrid,
            renderer->tileRenderer(), m_pendingSelectionJob.triVerts, m_pendingSelectionJob.tiles,
            m_pendingSelectionJob.nextTile, kMaxTilesPerFrame, m_pendingSelectionJob.mode,
            m_pendingSelectionJob.strength, m_pendingSelectionJob.processed);
        if (m_pendingSelectionJob.nextTile >= m_pendingSelectionJob.tiles.size()) {
            if (!m_pendingSelectionJob.processed.empty()) {
                if (m_pendingSelectionReadback.active)
                    selectionRenderer->deleteFence(m_pendingSelectionReadback.fence);
                m_pendingSelectionReadback.active = true;
                m_pendingSelectionReadback.keys = std::move(m_pendingSelectionJob.processed);
                m_pendingSelectionReadback.fence = selectionRenderer->startAsyncReadback(
                    maskGrid, m_pendingSelectionReadback.keys);
            }
            m_pendingSelectionJob = {};
        }
        if (m_ctx.startSelectionTick)
            m_ctx.startSelectionTick();
    }

    if (selectionRenderer && m_pendingSelectionReadback.active) {
        if (selectionRenderer->isReadbackComplete(m_pendingSelectionReadback.fence)) {
            // GPU → CPU readback writes new pixel data into mask tiles. Scope's
            // automatic soft-alpha invalidation captures that the cached state
            // is now stale; markMaskSoftAlphaUnknown() below is therefore
            // redundant but kept for clarity / defense in depth.
            {
                LassoSelectionManager::MaskMutationScope scope(m_lassoSelection);
                selectionRenderer->finishReadback(m_pendingSelectionReadback.fence, scope.grid(),
                    m_pendingSelectionReadback.keys);
            }
            m_lassoSelection.markMaskSoftAlphaUnknown();
            const Canvas* canvas = m_ctx.getCanvas ? &m_ctx.getCanvas() : nullptr;
            m_lassoSelection.rebuildEdgesFromMask(canvas ? selectionDocumentWidth(*canvas) : 0,
                canvas ? selectionDocumentHeight(*canvas) : 0);
            m_pendingSelectionReadback = {};
            return true;
        }
        if (m_ctx.startSelectionTick)
            m_ctx.startSelectionTick();
    }
    return false;
}

void CanvasSelectionController::shutdown(GLSelectionRenderer* selectionRenderer)
{
    if (selectionRenderer && m_pendingSelectionReadback.active) {
        selectionRenderer->deleteFence(m_pendingSelectionReadback.fence);
    }
    m_pendingSelectionReadback = {};
    m_pendingSelectionJob = {};
}

} // namespace aether
