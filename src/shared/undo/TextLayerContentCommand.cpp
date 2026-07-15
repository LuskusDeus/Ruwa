// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T E X T   L A Y E R   C O N T E N T
// ==========================================================================

#include "shared/undo/TextLayerContentCommand.h"

#include "features/layers/model/LayerModel.h"

#include <utility>

namespace aether {

TextLayerContentCommand::TextLayerContentCommand(ruwa::core::layers::LayerModel* layerModel,
    const ruwa::core::layers::LayerId& layerId, QString oldText, QString newText,
    QList<ruwa::core::layers::TextStyleRun> oldStyleRuns,
    QList<ruwa::core::layers::TextStyleRun> newStyleRuns, TransformState oldTransform,
    TransformState newTransform, RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
    : m_layerModel(layerModel)
    , m_layerId(layerId)
    , m_oldText(std::move(oldText))
    , m_newText(std::move(newText))
    , m_oldStyleRuns(std::move(oldStyleRuns))
    , m_newStyleRuns(std::move(newStyleRuns))
    , m_oldTransform(std::move(oldTransform))
    , m_newTransform(std::move(newTransform))
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
{
}

void TextLayerContentCommand::undo()
{
    applyTextState(m_oldText, m_oldStyleRuns, m_oldTransform);
}

void TextLayerContentCommand::redo()
{
    applyTextState(m_newText, m_newStyleRuns, m_newTransform);
}

void TextLayerContentCommand::applyTextState(const QString& textValue,
    const QList<ruwa::core::layers::TextStyleRun>& styleRuns, const TransformState& transform)
{
    if (!m_layerModel) {
        return;
    }

    auto* layer = m_layerModel->layerById(m_layerId);
    if (!layer || !layer->isText() || !layer->textData) {
        return;
    }

    layer->textData->text = textValue;
    layer->textData->styleRuns = styleRuns;
    layer->textData->transform = transform;
    layer->runtimeRetainedPayload.reset();
    layer->runtimeRetainedPayloadKey.clear();
    m_layerModel->refreshTextLayerAutoName(m_layerId);
    m_layerModel->notifyLayerDataChanged(m_layerId);

    if (m_requestRender) {
        m_requestRender();
    }
    if (m_onContentChanged) {
        m_onContentChanged();
    }
}

QString TextLayerContentCommand::text() const
{
    return QStringLiteral("Edit Text");
}

qint64 TextLayerContentCommand::memorySize() const
{
    return sizeof(TextLayerContentCommand)
        + static_cast<qint64>((m_oldText.size() + m_newText.size()) * sizeof(QChar))
        + static_cast<qint64>((m_oldStyleRuns.size() + m_newStyleRuns.size())
            * sizeof(ruwa::core::layers::TextStyleRun));
}

bool TextLayerContentCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    m_oldTransform.shiftForCanvasResize(offsetX, offsetY);
    m_newTransform.shiftForCanvasResize(offsetX, offsetY);
    return true;
}

} // namespace aether
