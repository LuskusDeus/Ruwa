// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   I N V E R T   M A S K   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_INVERTMASKCOMMAND_H
#define RUWA_CORE_UNDO_INVERTMASKCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileTypes.h"

#include <QtGlobal>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

/// Full snapshot of a layer mask grid (background + every tile), enough to
/// restore it byte-for-byte. Solid tiles are stored as just their packed color.
struct MaskGridState {
    uint32_t defaultFill = 0;
    struct Tile {
        TileKey key;
        bool solid = false;
        uint32_t solidColor = 0;
        std::vector<uint8_t> bytes; // when !solid (TILE_BYTE_SIZE)
    };
    std::vector<Tile> tiles;
};

/**
 * @brief Undo command for inverting a layer mask.
 *
 * Inverting rewrites every tile (and the infinite background) so that
 * reveal -> 1 - reveal. Because that normalizes the stored representation
 * (opaque grayscale), undo/redo restore exact pre/post snapshots rather than
 * recomputing. Masks are small, so storing both states is cheap.
 *
 * Header-only so it can be reused without a CMake reconfigure.
 */
class InvertMaskCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;

    InvertMaskCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, MaskGridState before, MaskGridState after,
        RequestRenderFn requestRender)
        : m_layerModel(layerModel)
        , m_layerId(layerId)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_requestRender(std::move(requestRender))
    {
    }

    void undo() override { restore(m_before); }
    void redo() override { restore(m_after); }

    QString text() const override { return QStringLiteral("Invert Mask"); }
    qint64 memorySize() const override
    {
        qint64 size = static_cast<qint64>(sizeof(*this));
        for (const auto& t : m_before.tiles)
            size += static_cast<qint64>(t.bytes.size());
        for (const auto& t : m_after.tiles)
            size += static_cast<qint64>(t.bytes.size());
        return size;
    }

    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override
    {
        Q_UNUSED(offsetX);
        Q_UNUSED(offsetY);
        Q_UNUSED(newWidth);
        Q_UNUSED(newHeight);
        return true;
    }

private:
    void restore(const MaskGridState& state)
    {
        if (!m_layerModel)
            return;
        auto* layer = m_layerModel->layerById(m_layerId);
        if (!layer)
            return;
        aether::TileGrid* mask = layer->maskTileGrid();
        if (!mask)
            return;

        mask->clear();
        mask->setDefaultFillPacked(state.defaultFill);
        for (const auto& t : state.tiles) {
            aether::TileData& dst = mask->getOrCreateTile(t.key);
            if (t.solid) {
                dst.setSolidPacked(t.solidColor);
            } else if (t.bytes.size() == TILE_BYTE_SIZE) {
                std::memcpy(dst.pixels(), t.bytes.data(), TILE_BYTE_SIZE);
            }
            dst.markDirty();
            mask->markDirty(t.key);
        }
        layer->maskThumbnailDirty = true;
        // Recomposites the layer's content tiles against the restored mask and
        // refreshes the panel row.
        m_layerModel->notifyLayerDataChanged(m_layerId);
        if (m_requestRender)
            m_requestRender();
    }

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    MaskGridState m_before;
    MaskGridState m_after;
    RequestRenderFn m_requestRender;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_INVERTMASKCOMMAND_H
