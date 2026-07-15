// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C A N V A S   R E S I Z E   C O M M A N D
// ==========================================================================

#include "CanvasResizeCommand.h"

#include "features/canvas/grid/GridRemap.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/scene/Canvas.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileData.h"

#include <QtConcurrent>

#include <cstring>
#include <utility>
#include <vector>

namespace aether {

namespace {

struct PerLayerResizeJob {
    ruwa::core::layers::LayerData* layer = nullptr;
    TileGrid* grid = nullptr;
};

void collectResizeJobs(ruwa::core::layers::LayerModel* layerModel,
    std::vector<PerLayerResizeJob>& pixelJobs,
    std::vector<ruwa::core::layers::LayerData*>& transformedLayers)
{
    const auto layers = layerModel->allLayersFlattened();
    for (auto* layer : layers) {
        if (!layer)
            continue;
        if (layer->isText() && layer->textData) {
            transformedLayers.push_back(layer);
            continue;
        }
        if (layer->isIsolatedPixelLayer()) {
            transformedLayers.push_back(layer);
            continue;
        }
        auto* grid = layer->pixelGrid();
        if (!grid)
            continue;
        pixelJobs.push_back({ layer, grid });
    }
}

/// Copy pixels of specific tile keys into a RawTileMap (parallelized).
CanvasResizeCommand::RawTileMap snapshotTiles(
    const TileGrid& grid, const std::vector<TileKey>& keys)
{
    CanvasResizeCommand::RawTileMap result;
    result.reserve(keys.size());

    // Size the snapshot buffers by the grid's own pixel format so 16F/32F
    // content is captured in full (a fixed TILE_BYTE_SIZE would truncate it to
    // a 256 KB RGBA8 slice and drop the boundary content on undo).
    const size_t tileBytes = aether::tileByteSize(grid.format());

    // Pre-insert empty slots so the map is stable for the parallel fill phase.
    for (const auto& k : keys) {
        result.emplace(k, std::vector<uint8_t>(tileBytes));
    }

    // Index-driven parallel fill. Each worker writes to a distinct tile's buffer
    // (keys are unique), so no synchronisation is required.
    std::vector<std::size_t> indices(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        indices[i] = i;
    }

    QtConcurrent::blockingMap(indices, [&](std::size_t i) {
        const TileKey& key = keys[i];
        auto it = result.find(key);
        if (it == result.end())
            return;
        const TileData* tile = grid.getTile(key);
        if (tile) {
            std::memcpy(it->second.data(), tile->pixels(), tileBytes);
        } else {
            std::memset(it->second.data(), 0, tileBytes);
        }
    });

    return result;
}

/// Apply a remap to every pixel layer (smart layers handled separately by caller).
/// Runs the per-layer remap sequentially; each remap is internally parallel.
void applyRemapToLayers(const std::vector<PerLayerResizeJob>& jobs, int offsetX, int offsetY,
    int newWidth, int newHeight)
{
    for (const auto& job : jobs) {
        ruwa::core::canvas::remapGridForCanvasRect(
            *job.grid, offsetX, offsetY, newWidth, newHeight);
    }
}

void shiftLayerTransforms(
    const std::vector<ruwa::core::layers::LayerData*>& transformedLayers, int offsetX, int offsetY)
{
    for (auto* layer : transformedLayers) {
        if (layer->isText()) {
            layer->textData->transform.shiftForCanvasResize(offsetX, offsetY);
        } else {
            layer->smartTransform.shiftForCanvasResize(offsetX, offsetY);
        }
    }
}

void rebuildIndicesAndMarkDirty(Canvas& canvas, ruwa::core::layers::LayerModel* layerModel,
    const std::vector<PerLayerResizeJob>& pixelJobs,
    const std::vector<ruwa::core::layers::LayerData*>& transformedLayers,
    const std::vector<TileKey>& oldIndexedKeys)
{
    auto& index = canvas.tilePositionIndex();
    auto& cache = canvas.compositionCache();
    auto& dirtyManager = canvas.dirtyManager();

    for (const auto& job : pixelJobs) {
        index.rebuildForLayer(job.layer->id, job.grid->tiles());
        layerModel->notifyLayerDataChanged(job.layer->id);
    }
    for (auto* layer : transformedLayers) {
        if (layer->isText()) {
            layer->runtimeRetainedPayload.reset();
            layer->runtimeRetainedPayloadKey.clear();
        }
        // Triggers smart-layer projection rebuild or text retained-payload rebuild.
        layerModel->notifyLayerDataChanged(layer->id);
    }

    for (const auto& key : oldIndexedKeys) {
        cache.markDirty(key);
    }
    dirtyManager.onStructureChanged();
}

void notifyHostAfterApply(
    const CanvasResizeCommand::Hooks& hooks, QSize newSize, bool sizeChanged, bool geometryChanged)
{
    if (sizeChanged && hooks.setCanvasSize) {
        hooks.setCanvasSize(newSize);
    } else if (geometryChanged && hooks.requestRender) {
        hooks.requestRender();
    }
    if (geometryChanged && hooks.onContentChanged) {
        hooks.onContentChanged();
    }
}

} // namespace

// --------------------------------------------------------------------------
//   a p p l y R e s i z e   ( s t a t i c )
// --------------------------------------------------------------------------

CanvasResizeCommand::Snapshot CanvasResizeCommand::applyResize(OpenGLCanvasWidget* glWidget,
    ruwa::core::layers::LayerModel* layerModel, QSize oldSize, int offsetX, int offsetY,
    QSize newSize, const Hooks& hooks)
{
    Snapshot snapshot;

    if (!glWidget || !layerModel) {
        return snapshot;
    }

    const int newWidth = newSize.width();
    const int newHeight = newSize.height();
    const bool sizeChanged = (newSize != oldSize);
    const bool offsetChanged = (offsetX != 0 || offsetY != 0);
    const bool geometryChanged = sizeChanged || offsetChanged;
    if (!geometryChanged) {
        return snapshot;
    }

    std::vector<PerLayerResizeJob> pixelJobs;
    std::vector<ruwa::core::layers::LayerData*> transformedLayers;
    collectResizeJobs(layerModel, pixelJobs, transformedLayers);

    // ---- Capture pre-resize state (minimal) ----
    snapshot.cameraPositionBefore = glWidget->viewport().camera().position();

    for (auto* layer : transformedLayers) {
        snapshot.transformsBefore[layer->id]
            = layer->isText() ? layer->textData->transform : layer->smartTransform;
    }

    for (const auto& job : pixelJobs) {
        auto croppedKeys = ruwa::core::canvas::tilesCroppedByResize(
            *job.grid, offsetX, offsetY, newWidth, newHeight);
        if (croppedKeys.empty())
            continue;
        snapshot.croppedTiles.insert(job.layer->id, snapshotTiles(*job.grid, croppedKeys));
    }

    // ---- Remap undo stack BEFORE mutating the grid ----
    glWidget->canvas().undoManager().remapForCanvasResize(offsetX, offsetY, newWidth, newHeight);

    // ---- Apply the resize ----
    auto& canvas = glWidget->canvas();
    const auto oldIndexedKeys = canvas.tilePositionIndex().allTileKeys();

    applyRemapToLayers(pixelJobs, offsetX, offsetY, newWidth, newHeight);
    shiftLayerTransforms(transformedLayers, offsetX, offsetY);

    canvas.setSize(static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));

