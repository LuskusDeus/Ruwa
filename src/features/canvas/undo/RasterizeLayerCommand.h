// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   R A S T E R I Z E   L A Y E R   C O M M A N D
// ============================================================================

#ifndef RUWA_CORE_UNDO_RASTERIZELAYERCOMMAND_H
#define RUWA_CORE_UNDO_RASTERIZELAYERCOMMAND_H

#include "features/layers/model/LayerData.h"
#include "shared/undo/UndoManager.h"

#include <functional>
#include <memory>
#include <vector>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

/**
 * State replaced by rasterization.
 *
 * The command moves this state between itself and the live layer on undo/redo.
 * This keeps both directions lossless without cloning potentially large tile
 * grids each time the command is applied.
 */
struct RasterizedLayerState {
    ruwa::core::layers::LayerType type = ruwa::core::layers::LayerType::Raster;
    std::unique_ptr<TileGrid> tileGrid;
    std::unique_ptr<TileGrid> smartContentGrid;
    TransformState smartTransform;
    std::unique_ptr<ruwa::core::layers::TextLayerData> textData;
    LayerVisualBackend runtimeVisualBackend = LayerVisualBackend::RasterTiles;
    std::shared_ptr<RetainedRenderPayload> runtimeRetainedPayload;
    QString runtimeRetainedPayloadKey;
};

class RasterizeLayerCommand final : public IUndoCommand {
public:
    using OnLayerStateChangedFn = std::function<void(const ruwa::core::layers::LayerId& layerId)>;

    RasterizeLayerCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, RasterizedLayerState replacedState,
        OnLayerStateChangedFn onLayerStateChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    struct PendingGridRemap {
        int offsetX = 0;
        int offsetY = 0;
        int newWidth = 0;
        int newHeight = 0;
    };

    void applyPendingGridRemaps();
    void swapWithLayer();

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    RasterizedLayerState m_inactiveState;
    OnLayerStateChangedFn m_onLayerStateChanged;
    std::vector<PendingGridRemap> m_pendingGridRemaps;
    qint64 m_stateMemoryUpperBound = 0;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_RASTERIZELAYERCOMMAND_H
