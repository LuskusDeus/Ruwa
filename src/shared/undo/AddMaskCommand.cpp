// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   A D D   M A S K   C O M M A N D
// ==========================================================================

#include "shared/undo/AddMaskCommand.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileGrid.h"

#include <cstring>

namespace aether {

AddMaskCommand::AddMaskCommand(ruwa::core::layers::LayerModel* layerModel,
    const ruwa::core::layers::LayerId& layerId, bool prevMaskEditActive,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_prevMaskEditActive(prevMaskEditActive)
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
{
    // Snapshot the mask grid as it exists right now (the caller has already
    // created and optionally selection-filled it). Empty for a plain "Add Mask".
    if (auto* layer = m_layerModel ? m_layerModel->layerById(m_layerId) : nullptr) {
        if (const aether::TileGrid* mask = layer->maskTileGrid()) {
            m_initialDefaultFill = mask->defaultFillPacked();
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
                m_initialMaskTiles.push_back(std::move(snap));
            }
        }
    }
}

void AddMaskCommand::redo()
{
    if (!m_layerModel)
        return;
    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer)
        return;

    // Recreate the mask and make it the active paint target (matches the button).
    layer->ensureMask();
    layer->maskEditActive = true;
    // Restore the background (reveal-all vs hide-all) chosen at creation time.
    if (aether::TileGrid* mask = layer->maskTileGrid()) {
        mask->setDefaultFillPacked(m_initialDefaultFill);
    }

    // Restore the captured initial content (selection bake), if any. A plain mask
    // has no tiles and stays reveal-all.
    if (!m_initialMaskTiles.empty()) {
        if (aether::TileGrid* mask = layer->maskTileGrid()) {
            for (const auto& snap : m_initialMaskTiles) {
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
            layer->maskThumbnailDirty = true;
        }
    }
    notify();
}

void AddMaskCommand::undo()
{
    if (!m_layerModel)
        return;
    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer)
        return;

    // Any strokes painted into the mask are separate commands already undone by
    // now, so the grid we drop here is back to its empty creation state.
    layer->clearMask(); // also forces maskEditActive = false
    layer->maskEditActive = m_prevMaskEditActive;
    notify();
}

void AddMaskCommand::notify()
{
    // Rebuilds the canvas layer stack (mask in/out) and refreshes the row.
    m_layerModel->notifyLayerDataChanged(m_layerId);
    if (m_requestRender)
        m_requestRender();
    if (m_onContentChanged)
        m_onContentChanged();
}

QString AddMaskCommand::text() const
{
    return QStringLiteral("Add Mask");
}

qint64 AddMaskCommand::memorySize() const
{
    qint64 size = sizeof(AddMaskCommand);
    for (const auto& snap : m_initialMaskTiles) {
        size += static_cast<qint64>(snap.bytes.size());
    }
    return size;
}

bool AddMaskCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

} // namespace aether
