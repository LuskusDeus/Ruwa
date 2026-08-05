// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A Y E R   C O M P O S I T I N G   B U I L D E R
// ==========================================================================
// Extracts layer stack building and compositing logic from OpenGLCanvasWidget.
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_LAYERCOMPOSITINGBUILDER_H
#define AETHER_ENGINE_OPENGL_LAYERCOMPOSITINGBUILDER_H

#include "shared/types/Types.h"
#include "features/canvas/rendering/GLCompositor.h"

#include <QHash>
#include <QString>
#include <QUuid>

#include <functional>
#include <vector>

namespace ruwa::core::layers {
class LayerModel;
struct LayerData;
} // namespace ruwa::core::layers

namespace aether {

class GLRenderer;
class TransformController;

struct LassoFillPreviewPlan {
    std::vector<CompositeLayerInfo> sceneBelowTargetGroup;
    std::vector<CompositeLayerInfo> targetGroup;
    std::vector<CompositeLayerInfo> sceneAboveTargetGroup;
    std::vector<CompositeLayerInfo> groupBelowTargetLayer;
    std::vector<CompositeLayerInfo> groupAboveTargetLayer;
    CompositeLayerInfo targetLayerBase;
    QUuid targetLayerId;
    QUuid groupBoundaryId;
    bool requiresIsolation = false;
    bool valid = false;
};

/**
 * @brief Context for LayerCompositingBuilder — callbacks to access widget/canvas state.
 */
struct LayerCompositingContext {
    std::function<ruwa::core::layers::LayerData*()> getActiveLayer;
    std::function<bool()> getBrushHasActiveStroke;
    std::function<TileGrid*()> getBrushStrokeBuffer;
    std::function<float()> getBrushStrokeOpacity;
    std::function<int()> getBrushStrokeBlendMode;
    std::function<bool()> getBrushIsEraseMode;
    std::function<bool()> getBrushIsBlurMode;
    std::function<bool()> getBrushIsSmudgeMode;
    std::function<bool()> getBrushIsWetMode;
    std::function<bool()> getBrushIsLiquifyMode;
    std::function<const TileGrid*()> getSelectionMaskGrid;
    std::function<bool()> getSelectionMaskHasSoftAlpha;
    std::function<bool(const ruwa::core::layers::LayerData*, const TileGrid*)>
        shouldPreserveAlphaForPaintMask;
    std::function<TransformController*()> getTransformController;
    std::function<GLRenderer*()> getRenderer;
    std::function<bool()> useViewportTransformPreview;
    std::function<bool()> getTransformPreserveMaskedSource;
};

/**
 * @brief Builds the layer stack for compositing from LayerModel.
 * Owns buildLayerStack, buildLayerStackRecursive, compositingGridForLayer,
 * resolveCanvasBackgroundColor.
 */
class LayerCompositingBuilder {
public:
    /// @param layerModelPtr Pointer to the widget's m_layerModel (so it stays current when
    /// setLayerModel is called)
    LayerCompositingBuilder(ruwa::core::layers::LayerModel* const* layerModelPtr,
        const QHash<QUuid, std::shared_ptr<TileGrid>>& smartProjectedGrids,
        const LayerCompositingContext& context);
    ~LayerCompositingBuilder();

    LayerCompositingBuilder(const LayerCompositingBuilder&) = delete;
    LayerCompositingBuilder& operator=(const LayerCompositingBuilder&) = delete;

    void setContext(const LayerCompositingContext& context)
    {
        m_context = context;
        invalidateCaches();
    }

    /// Build the full layer stack for compositing (bottom to top order).
    const std::vector<CompositeLayerInfo>& buildLayerStack() const;
    /// Build the visual-only layer stack that should render outside document bounds and stay out of
    /// export.
    const std::vector<CompositeLayerInfo>& buildBoardLayerStack() const;
    /// Build a stack from the document bottom through the target layer content,
    /// excluding active stroke preview and all layers above the target.
    std::vector<CompositeLayerInfo> buildStackThroughLayer(const QUuid& targetLayerId) const;

