// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   L A Y E R   A D D   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_LAYERADDCOMMAND_H
#define RUWA_CORE_UNDO_LAYERADDCOMMAND_H

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
 * @brief Undo command for layer add operations.
 *
 * Stores clones of added layers. undo() removes them by ID; redo() inserts them back.
 */
class LayerAddCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    LayerAddCommand(ruwa::core::layers::LayerModel* layerModel,
        QList<std::shared_ptr<ruwa::core::layers::LayerData>> layers,
        QList<std::pair<ruwa::core::layers::LayerId, int>> positions, RequestRenderFn requestRender,
        OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    QList<std::shared_ptr<ruwa::core::layers::LayerData>> m_layers;
    QList<std::pair<ruwa::core::layers::LayerId, int>> m_positions;

    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_LAYERADDCOMMAND_H
