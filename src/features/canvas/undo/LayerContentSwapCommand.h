// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   L A Y E R   C O N T E N T   S W A P
// ============================================================================

#ifndef RUWA_CORE_UNDO_LAYERCONTENTSWAPCOMMAND_H
#define RUWA_CORE_UNDO_LAYERCONTENTSWAPCOMMAND_H

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
 * Everything that describes HOW a layer stores its content, detached from it.
 *
 * A conversion (rasterize a smart object, convert a raster layer into one)
 * replaces the whole set at once; the displaced set is parked in the command and
 * swapped back on undo. Moving the state instead of cloning it keeps both
 * directions lossless without copying potentially large tile grids each time the
 * command is applied.
 */
struct LayerContentState {
    ruwa::core::layers::LayerType type = ruwa::core::layers::LayerType::Raster;
    std::unique_ptr<TileGrid> tileGrid;
    // The whole smart payload travels with the state, not just its pixels: the
    // content's identity (contentId, source, revision) must survive an
    // undo/redo round-trip, otherwise restoring a smart layer would hand it a
    // brand-new content and break every instance sharing it.
    std::shared_ptr<ruwa::core::layers::SmartContent> smartContent;
    TransformState smartTransform;
    std::unique_ptr<ruwa::core::layers::TextLayerData> textData;
    LayerVisualBackend runtimeVisualBackend = LayerVisualBackend::RasterTiles;
    std::shared_ptr<RetainedRenderPayload> runtimeRetainedPayload;
    QString runtimeRetainedPayloadKey;
};

/**
 * @brief Swaps a layer's content representation with a stored one.
 *
 * Both conversion directions are the same operation seen from opposite sides —
 * undo and redo are literally the identical swap — so one command serves both;
 * `commandText` is what tells them apart in the undo history.
 */
class LayerContentSwapCommand final : public IUndoCommand {
public:
    using OnLayerStateChangedFn = std::function<void(const ruwa::core::layers::LayerId& layerId)>;

    LayerContentSwapCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, LayerContentState replacedState,
        QString commandText, OnLayerStateChangedFn onLayerStateChanged);

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
    LayerContentState m_inactiveState;
    QString m_text;
    OnLayerStateChangedFn m_onLayerStateChanged;
    std::vector<PendingGridRemap> m_pendingGridRemaps;
    qint64 m_stateMemoryUpperBound = 0;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_LAYERCONTENTSWAPCOMMAND_H
