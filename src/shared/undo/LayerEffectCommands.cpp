// SPDX-License-Identifier: MPL-2.0

#include "shared/undo/LayerEffectCommands.h"

#include "features/layers/model/LayerModel.h"

#include <utility>

namespace aether {
namespace {

QString commandText(LayerEffectCommand::Kind kind)
{
    switch (kind) {
    case LayerEffectCommand::Kind::Add:
        return QStringLiteral("Add Layer Effect");
    case LayerEffectCommand::Kind::Remove:
        return QStringLiteral("Remove Layer Effect");
    case LayerEffectCommand::Kind::Move:
        return QStringLiteral("Move Layer Effect");
    case LayerEffectCommand::Kind::Enabled:
        return QStringLiteral("Toggle Layer Effect");
    case LayerEffectCommand::Kind::RealtimePreview:
        return QStringLiteral("Toggle Effect Preview");
    case LayerEffectCommand::Kind::Param:
        return QStringLiteral("Edit Layer Effect");
    }
    return QStringLiteral("Layer Effect");
}

qint64 roughEffectListSize(const LayerEffectCommand::EffectList& effects)
{
    qint64 size = effects.size() * sizeof(ruwa::core::effects::LayerEffectState);
    for (const auto& effect : effects) {
        size += effect.typeId.size() * sizeof(QChar);
        size += effect.params.size() * 96;
    }
    return size;
}

} // namespace

LayerEffectCommand::LayerEffectCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after, Kind kind,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged, bool affectsDocumentResult)
    : m_layerModel(layerModel)
    , m_layerId(std::move(layerId))
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_kind(kind)
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
    , m_affectsDocumentResult(affectsDocumentResult)
{
}

LayerEffectCommand::LayerEffectCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after, Kind kind,
    bool affectsDocumentResult)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after), kind,
          {}, {}, affectsDocumentResult)
{
}

bool LayerEffectCommand::mergeAfterStateFrom(const LayerEffectCommand& newer)
{
    if (m_layerModel != newer.m_layerModel || m_layerId != newer.m_layerId || m_kind != newer.m_kind
        || m_affectsDocumentResult != newer.m_affectsDocumentResult || m_after != newer.m_before) {
        return false;
    }

    m_after = newer.m_after;
    return true;
}

void LayerEffectCommand::undo()
{
    apply(m_before);
}

void LayerEffectCommand::redo()
{
    apply(m_after);
}

void LayerEffectCommand::apply(const EffectList& effects)
{
    if (!m_layerModel) {
        return;
    }

    m_layerModel->replaceLayerEffects(m_layerId, effects, m_affectsDocumentResult);
    if (m_requestRender) {
        m_requestRender();
    }
    if (m_onContentChanged) {
        m_onContentChanged();
    }
}

QString LayerEffectCommand::text() const
{
    return commandText(m_kind);
}

qint64 LayerEffectCommand::memorySize() const
{
    return sizeof(LayerEffectCommand) + roughEffectListSize(m_before)
        + roughEffectListSize(m_after);
}

bool LayerEffectCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

LayerEffectAddCommand::LayerEffectAddCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after),
          Kind::Add, std::move(requestRender), std::move(onContentChanged), true)
{
}

LayerEffectRemoveCommand::LayerEffectRemoveCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after),
          Kind::Remove, std::move(requestRender), std::move(onContentChanged), true)
{
}

LayerEffectMoveCommand::LayerEffectMoveCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after),
          Kind::Move, std::move(requestRender), std::move(onContentChanged), true)
{
}

LayerEffectEnabledCommand::LayerEffectEnabledCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after),
          Kind::Enabled, std::move(requestRender), std::move(onContentChanged), true)
{
}

LayerEffectRealtimePreviewCommand::LayerEffectRealtimePreviewCommand(
    ruwa::core::layers::LayerModel* layerModel, ruwa::core::layers::LayerId layerId,
    EffectList before, EffectList after, RequestRenderFn requestRender,
    OnContentChangedFn onContentChanged)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after),
          Kind::RealtimePreview, std::move(requestRender), std::move(onContentChanged), false)
{
}

LayerEffectParamCommand::LayerEffectParamCommand(ruwa::core::layers::LayerModel* layerModel,
    ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged, QUuid mergeSessionId)
    : LayerEffectCommand(layerModel, std::move(layerId), std::move(before), std::move(after),
          Kind::Param, std::move(requestRender), std::move(onContentChanged), true)
    , m_mergeSessionId(std::move(mergeSessionId))
{
}

bool LayerEffectParamCommand::mergeWith(const IUndoCommand& newer)
{
    const auto* newerParamCommand = dynamic_cast<const LayerEffectParamCommand*>(&newer);
    if (!newerParamCommand || m_mergeSessionId.isNull()
        || m_mergeSessionId != newerParamCommand->m_mergeSessionId) {
        return false;
    }
    return mergeAfterStateFrom(*newerParamCommand);
}

qint64 LayerEffectParamCommand::memorySize() const
{
    return LayerEffectCommand::memorySize() + sizeof(m_mergeSessionId);
}

} // namespace aether
