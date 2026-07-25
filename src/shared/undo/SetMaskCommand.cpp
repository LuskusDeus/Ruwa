// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S E T   M A S K   C O M M A N D
// ==========================================================================

#include "shared/undo/SetMaskCommand.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileGridClone.h"

namespace aether {

namespace {

qint64 gridMemorySize(const std::shared_ptr<const TileGrid>& grid)
{
    if (!grid) {
        return 0;
    }
    qint64 bytes = 0;
    const qint64 tileBytes = static_cast<qint64>(tileByteSize(grid->format()));
    for (const auto& [key, tile] : grid->tiles()) {
        Q_UNUSED(key);
        if (!tile.isSolid()) {
            bytes += tileBytes;
        }
    }
    return bytes;
}

} // namespace

SetMaskCommand::MaskState SetMaskCommand::captureState(const ruwa::core::layers::LayerData* layer)
{
    MaskState state;
    if (!layer) {
        return state;
    }
    state.enabled = layer->maskEnabled;
    state.linked = layer->maskLinked;
    state.editActive = layer->maskEditActive;
    if (const TileGrid* mask = layer->maskTileGrid()) {
        state.grid = cloneGridWithSolids(*mask);
    }
    return state;
}

void SetMaskCommand::applyState(ruwa::core::layers::LayerData* layer, const MaskState& state)
{
    if (!layer) {
        return;
    }

    layer->clearMask(); // also forces maskEditActive = false
    layer->maskEnabled = state.enabled;
    layer->maskLinked = state.linked;
    if (state.grid) {
        if (TileGrid* mask = layer->ensureMask()) {
            // Install a fresh deep copy: the stored snapshot has to survive any
            // number of undo/redo round trips (and mask painting on top of it).
            *mask = std::move(*cloneGridWithSolids(*state.grid));
        }
        layer->maskEditActive = state.editActive;
    }
    layer->maskThumbnailDirty = true;
}

SetMaskCommand::SetMaskCommand(ruwa::core::layers::LayerModel* layerModel,
    const ruwa::core::layers::LayerId& layerId, MaskState before, MaskState after,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
{
}

void SetMaskCommand::redo()
{
    apply(m_after);
}

void SetMaskCommand::undo()
{
    apply(m_before);
}

void SetMaskCommand::apply(const MaskState& state)
{
    if (!m_layerModel) {
        return;
    }
    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer) {
        return;
    }

    applyState(layer, state);

    // Structural compositing change (mask in/out): rebuilds the canvas layer
    // stack and refreshes the row — tile dirtying alone is not enough.
    m_layerModel->notifyLayerDataChanged(m_layerId);
    if (m_requestRender)
        m_requestRender();
    if (m_onContentChanged)
        m_onContentChanged();
}

QString SetMaskCommand::text() const
{
    return QStringLiteral("Paste Mask");
}

qint64 SetMaskCommand::memorySize() const
{
    return static_cast<qint64>(sizeof(SetMaskCommand)) + gridMemorySize(m_before.grid)
        + gridMemorySize(m_after.grid);
}

bool SetMaskCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    // Snapshot keys are left as-is, matching the other whole-grid structural
    // commands (AddMaskCommand, RemoveMaskCommand): the mask is restored in the
    // document coordinates it was authored in.
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

} // namespace aether
