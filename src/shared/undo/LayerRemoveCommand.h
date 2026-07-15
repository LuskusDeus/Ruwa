// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   L A Y E R   R E M O V E   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_LAYERREMOVECOMMAND_H
#define RUWA_CORE_UNDO_LAYERREMOVECOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/layers/model/LayerData.h"

#include <QList>
#include <functional>
#include <memory>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

/**
 * @brief Undo command for layer remove operations.
 *
 * Stores clones of removed layers. undo() inserts them back; redo() removes by ID.
 */
class LayerRemoveCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    LayerRemoveCommand(ruwa::core::layers::LayerModel* layerModel,
        QList<std::shared_ptr<ruwa::core::layers::LayerData>> removedLayers,
        QList<std::pair<ruwa::core::layers::LayerId, int>> restorePositions,
        RequestRenderFn requestRender, OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    QList<std::shared_ptr<ruwa::core::layers::LayerData>> m_removedLayers;
    QList<std::pair<ruwa::core::layers::LayerId, int>> m_restorePositions;

    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_LAYERREMOVECOMMAND_H
