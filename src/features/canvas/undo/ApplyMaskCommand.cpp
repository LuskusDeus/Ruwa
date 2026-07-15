// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   A P P L Y   M A S K   C O M M A N D
// ==========================================================================

#include "features/canvas/undo/ApplyMaskCommand.h"

#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"

namespace aether {
namespace {

ruwa::core::layers::LayerData* findLayer(ruwa::core::layers::LayerModel* model, const QUuid& id)
{
    if (!model) {
        return nullptr;
    }
    ruwa::core::layers::LayerData* found = nullptr;
    model->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (layer->id == id) {
            found = layer;
        }
    });
    return found;
}

} // namespace

ApplyMaskCommand::ApplyMaskCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
    const QUuid& layerId, StrokeSnapshot&& pixelSnapshot, StrokeSnapshot&& maskSnapshot,
    bool maskEnabled, bool maskLinked)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_pixelCmd(std::make_unique<DrawCommand>(canvas, layerModel, std::move(pixelSnapshot)))
    , m_maskCmd(std::make_unique<DrawCommand>(canvas, layerModel, std::move(maskSnapshot)))
    , m_maskEnabled(maskEnabled)
    , m_maskLinked(maskLinked)
{
}

ApplyMaskCommand::~ApplyMaskCommand() = default;

void ApplyMaskCommand::undo()
{
    // Restore the layer's original pixels, then bring the mask back and
    // repopulate its tiles.
    m_pixelCmd->undo();

    if (auto* layer = findLayer(m_layerModel, m_layerId)) {
        layer->ensureMask(); // allocate empty grid (maskEnabled=true, dirty)
        m_maskCmd->undo(); // writes the original mask tiles into it
        layer->maskEnabled = m_maskEnabled;
        layer->maskLinked = m_maskLinked;
        layer->maskEditActive = false;
        layer->maskThumbnailDirty = true;
    }

    // The mask coming back is a structural compositing change: the canvas must
    // invalidate its cached layer stacks so the restored pixels are gated by the
    // mask again, and the layers panel must show the mask thumbnail. The inner
    // DrawCommands only dirty tiles, which is not enough on its own.
    notifyMaskStructureChanged();
}

void ApplyMaskCommand::redo()
{
    // Re-bake the pixels and drop the mask again. The mask grid is removed
    // wholesale, so there is no need to replay m_maskCmd's "empty" state.
    m_pixelCmd->redo();

    if (auto* layer = findLayer(m_layerModel, m_layerId)) {
        layer->clearMask();
    }

    notifyMaskStructureChanged();
}

void ApplyMaskCommand::notifyMaskStructureChanged()
{
    // Rebuilds the canvas layer stack (mask in/out), refreshes the row thumbnail
    // and requests a render — mirrors what OpenGLCanvasWidget::applyLayerMask does
    // after the forward apply (OGCW::onLayerDataChanged).
    if (m_layerModel) {
        m_layerModel->notifyLayerDataChanged(m_layerId);
    }
}

QString ApplyMaskCommand::text() const
{
    return QStringLiteral("Apply Mask");
}

qint64 ApplyMaskCommand::memorySize() const
{
    return m_pixelCmd->memorySize() + m_maskCmd->memorySize();
}

bool ApplyMaskCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    const bool a = m_pixelCmd->remapForCanvasResize(offsetX, offsetY, newWidth, newHeight);
    const bool b = m_maskCmd->remapForCanvasResize(offsetX, offsetY, newWidth, newHeight);
    return a && b;
}

bool ApplyMaskCommand::requiresAsyncPreparationForUndo() const
{
    return m_pixelCmd->requiresAsyncPreparationForUndo()
        || m_maskCmd->requiresAsyncPreparationForUndo();
}

bool ApplyMaskCommand::requiresAsyncPreparationForRedo() const
{
    // redo() only touches the pixel grid; the mask is dropped, not replayed.
    return m_pixelCmd->requiresAsyncPreparationForRedo();
}

void ApplyMaskCommand::prepareUndo()
{
    m_pixelCmd->prepareUndo();
    m_maskCmd->prepareUndo();
}

void ApplyMaskCommand::prepareRedo()
{
    m_pixelCmd->prepareRedo();
}

QList<QPoint> ApplyMaskCommand::affectedTilePositions() const
{
    // Union of both grids. Overlapping tiles only cause a redundant
    // markDirty, so we concatenate rather than de-duplicate.
    QList<QPoint> merged = m_pixelCmd->affectedTilePositions();
    merged += m_maskCmd->affectedTilePositions();
    return merged;
}

} // namespace aether
