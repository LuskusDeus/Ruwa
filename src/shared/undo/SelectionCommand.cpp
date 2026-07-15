// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S E L E C T I O N   C O M M A N D
// ==========================================================================

#include "shared/undo/SelectionCommand.h"
#include "shared/undo/SelectionState.h"
#include "features/canvas/scene/Canvas.h"
#include "features/layers/model/LayerSelectionManager.h"
#include "features/layers/model/LayerModel.h"
#include "features/selection/LassoSelectionManager.h"

namespace aether {

SelectionCommand::SelectionCommand(ruwa::core::layers::LayerSelectionManager* layerSelection,
    LassoSelectionManager* lassoSelection, Canvas* canvas, SelectionState before,
    SelectionState after, LayerExistsFn layerExists, RequestRenderFn requestRender)
    : m_layerSelection(layerSelection)
    , m_lassoSelection(lassoSelection)
    , m_canvas(canvas)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_layerExists(std::move(layerExists))
    , m_requestRender(std::move(requestRender))
{
}

void SelectionCommand::undo()
{
    applyState(m_before);
}

void SelectionCommand::redo()
{
    applyState(m_after);
}

QString SelectionCommand::text() const
{
    return QStringLiteral("Selection");
}

qint64 SelectionCommand::memorySize() const
{
    qint64 size = sizeof(SelectionCommand);
    size += m_before.layer.selectedIds.size() * sizeof(ruwa::core::layers::LayerId);
    size += m_after.layer.selectedIds.size() * sizeof(ruwa::core::layers::LayerId);
    for (const auto& r : m_before.lasso.regions) {
        size += r.polygon.size() * sizeof(Vector2);
    }
    for (const auto& r : m_after.lasso.regions) {
        size += r.polygon.size() * sizeof(Vector2);
    }
    return size;
}

bool SelectionCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    if (!m_canvas)
        return true;
    const bool beforeFiniteBounds
        = m_before.lasso.canvasWidth > 0 && m_before.lasso.canvasHeight > 0;
    const bool afterFiniteBounds = m_after.lasso.canvasWidth > 0 && m_after.lasso.canvasHeight > 0;
    if (!beforeFiniteBounds && !afterFiniteBounds) {
        return true;
    }
    m_before.lasso.canvasWidth = static_cast<uint32_t>(newWidth);
    m_before.lasso.canvasHeight = static_cast<uint32_t>(newHeight);
    m_after.lasso.canvasWidth = static_cast<uint32_t>(newWidth);
    m_after.lasso.canvasHeight = static_cast<uint32_t>(newHeight);

    const float nw = static_cast<float>(newWidth);
    const float nh = static_cast<float>(newHeight);
    auto clamp = [nw, nh](Vector2& p) {
        p.x = (p.x < 0) ? 0 : (p.x > nw) ? nw : p.x;
        p.y = (p.y < 0) ? 0 : (p.y > nh) ? nh : p.y;
    };
    for (auto& r : m_before.lasso.regions) {
        for (auto& p : r.polygon) {
            p.x -= static_cast<float>(offsetX);
            p.y -= static_cast<float>(offsetY);
            clamp(p);
        }
    }
    for (auto& r : m_after.lasso.regions) {
        for (auto& p : r.polygon) {
            p.x -= static_cast<float>(offsetX);
            p.y -= static_cast<float>(offsetY);
            clamp(p);
        }
    }
    return true;
}

void SelectionCommand::applyState(const SelectionState& state)
{
    SelectionRestoreContext ctx;
    ctx.layerSelection = m_layerSelection;
    ctx.lassoSelection = m_lassoSelection;
    ctx.canvas = m_canvas;
    ctx.layerExists = m_layerExists;
    ctx.requestRender = m_requestRender;
    applySelectionRestore(ctx, state);
}

} // namespace aether
