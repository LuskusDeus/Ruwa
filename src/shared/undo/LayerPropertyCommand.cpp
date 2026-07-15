// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   L A Y E R   P R O P E R T Y   C O M M A N D
// ==========================================================================

#include "shared/undo/LayerPropertyCommand.h"
#include "features/layers/model/LayerModel.h"

namespace aether {

namespace {

QString propertyDisplayName(LayerPropertyCommand::Property p)
{
    switch (p) {
    case LayerPropertyCommand::Property::Opacity:
        return QStringLiteral("Opacity");
    case LayerPropertyCommand::Property::BlendMode:
        return QStringLiteral("Blend Mode");
    case LayerPropertyCommand::Property::Visible:
        return QStringLiteral("Visibility");
    case LayerPropertyCommand::Property::Locked:
        return QStringLiteral("Lock");
    case LayerPropertyCommand::Property::AlphaLock:
        return QStringLiteral("Alpha Lock");
    case LayerPropertyCommand::Property::Expanded:
        return QStringLiteral("Expand");
    case LayerPropertyCommand::Property::ClippedToBelow:
        return QStringLiteral("Clip to Below");
    }
    return QStringLiteral("Layer Property");
}

} // namespace

LayerPropertyCommand::LayerPropertyCommand(ruwa::core::layers::LayerModel* layerModel,
    Property property, QList<Entry> entries, RequestRenderFn requestRender,
    OnContentChangedFn onContentChanged)
    : m_layerModel(layerModel)
    , m_property(property)
    , m_entries(std::move(entries))
    , m_requestRender(std::move(requestRender))
    , m_onContentChanged(std::move(onContentChanged))
{
}

void LayerPropertyCommand::undo()
{
    applyValues(m_entries, true);
}

void LayerPropertyCommand::redo()
{
    applyValues(m_entries, false);
}

void LayerPropertyCommand::applyValues(const QList<Entry>& entries, bool useOld)
{
    if (!m_layerModel)
        return;

    for (const Entry& e : entries) {
        const QVariant& v = useOld ? e.oldValue : e.newValue;
        switch (m_property) {
        case Property::Opacity: {
            bool ok = false;
            qreal val = v.toDouble(&ok);
            if (ok)
                m_layerModel->setLayerOpacity(e.layerId, val);
            break;
        }
        case Property::BlendMode: {
            bool ok = false;
            int val = v.toInt(&ok);
            if (ok)
                m_layerModel->setLayerBlendMode(
                    e.layerId, static_cast<ruwa::core::layers::BlendMode>(val));
            break;
        }
        case Property::Visible:
            m_layerModel->setLayerVisible(e.layerId, v.toBool());
            break;
        case Property::Locked:
            m_layerModel->setLayerLocked(e.layerId, v.toBool());
            break;
        case Property::AlphaLock:
            m_layerModel->setLayerAlphaLock(e.layerId, v.toBool());
            break;
        case Property::Expanded:
            m_layerModel->setLayerExpanded(e.layerId, v.toBool());
            break;
        case Property::ClippedToBelow:
            m_layerModel->setLayerClippedToBelow(e.layerId, v.toBool());
            break;
        }
    }

    if (m_requestRender)
        m_requestRender();
    if (m_onContentChanged)
        m_onContentChanged();
}

QString LayerPropertyCommand::text() const
{
    return propertyDisplayName(m_property);
}

qint64 LayerPropertyCommand::memorySize() const
{
    qint64 size = sizeof(LayerPropertyCommand);
    size += m_entries.size() * (sizeof(Entry) + 64); // rough estimate for QVariant
    return size;
}

bool LayerPropertyCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    Q_UNUSED(offsetX);
    Q_UNUSED(offsetY);
    Q_UNUSED(newWidth);
    Q_UNUSED(newHeight);
    return true;
}

} // namespace aether
