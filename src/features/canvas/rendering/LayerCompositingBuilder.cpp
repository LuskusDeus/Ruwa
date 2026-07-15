// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A Y E R   C O M P O S I T I N G   B U I L D E R
// ==========================================================================

#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/canvas/rendering/GLRenderer.h"
#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"
#include "features/transform/GLTransformRenderer.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include "features/transform/TransformController.h"
#include "shared/tiles/TileTypes.h"

#include <QColor>
#include <QString>
#include <algorithm>

namespace aether {

namespace {

QString transformRenderKey(const TransformState& transform)
{
    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                      .arg(QString::number(transform.contentBounds.x, 'f', 3),
                          QString::number(transform.contentBounds.y, 'f', 3),
                          QString::number(transform.contentBounds.width, 'f', 3),
                          QString::number(transform.contentBounds.height, 'f', 3),
                          QString::number(transform.translation.x, 'f', 3),
                          QString::number(transform.translation.y, 'f', 3),
                          QString::number(transform.rotation, 'f', 3),
                          QString::number(transform.scale.x, 'f', 3),
                          QString::number(transform.scale.y, 'f', 3));
    key += QStringLiteral("|%1|%2").arg(
        QString::number(transform.pivot.x, 'f', 3), QString::number(transform.pivot.y, 'f', 3));
    if (transform.freeCorners.has_value()) {
        for (const auto& corner : *transform.freeCorners) {
            key += QStringLiteral("|c:%1,%2")
                       .arg(QString::number(corner.x, 'f', 3), QString::number(corner.y, 'f', 3));
        }
    }
    if (transform.deformMesh.has_value()) {
        key += QStringLiteral("|m:%1,%2")
                   .arg(transform.deformMesh->rows)
                   .arg(transform.deformMesh->cols);
        for (const auto& vertex : transform.deformMesh->vertices) {
            key += QStringLiteral("|v:%1,%2")
                       .arg(QString::number(vertex.target.x, 'f', 3),
                           QString::number(vertex.target.y, 'f', 3));
        }
    }
    return key;
}

bool appendStackThroughLayer(const std::vector<CompositeLayerInfo>& source,
    const QUuid& targetLayerId, std::vector<CompositeLayerInfo>& out)
{
    for (const auto& layer : source) {
        CompositeLayerInfo copy = layer;
        if (layer.isGroup) {
            copy.children.clear();
            if (appendStackThroughLayer(layer.children, targetLayerId, copy.children)) {
                out.push_back(std::move(copy));
                return true;
            }
        } else {
            out.push_back(copy);
            if (layer.id == targetLayerId) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

LayerCompositingBuilder::LayerCompositingBuilder(
    ruwa::core::layers::LayerModel* const* layerModelPtr,
    const QHash<QUuid, std::shared_ptr<TileGrid>>& smartProjectedGrids,
    const LayerCompositingContext& context)
    : m_layerModelPtr(layerModelPtr)
    , m_smartProjectedGrids(smartProjectedGrids)
    , m_context(context)
{
}

LayerCompositingBuilder::~LayerCompositingBuilder() = default;

bool LayerCompositingBuilder::BuildStateSnapshot::operator==(const BuildStateSnapshot& other) const
{
    return hasLayerModel == other.hasLayerModel && activeLayerId == other.activeLayerId
        && activeLayerAlphaLock == other.activeLayerAlphaLock
        && hasActiveStroke == other.hasActiveStroke
        && brushStrokeBlendMode == other.brushStrokeBlendMode
        && brushEraseMode == other.brushEraseMode && brushBlurMode == other.brushBlurMode
        && brushSmudgeMode == other.brushSmudgeMode
        && selectionMaskHasSoftAlpha == other.selectionMaskHasSoftAlpha
        && activeStrokePreserveAlpha == other.activeStrokePreserveAlpha
        && transformControllerActive == other.transformControllerActive
        && transformControllerLayerId == other.transformControllerLayerId
        && transformControllerStateKey == other.transformControllerStateKey
        && transformPreserveMaskedSource == other.transformPreserveMaskedSource
        && rendererHasTransformAtlas == other.rendererHasTransformAtlas
        && useViewportTransformPreview == other.useViewportTransformPreview
        && fillPreviewActive == other.fillPreviewActive
        && fillPreviewTargetLayerId == other.fillPreviewTargetLayerId
        && fillPreviewMaskTarget == other.fillPreviewMaskTarget
        && fillPreviewContentGrid == other.fillPreviewContentGrid
        && fillPreviewMaskGrid == other.fillPreviewMaskGrid
        && fillPreviewRetainedPayload == other.fillPreviewRetainedPayload
        && fillPreviewUseSolidColor == other.fillPreviewUseSolidColor
        && fillPreviewRenderAboveLayerContent == other.fillPreviewRenderAboveLayerContent
        && fillPreviewSolidColor.r == other.fillPreviewSolidColor.r
        && fillPreviewSolidColor.g == other.fillPreviewSolidColor.g
        && fillPreviewSolidColor.b == other.fillPreviewSolidColor.b
        && fillPreviewSolidColor.a == other.fillPreviewSolidColor.a
        && fillPreviewOrigin.x == other.fillPreviewOrigin.x
        && fillPreviewOrigin.y == other.fillPreviewOrigin.y
        && fillPreviewRadius == other.fillPreviewRadius
        && fillPreviewFeather == other.fillPreviewFeather;
}

// ==========================================================================
//   P U B L I C   A P I
// ==========================================================================

const std::vector<CompositeLayerInfo>& LayerCompositingBuilder::buildLayerStack() const
{
    return cachedStack(false);
}

const std::vector<CompositeLayerInfo>& LayerCompositingBuilder::buildBoardLayerStack() const
{
    return cachedStack(true);
}

std::vector<CompositeLayerInfo> LayerCompositingBuilder::buildStackThroughLayer(
    const QUuid& targetLayerId) const
{
    std::vector<CompositeLayerInfo> result;
    if (targetLayerId.isNull()) {
        return result;
    }
    if (!appendStackThroughLayer(buildLayerStack(), targetLayerId, result)) {
        result.clear();
    }
    return result;
}

LassoFillPreviewPlan LayerCompositingBuilder::buildLassoFillPreviewPlan(
    const QUuid& activeLayerId) const
{
    LassoFillPreviewPlan plan;
    if (activeLayerId.isNull()) {
        return plan;
    }

    plan.targetGroup = buildLayerStack();
    plan.targetLayerId = activeLayerId;
    plan.groupBoundaryId = activeLayerId;
    plan.requiresIsolation = false;

    std::function<bool(const std::vector<CompositeLayerInfo>&)> findTarget
        = [&](const std::vector<CompositeLayerInfo>& layers) -> bool {
        for (const auto& layer : layers) {
            if (layer.isGroup) {
                if (findTarget(layer.children)) {
                    return true;
                }
                continue;
            }
            if (layer.id == activeLayerId) {
                plan.targetLayerBase = layer;
                plan.valid = true;
                return true;
            }
        }
        return false;
    };
    findTarget(plan.targetGroup);
    return plan;
}

void LayerCompositingBuilder::invalidateCaches() const
{
    m_cachesDirty = true;
    m_cachedStateValid = false;
}

bool LayerCompositingBuilder::resolveCanvasBackgroundColor(Color& outColor) const
{
    auto* layerModel = m_layerModelPtr ? *m_layerModelPtr : nullptr;
    if (!layerModel) {
        return false;
    }

    const auto* backgroundLayer = layerModel->backgroundLayer();
    if (!backgroundLayer || !backgroundLayer->visible || backgroundLayer->backgroundTransparent) {
        return false;
    }

    const float layerOpacity = std::clamp(static_cast<float>(backgroundLayer->opacity), 0.0f, 1.0f);
    if (layerOpacity <= 0.0f) {
        return false;
    }

    const QColor bg = backgroundLayer->backgroundColor;
    const float alpha = std::clamp(static_cast<float>(bg.alphaF()) * layerOpacity, 0.0f, 1.0f);
    if (alpha <= 0.0f) {
        return false;
    }

    outColor = Color { static_cast<float>(bg.redF()), static_cast<float>(bg.greenF()),
        static_cast<float>(bg.blueF()), alpha };
    return true;
}

TileGrid* LayerCompositingBuilder::compositingGridForLayer(
    const ruwa::core::layers::LayerData* layer) const
{
    if (!layer) {
        return nullptr;
    }
    if (layer->isText()) {
        auto* mutableLayer = const_cast<ruwa::core::layers::LayerData*>(layer);
        ensureTextRetainedPayload(mutableLayer);
        return nullptr;
    }
    if (layer->isIsolatedPixelLayer()) {
        auto it = m_smartProjectedGrids.constFind(layer->id);
        if (it != m_smartProjectedGrids.constEnd() && it.value()) {
            return it.value().get();
        }
        return const_cast<TileGrid*>(layer->smartContentGrid.get());
    }
    return const_cast<TileGrid*>(layer->pixelGrid());
}

// ==========================================================================
//   P R I V A T E
// ==========================================================================

LayerCompositingBuilder::BuildStateSnapshot LayerCompositingBuilder::buildStateSnapshot() const
{
    BuildStateSnapshot snapshot;
    auto* layerModel = m_layerModelPtr ? *m_layerModelPtr : nullptr;
    snapshot.hasLayerModel = (layerModel != nullptr);

    auto* activeLayerPtr = m_context.getActiveLayer ? m_context.getActiveLayer() : nullptr;
    if (activeLayerPtr) {
        snapshot.activeLayerId = activeLayerPtr->id;
        snapshot.activeLayerAlphaLock = activeLayerPtr->alphaLock;
    }

    snapshot.hasActiveStroke = m_context.getBrushHasActiveStroke
        && m_context.getBrushHasActiveStroke() && activeLayerPtr != nullptr;
    snapshot.brushStrokeBlendMode
        = m_context.getBrushStrokeBlendMode ? m_context.getBrushStrokeBlendMode() : 0;
    snapshot.brushEraseMode = m_context.getBrushIsEraseMode && m_context.getBrushIsEraseMode();
    snapshot.brushBlurMode = m_context.getBrushIsBlurMode && m_context.getBrushIsBlurMode();
    snapshot.brushSmudgeMode = (m_context.getBrushIsSmudgeMode && m_context.getBrushIsSmudgeMode())
        || (m_context.getBrushIsWetMode && m_context.getBrushIsWetMode())
        || (m_context.getBrushIsLiquifyMode && m_context.getBrushIsLiquifyMode());
    const TileGrid* selectionMaskGrid
        = m_context.getSelectionMaskGrid ? m_context.getSelectionMaskGrid() : nullptr;
    snapshot.selectionMaskHasSoftAlpha = selectionMaskGrid
        && (m_context.getSelectionMaskHasSoftAlpha ? m_context.getSelectionMaskHasSoftAlpha()
                                                   : hasSoftMaskAlpha(selectionMaskGrid));
    snapshot.activeStrokePreserveAlpha = activeLayerPtr
        && (m_context.shouldPreserveAlphaForPaintMask
                ? m_context.shouldPreserveAlphaForPaintMask(activeLayerPtr, selectionMaskGrid)
                : activeLayerPtr->alphaLock);

    auto* transformController
        = m_context.getTransformController ? m_context.getTransformController() : nullptr;
    snapshot.transformControllerActive = transformController && transformController->isActive();
    if (snapshot.transformControllerActive) {
        snapshot.transformControllerLayerId = transformController->layerId();
        auto* transformLayer
            = layerModel ? layerModel->layerById(snapshot.transformControllerLayerId) : nullptr;
        if (transformLayer && transformLayer->isText()) {
            snapshot.transformControllerStateKey = transformRenderKey(transformController->state());
        }
    }
    snapshot.transformPreserveMaskedSource = m_context.getTransformPreserveMaskedSource
        && m_context.getTransformPreserveMaskedSource();
    auto* renderer = m_context.getRenderer ? m_context.getRenderer() : nullptr;
    snapshot.rendererHasTransformAtlas
        = renderer && renderer->transformRenderer() && renderer->transformRenderer()->hasAtlas();
    snapshot.useViewportTransformPreview
        = m_context.useViewportTransformPreview && m_context.useViewportTransformPreview();

    const FillPreviewCompositingState* fillPreview
        = m_context.getFillPreview ? m_context.getFillPreview() : nullptr;
    if (fillPreview) {
        snapshot.fillPreviewActive = fillPreview->active;
        snapshot.fillPreviewTargetLayerId = fillPreview->targetLayerId;
        snapshot.fillPreviewMaskTarget = fillPreview->maskTarget;
        snapshot.fillPreviewContentGrid = fillPreview->previewContentGrid;
        snapshot.fillPreviewMaskGrid = fillPreview->fillMaskGrid;
        snapshot.fillPreviewRetainedPayload = fillPreview->retainedPayload;
        snapshot.fillPreviewUseSolidColor = fillPreview->useSolidColor;
        snapshot.fillPreviewRenderAboveLayerContent = fillPreview->renderAboveLayerContent;
        snapshot.fillPreviewSolidColor = fillPreview->solidColor;
        snapshot.fillPreviewOrigin = fillPreview->origin;
        snapshot.fillPreviewRadius = fillPreview->radius;
        snapshot.fillPreviewFeather = fillPreview->feather;
    }

    return snapshot;
}

const std::vector<CompositeLayerInfo>& LayerCompositingBuilder::cachedStack(bool boardOnly) const
{
    const auto* layerModel = m_layerModelPtr ? *m_layerModelPtr : nullptr;
    auto& cache = boardOnly ? m_cachedBoardLayerStack : m_cachedLayerStack;
    if (!layerModel) {
        cache.clear();
        return cache;
    }

    const BuildStateSnapshot snapshot = buildStateSnapshot();
    if (!m_cachedStateValid || snapshot != m_cachedState) {
        m_cachedState = snapshot;
        m_cachedStateValid = true;
        m_cachesDirty = true;
    }

    if (m_cachesDirty) {
        m_cachedLayerStack = buildLayerStackRecursive(layerModel->rootLayers(), false);
        m_cachedBoardLayerStack = buildLayerStackRecursive(layerModel->rootLayers(), true);
        m_cachesDirty = false;
    }

    return cache;
}

bool LayerCompositingBuilder::hasSoftMaskAlpha(const TileGrid* mask)
{
    if (!mask || mask->empty())
        return false;
    constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
    for (const auto& [key, tile] : mask->tiles()) {
        Q_UNUSED(key);
        const uint8_t* px = tile.pixels();
        for (uint32_t i = 0; i < pixelCount; ++i) {
            const uint8_t a = px[i * TILE_CHANNELS + 3];
            if (a > 0 && a < 255) {
                return true;
            }
        }
    }
    return false;
}

std::vector<CompositeLayerInfo> LayerCompositingBuilder::buildLayerStackRecursive(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers, bool boardOnly) const
{
    std::vector<CompositeLayerInfo> result;
    result.reserve(layers.size());

    auto* activeLayerPtr = m_context.getActiveLayer ? m_context.getActiveLayer() : nullptr;
    const bool hasStroke = m_context.getBrushHasActiveStroke && m_context.getBrushHasActiveStroke()
        && activeLayerPtr != nullptr;
    const TileGrid* selectionMaskGrid
        = m_context.getSelectionMaskGrid ? m_context.getSelectionMaskGrid() : nullptr;
    const bool selectionMaskHasSoftAlpha = selectionMaskGrid
        && (m_context.getSelectionMaskHasSoftAlpha ? m_context.getSelectionMaskHasSoftAlpha()
                                                   : hasSoftMaskAlpha(selectionMaskGrid));
    const bool isEraseMode
        = m_context.getBrushIsEraseMode ? m_context.getBrushIsEraseMode() : false;
    const bool isBlurPreview = (m_context.getBrushIsBlurMode && m_context.getBrushIsBlurMode())
        || (m_context.getBrushIsSmudgeMode && m_context.getBrushIsSmudgeMode())
        || (m_context.getBrushIsWetMode && m_context.getBrushIsWetMode())
        || (m_context.getBrushIsLiquifyMode && m_context.getBrushIsLiquifyMode());
    const bool activeLayerAlphaLockPreview = !isEraseMode && activeLayerPtr
        && (m_context.shouldPreserveAlphaForPaintMask
                ? m_context.shouldPreserveAlphaForPaintMask(activeLayerPtr, selectionMaskGrid)
                : (activeLayerPtr->alphaLock || selectionMaskHasSoftAlpha));

    const FillPreviewCompositingState* fillPreview
        = m_context.getFillPreview ? m_context.getFillPreview() : nullptr;
    auto* transformController
        = m_context.getTransformController ? m_context.getTransformController() : nullptr;
    auto* renderer = m_context.getRenderer ? m_context.getRenderer() : nullptr;
    const bool useViewportTransformPreview
        = m_context.useViewportTransformPreview && m_context.useViewportTransformPreview();

    for (int i = layers.size() - 1; i >= 0; --i) {
        const auto& layerData = layers[i];
        if (!layerData) {
            continue;
        }
        if (!boardOnly && layerData->isExportExcluded()) {
            continue;
        }
        if (boardOnly && !layerData->isGroup() && !layerData->isExportExcluded()) {
            continue;
        }

        const bool isFillPreviewTarget = fillPreview && fillPreview->active && layerData->isRaster()
            && layerData->id == fillPreview->targetLayerId
            && (fillPreview->maskTarget
                    ? (fillPreview->previewContentGrid && !fillPreview->previewContentGrid->empty()
                          && fillPreview->fillMaskGrid && !fillPreview->fillMaskGrid->empty()
                          && layerData->maskTileGrid())
                    : ((fillPreview->fillMaskGrid && !fillPreview->fillMaskGrid->empty())
                          || fillPreview->retainedPayload));
        if (isFillPreviewTarget) {
            if (fillPreview->maskTarget) {
                CompositeLayerInfo info;
                info.id = layerData->id;
                info.effectChainRevision = layerData->effectChainRevision;
                info.effects = layerData->effects;
                info.liveEditedEffectId = layerData->liveEditedEffectId;
                info.liveEditedEffectParamKey = layerData->liveEditedEffectParamKey;
                info.liveEffectEditGeneration = layerData->liveEffectEditGeneration;
                info.opacity = static_cast<float>(layerData->opacity);
                info.blendMode = static_cast<int>(layerData->blendMode);
                info.visible = layerData->visible;
                info.clippedToBelow = layerData->clippedToBelow;
                info.tileGrid = compositingGridForLayer(layerData.get());
                info.externalClipMaskGrid = fillPreview->previewContentGrid;
                info.clipMaskGrid2 = const_cast<TileGrid*>(layerData->maskTileGrid());
                info.clipMaskEditReplace = true;
                info.clipMaskReplaceFallback = true;
                info.clipMaskEditStrokeOpacity = 1.0f;
                info.useRadialReveal = true;
                info.radialRevealOrigin = fillPreview->origin;
                info.radialRevealRadius = fillPreview->radius;
                info.radialRevealFeather = fillPreview->feather;
                result.push_back(std::move(info));
                continue;
            }

            CompositeLayerInfo groupInfo;
            groupInfo.id = layerData->id;
            groupInfo.effectChainRevision = layerData->effectChainRevision;
            groupInfo.effects = layerData->effects;
            groupInfo.liveEditedEffectId = layerData->liveEditedEffectId;
            groupInfo.liveEditedEffectParamKey = layerData->liveEditedEffectParamKey;
            groupInfo.liveEffectEditGeneration = layerData->liveEffectEditGeneration;
            groupInfo.opacity = static_cast<float>(layerData->opacity);
            groupInfo.blendMode = static_cast<int>(layerData->blendMode);
            groupInfo.visible = layerData->visible;
            groupInfo.isGroup = true;
            groupInfo.clippedToBelow = layerData->clippedToBelow;
            groupInfo.forceIsolation = true;
            const bool previewAboveLayerContent = fillPreview->renderAboveLayerContent;

            auto appendPreviewContent = [&]() {
                if (fillPreview->retainedPayload) {
                    CompositeLayerInfo previewContent;
                    previewContent.retainedPayload = fillPreview->retainedPayload;
                    previewContent.opacity = 1.0f;
                    previewContent.blendMode = 0;
                    previewContent.visible = true;
                    groupInfo.children.push_back(std::move(previewContent));
                } else if (fillPreview->useSolidColor) {
                    CompositeLayerInfo previewContent;
                    previewContent.hasSolidColor = true;
                    previewContent.solidColor = fillPreview->solidColor;
                    previewContent.opacity = 1.0f;
                    previewContent.blendMode = 0;
                    previewContent.visible = true;
                    previewContent.externalClipMaskGrid = fillPreview->fillMaskGrid;
                    previewContent.useRadialReveal = true;
                    previewContent.radialRevealOrigin = fillPreview->origin;
                    previewContent.radialRevealRadius = fillPreview->radius;
                    previewContent.radialRevealFeather = fillPreview->feather;
                    groupInfo.children.push_back(std::move(previewContent));
                } else if (fillPreview->previewContentGrid
                    && !fillPreview->previewContentGrid->empty()) {
                    CompositeLayerInfo previewContent;
                    previewContent.tileGrid = fillPreview->previewContentGrid;
                    previewContent.opacity = 1.0f;
                    previewContent.blendMode = 0;
                    previewContent.visible = true;
                    previewContent.externalClipMaskGrid = fillPreview->fillMaskGrid;
                    previewContent.useRadialReveal = true;
                    previewContent.radialRevealOrigin = fillPreview->origin;
                    previewContent.radialRevealRadius = fillPreview->radius;
                    previewContent.radialRevealFeather = fillPreview->feather;
                    groupInfo.children.push_back(std::move(previewContent));
                }
            };

            if (!previewAboveLayerContent) {
                appendPreviewContent();
            }

            CompositeLayerInfo filledContent;
            filledContent.id = layerData->id;
            filledContent.tileGrid = compositingGridForLayer(layerData.get());
            filledContent.opacity = 1.0f;
            filledContent.blendMode = 0;
            filledContent.visible = true;
            if (previewAboveLayerContent) {
                filledContent.externalClipMaskGrid = fillPreview->fillMaskGrid;
                filledContent.subtractClipRevealFromSrc = true;
                filledContent.useRadialReveal = true;
                filledContent.radialRevealOrigin = fillPreview->origin;
                filledContent.radialRevealRadius = fillPreview->radius;
                filledContent.radialRevealFeather = fillPreview->feather;
            }
            groupInfo.children.push_back(std::move(filledContent));

            if (previewAboveLayerContent) {
                appendPreviewContent();
            }

            if (layerData->maskAffectsCompositing()) {
                // Same gating as the static path / live-stroke path: a lasso-fill
                // preview on a masked layer must keep the mask applied. The fill is
                // baked into the layer pixels on commit and then gated by the mask,
                // so during preview the mask must gate the whole isolated group
                // (preview content + layer content). Without this the affected
                // tiles drop the mask mid-preview and snap back only on confirm.
                // groupInfo is already forceIsolation=true.
                groupInfo.externalClipMaskGrid = const_cast<TileGrid*>(layerData->maskTileGrid());
                groupInfo.clipMaskLuminanceReveal = true;
            }

            result.push_back(std::move(groupInfo));
            continue;
        }

        bool isActiveWithStroke
            = hasStroke && layerData->id == activeLayerPtr->id && layerData->isRaster();

        if (isActiveWithStroke) {
            const bool isMaskEditStroke
                = layerData->maskEditActive && layerData->maskGrid != nullptr;

            if (isMaskEditStroke) {
                // Live mask-edit preview, fully GPU and exact in a single pass.
                TileGrid* committedMask = const_cast<TileGrid*>(layerData->maskTileGrid());
                TileGrid* strokeBuffer
                    = m_context.getBrushStrokeBuffer ? m_context.getBrushStrokeBuffer() : nullptr;
                const float strokeOpacity
                    = m_context.getBrushStrokeOpacity ? m_context.getBrushStrokeOpacity() : 1.0f;

                CompositeLayerInfo info;
                info.id = layerData->id;
                info.effectChainRevision = layerData->effectChainRevision;
                info.effects = layerData->effects;
                info.liveEditedEffectId = layerData->liveEditedEffectId;
                info.liveEditedEffectParamKey = layerData->liveEditedEffectParamKey;
                info.liveEffectEditGeneration = layerData->liveEffectEditGeneration;
                info.opacity = static_cast<float>(layerData->opacity);
                info.blendMode = static_cast<int>(layerData->blendMode);
                info.visible = layerData->visible;
                info.clippedToBelow = layerData->clippedToBelow;
                info.tileGrid = compositingGridForLayer(layerData.get());
                info.externalClipMaskGrid = strokeBuffer; // primary clip = stroke
                info.clipMaskGrid2 = committedMask; // secondary clip = committed mask

                if (isBlurPreview) {
                    // Replace-mode tools (smudge / blur / liquify / wet) read the
                    // mask and write FINISHED tiles into the stroke buffer; commit
                    // does maskTile = mix(committed, stroke, op) — a REPLACE, not a
                    // src-over. Reveal is affine in the tile, so the exact preview
                    // is reveal = mix(committedReveal, strokeReveal, op). The
                    // additive edit-preview formula (stroke OVER committed) instead
                    // lets the old mask bleed through — the artifact seen live until
                    // confirm. clipMaskReplaceFallback makes tiles the stroke hasn't
                    // touched sample the committed mask, so the mix collapses to the
                    // unchanged committed reveal there.
                    info.clipMaskEditReplace = true;
                    info.clipMaskReplaceFallback = true;
                    info.clipMaskEditStrokeOpacity = strokeOpacity;
                } else {
                    // Accumulating brushes: shader combines the in-progress stroke
                    // and committed mask into the exact post-commit reveal
                    //   reveal = lum(stroke)*op + (1 - stroke.a*op) * committedReveal
                    // matching the flattened result (including soft brush edges).
                    info.clipMaskEditPreview = true;
                    info.clipMaskEditStrokeOpacity = strokeOpacity;
                }
                result.push_back(std::move(info));
                continue;
            }

            // "Preview" flag per effect: an enabled effect whose preview is OFF is
            // OMITTED from the in-progress stroke (so its — potentially expensive —
            // work is not computed while drawing), while committed pixels keep it.
            // preview-ON effects in the SAME chain still apply to the stroke. Build
            // the reduced chain (preview-disabled enabled effects removed, order of
            // the rest preserved) and put it on the GROUP (content + stroke), so the
            // stroke shows exactly the preview-applicable effects.
            //
            // When the reduced chain differs from the committed chain (i.e. some
            // effect is preview-disabled), OpenGLCanvasWidget markAllDirty's the
            // whole layer once on the stroke-state transition (m_strokeEffectSuppressed)
            // so the surrounding tiles also recomposite with the reduced chain (no
            // seam); the full chain is restored on commit/cancel via the same
            // transition. Handles any mix of preview on/off effects, and a fully
            // preview-off chain reduces to an empty chain (whole layer raw, cheap).
            auto previewEffects = layerData->effects;
            previewEffects.removeIf(
                [](const auto& fx) { return fx.enabled && !fx.realtimePreviewEnabled; });

            CompositeLayerInfo strokePreviewGroup;
            strokePreviewGroup.id = layerData->id;
            strokePreviewGroup.effectChainRevision = layerData->effectChainRevision;
            strokePreviewGroup.effects = previewEffects;
            strokePreviewGroup.liveEditedEffectId = layerData->liveEditedEffectId;
            strokePreviewGroup.liveEditedEffectParamKey = layerData->liveEditedEffectParamKey;
            strokePreviewGroup.liveEffectEditGeneration = layerData->liveEffectEditGeneration;
            strokePreviewGroup.opacity = static_cast<float>(layerData->opacity);
            strokePreviewGroup.blendMode = static_cast<int>(layerData->blendMode);
            strokePreviewGroup.visible = layerData->visible;
            strokePreviewGroup.isGroup = true;
            strokePreviewGroup.clippedToBelow = layerData->clippedToBelow;
            strokePreviewGroup.forceIsolation = true;

            CompositeLayerInfo layerContent;
            layerContent.id = layerData->id;
            layerContent.tileGrid = compositingGridForLayer(layerData.get());
            layerContent.opacity = 1.0f;
            layerContent.blendMode = 0;
            layerContent.visible = true;
            strokePreviewGroup.children.push_back(std::move(layerContent));

            TileGrid* strokeBuffer
                = m_context.getBrushStrokeBuffer ? m_context.getBrushStrokeBuffer() : nullptr;
            const float strokeOpacity
                = m_context.getBrushStrokeOpacity ? m_context.getBrushStrokeOpacity() : 1.0f;
            const bool isEraseMode
                = m_context.getBrushIsEraseMode ? m_context.getBrushIsEraseMode() : false;
            const int strokeBlendMode
                = m_context.getBrushStrokeBlendMode ? m_context.getBrushStrokeBlendMode() : 0;

            CompositeLayerInfo strokeLayer;
            strokeLayer.tileGrid = strokeBuffer;
            strokeLayer.opacity = strokeOpacity;
            strokeLayer.blendMode
                = isEraseMode ? kCompositeBlendModeErase : (isBlurPreview ? 0 : strokeBlendMode);
            strokeLayer.visible = true;
            strokeLayer.useStrokeBlendBackdrop
                = !isEraseMode && !isBlurPreview && strokeBlendMode != 0;
            strokeLayer.preserveBaseAlpha = !isBlurPreview && activeLayerAlphaLockPreview;
            strokeLayer.replaceBase = isBlurPreview;
            if (!isBlurPreview && !strokeLayer.preserveBaseAlpha && selectionMaskGrid) {
                // CompositeLayerInfo::externalClipMaskGrid is intentionally non-const
                // because GLCompositor performs per-tile GPU texture upload through it
                // (ensureTileTexture / uploadTileData). The pixel data of the selection
                // mask is read-only here — only TileData GPU bookkeeping (m_textureId,
                // m_dirty) is touched. Pixel-level mutation goes through
                // LassoSelectionManager::MaskMutationScope instead.
                strokeLayer.externalClipMaskGrid = const_cast<TileGrid*>(selectionMaskGrid);
                // Soft-selection alpha cap on non-source layers: the stroke pipeline
                // here is reached when preserveBaseAlpha is false (i.e. the alpha-lock
                // emulation is NOT active — typical case on non-source layers under
                // a soft selection). Without a cap, repeated strokes accumulate alpha
                // past the mask ceiling. Engaging the shader-level cap enforces the
                // user-facing rule "result alpha can never exceed mask alpha; pre-existing
                // alpha already above the cap is preserved verbatim". Binary masks
                // (no soft alpha) don't need this — gating alone is correct.
                strokeLayer.clipMaskAsAlphaCap = selectionMaskHasSoftAlpha;
            }
            strokePreviewGroup.children.push_back(std::move(strokeLayer));

            if (layerData->maskAffectsCompositing()) {
                // The layer carries an active mask but we are painting on the
                // PIXELS (not the mask — that is the isMaskEditStroke path above).
                // Apply the committed mask to the isolated stroke-preview group so
                // the in-progress content+stroke is gated exactly like the static
                // path (lines below). Without this the affected tiles drop the mask
                // mid-stroke and show the unmasked content, snapping back only on
                // commit. The group is already forceIsolation=true, so the mask
                // applies to the composited group result.
                strokePreviewGroup.externalClipMaskGrid
                    = const_cast<TileGrid*>(layerData->maskTileGrid());
                strokePreviewGroup.clipMaskLuminanceReveal = true;
            }

            result.push_back(std::move(strokePreviewGroup));
        } else {
            CompositeLayerInfo info;
            info.id = layerData->id;
            info.effectChainRevision = layerData->effectChainRevision;
            info.effects = layerData->effects;
            info.liveEditedEffectId = layerData->liveEditedEffectId;
            info.liveEditedEffectParamKey = layerData->liveEditedEffectParamKey;
            info.liveEffectEditGeneration = layerData->liveEffectEditGeneration;
            // "Preview" flag: while a brush stroke is in progress, drop the
            // enabled+preview-OFF effects so their (expensive) work — e.g. a blur on
            // ANY layer/group/adjustment that covers the tiles being drawn on — is
            // NOT recomputed every frame. The whole composite stack recomposites the
            // stroke's dirty tiles, so a preview-OFF effect on a layer OTHER than the
            // one being painted is still heavy; reduce it here too. The active raster
            // layer handles this via its strokePreviewGroup; every other layer is
            // reduced here. OpenGLCanvasWidget markAllDirty's on the suppress-state
            // transition so the surrounding tiles recomposite with the same reduced
            // chain (no seam); the full chain is restored on commit.
            if (hasStroke) {
                info.effects.removeIf(
                    [](const auto& fx) { return fx.enabled && !fx.realtimePreviewEnabled; });
            }
            info.opacity = static_cast<float>(layerData->opacity);
            info.blendMode = static_cast<int>(layerData->blendMode);
            info.visible = layerData->visible;
            info.isGroup = layerData->isGroup();
            info.forceIsolation = layerData->isGroupIsolated();
            info.isAdjustment = layerData->isAdjustment();
            info.clippedToBelow = layerData->clippedToBelow;
            if (layerData->isBackground()) {
                // The special background layer is rendered in dedicated backdrop passes.
                // Keeping it in the compositing stack makes affected cache tiles apply
                // the same background alpha twice on top of the checker/backdrop.
                continue;
            } else if (layerData->isText()) {
                auto* mutableLayer = layerData.get();
                if (ensureTextRetainedPayload(mutableLayer)) {
                    info.tileGrid = nullptr;
                    info.retainedPayload = mutableLayer->runtimeRetainedPayload.get();
                } else {
                    info.tileGrid = nullptr;
                    info.retainedPayload = nullptr;
                }
            } else if (layerData->hasRetainedVisualContent()) {
                info.tileGrid = nullptr;
                info.retainedPayload = layerData->runtimeRetainedPayload.get();
            } else {
                info.tileGrid = compositingGridForLayer(layerData.get());
            }

            const bool isTransformTarget = transformController && transformController->isActive()
                && layerData->id == transformController->layerId()
                && (layerData->isPixelLayer() || layerData->isText())
                && !useViewportTransformPreview;
            const bool isTextTransformTarget = isTransformTarget && layerData->isText();
            // A mask-edit transform warps the mask grid, not the content. The
            // screen-space viewport preview handles that case; this tile-composite
            // fallback (e.g. deform mesh) would otherwise warp the content grid by
            // the mask's transform, so suppress the content warp here. The mask
            // block below still applies the (unwarped) mask, so content stays put.
            const bool transformTargetEditingMask
                = isTransformTarget && layerData->maskEditActive && layerData->maskTileGrid();
            const bool isRasterTransformTarget = isTransformTarget && layerData->isPixelLayer()
                && !transformTargetEditingMask && renderer && renderer->transformRenderer()
                && renderer->transformRenderer()->hasAtlas();
            if (isTextTransformTarget) {
                info.tileGrid = nullptr;
                info.transform = nullptr;
                info.transformRenderer = nullptr;
                info.retainedPayloadOwner
                    = buildTextRetainedPayload(layerData.get(), transformController->state());
                info.retainedPayload = info.retainedPayloadOwner.get();
            } else if (isRasterTransformTarget) {
                info.tileGrid = nullptr;
                info.transform = &transformController->state();
                info.transformRenderer = renderer->transformRenderer();
                info.transformPreserveMaskedSource = m_context.getTransformPreserveMaskedSource
                    && m_context.getTransformPreserveMaskedSource();
            }

            if (layerData->isGroup() && layerData->hasChildren()) {
                info.children = buildLayerStackRecursive(layerData->children, boardOnly);
                if (boardOnly && info.children.empty()) {
                    continue;
                }
            }

            if (layerData->maskAffectsCompositing()) {
                // Layer mask: the grid stores the painted grayscale mask. The shader
                // computes reveal = luminance(rgb) + (1 - coverage), so white paint
                // reveals, black paint hides, and uncovered tiles default to fully
                // revealed (fresh mask = "reveal all"). Group masks are applied by
                // GLCompositor's final group-composite pass; they must not force the
                // children into transparent-background isolation.
                info.externalClipMaskGrid = const_cast<TileGrid*>(layerData->maskTileGrid());
                info.clipMaskLuminanceReveal = true;
            }

            result.push_back(std::move(info));
        }
    }

    return result;
}

} // namespace aether
