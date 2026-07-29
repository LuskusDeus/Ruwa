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
#include "features/fill/FloodFill.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

aether::MaskTileSnapshot fullCanvasSelectionMask(uint32_t width, uint32_t height)
{
    aether::MaskTileSnapshot mask;
    const uint32_t tileColumns = (width + aether::TILE_SIZE - 1) / aether::TILE_SIZE;
    const uint32_t tileRows = (height + aether::TILE_SIZE - 1) / aether::TILE_SIZE;
    mask.reserve(static_cast<size_t>(tileColumns) * tileRows);
    for (uint32_t tileY = 0; tileY < tileRows; ++tileY) {
        const uint32_t validHeight
            = std::min(aether::TILE_SIZE, height - tileY * aether::TILE_SIZE);
        for (uint32_t tileX = 0; tileX < tileColumns; ++tileX) {
            const uint32_t validWidth
                = std::min(aether::TILE_SIZE, width - tileX * aether::TILE_SIZE);
            const aether::TileKey key { static_cast<int32_t>(tileX), static_cast<int32_t>(tileY) };

            // A tile the canvas covers completely is uniform; only the tiles
            // clipped by the right / bottom canvas edge need real pixels.
            if (validWidth == aether::TILE_SIZE && validHeight == aether::TILE_SIZE) {
                mask.emplace(key, aether::makeUniformMaskTile(255, 255, 255, 255));
                continue;
            }

            std::vector<uint8_t> tile(aether::TILE_BYTE_SIZE, 0);
            for (uint32_t localY = 0; localY < validHeight; ++localY) {
                const size_t rowStart
                    = static_cast<size_t>(localY) * aether::TILE_SIZE * aether::TILE_CHANNELS;
                std::fill_n(tile.data() + rowStart,
                    static_cast<size_t>(validWidth) * aether::TILE_CHANNELS, uint8_t { 255 });
            }
            mask.emplace(key, std::move(tile));
        }
    }
    return mask;
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

    // Mask tiles are left dirty on purpose. Uploading the whole mask here costs
    // one texture allocation plus 256 KB of PCIe traffic per tile — ~150 MB on a
    // 6000x6000 selection, on the GUI thread, for tiles most of which no
    // consumer will ever sample. Every consumer that binds a mask tile texture
    // (the brush renderer, the fill blit, the transform mask atlas) already
    // uploads on demand and honors the dirty flag, so the work now happens
    // lazily and only where it is needed.

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

std::optional<MagicWandSelectionRequest> CanvasSelectionController::prepareMagicWandSelection(
    int worldX, int worldY, bool addSelection, bool subtractSelection) const
{
    if (!m_ctx.getCanvas || !m_ctx.getActiveLayer) {
        return std::nullopt;
    }

    const Canvas& canvas = m_ctx.getCanvas();
    if (!selectionUsesFiniteDocumentBounds(canvas) || worldX < 0 || worldY < 0
        || worldX >= static_cast<int>(canvas.width())
        || worldY >= static_cast<int>(canvas.height())) {
        return std::nullopt;
    }

    auto* layer = m_ctx.getActiveLayer();
    if (!layer || (!layer->isPixelLayer() && !layer->isBackground())) {
        return std::nullopt;
    }

    LassoSelectionMode mode = LassoSelectionMode::Replace;
    if (subtractSelection) {
        mode = LassoSelectionMode::Subtract;
    } else if (addSelection) {
        mode = LassoSelectionMode::Add;
    }
    if (mode == LassoSelectionMode::Subtract && m_lassoSelection.mask().empty()) {
        return std::nullopt;
    }

    MagicWandSelectionRequest request;
    request.sourceLayerId = layer->id;
    request.seedX = worldX;
    request.seedY = worldY;
    request.canvasWidth = canvas.width();
    request.canvasHeight = canvas.height();
    request.mode = mode;

    if (layer->isBackground() && !layer->backgroundTransparent) {
        request.selectFullCanvas = true;
    } else {
        std::shared_ptr<TileGrid> effectShapedGrid
            = m_ctx.getEffectShapedGrid ? m_ctx.getEffectShapedGrid(layer) : nullptr;
        const TileGrid* sourceGrid = effectShapedGrid
            ? effectShapedGrid.get()
            : (m_ctx.getCompositingGridForLayer ? m_ctx.getCompositingGridForLayer(layer)
                                                : nullptr);
        TileGrid emptySource;
        if (!sourceGrid) {
            sourceGrid = &emptySource;
        }
        request.sourceFormat = sourceGrid->format();
        request.sourceTiles = snapshotContentTiles(*sourceGrid);
    }

    return request;
}

MaskTileSnapshot CanvasSelectionController::computeMagicWandSelection(
    MagicWandSelectionRequest request)
{
    if (request.selectFullCanvas) {
        return fullCanvasSelectionMask(request.canvasWidth, request.canvasHeight);
    }

    return buildMagicWandSelectionMask(request.sourceTiles, request.seedX, request.seedY,
        static_cast<int>(request.canvasWidth), static_cast<int>(request.canvasHeight),
        request.sourceFormat);
}

bool CanvasSelectionController::applyMagicWandSelection(const MaskTileSnapshot& wandMask,
    LassoSelectionMode mode, uint32_t canvasWidth, uint32_t canvasHeight)
{
    if (mode == LassoSelectionMode::Subtract && m_lassoSelection.mask().empty()) {
        return false;
    }

    auto* tileRenderer = m_ctx.getTileRenderer ? m_ctx.getTileRenderer() : nullptr;
    if (tileRenderer) {
        LassoSelectionManager::MaskMutationScope scope(m_lassoSelection);
        scope.disableSoftAlphaInvalidation();
        for (auto& [key, tile] : scope.grid().tiles()) {
            (void) key;
            if (tile.hasTexture()) {
                tileRenderer->destroyTileTexture(tile);
            }
        }
        scope.disableSnapshotInvalidation();
    }

    m_lassoSelection.applyRasterSelectionMask(wandMask, mode, canvasWidth, canvasHeight);
    m_contentSelectionSourceLayerId = QUuid();

    // As in commitPolygonSelection: the tiles stay dirty and are uploaded by
    // whichever consumer first samples them.

    if (m_ctx.requestRender) {
        m_ctx.requestRender();
    }
    return true;
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
        // Filled tile by tile rather than pixel by pixel: the coverage is
        // uniform, so each tile is one lookup and one memset per scanline
        // instead of a hash lookup and a bounds-checked write per pixel.
        const int tileSize = static_cast<int>(TILE_SIZE);
        const int lastTileX = (canvasW - 1) / tileSize;
        const int lastTileY = (canvasH - 1) / tileSize;
        for (int tileY = 0; tileY <= lastTileY; ++tileY) {
            const uint32_t rowCount
                = static_cast<uint32_t>(std::min(tileSize, canvasH - tileY * tileSize));
            for (int tileX = 0; tileX <= lastTileX; ++tileX) {
                const uint32_t columnCount
                    = static_cast<uint32_t>(std::min(tileSize, canvasW - tileX * tileSize));
                TileData& dstTile = maskGrid.getOrCreateTile(TileKey { tileX, tileY });
                uint8_t* pixels = dstTile.pixels();
                for (uint32_t localY = 0; localY < rowCount; ++localY) {
                    std::memset(pixels + static_cast<size_t>(localY) * TILE_SIZE * TILE_CHANNELS,
                        bgAlpha, static_cast<size_t>(columnCount) * TILE_CHANNELS);
                }
                dstTile.markDirty();
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

void CanvasSelectionController::selectActiveLayerMask()
{
    clearSelectionMask();

    auto* layer = m_ctx.getActiveLayer ? m_ctx.getActiveLayer() : nullptr;
    if (!layer || !layer->hasMask())
        return;
    const TileGrid* maskSource = layer->maskTileGrid();
    if (!maskSource)
        return;

    if (!m_ctx.getCanvas)
        return;
    const Canvas& canvas = m_ctx.getCanvas();
    const int canvasW = static_cast<int>(canvas.width());
    const int canvasH = static_cast<int>(canvas.height());
    if (canvasW <= 0 || canvasH <= 0)
        return;

    // Selection coverage mirrors what the compositor reveals through the mask:
    // reveal = lum(premultiplied rgb) + (1 - a), so mid-grays stay mid-coverage
    // instead of collapsing to fully in/out.
    auto revealToCoverage = [](uint8_t pr, uint8_t pg, uint8_t pb, uint8_t a) -> uint8_t {
        const float lum = (0.299f * pr + 0.587f * pg + 0.114f * pb) / 255.0f;
        const float reveal = qBound(0.0f, lum + (1.0f - static_cast<float>(a) / 255.0f), 1.0f);
        return static_cast<uint8_t>(qBound(0, static_cast<int>(reveal * 255.0f + 0.5f), 255));
    };

    // Tiles the mask never allocated read as its default fill — a reveal-all
    // mask (transparent default) therefore selects everything outside the
    // painted tiles, a hide-all mask (opaque black) selects nothing there.
    uint8_t dr = 0, dg = 0, db = 0, da = 0;
    maskSource->defaultFill(dr, dg, db, da);
    const uint8_t defaultCoverage = revealToCoverage(dr, dg, db, da);

    LassoSelectionManager::MaskMutationScope maskScope(m_lassoSelection);
    // Soft-alpha state is set explicitly below via setMaskHasSoftAlpha().
    maskScope.disableSoftAlphaInvalidation();
    TileGrid& maskGrid = maskScope.grid();
    bool hasMaskContent = false;
    bool hasSoftAlpha = (defaultCoverage > 0 && defaultCoverage < 255);

    const int tileSize = static_cast<int>(TILE_SIZE);
    auto stampTile = [&](const TileKey& key, const TileData* srcTile) {
        const int baseX = key.x * tileSize;
        const int baseY = key.y * tileSize;
        if (!srcTile && defaultCoverage == 0)
            return;

        TileData* dstTile = nullptr;
        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int y = baseY + static_cast<int>(localY);
            if (y < 0 || y >= canvasH)
                continue;
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int x = baseX + static_cast<int>(localX);
                if (x < 0 || x >= canvasW)
                    continue;

                uint8_t coverage = defaultCoverage;
                if (srcTile) {
                    uint8_t pr = 0, pg = 0, pb = 0, a = 0;
                    srcTile->getPixel(localX, localY, pr, pg, pb, a);
                    coverage = revealToCoverage(pr, pg, pb, a);
                }
                if (coverage == 0)
                    continue;
                if (coverage < 255)
                    hasSoftAlpha = true;
                if (!dstTile)
                    dstTile = &maskGrid.getOrCreateTile(key);
                dstTile->setPixel(localX, localY, coverage, coverage, coverage, coverage);
                hasMaskContent = true;
            }
        }
    };

    if (defaultCoverage == 0) {
        // Only the painted tiles can contribute — skip the empty document area.
        for (const auto& [key, srcTile] : maskSource->tiles()) {
            stampTile(key, &srcTile);
        }
    } else {
        const int lastTileX = (canvasW - 1) / tileSize;
        const int lastTileY = (canvasH - 1) / tileSize;
        for (int tileY = 0; tileY <= lastTileY; ++tileY) {
            for (int tileX = 0; tileX <= lastTileX; ++tileX) {
                const TileKey key { tileX, tileY };
                stampTile(key, maskSource->getTile(key));
            }
        }
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
    // Deliberately not tagged as a content selection: the soft coverage comes
    // from the mask, not from this layer's own alpha, so painting must not
    // preserve-alpha against it.
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
