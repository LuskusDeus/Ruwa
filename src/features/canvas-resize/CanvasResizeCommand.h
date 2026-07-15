// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C A N V A S   R E S I Z E   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_CANVASRESIZECOMMAND_H
#define RUWA_CORE_UNDO_CANVASRESIZECOMMAND_H

#include "shared/undo/UndoManager.h"
#include "shared/tiles/TileTypes.h"
#include "features/transform/TransformState.h"

#include <QHash>
#include <QSize>
#include <QUuid>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class OpenGLCanvasWidget;

/**
 * @brief Undo command for canvas resize / crop.
 *
 * Stores only tiles that straddle or lie outside the keep-rect (the rest
 * is reconstructed losslessly by the inverse remap) plus pre-resize layer
 * transforms and camera position. Both redo() and initial apply share a
 * single static implementation to avoid divergence.
 */
class CanvasResizeCommand : public IUndoCommand {
public:
    using RawTileMap = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;
    using SetCanvasSizeFn = std::function<void(QSize)>;
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    struct Hooks {
        SetCanvasSizeFn setCanvasSize;
        RequestRenderFn requestRender;
        OnContentChangedFn onContentChanged;
    };

    struct Snapshot {
        /// Per-layer: only tiles NOT fully inside the keep-rect (raw premultiplied pixels).
        QHash<QUuid, RawTileMap> croppedTiles;
        /// Smart and text layers: transforms before the resize.
        QHash<QUuid, TransformState> transformsBefore;
        /// Camera world position before the resize.
        aether::Vector2 cameraPositionBefore { 0.0f, 0.0f };
    };

    /// Apply the resize synchronously (parallelized internally) and return
    /// a minimal snapshot for undo.
    static Snapshot applyResize(OpenGLCanvasWidget* glWidget,
        ruwa::core::layers::LayerModel* layerModel, QSize oldSize, int offsetX, int offsetY,
        QSize newSize, const Hooks& hooks);

    CanvasResizeCommand(OpenGLCanvasWidget* glWidget, ruwa::core::layers::LayerModel* layerModel,
        QSize oldSize, int offsetX, int offsetY, QSize newSize, Snapshot snapshot, Hooks hooks);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;

private:
    OpenGLCanvasWidget* m_glWidget = nullptr;
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;

    QSize m_oldSize;
    int m_offsetX = 0;
    int m_offsetY = 0;
    QSize m_newSize;

    Snapshot m_snapshot;
    Hooks m_hooks;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_CANVASRESIZECOMMAND_H