    rebuildIndicesAndMarkDirty(canvas, layerModel, pixelJobs, transformedLayers, oldIndexedKeys);

    glWidget->viewport().camera().move(-static_cast<float>(offsetX), -static_cast<float>(offsetY));

    notifyHostAfterApply(hooks, newSize, sizeChanged, geometryChanged);

    return snapshot;
}

// --------------------------------------------------------------------------
//   C o n s t r u c t o r
// --------------------------------------------------------------------------

CanvasResizeCommand::CanvasResizeCommand(OpenGLCanvasWidget* glWidget,
    ruwa::core::layers::LayerModel* layerModel, QSize oldSize, int offsetX, int offsetY,
    QSize newSize, Snapshot snapshot, Hooks hooks)
    : m_glWidget(glWidget)
    , m_layerModel(layerModel)
    , m_oldSize(oldSize)
    , m_offsetX(offsetX)
    , m_offsetY(offsetY)
    , m_newSize(newSize)
    , m_snapshot(std::move(snapshot))
    , m_hooks(std::move(hooks))
{
}

// --------------------------------------------------------------------------
//   u n d o
// --------------------------------------------------------------------------

void CanvasResizeCommand::undo()
{
    if (!m_glWidget || !m_layerModel) {
        return;
    }

    const int invOffsetX = -m_offsetX;
    const int invOffsetY = -m_offsetY;
    const int oldWidth = m_oldSize.width();
    const int oldHeight = m_oldSize.height();

    std::vector<PerLayerResizeJob> pixelJobs;
    std::vector<ruwa::core::layers::LayerData*> transformedLayers;
    collectResizeJobs(m_layerModel, pixelJobs, transformedLayers);

    auto& canvas = m_glWidget->canvas();

    // Keep dst-side tile keys around for dirty-marking post-restore.
    const auto prevIndexedKeys = canvas.tilePositionIndex().allTileKeys();

    canvas.setSize(static_cast<uint32_t>(oldWidth), static_cast<uint32_t>(oldHeight));

    // 1. Inverse remap reconstructs fully-inside-keep-rect tiles losslessly.
    applyRemapToLayers(pixelJobs, invOffsetX, invOffsetY, oldWidth, oldHeight);

    // 2. Restore snapshotted boundary/cropped tiles verbatim.
    for (const auto& job : pixelJobs) {
        auto it = m_snapshot.croppedTiles.find(job.layer->id);
        if (it == m_snapshot.croppedTiles.end())
            continue;
        // Buffers were captured at the grid's pixel format; restore size-exact
        // so 16F/32F boundary content round-trips (a fixed TILE_BYTE_SIZE check
        // would reject correctly-sized wide buffers and silently skip them).
        const size_t tileBytes = aether::tileByteSize(job.grid->format());
        for (const auto& [key, pixels] : it.value()) {
            if (pixels.size() != tileBytes)
                continue;
            auto& tile = job.grid->getOrCreateTile(key);
            std::memcpy(tile.pixels(), pixels.data(), tileBytes);
            tile.markDirty();
        }
    }

    // 3. Restore smart- and text-layer transforms.
    for (auto* layer : transformedLayers) {
        auto it = m_snapshot.transformsBefore.find(layer->id);
        if (it != m_snapshot.transformsBefore.end()) {
            if (layer->isText()) {
                layer->textData->transform = it.value();
            } else {
                layer->smartTransform = it.value();
            }
        }
    }

    rebuildIndicesAndMarkDirty(canvas, m_layerModel, pixelJobs, transformedLayers, prevIndexedKeys);

    m_glWidget->viewport().camera().setPosition(
        m_snapshot.cameraPositionBefore.x, m_snapshot.cameraPositionBefore.y);

    canvas.undoManager().remapForCanvasResize(invOffsetX, invOffsetY, oldWidth, oldHeight);

    notifyHostAfterApply(m_hooks, m_oldSize, /*sizeChanged=*/true, /*geometryChanged=*/true);
}

