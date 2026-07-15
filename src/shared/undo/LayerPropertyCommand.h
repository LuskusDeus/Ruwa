// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   L A Y E R   P R O P E R T Y   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_LAYERPROPERTYCOMMAND_H
#define RUWA_CORE_UNDO_LAYERPROPERTYCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "features/layers/model/LayerData.h"

#include <QList>
#include <QVariant>
#include <functional>
#include <utility>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

/**
 * @brief Undo command for layer property changes.
 *
 * Stores (layerId, oldValue, newValue) per layer. Supports:
 * - Opacity (save only on slider release)
 * - BlendMode
 * - Visible
 * - Locked
 * - AlphaLock
 * - Expanded
 * - ClippedToBelow
 */
class LayerPropertyCommand : public IUndoCommand {
public:
    enum class Property {
        Opacity,
        BlendMode,
        Visible,
        Locked,
        AlphaLock,
        Expanded,
        ClippedToBelow
    };

    struct Entry {
        ruwa::core::layers::LayerId layerId;
        QVariant oldValue;
        QVariant newValue;
    };

    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    LayerPropertyCommand(ruwa::core::layers::LayerModel* layerModel, Property property,
        QList<Entry> entries, RequestRenderFn requestRender, OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    void applyValues(const QList<Entry>& entries, bool useOld);

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    Property m_property = Property::Opacity;
    QList<Entry> m_entries;

    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_LAYERPROPERTYCOMMAND_H
