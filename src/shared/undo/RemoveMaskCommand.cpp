// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   R E M O V E   M A S K   C O M M A N D
// ==========================================================================

#include "shared/undo/RemoveMaskCommand.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileGrid.h"

#include <cstring>

namespace aether {

RemoveMaskCommand::RemoveMaskCommand(ruwa::core::layers::LayerModel* layerModel,
    const ruwa::core::layers::LayerId& layerId, RequestRenderFn requestRender,
    OnContentChangedFn onContentChanged)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
{
    // Snapshot the mask as it stands right now — the caller removes it after
    // constructing us, so this is the state undo() has to rebuild.
    auto* layer = m_layerModel ? m_layerModel->layerById(m_layerId) : nullptr;
    if (!layer) {
        return;
    }
    m_maskEnabled = layer->maskEnabled;
    m_maskLinked = layer->maskLinked;
    m_maskEditActive = layer->maskEditActive;

    const aether::TileGrid* mask = layer->maskTileGrid();
    if (!mask) {
        return;
    }
    m_defaultFill = mask->defaultFillPacked();
    for (const auto& [key, tile] : mask->tiles()) {
        MaskTileSnapshot snap;
        snap.key = key;
        if (tile.isSolid()) {
            snap.solid = true;
            snap.solidColor = tile.solidColorPacked();
        } else {
            snap.bytes.resize(TILE_BYTE_SIZE);
            std::memcpy(snap.bytes.data(), tile.pixels(), TILE_BYTE_SIZE);
        }
        m_maskTiles.push_back(std::move(snap));
    }
}

void RemoveMaskCommand::redo()
{
    if (!m_layerModel)
        return;
    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer)
        return;

    layer->clearMask(); // also forces maskEditActive = false
    notify();
}

void RemoveMaskCommand::undo()
{
    if (!m_layerModel)
        return;
    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer)
        return;

    aether::TileGrid* mask = layer->ensureMask();
    if (mask) {
        mask->setDefaultFillPacked(m_defaultFill);
        for (const auto& snap : m_maskTiles) {
            aether::TileData& dst = mask->getOrCreateTile(snap.key);
            if (snap.solid) {
                dst.setSolidPacked(snap.solidColor);
            } else {
                if (snap.bytes.size() != TILE_BYTE_SIZE)
                    continue;
                std::memcpy(dst.pixels(), snap.bytes.data(), TILE_BYTE_SIZE);
            }
            dst.markDirty();
            mask->markDirty(snap.key);
        }
    }
    layer->maskEnabled = m_maskEnabled;
    layer->maskLinked = m_maskLinked;
    layer->maskEditActive = m_maskEditActive;
    layer->maskThumbnailDirty = true;
    notify();
}

void RemoveMaskCommand::notify()
{
    // Structural compositing change (mask in/out): rebuilds the canvas layer
    // stack and refreshes the row — tile dirtying alone is not enough.
    if (m_layerModel) {
        m_layerModel->notifyLayerDataChanged(m_layerId);
    }
    if (m_requestRender)
        m_requestRender();
    if (m_onContentChanged)
        m_onContentChanged();
}

QString RemoveMaskCommand::text() const
{
    return QStringLiteral("Delete Mask");
}

qint64 RemoveMaskCommand::memorySize() const
{
    qint64 size = sizeof(RemoveMaskCommand);
    for (const auto& snap : m_maskTiles) {
        size += static_cast<qint64>(snap.bytes.size());
    }
    return size;
}

bool RemoveMaskCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    // Snapshot keys are left as-is, matching the other whole-grid structural
    // commands (LayerRemoveCommand, AddMaskCommand): the mask is restored in the
    // document coordinates it was authored in.
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

} // namespace aether