    /**
     * @brief Build a compositing stack for a layer list that is NOT the live
     *        document — a smart object's nested document, flattened offscreen.
     *
     * Everything the live stack folds in because a human is looking at it (the
     * in-progress brush stroke, the transform preview, the reduced
     * preview-disabled effect chain) is deliberately left out: this result is
     * BAKED into pixels, so it must be the committed, fully-effected truth.
     *
     * Not cached — the caller composites the result and throws it away, and the
     * cache identity here would be the document, not the widget state.
     */
    std::vector<CompositeLayerInfo> buildOffscreenLayerStack(
        const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& roots) const;
    LassoFillPreviewPlan buildLassoFillPreviewPlan(const QUuid& activeLayerId) const;
    void invalidateCaches() const;

    /// Resolve the canvas background color from the background layer. Returns false if
    /// transparent/none.
    bool resolveCanvasBackgroundColor(Color& outColor) const;

    /// Same rule, for a bare layer list with no LayerModel behind it (a smart
    /// object's nested document). Note that an OFFSCREEN stack composites that
    /// background as a real solid layer instead of leaving it to a backdrop
    /// pass, so this answers "is the canvas covered", not "what to pass as the
    /// backdrop colour".
    static bool resolveBackgroundColorForLayers(
        const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& roots, Color& outColor);

    /// Get the compositing grid for a layer (smart projected or direct pixel grid).
    TileGrid* compositingGridForLayer(const ruwa::core::layers::LayerData* layer) const
    {
        return compositingGridForLayer(layer, /*useProjectionCache=*/true);
    }

    /// Returns true if the mask has any semi-transparent pixels (0 < alpha < 255).
    /// Used to detect soft masks; callers decide whether that should preserve target alpha.
    static bool hasSoftMaskAlpha(const TileGrid* mask);

private:
    struct BuildStateSnapshot {
        bool hasLayerModel = false;
        QUuid activeLayerId;
        bool activeLayerAlphaLock = false;
        bool hasActiveStroke = false;
        int brushStrokeBlendMode = 0;
        bool brushEraseMode = false;
        bool brushBlurMode = false;
        bool brushSmudgeMode = false;
        bool selectionMaskHasSoftAlpha = false;
        bool activeStrokePreserveAlpha = false;
        bool transformControllerActive = false;
        QUuid transformControllerLayerId;
        QString transformControllerStateKey;
        bool transformPreserveMaskedSource = false;
        bool rendererHasTransformAtlas = false;
        bool useViewportTransformPreview = false;
        bool operator==(const BuildStateSnapshot& other) const;
        bool operator!=(const BuildStateSnapshot& other) const { return !(*this == other); }
    };

    /// @param useProjectionCache reads the widget's per-layer smart projection
    ///        cache. False for an offscreen stack: that cache describes the LIVE
    ///        document's layers, and a nested document's layers are projected by
    ///        the caller instead (SmartContentCompositor). Ids are unique, so
    ///        this is belt-and-braces — but a nested stack must never be able to
    ///        pick up a projection that belongs to somebody else's layer.
    TileGrid* compositingGridForLayer(
        const ruwa::core::layers::LayerData* layer, bool useProjectionCache) const;

    BuildStateSnapshot buildStateSnapshot() const;
    const std::vector<CompositeLayerInfo>& cachedStack(bool boardOnly) const;
    /// @param offscreen suppresses every live-edit input (active stroke,
    ///        transform preview, preview-disabled effect reduction) — see
    ///        buildOffscreenLayerStack.
    std::vector<CompositeLayerInfo> buildLayerStackRecursive(
        const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers, bool boardOnly,
        bool offscreen = false) const;

    ruwa::core::layers::LayerModel* const* m_layerModelPtr;
    const QHash<QUuid, std::shared_ptr<TileGrid>>& m_smartProjectedGrids;
    LayerCompositingContext m_context;
    mutable bool m_cachesDirty = true;
    mutable bool m_cachedStateValid = false;
    mutable BuildStateSnapshot m_cachedState;
    mutable std::vector<CompositeLayerInfo> m_cachedLayerStack;
    mutable std::vector<CompositeLayerInfo> m_cachedBoardLayerStack;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_LAYERCOMPOSITINGBUILDER_H
