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
class LayerData;
} // namespace ruwa::core::layers

namespace aether {

class GLRenderer;
class TransformController;

struct FillPreviewCompositingState {
    bool active = false;
    QUuid targetLayerId;
    bool maskTarget = false;
    TileGrid* previewContentGrid = nullptr;
    TileGrid* fillMaskGrid = nullptr;
    const RetainedRenderPayload* retainedPayload = nullptr;
    bool useSolidColor = false;
    bool renderAboveLayerContent = false;
    Color solidColor {};
    Vector2 origin {};
    float radius = 0.0f;
    float feather = 0.0f;
};

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
    std::function<const FillPreviewCompositingState*()> getFillPreview;
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
    LassoFillPreviewPlan buildLassoFillPreviewPlan(const QUuid& activeLayerId) const;
    void invalidateCaches() const;

    /// Resolve the canvas background color from the background layer. Returns false if
    /// transparent/none.
    bool resolveCanvasBackgroundColor(Color& outColor) const;

    /// Get the compositing grid for a layer (smart projected or direct pixel grid).
    TileGrid* compositingGridForLayer(const ruwa::core::layers::LayerData* layer) const;

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
        bool fillPreviewActive = false;
        QUuid fillPreviewTargetLayerId;
        bool fillPreviewMaskTarget = false;
        TileGrid* fillPreviewContentGrid = nullptr;
        TileGrid* fillPreviewMaskGrid = nullptr;
        const RetainedRenderPayload* fillPreviewRetainedPayload = nullptr;
        bool fillPreviewUseSolidColor = false;
        bool fillPreviewRenderAboveLayerContent = false;
        Color fillPreviewSolidColor {};
        Vector2 fillPreviewOrigin {};
        float fillPreviewRadius = 0.0f;
        float fillPreviewFeather = 0.0f;

        bool operator==(const BuildStateSnapshot& other) const;
        bool operator!=(const BuildStateSnapshot& other) const { return !(*this == other); }
    };

    BuildStateSnapshot buildStateSnapshot() const;
    const std::vector<CompositeLayerInfo>& cachedStack(bool boardOnly) const;
    std::vector<CompositeLayerInfo> buildLayerStackRecursive(
        const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers, bool boardOnly) const;

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
