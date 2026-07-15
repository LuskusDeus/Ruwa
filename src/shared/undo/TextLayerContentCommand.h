// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T E X T   L A Y E R   C O N T E N T
// ==========================================================================

#ifndef RUWA_CORE_UNDO_TEXTLAYERCONTENTCOMMAND_H
#define RUWA_CORE_UNDO_TEXTLAYERCONTENTCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/layers/model/LayerData.h"

#include <functional>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class TextLayerContentCommand : public IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    TextLayerContentCommand(ruwa::core::layers::LayerModel* layerModel,
        const ruwa::core::layers::LayerId& layerId, QString oldText, QString newText,
        QList<ruwa::core::layers::TextStyleRun> oldStyleRuns,
        QList<ruwa::core::layers::TextStyleRun> newStyleRuns, TransformState oldTransform,
        TransformState newTransform, RequestRenderFn requestRender,
        OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    void applyTextState(const QString& textValue,
        const QList<ruwa::core::layers::TextStyleRun>& styleRuns, const TransformState& transform);

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    QString m_oldText;
    QString m_newText;
    QList<ruwa::core::layers::TextStyleRun> m_oldStyleRuns;
    QList<ruwa::core::layers::TextStyleRun> m_newStyleRuns;
    TransformState m_oldTransform;
    TransformState m_newTransform;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_TEXTLAYERCONTENTCOMMAND_H
