// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   R A S T E R I Z E   L A Y E R   C O M M A N D
// ============================================================================

#include "features/canvas/undo/RasterizeLayerCommand.h"

#include "features/canvas/grid/GridRemap.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileGrid.h"

#include <algorithm>
#include <utility>

namespace aether {
namespace {

qint64 gridMemorySize(const std::unique_ptr<TileGrid>& grid)
{
    if (!grid) {
        return 0;
    }
    return static_cast<qint64>(grid->tiles().size())
        * static_cast<qint64>(tileByteSize(grid->format()) + sizeof(TileKey) + 64);
}

qint64 retainedPayloadMemorySize(const std::shared_ptr<RetainedRenderPayload>& payload)
{
    if (!payload) {
        return 0;
    }

    qint64 size = sizeof(RetainedRenderPayload);
    size += static_cast<qint64>(payload->primitives.capacity()) * sizeof(RetainedPrimitive);
    for (const RetainedPrimitive& primitive : payload->primitives) {
        size += static_cast<qint64>(primitive.points.capacity()) * sizeof(Vector2);
        size += static_cast<qint64>(primitive.glyphRuns.capacity()) * sizeof(RetainedGlyphRun);
        for (const RetainedGlyphRun& run : primitive.glyphRuns) {
            size += static_cast<qint64>(run.fontFamily.capacity());
            size += static_cast<qint64>(run.glyphIndexes.capacity()) * sizeof(uint32_t);
            size += static_cast<qint64>(run.sourcePositions.capacity()) * sizeof(Vector2);
            size += static_cast<qint64>(run.worldPositions.capacity()) * sizeof(Vector2);
        }
    }
    return size;
}

qint64 inactiveStateMemorySize(const RasterizedLayerState& state)
{
    qint64 size = gridMemorySize(state.tileGrid) + gridMemorySize(state.smartContentGrid)
        + retainedPayloadMemorySize(state.runtimeRetainedPayload);
    size += static_cast<qint64>(state.runtimeRetainedPayloadKey.capacity()) * sizeof(QChar);
    if (state.textData) {
        const auto& text = *state.textData;
        size += sizeof(ruwa::core::layers::TextLayerData);
        size += static_cast<qint64>(text.text.capacity() + text.fontFamily.capacity())
            * sizeof(QChar);
        size += static_cast<qint64>(text.styleRuns.capacity())
            * sizeof(ruwa::core::layers::TextStyleRun);
        for (const auto& run : text.styleRuns) {
            size += static_cast<qint64>(run.fontFamily.capacity()) * sizeof(QChar);
        }
    }
    return size;
}

} // namespace

RasterizeLayerCommand::RasterizeLayerCommand(ruwa::core::layers::LayerModel* layerModel,
    const ruwa::core::layers::LayerId& layerId, RasterizedLayerState replacedState,
    OnLayerStateChangedFn onLayerStateChanged)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_inactiveState(std::move(replacedState))
    , m_onLayerStateChanged(std::move(onLayerStateChanged))
{
    const auto* appliedLayer = m_layerModel ? m_layerModel->layerById(m_layerId) : nullptr;
    const qint64 appliedRasterMemory = appliedLayer ? gridMemorySize(appliedLayer->tileGrid) : 0;
    m_stateMemoryUpperBound
        = std::max(inactiveStateMemorySize(m_inactiveState), appliedRasterMemory);
}

void RasterizeLayerCommand::undo()
{
    swapWithLayer();
}

void RasterizeLayerCommand::redo()
{
    swapWithLayer();
}

void RasterizeLayerCommand::swapWithLayer()
{
    if (!m_layerModel) {
        return;
    }
    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer) {
        return;
    }

    applyPendingGridRemaps();

    using std::swap;
    swap(layer->type, m_inactiveState.type);
    swap(layer->tileGrid, m_inactiveState.tileGrid);
    swap(layer->smartContentGrid, m_inactiveState.smartContentGrid);
    swap(layer->smartTransform, m_inactiveState.smartTransform);
    swap(layer->textData, m_inactiveState.textData);
    swap(layer->runtimeVisualBackend, m_inactiveState.runtimeVisualBackend);
    swap(layer->runtimeRetainedPayload, m_inactiveState.runtimeRetainedPayload);
    swap(layer->runtimeRetainedPayloadKey, m_inactiveState.runtimeRetainedPayloadKey);

    layer->thumbnailDirty = true;
    if (m_onLayerStateChanged) {
        m_onLayerStateChanged(m_layerId);
    } else {
        m_layerModel->notifyLayerDataChanged(m_layerId);
    }
}

QString RasterizeLayerCommand::text() const
{
    return QStringLiteral("Rasterize Layer");
}

qint64 RasterizeLayerCommand::memorySize() const
{
    return sizeof(RasterizeLayerCommand) + m_stateMemoryUpperBound
        + static_cast<qint64>(m_pendingGridRemaps.capacity()) * sizeof(PendingGridRemap);
}

bool RasterizeLayerCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    if (m_inactiveState.type == ruwa::core::layers::LayerType::Raster) {
        if (m_inactiveState.tileGrid) {
            // Match the existing tile commands: canvas resize only queues the
            // potentially heavy work. It is applied when this state is next
            // restored by undo/redo.
            m_pendingGridRemaps.push_back({ offsetX, offsetY, newWidth, newHeight });
        }
    } else if (m_inactiveState.type == ruwa::core::layers::LayerType::Smart
        || m_inactiveState.type == ruwa::core::layers::LayerType::Board) {
        m_inactiveState.smartTransform.shiftForCanvasResize(offsetX, offsetY);
    } else if (m_inactiveState.type == ruwa::core::layers::LayerType::Text
        && m_inactiveState.textData) {
        m_inactiveState.textData->transform.shiftForCanvasResize(offsetX, offsetY);
        // The retained payload is derived from textData and is no longer valid
        // after its transform changes.
        m_inactiveState.runtimeRetainedPayload.reset();
        m_inactiveState.runtimeRetainedPayloadKey.clear();
        m_inactiveState.runtimeVisualBackend = LayerVisualBackend::RasterTiles;
    }
    return true;
}

void RasterizeLayerCommand::applyPendingGridRemaps()
{
    if (!m_inactiveState.tileGrid) {
        m_pendingGridRemaps.clear();
        return;
    }
    for (const PendingGridRemap& remap : m_pendingGridRemaps) {
        ruwa::core::canvas::remapGridForCanvasRect(*m_inactiveState.tileGrid, remap.offsetX,
            remap.offsetY, remap.newWidth, remap.newHeight);
    }
    m_pendingGridRemaps.clear();
}

} // namespace aether
