// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S E L E C T I O N   C O N T R O L L E R
// ==========================================================================
// Extracts lasso, rect, circle selection logic from OpenGLCanvasWidget.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_SELECTION_CANVASSELECTIONCONTROLLER_H
#define RUWA_FEATURES_CANVAS_SELECTION_CANVASSELECTIONCONTROLLER_H

#include "shared/types/Types.h"
#include "features/selection/LassoSelectionManager.h"

#include <QtGui/qopengl.h>

#include <QColor>
#include <QRectF>
#include <QUuid>

#include <functional>
#include <memory>
#include <vector>

namespace ruwa::core::layers {
class LayerModel;
struct LayerData;
} // namespace ruwa::core::layers

namespace aether {

class Canvas;
class GLSelectionRenderer;
class GLTileRenderer;
class GLRenderer;
class DrawCommand;

/**
 * @brief Context for CanvasSelectionController — callbacks to access widget/canvas state.
 */
struct CanvasSelectionContext {
    std::function<const Canvas&()> getCanvas;
    std::function<Canvas*()> getCanvasForEdit;
    std::function<float()> getZoom;
    std::function<GLTileRenderer*()> getTileRenderer;
    std::function<GLSelectionRenderer*()> getSelectionRenderer;
    std::function<GLRenderer*()> getRenderer;
    std::function<ruwa::core::layers::LayerData*()> getActiveLayer;
    std::function<ruwa::core::layers::LayerModel*()> getLayerModel;
    std::function<TileGrid*(const ruwa::core::layers::LayerData*)> getCompositingGridForLayer;
    // Returns a throwaway CPU tile grid whose alpha matches the layer's
    // effect-processed (composited) result — used so a content selection traces
    // the shape the effect chain actually renders (e.g. a twirl/blur silhouette)
    // instead of the raw pixels. Returns nullptr when there is nothing to bake
    // (no enabled effects, or a non-raster layer), in which case the caller
    // falls back to getCompositingGridForLayer.
    std::function<std::shared_ptr<TileGrid>(const ruwa::core::layers::LayerData*)>
        getEffectShapedGrid;
    std::function<bool()> isTransformActive;
    std::function<void()> requestRender;
    std::function<void()> startSelectionTick;
    std::function<bool()> isSelectionTickActive;
    std::function<bool(const QColor&)> executeFillWithColor;
    std::function<bool()> executeClearSelectionContent;
};

struct PendingSelectionReadback {
    bool active = false;
    std::vector<TileKey> keys;
    GLsync fence = nullptr;
};

struct PendingSelectionJob {
    bool active = false;
    LassoSelectionMode mode = LassoSelectionMode::Replace;
    uint8_t strength = 255;
    std::vector<Vector2> polygon;
    std::vector<Vector2> triVerts;
    std::vector<TileKey> tiles;
    size_t nextTile = 0;
    std::vector<TileKey> processed;
};

class CanvasSelectionController {
public:
    explicit CanvasSelectionController(const CanvasSelectionContext& ctx);
    ~CanvasSelectionController();

    CanvasSelectionController(const CanvasSelectionController&) = delete;
    CanvasSelectionController& operator=(const CanvasSelectionController&) = delete;

    void setContext(const CanvasSelectionContext& ctx) { m_ctx = ctx; }

    void beginLasso(float worldX, float worldY, bool addSelection, bool subtractSelection);
    void updateLasso(float worldX, float worldY);
    void endLasso(bool addSelection, bool subtractSelection);

    void beginRectSelection(float worldX, float worldY, bool addSelection, bool subtractSelection);
    void updateRectSelection(float worldX, float worldY);
    void endRectSelection(bool addSelection, bool subtractSelection);

    void beginCircleSelection(
        float worldX, float worldY, bool addSelection, bool subtractSelection);
    void updateCircleSelection(float worldX, float worldY);
    void endCircleSelection(bool addSelection, bool subtractSelection);

    void translateActiveSelection(float dx, float dy);
    void clearSelectionMask();
    void selectActiveLayerContent();

    bool hasSelectionMask() const;
    bool selectionBoundsWorld(QRectF& outBounds) const;
    bool fillSelectionWithColor(const QColor& color);
    bool clearSelectionContent();

    bool isLassoActive() const { return m_isLassoActive; }
    bool isRectSelectionActive() const { return m_isRectSelectionActive; }
    /// While a rect selection is being dragged, the live (normalized) rectangle in
    /// world/document coordinates. Returns false before the first drag update.
    bool liveRectBoundsWorld(QRectF& out) const;
    bool isCircleSelectionActive() const { return m_isCircleSelectionActive; }
    bool selectionWillReplace() const { return m_selectionWillReplace; }
    bool selectionIsAdd() const { return m_selectionIsAdd; }
    bool selectionIsSubtract() const { return m_selectionIsSubtract; }
    const std::vector<Vector2>& lassoPoints() const { return m_lassoPoints; }
    LassoSelectionManager& lassoSelection() { return m_lassoSelection; }
    const LassoSelectionManager& lassoSelection() const { return m_lassoSelection; }
    QUuid contentSelectionSourceLayerId() const { return m_contentSelectionSourceLayerId; }
    PendingSelectionJob& pendingSelectionJob() { return m_pendingSelectionJob; }
    const PendingSelectionJob& pendingSelectionJob() const { return m_pendingSelectionJob; }
    PendingSelectionReadback& pendingSelectionReadback() { return m_pendingSelectionReadback; }
    const PendingSelectionReadback& pendingSelectionReadback() const
    {
        return m_pendingSelectionReadback;
    }

    /// Process one frame of selection readback. Returns true if update() should be called.
    bool processSelectionReadbackFrame();

    void shutdown(GLSelectionRenderer* selectionRenderer);

private:
    void commitPolygonSelection(std::vector<Vector2> clipped, LassoSelectionMode mode);
    void clearSelectionInternal();

    CanvasSelectionContext m_ctx;
    LassoSelectionManager m_lassoSelection;
    QUuid m_contentSelectionSourceLayerId;
    PendingSelectionReadback m_pendingSelectionReadback;
    PendingSelectionJob m_pendingSelectionJob;

    bool m_isLassoActive = false;
    std::vector<Vector2> m_lassoPoints;
    bool m_selectionWillReplace = false;
    bool m_selectionIsAdd = false;
    bool m_selectionIsSubtract = false;

    bool m_isRectSelectionActive = false;
    float m_rectStartX = 0.0f;
    float m_rectStartY = 0.0f;

    bool m_isCircleSelectionActive = false;
    float m_circleStartX = 0.0f;
    float m_circleStartY = 0.0f;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_SELECTION_CANVASSELECTIONCONTROLLER_H
