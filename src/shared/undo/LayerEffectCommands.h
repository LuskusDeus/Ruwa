// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_UNDO_LAYEREFFECTCOMMANDS_H
#define RUWA_CORE_UNDO_LAYEREFFECTCOMMANDS_H

#include "features/effects/LayerEffectTypes.h"
#include "features/layers/model/LayerData.h"
#include "shared/undo/UndoManager.h"

#include <QList>
#include <functional>
#include <utility>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class LayerEffectCommand : public IUndoCommand {
public:
    enum class Kind { Add, Remove, Move, Enabled, RealtimePreview, Param };

    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;
    using EffectList = QList<ruwa::core::effects::LayerEffectState>;

    LayerEffectCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after, Kind kind,
        RequestRenderFn requestRender, OnContentChangedFn onContentChanged,
        bool affectsDocumentResult = true);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

protected:
    LayerEffectCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after, Kind kind,
        bool affectsDocumentResult = true);
    bool mergeAfterStateFrom(const LayerEffectCommand& newer);

private:
    void apply(const EffectList& effects);

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    EffectList m_before;
    EffectList m_after;
    Kind m_kind = Kind::Param;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
    bool m_affectsDocumentResult = true;
};

class LayerEffectAddCommand : public LayerEffectCommand {
public:
    LayerEffectAddCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
        RequestRenderFn requestRender = {}, OnContentChangedFn onContentChanged = {});
};

class LayerEffectRemoveCommand : public LayerEffectCommand {
public:
    LayerEffectRemoveCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
        RequestRenderFn requestRender = {}, OnContentChangedFn onContentChanged = {});
};

class LayerEffectMoveCommand : public LayerEffectCommand {
public:
    LayerEffectMoveCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
        RequestRenderFn requestRender = {}, OnContentChangedFn onContentChanged = {});
};

class LayerEffectEnabledCommand : public LayerEffectCommand {
public:
    LayerEffectEnabledCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
        RequestRenderFn requestRender = {}, OnContentChangedFn onContentChanged = {});
};

class LayerEffectRealtimePreviewCommand : public LayerEffectCommand {
public:
    LayerEffectRealtimePreviewCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
        RequestRenderFn requestRender = {}, OnContentChangedFn onContentChanged = {});
};

class LayerEffectParamCommand : public LayerEffectCommand {
public:
    LayerEffectParamCommand(ruwa::core::layers::LayerModel* layerModel,
        ruwa::core::layers::LayerId layerId, EffectList before, EffectList after,
        RequestRenderFn requestRender = {}, OnContentChangedFn onContentChanged = {},
        QUuid mergeSessionId = {});

    bool mergeWith(const IUndoCommand& newer) override;
    qint64 memorySize() const override;

private:
    QUuid m_mergeSessionId;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_LAYEREFFECTCOMMANDS_H
