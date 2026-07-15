// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   L A Y E R   R E M O V E   C O M M A N D
// ==========================================================================

#include "shared/undo/LayerRemoveCommand.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileTypes.h"

namespace aether {

namespace {

// Collect only root layer IDs — removeLayerRecursive removes whole subtree,
// so passing child IDs would emit layerAboutToBeRemoved for already-deleted layers (UAF crash).
QList<ruwa::core::layers::LayerId> collectRootLayerIds(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers)
{
    QList<ruwa::core::layers::LayerId> ids;
    for (const auto& layer : layers) {
        if (layer && !layer->id.isNull()) {
            ids.append(layer->id);
        }
    }
    return ids;
}

} // namespace

LayerRemoveCommand::LayerRemoveCommand(ruwa::core::layers::LayerModel* layerModel,
    QList<std::shared_ptr<ruwa::core::layers::LayerData>> removedLayers,
    QList<std::pair<ruwa::core::layers::LayerId, int>> restorePositions,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : m_layerModel(layerModel)
    , m_removedLayers(std::move(removedLayers))
    , m_restorePositions(std::move(restorePositions))
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
{
}

void LayerRemoveCommand::undo()
{
    if (!m_layerModel)
        return;

    for (int i = 0; i < m_removedLayers.size(); ++i) {
        const auto& layer = m_removedLayers[i];
        if (!layer)
            continue;

        const auto& [parentId, index]
            = m_restorePositions.value(i, { ruwa::core::layers::LayerId(), -1 });

        if (parentId.isNull()) {
            m_layerModel->addLayer(layer, index);
        } else {
            m_layerModel->addLayerTo(layer, parentId, index);
        }
    }

    if (m_requestRender)
        m_requestRender();
    if (m_onContentChanged)
        m_onContentChanged();
}

void LayerRemoveCommand::redo()
{
    if (!m_layerModel)
        return;

    const QList<ruwa::core::layers::LayerId> idsToRemove = collectRootLayerIds(m_removedLayers);
    m_layerModel->removeLayers(idsToRemove);

    if (m_requestRender)
        m_requestRender();
    if (m_onContentChanged)
        m_onContentChanged();
}

QString LayerRemoveCommand::text() const
{
    return QStringLiteral("Remove Layer");
}

qint64 LayerRemoveCommand::memorySize() const
{
    qint64 size = sizeof(LayerRemoveCommand);
    for (const auto& layer : m_removedLayers) {
        if (layer && layer->pixelGrid()) {
            const auto& tiles = layer->pixelGrid()->tiles();
            size += static_cast<qint64>(tiles.size())
                * (sizeof(aether::TileKey) + aether::TILE_BYTE_SIZE + 64);
        }
    }
    return size;
}

bool LayerRemoveCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

} // namespace aether
