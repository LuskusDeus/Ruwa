// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S E L E C T I O N   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_SELECTIONCOMMAND_H
#define RUWA_CORE_UNDO_SELECTIONCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "shared/undo/SelectionState.h"

#include <functional>

namespace ruwa::core::layers {
class LayerSelectionManager;
class LayerModel;
} // namespace ruwa::core::layers

namespace aether {

class Canvas;
class LassoSelectionManager;

/**
 * @brief Undo command that restores layer and lasso selection state.
 */
class SelectionCommand : public IUndoCommand {
public:
    using LayerExistsFn = std::function<bool(const ruwa::core::layers::LayerId&)>;
    using RequestRenderFn = std::function<void()>;

    SelectionCommand(ruwa::core::layers::LayerSelectionManager* layerSelection,
        LassoSelectionManager* lassoSelection, Canvas* canvas, SelectionState before,
        SelectionState after, LayerExistsFn layerExists, RequestRenderFn requestRender);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    void applyState(const SelectionState& state);

    ruwa::core::layers::LayerSelectionManager* m_layerSelection = nullptr;
    LassoSelectionManager* m_lassoSelection = nullptr;
    Canvas* m_canvas = nullptr;

    SelectionState m_before;
    SelectionState m_after;

    LayerExistsFn m_layerExists;
    RequestRenderFn m_requestRender;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_SELECTIONCOMMAND_H
