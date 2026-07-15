// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   A P P L Y   L A Y E R   E F F E C T S   C O M M A N D
// ==========================================================================

#include "features/canvas/undo/ApplyLayerEffectsCommand.h"

#include "features/layers/model/LayerModel.h"

namespace aether {

ApplyLayerEffectsCommand::ApplyLayerEffectsCommand(Canvas* canvas,
    ruwa::core::layers::LayerModel* layerModel, const QUuid& layerId,
    StrokeSnapshot&& pixelSnapshot, QList<ruwa::core::effects::LayerEffectState> beforeEffects)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_pixelCmd(std::make_unique<DrawCommand>(canvas, layerModel, std::move(pixelSnapshot)))
    , m_beforeEffects(std::move(beforeEffects))
{
}

ApplyLayerEffectsCommand::~ApplyLayerEffectsCommand() = default;

void ApplyLayerEffectsCommand::undo()
{
    m_pixelCmd->undo();
    if (m_layerModel) {
        m_layerModel->replaceLayerEffects(
            m_layerId, m_beforeEffects, /*affectsDocumentResult=*/true);
    }
}

void ApplyLayerEffectsCommand::redo()
{
    m_pixelCmd->redo();
    if (m_layerModel) {
        m_layerModel->replaceLayerEffects(m_layerId, QList<ruwa::core::effects::LayerEffectState>(),
            /*affectsDocumentResult=*/true);
    }
}

QString ApplyLayerEffectsCommand::text() const
{
    return QStringLiteral("Apply All Effects");
}

qint64 ApplyLayerEffectsCommand::memorySize() const
{
    return m_pixelCmd->memorySize()
        + m_beforeEffects.size()
        * static_cast<qint64>(sizeof(ruwa::core::effects::LayerEffectState));
}

bool ApplyLayerEffectsCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    return m_pixelCmd->remapForCanvasResize(offsetX, offsetY, newWidth, newHeight);
}

bool ApplyLayerEffectsCommand::requiresAsyncPreparationForUndo() const
{
    return m_pixelCmd->requiresAsyncPreparationForUndo();
}

bool ApplyLayerEffectsCommand::requiresAsyncPreparationForRedo() const
{
    return m_pixelCmd->requiresAsyncPreparationForRedo();
}

void ApplyLayerEffectsCommand::prepareUndo()
{
    m_pixelCmd->prepareUndo();
}

void ApplyLayerEffectsCommand::prepareRedo()
{
    m_pixelCmd->prepareRedo();
}

QList<QPoint> ApplyLayerEffectsCommand::affectedTilePositions() const
{
    return m_pixelCmd->affectedTilePositions();
}

} // namespace aether