// --------------------------------------------------------------------------
//   r e d o
// --------------------------------------------------------------------------

void CanvasResizeCommand::redo()
{
    if (!m_glWidget || !m_layerModel) {
        return;
    }

    std::vector<PerLayerResizeJob> pixelJobs;
    std::vector<ruwa::core::layers::LayerData*> transformedLayers;
    collectResizeJobs(m_layerModel, pixelJobs, transformedLayers);

    auto& canvas = m_glWidget->canvas();
    const auto oldIndexedKeys = canvas.tilePositionIndex().allTileKeys();

    canvas.undoManager().remapForCanvasResize(
        m_offsetX, m_offsetY, m_newSize.width(), m_newSize.height());

    applyRemapToLayers(pixelJobs, m_offsetX, m_offsetY, m_newSize.width(), m_newSize.height());
    shiftLayerTransforms(transformedLayers, m_offsetX, m_offsetY);

    canvas.setSize(
        static_cast<uint32_t>(m_newSize.width()), static_cast<uint32_t>(m_newSize.height()));

    rebuildIndicesAndMarkDirty(canvas, m_layerModel, pixelJobs, transformedLayers, oldIndexedKeys);

    m_glWidget->viewport().camera().move(
        -static_cast<float>(m_offsetX), -static_cast<float>(m_offsetY));

    notifyHostAfterApply(m_hooks, m_newSize, /*sizeChanged=*/true, /*geometryChanged=*/true);
}

// --------------------------------------------------------------------------
//   m e t a
// --------------------------------------------------------------------------

QString CanvasResizeCommand::text() const
{
    return QStringLiteral("Canvas Resize");
}

qint64 CanvasResizeCommand::memorySize() const
{
    qint64 size = sizeof(CanvasResizeCommand);
    for (auto it = m_snapshot.croppedTiles.constBegin(); it != m_snapshot.croppedTiles.constEnd();
        ++it) {
        size += static_cast<qint64>(it.value().size()) * (sizeof(TileKey) + TILE_BYTE_SIZE + 64);
    }
    size += m_snapshot.transformsBefore.size()
        * static_cast<qint64>(sizeof(QUuid) + sizeof(TransformState));
    return size;
}

bool CanvasResizeCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

} // namespace aether
