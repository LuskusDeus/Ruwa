// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   C O N T R O L L E R
// ==========================================================================

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMCONTROLLER_H
#define RUWA_CORE_TRANSFORM_TRANSFORMCONTROLLER_H

#include "TransformState.h"
#include "TransformApplicator.h"
#include "features/transform/TransformCommand.h"
#include "shared/tiles/TileGrid.h"
#include "features/canvas/scene/Viewport.h"
#include "features/layers/model/LayerData.h"

#include <QUuid>
#include <optional>
#include <utility>
#include <vector>
#include <chrono>
#include <cstring>
#include <cmath>
#include <limits>

namespace ruwa::core::layers {
class LayerModel;
struct LayerData;
} // namespace ruwa::core::layers

namespace aether {

class Canvas;

enum class TransformInteractionMode { Classic, Deform };

struct TransformSnapContext {
    Vector2 canvasSize {};
    bool snapToCanvasCenter = false;
    bool snapToCanvasEdges = false;
};

struct TransformAutoSnapGuideState {
    bool hasVertical = false;
    bool hasSecondVertical = false;
    bool hasHorizontal = false;
    bool hasSecondHorizontal = false;
    float verticalX = 0.0f;
    float secondVerticalX = 0.0f;
    float horizontalY = 0.0f;
    float secondHorizontalY = 0.0f;

    bool active() const
    {
        return hasVertical || hasSecondVertical || hasHorizontal || hasSecondHorizontal;
    }
};

class TransformController {
public:
    TransformController() = default;

    static constexpr float ROTATION_SNAP_STEP_RADIANS = 15.0f * 3.14159265358979323846f / 180.0f;
    static constexpr float ROTATION_SMOOTH_SPEED = 22.0f;
    static constexpr float SCALE_ANIMATION_DURATION = 0.18f;
    static constexpr float AUTO_SNAP_SCREEN_THRESHOLD_PX = 10.0f;
    /// Shift-move axis guide fade (same exponential style as rotation smoothing).
    static constexpr float MOVE_AXIS_GUIDE_OPACITY_SPEED = 28.0f;
    /// Screen-space half-width of invisible Shift-move axis trigger zones.
    static constexpr float SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX = 24.f;
    /// Ignore Shift-move axis switching while cursor speed is above this screen-space velocity.
    static constexpr float SHIFT_MOVE_AXIS_SWITCH_MAX_SPEED_SCREEN_PX_PER_SEC = 50.f;

    // ---- Mode management ----

    /// Enter transform mode for the given layer. Returns false if layer is invalid.
    /// If selectionMask is provided, transform is limited to selected pixels.
    /// If moveOnly is true, only translation is allowed (no resize/rotate handles).
    bool enter(ruwa::core::layers::LayerData* layer, const TileGrid* selectionMask = nullptr,
        bool moveOnly = false)
    {
        if (!layer || !layer->isPixelLayer())
            return false;
        TileGrid* sourceGrid = layer->pixelGrid();
        return enter(layer->id, sourceGrid, selectionMask, moveOnly);
    }

    /// Enter transform mode using an explicit source grid. Used by model-backed
    /// layers whose pixels are generated at runtime, such as text layers.
    bool enter(const QUuid& layerId, TileGrid* sourceGrid, const TileGrid* selectionMask = nullptr,
        bool moveOnly = false)
    {
        if (layerId.isNull() || !sourceGrid || sourceGrid->empty())
            return false;
        m_active = true;
        m_layerId = layerId;
        m_grid = sourceGrid;
        m_selectionMask = (selectionMask && !selectionMask->empty()) ? selectionMask : nullptr;

        // Compute content bounds
        if (m_selectionMask) {
            m_state.contentBounds = TransformState::computeContentBounds(*m_selectionMask);
        } else {
            m_state.contentBounds = TransformState::computeContentBounds(*m_grid);
        }
        if (m_state.contentBounds.width <= 0 || m_state.contentBounds.height <= 0) {
            m_active = false;
            return false;
        }

        // Set pivot to center of content
        m_state.pivot = m_state.contentBounds.center();
        m_state.reset();

        m_beforeSnapshot.clear();
        m_beforeKeys.clear();

        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartDeformMesh.reset();
        m_moveOnly = moveOnly;
        // Always start a transform session in Classic mode; the previously used
        // interaction mode is intentionally not remembered.
        m_interactionMode = TransformInteractionMode::Classic;
        if (!m_moveOnly && m_interactionMode == TransformInteractionMode::Deform) {
            m_state.initializeDeformMeshFromCurrentTransform();
        }
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
        syncAnimatedState();
        captureTransformModeEntryReference();

        return true;
    }

    /// Enter transform mode for retained/model-backed content with explicit bounds.
    /// No TileGrid snapshot is captured; the caller owns non-destructive apply/cancel.
    bool enter(const QUuid& layerId, const Rect& contentBounds, bool moveOnly = false)
    {
        return enter(layerId, contentBounds, nullptr, moveOnly);
    }

    /// Enter transform mode for retained/model-backed content with explicit bounds.
    /// Selection mask is tracked only for session semantics/preview invalidation.
    bool enter(const QUuid& layerId, const Rect& contentBounds, const TileGrid* selectionMask,
        bool moveOnly = false)
    {
        if (layerId.isNull() || contentBounds.width <= 0.0f || contentBounds.height <= 0.0f) {
            return false;
        }

        m_active = true;
        m_layerId = layerId;
        m_grid = nullptr;
        m_selectionMask = (selectionMask && !selectionMask->empty()) ? selectionMask : nullptr;
        m_beforeSnapshot.clear();
        m_beforeKeys.clear();

        m_state.contentBounds = contentBounds;
        m_state.pivot = contentBounds.center();
        m_state.reset();

        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartDeformMesh.reset();
        m_moveOnly = moveOnly;
        // Always start a transform session in Classic mode; the previously used
        // interaction mode is intentionally not remembered.
        m_interactionMode = TransformInteractionMode::Classic;
        if (!m_moveOnly && m_interactionMode == TransformInteractionMode::Deform) {
            m_state.initializeDeformMeshFromCurrentTransform();
        }
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
        syncAnimatedState();
        captureTransformModeEntryReference();

        return true;
    }

    /// World pivot (pivot + translation) at transform entry; refresh after host adjusts state (e.g.
    /// smart layer).
    void captureTransformModeEntryReference()
    {
        m_transformModeEntryReferenceWorld
            = { m_state.pivot.x + m_state.translation.x, m_state.pivot.y + m_state.translation.y };
    }

    /// Apply the transform and exit mode. Returns a snapshot for undo.
    TransformSnapshot applyAndExit(Canvas& canvas, ruwa::core::layers::LayerModel* layerModel)
    {
        TransformSnapshot snapshot;
        snapshot.layerId = m_layerId;
        captureBeforeSnapshotIfNeeded();
        snapshot.beforeTiles = std::move(m_beforeSnapshot);

        if (!m_state.isIdentity()) {
            // Apply the transform to the tile grid
            auto result = TransformApplicator::apply(*m_grid, m_state, m_selectionMask);

            snapshot.createdTiles = std::move(result.createdTiles);
            snapshot.removedTiles = std::move(result.removedTiles);

            // Capture after-snapshot (format-sized transport)
            for (const auto& [key, tile] : m_grid->tiles()) {
                auto& buf = snapshot.afterTiles[key];
                buf.resize(tileByteSize(tile.format()));
                if (tile.isSolid()) {
                    uint8_t r, g, b, a;
                    tile.solidColor(r, g, b, a);
                    fillTileSolid(buf.data(), tile.format(), r, g, b, a);
                } else {
                    std::memcpy(buf.data(), tile.pixels(), tileByteSize(tile.format()));
                }
            }

            // Also add empty data for removed tiles
            for (const auto& key : snapshot.removedTiles) {
                if (snapshot.afterTiles.find(key) == snapshot.afterTiles.end()) {
                    snapshot.afterTiles[key].resize(tileByteSize(m_grid->format()), 0);
                }
            }
        }

        m_active = false;
        m_grid = nullptr;
        m_selectionMask = nullptr;
        m_beforeSnapshot.clear();
        m_beforeKeys.clear();
        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartDeformMesh.reset();
        m_moveOnly = false;
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();

        return snapshot;
    }

    /// Cancel transform (restore original state) and exit mode
    void cancelAndExit()
    {
        if (!m_active || !m_grid) {
            m_active = false;
            m_grid = nullptr;
            m_selectionMask = nullptr;
            m_beforeSnapshot.clear();
            m_beforeKeys.clear();
            m_dragging = false;
            m_activeHandle = TransformHandle::None;
            m_classicCornerRotateFromIcon = false;
            m_activeDeformPoint = -1;
            m_ctrlFreeDragActive = false;
            m_freeSideShiftHeld = false;
            m_dragStartDeformMesh.reset();
            m_moveOnly = false;
            resetMoveAxisGuideState();
            resetMoveShiftSmoothState();
            syncAnimatedState();
            return;
        }

        // Restore original tile data
        // First remove tiles that didn't exist before
        // (In cancel case the grid hasn't been modified yet — it's only
        //  modified on apply. But for safety, do a full restore.)
        // Actually, we DON'T apply the transform during preview in v1,
        // so cancel just exits.

        m_active = false;
        m_grid = nullptr;
        m_selectionMask = nullptr;
        m_beforeSnapshot.clear();
        m_beforeKeys.clear();
        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartDeformMesh.reset();
        m_moveOnly = false;
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
        syncAnimatedState();
    }

    // ---- Mouse interaction ----

    TransformHitResult hitTestDetailed(const Vector2& worldPos, float screenZoom) const
    {
        TransformHitResult r;
        if (!m_active) {
            return r;
        }
        if (m_moveOnly) {
            if (m_state.pointInTransformedRect(worldPos)) {
                r.handle = TransformHandle::Move;
            }
            return r;
        }
        if (m_interactionMode == TransformInteractionMode::Deform && m_state.hasDeformMesh()) {
            if (m_state.hitTestDeformControlPoint(worldPos, screenZoom).has_value()) {
                r.handle = TransformHandle::DeformPoint;
            } else if (m_state.hitTestDeformRegion(worldPos)) {
                r.handle = TransformHandle::Move;
            }
            return r;
        }
        return m_state.hitTestDetailed(worldPos, screenZoom, cornersActAsRotationHandles());
    }

    TransformHandle hitTest(const Vector2& worldPos, float screenZoom) const
    {
        return hitTestDetailed(worldPos, screenZoom).handle;
    }

    /// Classic mode: vertex scales/free-drags, offset glyphs rotate; no separate top rotation
    /// handle.
    bool cornersActAsRotationHandles() const
    {
        return m_interactionMode == TransformInteractionMode::Classic;
    }

    /// Start drag at world position. Returns true if a handle was hit.
    bool mousePress(
        const Vector2& worldPos, float screenZoom, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        if (!m_active)
            return false;

        m_ctrlFreeDragActive = false;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_deformRegionActive = false;

        if (!m_moveOnly && m_interactionMode == TransformInteractionMode::Deform) {
            if (!m_state.hasDeformMesh()) {
                m_state.initializeDeformMeshFromCurrentTransform();
            }
            const auto deformPoint = m_state.hitTestDeformControlPoint(worldPos, screenZoom);
            float regionU = 0.0f, regionV = 0.0f;
            if (deformPoint.has_value()) {
                m_activeHandle = TransformHandle::DeformPoint;
                m_activeDeformPoint = *deformPoint;
            } else if (m_state.inverseBSplineSurfaceRobust(worldPos, regionU, regionV)) {
                // Dragging inside the mesh warps content under the cursor
                // (Photoshop Warp), instead of translating the whole mesh.
                m_activeHandle = TransformHandle::Move;
                m_deformRegionActive = true;
                m_state.computeDeformRegionWeights(regionU, regionV, m_deformRegionWeights);
            } else {
                return false;
            }
            m_dragging = true;
            m_dragStartWorld = worldPos;
            m_dragPrevWorld = worldPos;
            m_dragStartContentCenter = currentTransformContentCenterWorld();
            m_dragStartTranslation = { m_dragStartContentCenter.x - m_state.pivot.x,
                m_dragStartContentCenter.y - m_state.pivot.y };
            m_dragStartDeformMesh = m_state.deformMesh;
            resetMoveAxisGuideState();
            resetMoveShiftSmoothState();
            return true;
        } else if (m_moveOnly) {
            if (!m_state.pointInTransformedRect(worldPos))
                return false;
            m_activeHandle = TransformHandle::Move;
        } else {
            const TransformHitResult hit = hitTestDetailed(worldPos, screenZoom);
            m_activeHandle = hit.handle;
            m_classicCornerRotateFromIcon = hit.classicCornerRotationAffordance;
            if (m_state.hasFreeQuad() && m_classicCornerRotateFromIcon
                && isCornerHandle(m_activeHandle)) {
                m_activeHandle = TransformHandle::Rotate;
            }
            if (m_activeHandle == TransformHandle::None)
                return false;
        }

        const bool ctrlHeld = (mods & Qt::ControlModifier);
        const bool isCorner = isCornerHandle(m_activeHandle);
        const bool isSide = isSideHandle(m_activeHandle);

        // Ctrl + corner: enter free-form mode, drag corner individually
        if (ctrlHeld && isCorner) {
            if (!m_state.hasFreeQuad()) {
                m_state.freeCorners = m_state.transformedCorners();
            }
            m_ctrlFreeDragActive = true;
            m_dragging = true;
            m_dragStartWorld = worldPos;
            if ((mods & Qt::ShiftModifier) || (mods & Qt::AltModifier)) {
                m_dragStartQuad = *m_state.freeCorners;
            }
            return true;
        }

        // Ctrl + side: enter free-form mode, drag side from the start quad
        if (ctrlHeld && isSide) {
            if (!m_state.hasFreeQuad()) {
                m_state.freeCorners = m_state.transformedCorners();
            }
            m_ctrlFreeDragActive = true;
            m_freeSideShiftHeld = (mods & Qt::ShiftModifier) != 0;
            m_dragging = true;
            m_dragStartWorld = worldPos;
            m_dragPrevWorld = worldPos;
            m_dragStartQuad = *m_state.freeCorners;
            return true;
        }

        m_dragging = true;
        m_dragStartWorld = worldPos;
        m_dragStartTranslation = m_state.translation;
        m_dragStartScale = m_state.scale;
        m_dragStartRotation = m_state.rotation;
        m_dragStartContentCenter = currentTransformContentCenterWorld();
        m_targetRotation = m_state.rotation;
        m_rotationAnimationActive = false;
        m_freeRotateAnimationActive = false;
        m_freeRotateAnimationBaseQuad.reset();
        m_scaleAnimationStartScale = m_state.scale;
        m_targetScale = m_state.scale;
        m_scaleAnimationElapsed = 0.0f;
        m_scaleAnimationActive = false;
        if (m_state.hasFreeQuad() && (isCorner || isSide)) {
            m_dragStartQuad = *m_state.freeCorners;
        }
        if (m_state.hasFreeQuad()
            && (m_activeHandle == TransformHandle::Move || isSideHandle(m_activeHandle))) {
            m_dragPrevWorld = worldPos;
            if (m_activeHandle == TransformHandle::Move) {
                m_dragStartQuad = *m_state.freeCorners;
            }
        }
        if (m_state.hasFreeQuad()
            && (m_activeHandle == TransformHandle::Rotate
                || ((isCorner || isSide)
                    && ((mods & Qt::ShiftModifier) || (mods & Qt::AltModifier))))) {
            m_dragStartQuad = *m_state.freeCorners;
        }
        if (m_state.hasFreeQuad() && m_activeHandle == TransformHandle::Rotate) {
            m_freeRotateDisplayAngle = 0.0f;
            m_freeRotateTargetAngle = 0.0f;
            m_freeRotateAnimationCenter = quadCenter(*m_state.freeCorners);
        }
        // A deform-mesh state (e.g. a smart/board layer whose stored transform
        // contains a deform) can be move-dragged in Classic / move-only mode.
        // translation is ignored once a deform mesh exists, so the move offset
        // must be applied to the mesh vertices — which requires the drag-start
        // mesh, just like free-quad moves require m_dragStartQuad above.
        if (m_state.hasDeformMesh() && m_activeHandle == TransformHandle::Move) {
            m_dragStartDeformMesh = m_state.deformMesh;
        }

        if (m_activeHandle == TransformHandle::Move) {
            resetMoveShiftSmoothState();
        }

        return true;
    }

    /// Continue drag. Returns true if state changed.
    bool mouseMove(const Vector2& worldPos, float screenZoom,
        Qt::KeyboardModifiers mods = Qt::NoModifier, const Viewport* viewport = nullptr,
        const TransformSnapContext* snapContext = nullptr)
    {
        if (!m_active || !m_dragging)
            return false;

        if (m_interactionMode == TransformInteractionMode::Deform && m_state.hasDeformMesh()
            && m_activeDeformPoint >= 0) {
            const bool changed = handleDeformPointDrag(worldPos);
            if (changed) {
                (void) applyAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            }
            return changed;
        }

        if (m_interactionMode == TransformInteractionMode::Deform && m_state.hasDeformMesh()
            && m_deformRegionActive) {
            return handleDeformRegionDrag(worldPos);
        }

        if (m_activeHandle != TransformHandle::Move) {
            m_moveGuideOpacityTarget = 0.f;
            m_moveAxisGuideActive = false;
        }

        // Free-form corner drag
        if (m_ctrlFreeDragActive && m_state.hasFreeQuad() && isCornerHandle(m_activeHandle)) {
            const bool changed = handleFreeCornerDrag(worldPos, mods);
            if (changed)
                (void) applyAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            return changed;
        }

        // Free-form side drag
        if (m_ctrlFreeDragActive && m_state.hasFreeQuad() && isSideHandle(m_activeHandle)) {
            const bool changed = handleFreeSideDrag(worldPos, mods);
            if (changed)
                (void) applyAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            return changed;
        }

        // Free-form move: apply delta to all corners (translation is ignored when freeCorners is
        // set)
        if (m_state.hasFreeQuad() && m_activeHandle == TransformHandle::Move) {
            const bool r = handleFreeMoveDrag(worldPos, mods, screenZoom, viewport);
            const bool snapHandledByMoveTarget
                = r && applyMoveTargetAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            if (r && (((mods & Qt::ShiftModifier) != 0) || snapHandledByMoveTarget)) {
                m_moveTranslationAnimActive = true;
            }
            if (r && m_moveTranslationAnimActive) {
                (void) updateAnimation(1.0f / 60.0f);
            } else if (r) {
                m_moveSmoothOffset = m_moveTargetOffset;
                applyMoveSmoothOffsetToState();
            }
            return r;
        }

        // Free-form rotate: rotate quad around its center (rotation is ignored when freeCorners is
        // set)
        if (m_state.hasFreeQuad() && m_activeHandle == TransformHandle::Rotate) {
            return handleFreeRotateDrag(worldPos, mods);
        }

        if (m_state.hasFreeQuad()
            && (isCornerHandle(m_activeHandle) || isSideHandle(m_activeHandle))) {
            const bool changed = handleQuadResize(worldPos, mods);
            if (changed)
                (void) applyAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            return changed;
        }

        if (cornersActAsRotationHandles() && !m_ctrlFreeDragActive && isCornerHandle(m_activeHandle)
            && m_classicCornerRotateFromIcon) {
            return updateRotationDragFromWorld(worldPos, mods);
        }

        switch (m_activeHandle) {
        case TransformHandle::Move: {
            const Vector2 rawOff = pixelAlignedMoveOffset(
                { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y });
            const bool shift = (mods & Qt::ShiftModifier) != 0;
            if (shift) {
                beginShiftMoveReferenceIfNeeded();
                updateShiftMoveLineAxis(worldPos, rawOff, screenZoom, viewport);
                const Vector2 pivotWorldDragStart = { m_state.pivot.x + m_dragStartTranslation.x,
                    m_state.pivot.y + m_dragStartTranslation.y };
                m_moveTargetOffset = pixelAlignedMoveOffset(
                    shiftConstrainedMoveAlongFixedAxis(worldPos, m_dragStartWorld,
                        shiftMoveReferenceWorld(), pivotWorldDragStart, m_moveShiftLineUnit));
                m_moveTranslationAnimActive = true;
            } else {
                clearShiftMoveReference();
                m_moveTargetOffset = rawOff;
                m_moveTranslationAnimActive = false;
            }
            m_wasShiftHeldForMove = shift;
            updateMoveAxisGuideForShiftMove(mods);
            const bool snapHandledByMoveTarget
                = applyMoveTargetAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            if (shift || snapHandledByMoveTarget) {
                m_moveTranslationAnimActive = true;
            }
            if (m_moveTranslationAnimActive) {
                (void) updateAnimation(1.0f / 60.0f);
            } else {
                m_moveSmoothOffset = m_moveTargetOffset;
                applyMoveSmoothOffsetToState();
            }
            return true;
        }

        case TransformHandle::Rotate:
            return updateRotationDragFromWorld(worldPos, mods);

        case TransformHandle::TopLeft:
        case TransformHandle::TopRight:
        case TransformHandle::BottomLeft:
        case TransformHandle::BottomRight:
        case TransformHandle::Top:
        case TransformHandle::Bottom:
        case TransformHandle::Left:
        case TransformHandle::Right: {
            const bool changed = handleResize(worldPos, mods);
            if (changed)
                (void) applyAutoSnap(snapContext, viewport, screenZoom, mods, worldPos);
            return changed;
        }

        default:
            return false;
        }
    }

    /// End drag
    void mouseRelease()
    {
        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartDeformMesh.reset();
        m_dragStartQuad.reset();
        m_wasAltHeld = false;
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
    }

    // ---- State access ----

    bool isActive() const { return m_active; }
    bool isDragging() const { return m_dragging; }
    const TransformState& state() const { return m_state; }
    TransformState& state() { return m_state; }
    void restoreStateForUndo(const TransformState& state)
    {
        restoreStateForUndo(state, m_interactionMode);
    }

    void restoreStateForUndo(const TransformState& state, TransformInteractionMode mode)
    {
        if (!m_active) {
            return;
        }

        m_interactionMode = mode;
        m_state = state;
        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartQuad.reset();
        m_dragStartDeformMesh.reset();
        m_wasAltHeld = false;
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
        syncAnimatedState();
        captureTransformModeEntryReference();
    }
    TransformHandle activeHandle() const { return m_activeHandle; }
    int activeDeformPoint() const { return m_activeDeformPoint; }
    TransformInteractionMode interactionMode() const { return m_interactionMode; }
    void setInteractionMode(TransformInteractionMode mode)
    {
        if (m_interactionMode == mode) {
            return;
        }
        if (m_active && !m_moveOnly) {
            if (mode == TransformInteractionMode::Deform) {
                m_state.initializeDeformMeshFromCurrentTransform();
            } else if (m_state.hasDeformMesh()) {
                m_state.collapseDeformMeshToFreeQuad();
            }
        }
        m_interactionMode = mode;
        m_dragging = false;
        m_activeHandle = TransformHandle::None;
        m_classicCornerRotateFromIcon = false;
        m_activeDeformPoint = -1;
        m_ctrlFreeDragActive = false;
        m_freeSideShiftHeld = false;
        m_dragStartDeformMesh.reset();
        m_dragStartQuad.reset();
        m_wasAltHeld = false;
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
        syncAnimatedState();
        captureTransformModeEntryReference();
    }
    const QUuid& layerId() const { return m_layerId; }
    bool hasChanges() const { return !m_state.isIdentity(); }
    bool usesSelectionMask() const { return m_selectionMask != nullptr; }
    bool moveDragHasNonzeroPixelAlignedOffset(const Vector2& worldPos) const
    {
        if (!m_active || !m_dragging || m_activeHandle != TransformHandle::Move) {
            return false;
        }
        const Vector2 offset = pixelAlignedMoveOffset(
            { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y });
        return offset.x != 0.0f || offset.y != 0.0f;
    }
    bool hasPendingAnimation() const
    {
        return m_rotationAnimationActive || m_scaleAnimationActive || m_moveTranslationAnimActive
            || m_freeRotateAnimationActive || moveAxisGuideAnimating();
    }

    bool moveAxisGuideAnimating() const
    {
        if (std::abs(m_moveGuideOpacity - m_moveGuideOpacityTarget) > 0.002f)
            return true;
        if (m_moveGuideOpacity <= 0.002f)
            return false;
        return std::abs(normalizeAngleDelta(m_moveGuideTargetAngle - m_moveGuideDisplayAngle))
            > 0.0002f;
    }

    /// Shift-move axis guides (fades out after Shift release).
    bool moveAxisGuideActive() const { return m_moveGuideOpacity > 0.002f; }
    float moveAxisGuideOpacity() const { return m_moveGuideOpacity; }
    const Vector2& moveAxisGuideOriginWorld() const { return m_moveAxisGuideOrigin; }
    Vector2 moveAxisGuideAxisDirWorld() const
    {
        return { std::cos(m_moveGuideDisplayAngle), std::sin(m_moveGuideDisplayAngle) };
    }
    const TransformAutoSnapGuideState& autoSnapGuideState() const { return m_autoSnapGuide; }

    bool animateFlipHorizontal()
    {
        if (!m_active || m_state.hasFreeQuad() || m_state.hasDeformMesh())
            return false;
        Vector2 targetScale = m_state.scale;
        targetScale.x = -targetScale.x;
        clampScale(targetScale.x);
        return animateScaleTo(targetScale);
    }

    bool animateFlipVertical()
    {
        if (!m_active || m_state.hasFreeQuad() || m_state.hasDeformMesh())
            return false;
        Vector2 targetScale = m_state.scale;
        targetScale.y = -targetScale.y;
        clampScale(targetScale.y);
        return animateScaleTo(targetScale);
    }

    /// Re-sync drag-start snapshot after external state modifications
    /// (e.g. text re-rasterization changing contentBounds/pivot/scale).
    /// Must be called while a drag is in progress to prevent visual jumps
    /// on the next handleResize call.
    void resyncDragStart(const Vector2& currentWorldPos)
    {
        if (!m_dragging)
            return;
        m_dragStartWorld = currentWorldPos;
        m_dragStartTranslation = m_state.translation;
        m_dragStartScale = m_state.scale;
        m_dragStartRotation = m_state.rotation;
        m_dragStartContentCenter = currentTransformContentCenterWorld();
        m_targetRotation = m_state.rotation;
        m_rotationAnimationActive = false;
        m_freeRotateAnimationActive = false;
        m_freeRotateAnimationBaseQuad.reset();
        m_scaleAnimationStartScale = m_state.scale;
        m_targetScale = m_state.scale;
        m_scaleAnimationElapsed = 0.0f;
        m_scaleAnimationActive = false;
        m_dragStartDeformMesh = m_state.deformMesh;
        resetMoveAxisGuideState();
        resetMoveShiftSmoothState();
    }

    bool updateAnimation(float dt)
    {
        bool changed = false;
        const float dtClamped = std::max(dt, 0.0f);
        const float smoothT = 1.0f - std::exp(-ROTATION_SMOOTH_SPEED * dtClamped);
        const float opacityT = 1.0f - std::exp(-MOVE_AXIS_GUIDE_OPACITY_SPEED * dtClamped);

        changed = stepMoveShiftSmoothing(dtClamped, smoothT) || changed;
        changed = stepMoveAxisGuideAnimation(dtClamped, smoothT, opacityT) || changed;
        changed = stepFreeRotateAnimation(smoothT) || changed;

        if (m_state.hasFreeQuad() || m_state.hasDeformMesh())
            return changed;

        if (m_scaleAnimationActive) {
            m_scaleAnimationElapsed += std::max(dt, 0.0f);
            const float t
                = std::clamp(m_scaleAnimationElapsed / SCALE_ANIMATION_DURATION, 0.0f, 1.0f);
            const float eased = easeOutCubic(t);
            m_state.scale = { interpolateScaleComponent(
                                  m_scaleAnimationStartScale.x, m_targetScale.x, eased),
                interpolateScaleComponent(m_scaleAnimationStartScale.y, m_targetScale.y, eased) };
            if (t >= 1.0f) {
                m_state.scale = m_targetScale;
                m_scaleAnimationActive = false;
            }
            changed = true;
        }

        if (m_rotationAnimationActive) {
            const float delta = normalizeAngleDelta(m_targetRotation - m_state.rotation);
            if (std::abs(delta) <= 0.0001f) {
                m_state.rotation = m_targetRotation;
                m_rotationAnimationActive = false;
            } else {
                const float t = 1.0f - std::exp(-ROTATION_SMOOTH_SPEED * std::max(dt, 0.0f));
                const float nextRotation = m_state.rotation + delta * t;
                if (std::abs(normalizeAngleDelta(m_targetRotation - nextRotation)) <= 0.0005f) {
                    m_state.rotation = m_targetRotation;
                    m_rotationAnimationActive = false;
                } else {
                    m_state.rotation = nextRotation;
                }
                changed = true;
            }
        }

        return changed;
    }

    void syncAnimatedState()
    {
        m_targetRotation = m_state.rotation;
        m_rotationAnimationActive = false;
        m_freeRotateAnimationActive = false;
        m_freeRotateAnimationBaseQuad.reset();
        m_scaleAnimationStartScale = m_state.scale;
        m_targetScale = m_state.scale;
        m_scaleAnimationElapsed = 0.0f;
        m_scaleAnimationActive = false;
    }

    void finalizePendingAnimation()
    {
        if (m_scaleAnimationActive) {
            m_state.scale = m_targetScale;
            m_scaleAnimationActive = false;
        }
        if (m_rotationAnimationActive) {
            m_state.rotation = m_targetRotation;
            m_rotationAnimationActive = false;
        }
        if (m_freeRotateAnimationActive) {
            m_freeRotateDisplayAngle = m_freeRotateTargetAngle;
            applyFreeRotateAnimationAngle(m_freeRotateDisplayAngle);
            m_freeRotateAnimationActive = false;
            m_freeRotateAnimationBaseQuad.reset();
        }
        if (m_moveTranslationAnimActive) {
            m_moveSmoothOffset = m_moveTargetOffset;
            applyMoveSmoothOffsetToState();
            m_moveTranslationAnimActive = false;
        }
        m_moveGuideOpacity = m_moveGuideOpacityTarget;
        m_moveGuideDisplayAngle = m_moveGuideTargetAngle;
    }

    // ---- GPU apply support: extract snapshots without applying CPU transform ----

    /// Move before-snapshot out (for GPU apply path)
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> takeBeforeSnapshot()
    {
        captureBeforeSnapshotIfNeeded();
        return std::move(m_beforeSnapshot);
    }

    /// Move before-keys out (for GPU apply path)
    std::unordered_set<TileKey, TileKeyHash> takeBeforeKeys()
    {
        captureBeforeSnapshotIfNeeded();
        return std::move(m_beforeKeys);
    }

private:
    enum class ShiftMoveLineSlot { None, Horizontal, Vertical };

    void captureBeforeSnapshotIfNeeded()
    {
        if (!m_grid || !m_beforeSnapshot.empty() || !m_beforeKeys.empty()) {
            return;
        }

        m_beforeSnapshot.reserve(m_grid->tiles().size());
        m_beforeKeys.reserve(m_grid->tiles().size());
        for (const auto& [key, tile] : m_grid->tiles()) {
            auto& buf = m_beforeSnapshot[key];
            buf.resize(tileByteSize(tile.format()));
            if (tile.isSolid()) {
                uint8_t r, g, b, a;
                tile.solidColor(r, g, b, a);
                fillTileSolid(buf.data(), tile.format(), r, g, b, a);
            } else {
                std::memcpy(buf.data(), tile.pixels(), tileByteSize(tile.format()));
            }
            m_beforeKeys.insert(key);
        }
    }

    struct AutoSnapCandidate {
        bool hasX = false;
        bool hasY = false;
        Vector2 offset {};
        float guideX = 0.0f;
        float guideY = 0.0f;
        float screenDistanceXSq = std::numeric_limits<float>::max();
        float screenDistanceYSq = std::numeric_limits<float>::max();

        bool valid() const { return hasX || hasY; }
    };

    struct AutoSnapTargetCandidate {
        bool valid = false;
        float target = 0.0f;
        float screenDistanceSq = std::numeric_limits<float>::max();
    };

    struct ResizeAutoSnapCandidate {
        bool hasX = false;
        bool hasY = false;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float screenDistanceXSq = std::numeric_limits<float>::max();
        float screenDistanceYSq = std::numeric_limits<float>::max();

        bool valid() const { return hasX || hasY; }
    };

    std::array<Vector2, 5> transformSnapPoints() const
    {
        if (m_state.hasDeformMesh()) {
            const Rect aabb = m_state.transformedAABB();
            const float l = aabb.left();
            const float r = aabb.right();
            const float t = aabb.top();
            const float b = aabb.bottom();
            return { { { l, t }, { r, t }, { r, b }, { l, b }, aabb.center() } };
        }

        const auto corners = m_state.transformedCorners();
        return { { { corners[0].x, corners[0].y }, { corners[1].x, corners[1].y },
            { corners[2].x, corners[2].y }, { corners[3].x, corners[3].y }, quadCenter(corners) } };
    }

    static Vector2 screenPointForWorld(
        const Vector2& worldPos, const Viewport* viewport, float screenZoom)
    {
        if (viewport) {
            return viewport->worldToScreen(worldPos);
        }
        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        return { worldPos.x * z, worldPos.y * z };
    }

    static float screenDistanceSq(
        const Vector2& a, const Vector2& b, const Viewport* viewport, float screenZoom)
    {
        const Vector2 as = screenPointForWorld(a, viewport, screenZoom);
        const Vector2 bs = screenPointForWorld(b, viewport, screenZoom);
        const float dx = as.x - bs.x;
        const float dy = as.y - bs.y;
        return dx * dx + dy * dy;
    }

    static void considerAutoSnapX(const Vector2& source, float targetX, const Viewport* viewport,
        float screenZoom, float thresholdSq, AutoSnapCandidate& best)
    {
        const float distSq = screenDistanceSq(source, { targetX, source.y }, viewport, screenZoom);
        if (distSq <= thresholdSq && distSq < best.screenDistanceXSq) {
            best.hasX = true;
            best.offset.x = targetX - source.x;
            best.guideX = targetX;
            best.screenDistanceXSq = distSq;
        }
    }

    static void considerAutoSnapY(const Vector2& source, float targetY, const Viewport* viewport,
        float screenZoom, float thresholdSq, AutoSnapCandidate& best)
    {
        const float distSq = screenDistanceSq(source, { source.x, targetY }, viewport, screenZoom);
        if (distSq <= thresholdSq && distSq < best.screenDistanceYSq) {
            best.hasY = true;
            best.offset.y = targetY - source.y;
            best.guideY = targetY;
            best.screenDistanceYSq = distSq;
        }
    }

    static AutoSnapCandidate findAutoSnapOffsetForPoint(const Vector2& point,
        const TransformSnapContext& snap, const Viewport* viewport, float screenZoom,
        bool allowCanvasCenterX = true, bool allowCanvasCenterY = true)
    {
        AutoSnapCandidate best;
        const float w = snap.canvasSize.x;
        const float h = snap.canvasSize.y;
        if (w <= 0.0f || h <= 0.0f) {
            return best;
        }

        const float thresholdSq = AUTO_SNAP_SCREEN_THRESHOLD_PX * AUTO_SNAP_SCREEN_THRESHOLD_PX;

        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        const float worldMargin = AUTO_SNAP_SCREEN_THRESHOLD_PX / z;

        if (snap.snapToCanvasCenter) {
            if (allowCanvasCenterX && point.y >= -worldMargin && point.y <= h + worldMargin) {
                considerAutoSnapX(point, w * 0.5f, viewport, screenZoom, thresholdSq, best);
            }
            if (allowCanvasCenterY && point.x >= -worldMargin && point.x <= w + worldMargin) {
                considerAutoSnapY(point, h * 0.5f, viewport, screenZoom, thresholdSq, best);
            }
        }

        if (snap.snapToCanvasEdges) {
            if (point.y >= -worldMargin && point.y <= h + worldMargin) {
                considerAutoSnapX(point, 0.0f, viewport, screenZoom, thresholdSq, best);
                considerAutoSnapX(point, w, viewport, screenZoom, thresholdSq, best);
            }
            if (point.x >= -worldMargin && point.x <= w + worldMargin) {
                considerAutoSnapY(point, 0.0f, viewport, screenZoom, thresholdSq, best);
                considerAutoSnapY(point, h, viewport, screenZoom, thresholdSq, best);
            }
        }

        return best;
    }

    static void considerAutoSnapTargetX(const Vector2& source, float targetX,
        const Viewport* viewport, float screenZoom, float thresholdSq,
        AutoSnapTargetCandidate& best)
    {
        const float distSq = screenDistanceSq(source, { targetX, source.y }, viewport, screenZoom);
        if (distSq <= thresholdSq && distSq < best.screenDistanceSq) {
            best.valid = true;
            best.target = targetX;
            best.screenDistanceSq = distSq;
        }
    }

    static void considerAutoSnapTargetY(const Vector2& source, float targetY,
        const Viewport* viewport, float screenZoom, float thresholdSq,
        AutoSnapTargetCandidate& best)
    {
        const float distSq = screenDistanceSq(source, { source.x, targetY }, viewport, screenZoom);
        if (distSq <= thresholdSq && distSq < best.screenDistanceSq) {
            best.valid = true;
            best.target = targetY;
            best.screenDistanceSq = distSq;
        }
    }

    static AutoSnapTargetCandidate findAutoSnapTargetX(const Vector2& point,
        const TransformSnapContext& snap, const Viewport* viewport, float screenZoom)
    {
        AutoSnapTargetCandidate best;
        const float w = snap.canvasSize.x;
        const float h = snap.canvasSize.y;
        if (w <= 0.0f || h <= 0.0f) {
            return best;
        }

        const float thresholdSq = AUTO_SNAP_SCREEN_THRESHOLD_PX * AUTO_SNAP_SCREEN_THRESHOLD_PX;
        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        const float worldMargin = AUTO_SNAP_SCREEN_THRESHOLD_PX / z;
        if (point.y < -worldMargin || point.y > h + worldMargin) {
            return best;
        }

        if (snap.snapToCanvasCenter) {
            considerAutoSnapTargetX(point, w * 0.5f, viewport, screenZoom, thresholdSq, best);
        }
        if (snap.snapToCanvasEdges) {
            considerAutoSnapTargetX(point, 0.0f, viewport, screenZoom, thresholdSq, best);
            considerAutoSnapTargetX(point, w, viewport, screenZoom, thresholdSq, best);
        }
        return best;
    }

    static AutoSnapTargetCandidate findAutoSnapTargetY(const Vector2& point,
        const TransformSnapContext& snap, const Viewport* viewport, float screenZoom)
    {
        AutoSnapTargetCandidate best;
        const float w = snap.canvasSize.x;
        const float h = snap.canvasSize.y;
        if (w <= 0.0f || h <= 0.0f) {
            return best;
        }

        const float thresholdSq = AUTO_SNAP_SCREEN_THRESHOLD_PX * AUTO_SNAP_SCREEN_THRESHOLD_PX;
        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        const float worldMargin = AUTO_SNAP_SCREEN_THRESHOLD_PX / z;
        if (point.x < -worldMargin || point.x > w + worldMargin) {
            return best;
        }

        if (snap.snapToCanvasCenter) {
            considerAutoSnapTargetY(point, h * 0.5f, viewport, screenZoom, thresholdSq, best);
        }
        if (snap.snapToCanvasEdges) {
            considerAutoSnapTargetY(point, 0.0f, viewport, screenZoom, thresholdSq, best);
            considerAutoSnapTargetY(point, h, viewport, screenZoom, thresholdSq, best);
        }
        return best;
    }

    static bool verticalGuideTouchesShiftTriggerCenter(
        float guideX, const Vector2& centerWorld, const Viewport* viewport, float screenZoom)
    {
        if (viewport) {
            const Vector2 centerScreen = viewport->worldToScreen(centerWorld);
            const Vector2 guideOriginScreen = viewport->worldToScreen({ guideX, centerWorld.y });
            const Vector2 guideAxisScreen
                = viewport->worldToScreen({ guideX, centerWorld.y + 1.0f });
            return screenDistanceToAxis(centerScreen, guideOriginScreen, guideAxisScreen)
                <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
        }

        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.f;
        return std::abs((guideX - centerWorld.x) * z) <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
    }

    static bool horizontalGuideTouchesShiftTriggerCenter(
        float guideY, const Vector2& centerWorld, const Viewport* viewport, float screenZoom)
    {
        if (viewport) {
            const Vector2 centerScreen = viewport->worldToScreen(centerWorld);
            const Vector2 guideOriginScreen = viewport->worldToScreen({ centerWorld.x, guideY });
            const Vector2 guideAxisScreen
                = viewport->worldToScreen({ centerWorld.x + 1.0f, guideY });
            return screenDistanceToAxis(centerScreen, guideOriginScreen, guideAxisScreen)
                <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
        }

        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.f;
        return std::abs((guideY - centerWorld.y) * z) <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
    }

    AutoSnapCandidate filterMoveAutoSnapCandidateForShiftLock(
        const AutoSnapCandidate& candidate, const Viewport* viewport, float screenZoom) const
    {
        if (m_activeHandle != TransformHandle::Move || !m_moveShiftReferenceValid
            || m_moveShiftLineSlot == ShiftMoveLineSlot::None) {
            return candidate;
        }

        AutoSnapCandidate filtered = candidate;
        const Vector2& centerWorld = shiftMoveReferenceWorld();

        if (filtered.hasX) {
            const bool suppressVerticalGuide = m_moveShiftLineSlot == ShiftMoveLineSlot::Vertical
                || verticalGuideTouchesShiftTriggerCenter(
                    candidate.guideX, centerWorld, viewport, screenZoom);
            if (suppressVerticalGuide) {
                filtered.hasX = false;
                filtered.offset.x = 0.0f;
                filtered.screenDistanceXSq = std::numeric_limits<float>::max();
            }
        }

        if (filtered.hasY) {
            const bool suppressHorizontalGuide
                = m_moveShiftLineSlot == ShiftMoveLineSlot::Horizontal
                || horizontalGuideTouchesShiftTriggerCenter(
                    candidate.guideY, centerWorld, viewport, screenZoom);
            if (suppressHorizontalGuide) {
                filtered.hasY = false;
                filtered.offset.y = 0.0f;
                filtered.screenDistanceYSq = std::numeric_limits<float>::max();
            }
        }

        return filtered;
    }

    void pruneAutoSnapGuideStateForShiftLock(const Viewport* viewport, float screenZoom)
    {
        if (m_activeHandle != TransformHandle::Move || !m_moveShiftReferenceValid
            || m_moveShiftLineSlot == ShiftMoveLineSlot::None) {
            return;
        }

        const Vector2& centerWorld = shiftMoveReferenceWorld();
        auto suppressVertical = [&](float guideX) {
            return m_moveShiftLineSlot == ShiftMoveLineSlot::Vertical
                || verticalGuideTouchesShiftTriggerCenter(
                    guideX, centerWorld, viewport, screenZoom);
        };
        auto suppressHorizontal = [&](float guideY) {
            return m_moveShiftLineSlot == ShiftMoveLineSlot::Horizontal
                || horizontalGuideTouchesShiftTriggerCenter(
                    guideY, centerWorld, viewport, screenZoom);
        };

        if (m_autoSnapGuide.hasVertical && suppressVertical(m_autoSnapGuide.verticalX)) {
            m_autoSnapGuide.hasVertical = false;
        }
        if (m_autoSnapGuide.hasSecondVertical
            && suppressVertical(m_autoSnapGuide.secondVerticalX)) {
            m_autoSnapGuide.hasSecondVertical = false;
        }
        if (m_autoSnapGuide.hasHorizontal && suppressHorizontal(m_autoSnapGuide.horizontalY)) {
            m_autoSnapGuide.hasHorizontal = false;
        }
        if (m_autoSnapGuide.hasSecondHorizontal
            && suppressHorizontal(m_autoSnapGuide.secondHorizontalY)) {
            m_autoSnapGuide.hasSecondHorizontal = false;
        }
    }

    AutoSnapCandidate findAutoSnapOffset(
        const TransformSnapContext& snap, const Viewport* viewport, float screenZoom) const
    {
        AutoSnapCandidate best;
        const auto points = transformSnapPoints();
        float minX = points[0].x;
        float maxX = points[0].x;
        float minY = points[0].y;
        float maxY = points[0].y;
        for (const Vector2& point : points) {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }

        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        const float sizeTolerance = std::max(0.001f, 0.5f / z);
        const bool contentMatchesCanvasWidth
            = std::abs((maxX - minX) - snap.canvasSize.x) <= sizeTolerance;
        const bool contentMatchesCanvasHeight
            = std::abs((maxY - minY) - snap.canvasSize.y) <= sizeTolerance;
        const bool suppressCenterPointX = snap.snapToCanvasEdges && contentMatchesCanvasWidth;
        const bool suppressCenterPointY = snap.snapToCanvasEdges && contentMatchesCanvasHeight;

        for (size_t i = 0; i < points.size(); ++i) {
            const Vector2& point = points[i];
            const bool centerPoint = (i == points.size() - 1);
            const AutoSnapCandidate candidate
                = findAutoSnapOffsetForPoint(point, snap, viewport, screenZoom,
                    !centerPoint || !suppressCenterPointX, !centerPoint || !suppressCenterPointY);
            if (candidate.hasX && candidate.screenDistanceXSq < best.screenDistanceXSq) {
                best.hasX = true;
                best.offset.x = candidate.offset.x;
                best.guideX = candidate.guideX;
                best.screenDistanceXSq = candidate.screenDistanceXSq;
            }
            if (candidate.hasY && candidate.screenDistanceYSq < best.screenDistanceYSq) {
                best.hasY = true;
                best.offset.y = candidate.offset.y;
                best.guideY = candidate.guideY;
                best.screenDistanceYSq = candidate.screenDistanceYSq;
            }
        }
        return best;
    }

    void clearAutoSnapGuide() { m_autoSnapGuide = {}; }

    void setAutoSnapGuide(const AutoSnapCandidate& candidate)
    {
        m_autoSnapGuide.hasVertical = candidate.hasX;
        m_autoSnapGuide.verticalX = candidate.guideX;
        m_autoSnapGuide.hasSecondVertical = false;
        m_autoSnapGuide.secondVerticalX = 0.0f;
        m_autoSnapGuide.hasHorizontal = candidate.hasY;
        m_autoSnapGuide.horizontalY = candidate.guideY;
        m_autoSnapGuide.hasSecondHorizontal = false;
        m_autoSnapGuide.secondHorizontalY = 0.0f;
    }

    void setMoveAutoSnapGuide(
        const AutoSnapCandidate& candidate, const TransformSnapContext& snap, float screenZoom)
    {
        setAutoSnapGuide(candidate);
        if (!snap.snapToCanvasEdges) {
            return;
        }

        const float w = snap.canvasSize.x;
        const float h = snap.canvasSize.y;
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }

        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        const float edgeTolerance = std::max(0.001f, 0.5f / z);
        const float rangeMargin = AUTO_SNAP_SCREEN_THRESHOLD_PX / z;
        const auto points = transformSnapPoints();

        auto guideMatches = [edgeTolerance](float guide, float edge) {
            return std::abs(guide - edge) <= edgeTolerance;
        };

        if (candidate.hasX) {
            const bool primaryLeft = guideMatches(candidate.guideX, 0.0f);
            const bool primaryRight = guideMatches(candidate.guideX, w);
            if (primaryLeft != primaryRight) {
                const float opposite = primaryLeft ? w : 0.0f;
                for (const Vector2& point : points) {
                    const float snappedX = point.x + candidate.offset.x;
                    const float snappedY = point.y + (candidate.hasY ? candidate.offset.y : 0.0f);
                    if (std::abs(snappedX - opposite) <= edgeTolerance && snappedY >= -rangeMargin
                        && snappedY <= h + rangeMargin) {
                        m_autoSnapGuide.hasSecondVertical = true;
                        m_autoSnapGuide.secondVerticalX = opposite;
                        break;
                    }
                }
            }
        }

        if (candidate.hasY) {
            const bool primaryTop = guideMatches(candidate.guideY, 0.0f);
            const bool primaryBottom = guideMatches(candidate.guideY, h);
            if (primaryTop != primaryBottom) {
                const float opposite = primaryTop ? h : 0.0f;
                for (const Vector2& point : points) {
                    const float snappedX = point.x + (candidate.hasX ? candidate.offset.x : 0.0f);
                    const float snappedY = point.y + candidate.offset.y;
                    if (std::abs(snappedY - opposite) <= edgeTolerance && snappedX >= -rangeMargin
                        && snappedX <= w + rangeMargin) {
                        m_autoSnapGuide.hasSecondHorizontal = true;
                        m_autoSnapGuide.secondHorizontalY = opposite;
                        break;
                    }
                }
            }
        }
    }

    void setResizeAutoSnapGuide(const ResizeAutoSnapCandidate& candidate,
        const TransformSnapContext& snap, const Viewport* viewport, float screenZoom,
        const Vector2& oppositeWorld)
    {
        m_autoSnapGuide = {};
        if (candidate.hasX) {
            m_autoSnapGuide.hasVertical = true;
            m_autoSnapGuide.verticalX = candidate.targetX;

            const AutoSnapTargetCandidate opposite
                = findAutoSnapTargetX(oppositeWorld, snap, viewport, screenZoom);
            if (opposite.valid && std::abs(opposite.target - candidate.targetX) > 0.001f) {
                m_autoSnapGuide.hasSecondVertical = true;
                m_autoSnapGuide.secondVerticalX = opposite.target;
            }
        }
        if (candidate.hasY) {
            m_autoSnapGuide.hasHorizontal = true;
            m_autoSnapGuide.horizontalY = candidate.targetY;

            const AutoSnapTargetCandidate opposite
                = findAutoSnapTargetY(oppositeWorld, snap, viewport, screenZoom);
            if (opposite.valid && std::abs(opposite.target - candidate.targetY) > 0.001f) {
                m_autoSnapGuide.hasSecondHorizontal = true;
                m_autoSnapGuide.secondHorizontalY = opposite.target;
            }
        }
    }

    void translateCurrentTransform(const Vector2& offset)
    {
        if (std::abs(offset.x) <= 1.0e-6f && std::abs(offset.y) <= 1.0e-6f) {
            return;
        }

        if (m_state.hasDeformMesh()) {
            m_state.translateDeformMesh(offset);
            return;
        }

        if (m_state.hasFreeQuad()) {
            auto q = *m_state.freeCorners;
            for (auto& corner : q) {
                corner.x += offset.x;
                corner.y += offset.y;
            }
            m_state.freeCorners = q;
            return;
        }

        m_state.translation
            = { m_state.translation.x + offset.x, m_state.translation.y + offset.y };
    }

    void keepMoveSmoothingAlignedWithSnap(const Vector2& offset)
    {
        if (m_activeHandle != TransformHandle::Move || !m_moveTranslationAnimActive) {
            return;
        }

        m_moveSmoothOffset.x += offset.x;
        m_moveSmoothOffset.y += offset.y;
        m_moveTargetOffset.x += offset.x;
        m_moveTargetOffset.y += offset.y;
        m_moveSmoothOffset = pixelAlignedMoveOffset(m_moveSmoothOffset);
        m_moveTargetOffset = pixelAlignedMoveOffset(m_moveTargetOffset);
    }

    bool applyMoveTargetAutoSnap(const TransformSnapContext* snapContext, const Viewport* viewport,
        float screenZoom, Qt::KeyboardModifiers mods, const Vector2& cursorWorldPos)
    {
        if (!snapContext || (!snapContext->snapToCanvasCenter && !snapContext->snapToCanvasEdges)) {
            clearAutoSnapGuide();
            return false;
        }
        if (m_activeHandle == TransformHandle::Move && (mods & Qt::AltModifier)) {
            clearAutoSnapGuide();
            return false;
        }
        const bool allowNewSnapConnections
            = autoSnapSpeedAllowsNewConnections(cursorWorldPos, viewport, screenZoom);

        const TransformState savedState = m_state;
        const Vector2 savedSmoothOffset = m_moveSmoothOffset;

        m_moveSmoothOffset = m_moveTargetOffset;
        applyMoveSmoothOffsetToState();
        AutoSnapCandidate candidate = findAutoSnapOffset(*snapContext, viewport, screenZoom);
        candidate = filterMoveAutoSnapCandidateForShiftLock(candidate, viewport, screenZoom);
        candidate = filterAutoSnapCandidateForSpeed(candidate, allowNewSnapConnections, screenZoom);
        if (candidate.valid()) {
            const TransformAutoSnapGuideState previousGuide = m_autoSnapGuide;
            setMoveAutoSnapGuide(candidate, *snapContext, screenZoom);
            pruneAutoSnapGuideStateForSpeed(previousGuide, allowNewSnapConnections, screenZoom);
            pruneAutoSnapGuideStateForShiftLock(viewport, screenZoom);
        }

        m_state = savedState;
        m_moveSmoothOffset = savedSmoothOffset;

        if (!candidate.valid()) {
            clearAutoSnapGuide();
            return false;
        }

        m_moveTargetOffset = pixelAlignedMoveOffset({ m_moveTargetOffset.x + candidate.offset.x,
            m_moveTargetOffset.y + candidate.offset.y });
        return true;
    }

    bool snapActiveFreeCornerOnly(const TransformSnapContext& snap, const Viewport* viewport,
        float screenZoom, bool allowNewSnapConnections)
    {
        if (!m_state.hasFreeQuad() || !m_ctrlFreeDragActive || !isCornerHandle(m_activeHandle)) {
            return false;
        }

        const int idx = cornerHandleToIndex(m_activeHandle);
        if (idx < 0) {
            return false;
        }

        auto q = *m_state.freeCorners;
        AutoSnapCandidate candidate
            = findAutoSnapOffsetForPoint(q[static_cast<size_t>(idx)], snap, viewport, screenZoom);
        candidate = filterAutoSnapCandidateForSpeed(candidate, allowNewSnapConnections, screenZoom);
        if (!candidate.valid()) {
            return false;
        }

        setAutoSnapGuide(candidate);
        q[static_cast<size_t>(idx)].x += candidate.offset.x;
        q[static_cast<size_t>(idx)].y += candidate.offset.y;
        m_state.freeCorners = q;
        return true;
    }

    AutoSnapCandidate findFreeSideAutoSnapOffset(
        const TransformSnapContext& snap, const Viewport* viewport, float screenZoom) const
    {
        AutoSnapCandidate best;
        if (!m_state.hasFreeQuad() || !isSideHandle(m_activeHandle)) {
            return best;
        }

        const auto [i, j] = sideHandleToCornerIndices(m_activeHandle);
        if (i < 0 || j < 0) {
            return best;
        }

        const auto& q = *m_state.freeCorners;
        const std::array<Vector2, 3> points
            = { { q[static_cast<size_t>(i)], q[static_cast<size_t>(j)],
                { (q[static_cast<size_t>(i)].x + q[static_cast<size_t>(j)].x) * 0.5f,
                    (q[static_cast<size_t>(i)].y + q[static_cast<size_t>(j)].y) * 0.5f } } };

        for (const Vector2& point : points) {
            const AutoSnapCandidate candidate
                = findAutoSnapOffsetForPoint(point, snap, viewport, screenZoom);
            if (candidate.hasX && candidate.screenDistanceXSq < best.screenDistanceXSq) {
                best.hasX = true;
                best.offset.x = candidate.offset.x;
                best.guideX = candidate.guideX;
                best.screenDistanceXSq = candidate.screenDistanceXSq;
            }
            if (candidate.hasY && candidate.screenDistanceYSq < best.screenDistanceYSq) {
                best.hasY = true;
                best.offset.y = candidate.offset.y;
                best.guideY = candidate.guideY;
                best.screenDistanceYSq = candidate.screenDistanceYSq;
            }
        }

        return best;
    }

    ResizeAutoSnapCandidate findResizeAutoSnapCandidate(const TransformSnapContext& snap,
        const Viewport* viewport, float screenZoom, Qt::KeyboardModifiers mods) const
    {
        ResizeAutoSnapCandidate candidate;
        if (!isCornerHandle(m_activeHandle) && !isSideHandle(m_activeHandle)) {
            return candidate;
        }

        const Vector2 activePoint = m_state.handlePosition(m_activeHandle);
        const bool allowX = isCornerHandle(m_activeHandle)
            || m_activeHandle == TransformHandle::Left || m_activeHandle == TransformHandle::Right;
        const bool allowY = isCornerHandle(m_activeHandle) || m_activeHandle == TransformHandle::Top
            || m_activeHandle == TransformHandle::Bottom;

        if (allowX) {
            const AutoSnapTargetCandidate x
                = findAutoSnapTargetX(activePoint, snap, viewport, screenZoom);
            if (x.valid) {
                candidate.hasX = true;
                candidate.targetX = x.target;
                candidate.screenDistanceXSq = x.screenDistanceSq;
            }
        }
        if (allowY) {
            const AutoSnapTargetCandidate y
                = findAutoSnapTargetY(activePoint, snap, viewport, screenZoom);
            if (y.valid) {
                candidate.hasY = true;
                candidate.targetY = y.target;
                candidate.screenDistanceYSq = y.screenDistanceSq;
            }
        }

        const bool freeResize = (mods & Qt::ShiftModifier);
        if (!freeResize && isCornerHandle(m_activeHandle) && candidate.hasX && candidate.hasY) {
            if (candidate.screenDistanceXSq <= candidate.screenDistanceYSq) {
                candidate.hasY = false;
            } else {
                candidate.hasX = false;
            }
        }
        return candidate;
    }

    bool applyClassicResizeAutoSnap(const TransformSnapContext& snap, const Viewport* viewport,
        float screenZoom, Qt::KeyboardModifiers mods, bool allowNewSnapConnections)
    {
        if (m_state.hasFreeQuad() || m_state.hasDeformMesh() || m_ctrlFreeDragActive
            || (!isCornerHandle(m_activeHandle) && !isSideHandle(m_activeHandle))) {
            return false;
        }

        ResizeAutoSnapCandidate candidate
            = findResizeAutoSnapCandidate(snap, viewport, screenZoom, mods);
        candidate
            = filterResizeAutoSnapCandidateForSpeed(candidate, allowNewSnapConnections, screenZoom);
        if (!candidate.valid()) {
            return false;
        }

        const bool fromCenter = (mods & Qt::AltModifier);
        const bool freeResize = (mods & Qt::ShiftModifier);
        const Vector2 anchorContent = fromCenter ? m_state.pivot : oppositeAnchor(m_activeHandle);
        const Vector2 handleContent = handleContentPos(m_activeHandle);
        const Vector2 cd { handleContent.x - anchorContent.x, handleContent.y - anchorContent.y };
        const Vector2 anchorWorld = contentToWorld(
            anchorContent, m_dragStartScale, m_dragStartRotation, m_dragStartTranslation);

        float scaleX = m_state.scale.x;
        float scaleY = m_state.scale.y;
        constexpr float eps = 0.001f;
        const float c = std::cos(m_dragStartRotation);
        const float s = std::sin(m_dragStartRotation);
        bool solved = false;

        if (freeResize) {
            if (candidate.hasX && candidate.hasY) {
                const float a = c * cd.x;
                const float b = -s * cd.y;
                const float d = s * cd.x;
                const float e = c * cd.y;
                const float det = a * e - b * d;
                if (std::abs(det) > eps) {
                    const float tx = candidate.targetX - anchorWorld.x;
                    const float ty = candidate.targetY - anchorWorld.y;
                    scaleX = (tx * e - b * ty) / det;
                    scaleY = (a * ty - tx * d) / det;
                    solved = true;
                }
            }

            auto solveSingleAxis = [&](bool xAxis, float target) {
                const float coeffX = (xAxis ? c : s) * cd.x;
                const float coeffY = (xAxis ? -s : c) * cd.y;
                const float fixedByX
                    = xAxis ? (anchorWorld.x + coeffY * scaleY) : (anchorWorld.y + coeffY * scaleY);
                const float fixedByY
                    = xAxis ? (anchorWorld.x + coeffX * scaleX) : (anchorWorld.y + coeffX * scaleX);

                if (std::abs(coeffX) >= std::abs(coeffY) && std::abs(coeffX) > eps) {
                    scaleX = (target - fixedByX) / coeffX;
                    return true;
                }
                if (std::abs(coeffY) > eps) {
                    scaleY = (target - fixedByY) / coeffY;
                    return true;
                }
                return false;
            };

            if (!solved) {
                if (candidate.hasX)
                    solved = solveSingleAxis(true, candidate.targetX) || solved;
                if (candidate.hasY)
                    solved = solveSingleAxis(false, candidate.targetY) || solved;
            }
        } else {
            const float baseX = c * (m_dragStartScale.x * cd.x) - s * (m_dragStartScale.y * cd.y);
            const float baseY = s * (m_dragStartScale.x * cd.x) + c * (m_dragStartScale.y * cd.y);
            float ratio = 1.0f;
            if (candidate.hasX && std::abs(baseX) > eps) {
                ratio = (candidate.targetX - anchorWorld.x) / baseX;
                solved = true;
            } else if (candidate.hasY && std::abs(baseY) > eps) {
                ratio = (candidate.targetY - anchorWorld.y) / baseY;
                solved = true;
            }
            if (solved) {
                scaleX = m_dragStartScale.x * ratio;
                scaleY = m_dragStartScale.y * ratio;
            }
        }

        if (!solved) {
            return false;
        }

        clampScale(scaleX);
        clampScale(scaleY);
        m_state.scale = { scaleX, scaleY };
        m_state.rotation = m_dragStartRotation;
        if (fromCenter) {
            m_state.translation = m_dragStartTranslation;
        } else {
            const Vector2 anchorWorldNew = contentToWorld(
                anchorContent, m_state.scale, m_dragStartRotation, m_dragStartTranslation);
            m_state.translation = { m_dragStartTranslation.x + (anchorWorld.x - anchorWorldNew.x),
                m_dragStartTranslation.y + (anchorWorld.y - anchorWorldNew.y) };
        }

        const Vector2 oppositeWorld = contentToWorld(
            oppositeAnchor(m_activeHandle), m_state.scale, m_state.rotation, m_state.translation);
        const TransformAutoSnapGuideState previousGuide = m_autoSnapGuide;
        setResizeAutoSnapGuide(candidate, snap, viewport, screenZoom, oppositeWorld);
        pruneAutoSnapGuideStateForSpeed(previousGuide, allowNewSnapConnections, screenZoom);
        return true;
    }

    bool solveSingleScaleForGuide(const Vector2& anchor, const Vector2& fixedOffset,
        const Vector2& scaleVector, bool snapX, float target, float* outScale) const
    {
        if (!outScale) {
            return false;
        }
        constexpr float eps = 0.001f;
        const float coeff = snapX ? scaleVector.x : scaleVector.y;
        if (std::abs(coeff) <= eps) {
            return false;
        }
        const float base = snapX ? (anchor.x + fixedOffset.x) : (anchor.y + fixedOffset.y);
        *outScale = (target - base) / coeff;
        return true;
    }

    TransformHandle oppositeSideHandle(TransformHandle handle) const
    {
        switch (handle) {
        case TransformHandle::Top:
            return TransformHandle::Bottom;
        case TransformHandle::Right:
            return TransformHandle::Left;
        case TransformHandle::Bottom:
            return TransformHandle::Top;
        case TransformHandle::Left:
            return TransformHandle::Right;
        default:
            return TransformHandle::None;
        }
    }

    bool applyFreeQuadSideResizeAutoSnap(const TransformSnapContext& snap, const Viewport* viewport,
        float screenZoom, Qt::KeyboardModifiers mods, bool allowNewSnapConnections)
    {
        if (!m_state.hasFreeQuad() || !m_dragStartQuad || m_ctrlFreeDragActive
            || !isSideHandle(m_activeHandle)) {
            return false;
        }

        ResizeAutoSnapCandidate candidate
            = findResizeAutoSnapCandidate(snap, viewport, screenZoom, mods);
        candidate
            = filterResizeAutoSnapCandidateForSpeed(candidate, allowNewSnapConnections, screenZoom);
        if (!candidate.valid()) {
            return false;
        }

        const auto& q0 = *m_dragStartQuad;
        const bool fromCenter = (mods & Qt::AltModifier);
        const bool freeResize = (mods & Qt::ShiftModifier);
        const bool horizontalEdge = isHorizontalEdge(m_activeHandle);
        const bool snapX = !horizontalEdge;
        const float target = snapX ? candidate.targetX : candidate.targetY;
        const auto [i, j] = sideHandleToCornerIndices(m_activeHandle);
        if (i < 0 || j < 0) {
            return false;
        }

        const int oppI = (i + 2) % 4;
        const int oppJ = (j + 2) % 4;
        const Vector2 draggedMid = { (q0[i].x + q0[j].x) * 0.5f, (q0[i].y + q0[j].y) * 0.5f };
        const Vector2 anchor = fromCenter
            ? quadCenter(q0)
            : Vector2 { (q0[oppI].x + q0[oppJ].x) * 0.5f, (q0[oppI].y + q0[oppJ].y) * 0.5f };
        const auto [axisX, axisY] = quadLocalAxes(q0);

        float scale = 1.0f;
        bool solved = false;

        if (freeResize && !fromCenter) {
            const Vector2 sideAxis = { q0[j].x - q0[i].x, q0[j].y - q0[i].y };
            const Vector2 crossAxis = { draggedMid.x - anchor.x, draggedMid.y - anchor.y };
            solved = solveSingleScaleForGuide(
                anchor, { 0.0f, 0.0f }, crossAxis, snapX, target, &scale);
            if (solved) {
                clampScale(scale);
                m_state.freeCorners
                    = scaleQuadAlongAxes(q0, anchor, crossAxis, sideAxis, scale, 1.0f);
            }
        } else if (freeResize) {
            float coeffX = 0.0f;
            float coeffY = 0.0f;
            if (decomposeInBasis(draggedMid - anchor, axisX, axisY, &coeffX, &coeffY)) {
                if (horizontalEdge) {
                    const Vector2 fixedOffset { coeffX * axisX.x, coeffX * axisX.y };
                    const Vector2 scaleVector { coeffY * axisY.x, coeffY * axisY.y };
                    solved = solveSingleScaleForGuide(
                        anchor, fixedOffset, scaleVector, snapX, target, &scale);
                    if (solved) {
                        clampScale(scale);
                        m_state.freeCorners
                            = scaleQuadAlongAxes(q0, anchor, axisX, axisY, 1.0f, scale);
                    }
                } else {
                    const Vector2 fixedOffset { coeffY * axisY.x, coeffY * axisY.y };
                    const Vector2 scaleVector { coeffX * axisX.x, coeffX * axisX.y };
                    solved = solveSingleScaleForGuide(
                        anchor, fixedOffset, scaleVector, snapX, target, &scale);
                    if (solved) {
                        clampScale(scale);
                        m_state.freeCorners
                            = scaleQuadAlongAxes(q0, anchor, axisX, axisY, scale, 1.0f);
                    }
                }
            }
        } else {
            const Vector2 scaleVector { draggedMid.x - anchor.x, draggedMid.y - anchor.y };
            solved = solveSingleScaleForGuide(
                anchor, { 0.0f, 0.0f }, scaleVector, snapX, target, &scale);
            if (solved) {
                clampScale(scale);
                m_state.freeCorners = scaleQuadAlongAxes(q0, anchor, axisX, axisY, scale, scale);
            }
        }

        if (!solved) {
            return false;
        }

        const TransformHandle opposite = oppositeSideHandle(m_activeHandle);
        const Vector2 oppositeWorld = m_state.handlePosition(opposite);
        const TransformAutoSnapGuideState previousGuide = m_autoSnapGuide;
        setResizeAutoSnapGuide(candidate, snap, viewport, screenZoom, oppositeWorld);
        pruneAutoSnapGuideStateForSpeed(previousGuide, allowNewSnapConnections, screenZoom);
        return true;
    }

    bool snapActiveFreeSideOnly(const TransformSnapContext& snap, const Viewport* viewport,
        float screenZoom, Qt::KeyboardModifiers mods, bool allowNewSnapConnections)
    {
        if (!m_state.hasFreeQuad() || !m_ctrlFreeDragActive || !isSideHandle(m_activeHandle)
            || (mods & Qt::AltModifier)) {
            return false;
        }

        AutoSnapCandidate candidate = findFreeSideAutoSnapOffset(snap, viewport, screenZoom);
        candidate = filterAutoSnapCandidateForSpeed(candidate, allowNewSnapConnections, screenZoom);
        if (!candidate.valid()) {
            return false;
        }

        const auto [i, j] = sideHandleToCornerIndices(m_activeHandle);
        if (i < 0 || j < 0) {
            return false;
        }

        const Vector2 activePoint = m_state.handlePosition(m_activeHandle);
        Vector2 offset { 0.0f, 0.0f };
        AutoSnapCandidate guideCandidate = candidate;
        if (mods & Qt::ShiftModifier) {
            const auto& qCurrent = *m_state.freeCorners;
            Vector2 sideDir
                = { qCurrent[static_cast<size_t>(j)].x - qCurrent[static_cast<size_t>(i)].x,
                      qCurrent[static_cast<size_t>(j)].y - qCurrent[static_cast<size_t>(i)].y };
            Vector2 normal = normalized({ -sideDir.y, sideDir.x });
            bool snapX = candidate.hasX
                && (!candidate.hasY || candidate.screenDistanceXSq <= candidate.screenDistanceYSq);
            float coeff = snapX ? normal.x : normal.y;
            if (std::abs(coeff) <= 0.001f && candidate.hasX && candidate.hasY) {
                snapX = !snapX;
                coeff = snapX ? normal.x : normal.y;
            }
            if (std::abs(coeff) <= 0.001f) {
                return false;
            }
            const float target = snapX ? candidate.guideX : candidate.guideY;
            const float current = snapX ? activePoint.x : activePoint.y;
            const float amount = (target - current) / coeff;
            offset = { normal.x * amount, normal.y * amount };
            if (snapX) {
                guideCandidate.hasY = false;
            } else {
                guideCandidate.hasX = false;
            }
        } else {
            if (candidate.hasX) {
                offset.x = candidate.offset.x;
            }
            if (candidate.hasY) {
                offset.y = candidate.offset.y;
            }
        }

        auto q = *m_state.freeCorners;
        q[static_cast<size_t>(i)].x += offset.x;
        q[static_cast<size_t>(i)].y += offset.y;
        q[static_cast<size_t>(j)].x += offset.x;
        q[static_cast<size_t>(j)].y += offset.y;
        m_state.freeCorners = q;

        const TransformAutoSnapGuideState previousGuide = m_autoSnapGuide;
        setAutoSnapGuide(guideCandidate);
        pruneAutoSnapGuideStateForSpeed(previousGuide, allowNewSnapConnections, screenZoom);
        return true;
    }

    bool applyAutoSnap(const TransformSnapContext* snapContext, const Viewport* viewport,
        float screenZoom, Qt::KeyboardModifiers mods, const Vector2& cursorWorldPos)
    {
        if (!snapContext || (!snapContext->snapToCanvasCenter && !snapContext->snapToCanvasEdges)) {
            clearAutoSnapGuide();
            return false;
        }
        if (m_activeHandle == TransformHandle::Rotate) {
            clearAutoSnapGuide();
            return false;
        }
        if (m_activeHandle == TransformHandle::Move && (mods & Qt::AltModifier)) {
            clearAutoSnapGuide();
            return false;
        }
        // Auto snap is disabled in deform mode — it isn't useful there and behaves buggily.
        if (m_state.hasDeformMesh()) {
            clearAutoSnapGuide();
            return false;
        }
        const bool allowNewSnapConnections
            = autoSnapSpeedAllowsNewConnections(cursorWorldPos, viewport, screenZoom);

        if (applyClassicResizeAutoSnap(
                *snapContext, viewport, screenZoom, mods, allowNewSnapConnections)) {
            return true;
        }

        if (applyFreeQuadSideResizeAutoSnap(
                *snapContext, viewport, screenZoom, mods, allowNewSnapConnections)) {
            return true;
        }

        if (snapActiveFreeSideOnly(
                *snapContext, viewport, screenZoom, mods, allowNewSnapConnections)) {
            return true;
        }

        const bool freeCornerScalarMode = m_ctrlFreeDragActive && isCornerHandle(m_activeHandle)
            && ((mods & (Qt::ShiftModifier | Qt::AltModifier)) != 0);
        if (!freeCornerScalarMode
            && snapActiveFreeCornerOnly(
                *snapContext, viewport, screenZoom, allowNewSnapConnections)) {
            return true;
        }

        if (isCornerHandle(m_activeHandle) || isSideHandle(m_activeHandle)) {
            clearAutoSnapGuide();
            return false;
        }

        AutoSnapCandidate candidate = findAutoSnapOffset(*snapContext, viewport, screenZoom);
        if (m_activeHandle == TransformHandle::Move) {
            candidate = filterMoveAutoSnapCandidateForShiftLock(candidate, viewport, screenZoom);
        }
        candidate = filterAutoSnapCandidateForSpeed(candidate, allowNewSnapConnections, screenZoom);
        if (!candidate.valid()) {
            clearAutoSnapGuide();
            return false;
        }

        if (m_activeHandle == TransformHandle::Move) {
            const TransformAutoSnapGuideState previousGuide = m_autoSnapGuide;
            setMoveAutoSnapGuide(candidate, *snapContext, screenZoom);
            pruneAutoSnapGuideStateForSpeed(previousGuide, allowNewSnapConnections, screenZoom);
            pruneAutoSnapGuideStateForShiftLock(viewport, screenZoom);
        } else {
            setAutoSnapGuide(candidate);
        }
        translateCurrentTransform(candidate.offset);
        keepMoveSmoothingAlignedWithSnap(candidate.offset);
        return true;
    }

    bool handleDeformPointDrag(const Vector2& worldPos)
    {
        if (!m_state.hasDeformMesh() || m_activeDeformPoint < 0) {
            return false;
        }
        const Vector2 totalDelta = worldPos - m_dragStartWorld;
        if (std::abs(totalDelta.x) <= 0.0001f && std::abs(totalDelta.y) <= 0.0001f) {
            return false;
        }

        if (!m_dragStartDeformMesh.has_value()
            || !m_dragStartDeformMesh->isValidVertexIndex(m_activeDeformPoint)) {
            const Vector2 stepDelta = worldPos - m_dragPrevWorld;
            m_state.offsetDeformMeshPoint(m_activeDeformPoint, stepDelta);
            m_dragPrevWorld = worldPos;
            return true;
        }

        const auto& startMesh = *m_dragStartDeformMesh;
        auto mesh = startMesh;
        // Photoshop Warp: the dragged boundary point moves independently.
        const Vector2 anchor = startMesh.vertices[static_cast<size_t>(m_activeDeformPoint)].target;
        mesh.vertices[static_cast<size_t>(m_activeDeformPoint)].target
            = { anchor.x + totalDelta.x, anchor.y + totalDelta.y };

        // Interior points follow the boundary: their boundary-derived base moves
        // with the boundary, while any independent offset from a prior region
        // drag is preserved (so warping the center then nudging a corner keeps
        // the center warp).
        const auto f0 = startMesh.fitBoundaryAffine();
        const auto f1 = mesh.fitBoundaryAffine();
        if (f0.ok && f1.ok) {
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                if (!mesh.isInteriorIndex(static_cast<int>(i))) {
                    continue;
                }
                const Vector2 startBase = startMesh.derivedInteriorTarget(static_cast<int>(i), f0);
                const Vector2& startTarget = startMesh.vertices[i].target;
                const Vector2 newBase = mesh.derivedInteriorTarget(static_cast<int>(i), f1);
                mesh.vertices[i].target = { newBase.x + (startTarget.x - startBase.x),
                    newBase.y + (startTarget.y - startBase.y) };
            }
        } else {
            mesh.recomputeInteriorFromBoundary();
        }

        m_state.deformMesh = std::move(mesh);
        m_dragPrevWorld = worldPos;
        return true;
    }

    /// Drag inside the deform region: warp the content under the cursor like
    /// Photoshop Warp. Every control point moves by the drag delta scaled by a
    /// weight that falls off with distance from the cursor, so the nearest
    /// points move the most and far points progressively less. The weight is
    /// the SQUARE of each point's B-spline influence N_i at the press point
    /// (a sharper falloff than N alone), normalized by Σ(N³) so the surface
    /// point under the cursor follows the drag exactly:
    ///   δP_i = delta * N_i² / Σ(N_j³).
    /// Because the falloff is sharp, clicking the central cell moves the 4
    /// interior points strongly while the boundary points (far in parameter
    /// space) barely move — they appear fixed — yet everything stays smooth and
    /// distance-weighted. Clicking near an edge naturally moves the nearest
    /// (edge/corner) points instead. Interior points are moved directly here,
    /// not re-derived, which is what gives a strong local warp.
    bool handleDeformRegionDrag(const Vector2& worldPos)
    {
        if (!m_state.hasDeformMesh() || !m_dragStartDeformMesh.has_value()) {
            return false;
        }
        const Vector2 totalDelta = worldPos - m_dragStartWorld;
        if (std::abs(totalDelta.x) <= 0.0001f && std::abs(totalDelta.y) <= 0.0001f) {
            return false;
        }

        const auto& startMesh = *m_dragStartDeformMesh;
        if (m_deformRegionWeights.size() != startMesh.vertices.size()) {
            return false;
        }

        double sumCube = 0.0;
        for (float w : m_deformRegionWeights) {
            const double n = static_cast<double>(w);
            sumCube += n * n * n;
        }
        if (sumCube <= 1e-12) {
            return false;
        }

        auto mesh = startMesh;
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            const double n = static_cast<double>(m_deformRegionWeights[i]);
            const float k = static_cast<float>((n * n) / sumCube);
            if (k <= 1e-6f) {
                continue; // far point: effectively fixed
            }
            const Vector2& base = startMesh.vertices[i].target;
            mesh.vertices[i].target = { base.x + totalDelta.x * k, base.y + totalDelta.y * k };
        }

        m_state.deformMesh = std::move(mesh);
        m_dragPrevWorld = worldPos;
        return true;
    }

    // ====================================================================
    //  F R E E   C O R N E R   D R A G
    // ====================================================================
    //  Default   — move corner to cursor (free)
    //  Shift     — move corner constrained to dominant adjacent edge axis
    //  Alt       — uniform scale entire quad from center
    //  Alt+Shift — free (independent X/Y) scale from center
    // ====================================================================

    bool handleFreeCornerDrag(const Vector2& worldPos, Qt::KeyboardModifiers mods)
    {
        if (!m_state.freeCorners)
            return false;
        int idx = cornerHandleToIndex(m_activeHandle);
        if (idx < 0)
            return false;

        const bool altHeld = (mods & Qt::AltModifier);
        const bool shiftHeld = (mods & Qt::ShiftModifier);

        // ---- Alt held: scale entire quad from center/opposite corner ----
        if (altHeld) {
            if (!m_dragStartQuad) {
                // Sync dragged corner to cursor before capture (handles no mouseMove between
                // rollback and Alt)
                auto q = *m_state.freeCorners;
                q[idx] = worldPos;
                m_state.freeCorners = q;
                m_dragStartQuad = *m_state.freeCorners;
                m_dragStartWorld = worldPos;
            }
            const auto& q0 = *m_dragStartQuad;
            constexpr float eps = 0.0001f;

            Vector2 anchor = quadCenter(q0);

            if (shiftHeld) {
                // Alt+Shift: free (independent X/Y) scale from center
                auto [axX, axY] = quadLocalAxes(q0);
                float refLX = dotV(m_dragStartWorld - anchor, axX);
                float refLY = dotV(m_dragStartWorld - anchor, axY);
                float curLX = dotV(worldPos - anchor, axX);
                float curLY = dotV(worldPos - anchor, axY);
                float scaleX = (std::abs(refLX) > eps) ? (curLX / refLX) : 1.0f;
                float scaleY = (std::abs(refLY) > eps) ? (curLY / refLY) : 1.0f;
                clampScale(scaleX);
                clampScale(scaleY);
                m_state.freeCorners = scaleQuadAlongAxes(q0, anchor, axX, axY, scaleX, scaleY);
            } else {
                // Alt only: proportional (uniform) scale from center
                float len0 = distance(m_dragStartWorld, anchor);
                float len1 = distance(worldPos, anchor);
                float s = (len0 > eps) ? (len1 / len0) : 1.0f;
                clampScale(s);
                m_state.freeCorners = scaleQuadUniform(q0, anchor, s);
            }
            m_dragPrevWorld = worldPos; // Keep current for smooth transition when releasing Alt
            m_wasAltHeld = true;
            return true;
        }

        // ---- Alt released mid-drag: rollback Alt transformation (mouse not released yet) ----
        if (m_wasAltHeld && m_dragStartQuad) {
            m_state.freeCorners = *m_dragStartQuad;
            m_dragStartWorld = worldPos;
            m_dragPrevWorld = worldPos;
            m_wasAltHeld = false;
            m_dragStartQuad.reset(); // Force fresh capture on next Alt/Shift press
        }

        // ---- Shift only: constrain single corner to dominant local axis ----
        if (shiftHeld) {
            if (!m_dragStartQuad) {
                m_dragStartQuad = *m_state.freeCorners;
                m_dragStartWorld = worldPos; // Avoid jump when pressing Shift mid-drag
            }
            const auto& q0 = *m_dragStartQuad;

            Vector2 delta = { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y };

            // Use edges adjacent to this corner to determine local axes
            int prev = (idx + 3) % 4;
            int next = (idx + 1) % 4;
            Vector2 axA = normalized({ q0[next].x - q0[idx].x, q0[next].y - q0[idx].y });
            Vector2 axB = normalized({ q0[prev].x - q0[idx].x, q0[prev].y - q0[idx].y });

            float projA = dotV(delta, axA);
            float projB = dotV(delta, axB);

            // Constrain to the axis with larger projection
            Vector2 constrained;
            if (std::abs(projA) >= std::abs(projB))
                constrained = { projA * axA.x, projA * axA.y };
            else
                constrained = { projB * axB.x, projB * axB.y };

            auto q = q0;
            q[idx] = { q0[idx].x + constrained.x, q0[idx].y + constrained.y };
            m_state.freeCorners = q;
            m_dragPrevWorld = worldPos; // Keep current for smooth transition when releasing Shift
            return true;
        }

        // ---- Default: free corner drag (move corner to cursor) ----
        auto q = *m_state.freeCorners;
        q[idx] = worldPos;
        m_state.freeCorners = q;
        return true;
    }

    // ====================================================================
    //  F R E E   S I D E   D R A G
    // ====================================================================
    //  Default   — move both edge endpoints by delta (free)
    //  Shift     — move side along its normal only (2 corners move)
    //  Alt       — uniform scale entire quad from center
    //  Alt+Shift — scale along side normal from center
    // ====================================================================

    bool handleFreeSideDrag(const Vector2& worldPos, Qt::KeyboardModifiers mods)
    {
        if (!m_state.freeCorners)
            return false;
        auto [i, j] = sideHandleToCornerIndices(m_activeHandle);
        if (i < 0)
            return false;

        const bool altHeld = (mods & Qt::AltModifier);
        const bool shiftHeld = (mods & Qt::ShiftModifier);

        // ---- Alt held: scale entire quad from center/opposite edge ----
        if (altHeld) {
            if (!m_wasAltHeld) {
                // Sync dragged side to cursor before capture (handles switching to Alt mid-drag)
                Vector2 delta = { worldPos.x - m_dragPrevWorld.x, worldPos.y - m_dragPrevWorld.y };
                auto q = *m_state.freeCorners;
                q[i].x += delta.x;
                q[i].y += delta.y;
                q[j].x += delta.x;
                q[j].y += delta.y;
                m_state.freeCorners = q;
                m_dragPrevWorld = worldPos;
                m_dragStartQuad = *m_state.freeCorners;
                m_dragStartWorld = worldPos;
                m_freeSideShiftHeld = false;
            }
            const auto& q0 = *m_dragStartQuad;
            constexpr float eps = 0.0001f;

            Vector2 anchor = quadCenter(q0);

            if (shiftHeld) {
                // Alt+Shift: scale along normal to this side only, from center
                Vector2 sideDir = { q0[j].x - q0[i].x, q0[j].y - q0[i].y };
                Vector2 normal = normalized({ -sideDir.y, sideDir.x });
                Vector2 tangent = normalized(sideDir);
                Vector2 edgeMid = { (q0[i].x + q0[j].x) * 0.5f, (q0[i].y + q0[j].y) * 0.5f };
                if (dotV(edgeMid - anchor, normal) < 0.0f)
                    normal = { -normal.x, -normal.y };

                float refProj = dotV(m_dragStartWorld - anchor, normal);
                float curProj = dotV(worldPos - anchor, normal);
                float scale = (std::abs(refProj) > eps) ? (curProj / refProj) : 1.0f;
                clampScale(scale);
                m_state.freeCorners = scaleQuadAlongAxes(q0, anchor, normal, tangent, scale, 1.0f);
            } else {
                // Alt only: proportional (uniform) scale from center
                float len0 = distance(m_dragStartWorld, anchor);
                float len1 = distance(worldPos, anchor);
                float s = (len0 > eps) ? (len1 / len0) : 1.0f;
                clampScale(s);
                m_state.freeCorners = scaleQuadUniform(q0, anchor, s);
            }
            m_dragPrevWorld = worldPos; // Keep current for smooth transition when releasing Alt
            m_wasAltHeld = true;
            return true;
        }

        // ---- Alt released mid-drag: rollback Alt transformation (mouse not released yet) ----
        if (m_wasAltHeld && m_dragStartQuad) {
            m_state.freeCorners = *m_dragStartQuad;
            m_dragStartWorld = worldPos;
            m_dragPrevWorld = worldPos;
            m_wasAltHeld = false;
            m_freeSideShiftHeld = false;
            m_dragStartQuad.reset(); // Force fresh capture on next Alt/Shift press
        }

        // ---- Shift only: move side along its normal (only 2 corners move) ----
        if (shiftHeld) {
            if (!m_freeSideShiftHeld || !m_dragStartQuad) {
                if (m_dragStartQuad) {
                    Vector2 delta
                        = { worldPos.x - m_dragPrevWorld.x, worldPos.y - m_dragPrevWorld.y };
                    auto q = *m_state.freeCorners;
                    q[i].x += delta.x;
                    q[i].y += delta.y;
                    q[j].x += delta.x;
                    q[j].y += delta.y;
                    m_state.freeCorners = q;
                    m_dragPrevWorld = worldPos;
                }
                m_dragStartQuad = *m_state.freeCorners;
                m_dragStartWorld = worldPos; // Avoid jump when pressing Shift mid-drag
                m_freeSideShiftHeld = true;
            }
            const auto& q0 = *m_dragStartQuad;

            Vector2 delta = { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y };

            // Normal to the side
            Vector2 sideDir = { q0[j].x - q0[i].x, q0[j].y - q0[i].y };
            Vector2 normal = normalized({ -sideDir.y, sideDir.x });
            float proj = dotV(delta, normal);

            auto q = q0;
            q[i] = { q0[i].x + proj * normal.x, q0[i].y + proj * normal.y };
            q[j] = { q0[j].x + proj * normal.x, q0[j].y + proj * normal.y };
            m_state.freeCorners = q;
            m_dragPrevWorld = worldPos; // Keep current for smooth transition when releasing Shift
            return true;
        }

        // ---- Default: free side drag (move both endpoints from the drag-start quad) ----
        if (m_freeSideShiftHeld) {
            m_dragStartQuad = *m_state.freeCorners;
            m_dragStartWorld = worldPos;
            m_freeSideShiftHeld = false;
        }
        if (!m_dragStartQuad) {
            m_dragStartQuad = *m_state.freeCorners;
            m_dragStartWorld = worldPos;
        }
        const auto& q0 = *m_dragStartQuad;
        Vector2 delta = { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y };
        m_dragPrevWorld = worldPos;
        auto q = q0;
        q[i] = { q0[i].x + delta.x, q0[i].y + delta.y };
        q[j] = { q0[j].x + delta.x, q0[j].y + delta.y };
        m_state.freeCorners = q;
        return true;
    }

    bool handleFreeMoveDrag(const Vector2& worldPos, Qt::KeyboardModifiers mods, float screenZoom,
        const Viewport* viewport)
    {
        if (!m_state.freeCorners)
            return false;
        const bool shiftHeld = (mods & Qt::ShiftModifier);

        if (shiftHeld && m_dragStartQuad) {
            const Vector2 rawOff = pixelAlignedMoveOffset(
                { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y });
            beginShiftMoveReferenceIfNeeded();
            updateShiftMoveLineAxis(worldPos, rawOff, screenZoom, viewport);
            const Vector2 centerDragStart = quadCenter(*m_dragStartQuad);
            m_moveTargetOffset = pixelAlignedMoveOffset(shiftConstrainedMoveAlongFixedAxis(worldPos,
                m_dragStartWorld, shiftMoveReferenceWorld(), centerDragStart, m_moveShiftLineUnit));
            if (!m_wasShiftHeldForMove) {
                const auto& q0 = *m_dragStartQuad;
                const auto& qc = *m_state.freeCorners;
                m_moveSmoothOffset
                    = pixelAlignedMoveOffset({ qc[0].x - q0[0].x, qc[0].y - q0[0].y });
            }
            m_moveTranslationAnimActive = true;
            m_wasShiftHeldForMove = true;
            m_dragPrevWorld = worldPos;
            updateMoveAxisGuideForShiftMove(mods);
            return true;
        }

        m_wasShiftHeldForMove = false;
        clearShiftMoveReference();
        m_moveTranslationAnimActive = false;
        if (m_dragStartQuad) {
            m_moveTargetOffset = pixelAlignedMoveOffset(
                { worldPos.x - m_dragStartWorld.x, worldPos.y - m_dragStartWorld.y });
            m_dragPrevWorld = worldPos;
        } else {
            Vector2 delta = { worldPos.x - m_dragPrevWorld.x, worldPos.y - m_dragPrevWorld.y };
            m_dragPrevWorld = worldPos;
            auto q = *m_state.freeCorners;
            for (int i = 0; i < 4; ++i) {
                q[i].x += delta.x;
                q[i].y += delta.y;
            }
            m_state.freeCorners = q;
            m_moveSmoothOffset = { 0.f, 0.f };
            m_moveTargetOffset = { 0.f, 0.f };
        }
        updateMoveAxisGuideForShiftMove(mods);
        return true;
    }

    bool handleQuadResize(const Vector2& worldPos, Qt::KeyboardModifiers mods)
    {
        if (!m_state.freeCorners || !m_dragStartQuad)
            return false;

        const auto& q0 = *m_dragStartQuad;
        const bool fromCenter = (mods & Qt::AltModifier);
        const bool freeResize = (mods & Qt::ShiftModifier);
        auto [axisX, axisY] = quadLocalAxes(q0);
        constexpr float eps = 0.001f;

        auto scaleCurrentQuad = [&](const Vector2& anchor, float scaleA, float scaleB) {
            clampScale(scaleA);
            clampScale(scaleB);
            m_state.freeCorners = scaleQuadAlongAxes(q0, anchor, axisX, axisY, scaleA, scaleB);
            return true;
        };

        auto scaleQuadWithBasis = [&](const Vector2& anchor, const Vector2& basisA,
                                      const Vector2& basisB, float scaleA, float scaleB) {
            clampScale(scaleA);
            clampScale(scaleB);
            m_state.freeCorners = scaleQuadAlongAxes(q0, anchor, basisA, basisB, scaleA, scaleB);
            return true;
        };

        if (isCornerHandle(m_activeHandle)) {
            const int idx = cornerHandleToIndex(m_activeHandle);
            if (idx < 0)
                return false;

            const Vector2 anchor = fromCenter ? quadCenter(q0) : q0[(idx + 2) % 4];
            const Vector2 startVec = { q0[idx].x - anchor.x, q0[idx].y - anchor.y };
            const Vector2 currentVec = { worldPos.x - anchor.x, worldPos.y - anchor.y };

            if (freeResize && !fromCenter) {
                const int anchorIdx = (idx + 2) % 4;
                const int adjA = (anchorIdx + 1) % 4;
                const int adjB = (anchorIdx + 3) % 4;
                const Vector2 basisA = { q0[adjA].x - anchor.x, q0[adjA].y - anchor.y };
                const Vector2 basisB = { q0[adjB].x - anchor.x, q0[adjB].y - anchor.y };

                float startA = 0.0f, startB = 0.0f;
                float currentA = 0.0f, currentB = 0.0f;
                if (decomposeInBasis(startVec, basisA, basisB, &startA, &startB)
                    && decomposeInBasis(currentVec, basisA, basisB, &currentA, &currentB)) {
                    const float scaleA = (std::abs(startA) > eps) ? (currentA / startA) : 1.0f;
                    const float scaleB = (std::abs(startB) > eps) ? (currentB / startB) : 1.0f;
                    return scaleQuadWithBasis(anchor, basisA, basisB, scaleA, scaleB);
                }
            }

            const float startX = dotV(startVec, axisX);
            const float startY = dotV(startVec, axisY);
            const float currentX = dotV(currentVec, axisX);
            const float currentY = dotV(currentVec, axisY);

            if (freeResize) {
                float scaleX = (std::abs(startX) > eps) ? (currentX / startX) : 1.0f;
                float scaleY = (std::abs(startY) > eps) ? (currentY / startY) : 1.0f;
                return scaleCurrentQuad(anchor, scaleX, scaleY);
            }

            const float startDiagLenSq = startVec.x * startVec.x + startVec.y * startVec.y;
            float scale = 1.0f;
            if (startDiagLenSq > eps) {
                scale = (currentVec.x * startVec.x + currentVec.y * startVec.y) / startDiagLenSq;
            }
            return scaleCurrentQuad(anchor, scale, scale);
        }

        auto [i, j] = sideHandleToCornerIndices(m_activeHandle);
        if (i < 0 || j < 0)
            return false;

        const int oppI = (i + 2) % 4;
        const int oppJ = (j + 2) % 4;
        const Vector2 draggedMid = { (q0[i].x + q0[j].x) * 0.5f, (q0[i].y + q0[j].y) * 0.5f };
        const Vector2 anchor = fromCenter
            ? quadCenter(q0)
            : Vector2 { (q0[oppI].x + q0[oppJ].x) * 0.5f, (q0[oppI].y + q0[oppJ].y) * 0.5f };

        const Vector2 currentVec = { worldPos.x - anchor.x, worldPos.y - anchor.y };

        if (freeResize && !fromCenter) {
            const Vector2 sideAxis = { q0[j].x - q0[i].x, q0[j].y - q0[i].y };
            const Vector2 crossAxis = { draggedMid.x - anchor.x, draggedMid.y - anchor.y };
            float startCross = 0.0f, startSide = 0.0f;
            float currentCross = 0.0f, currentSide = 0.0f;
            if (decomposeInBasis(draggedMid - anchor, crossAxis, sideAxis, &startCross, &startSide)
                && decomposeInBasis(currentVec, crossAxis, sideAxis, &currentCross, &currentSide)) {
                const float scaleCross
                    = (std::abs(startCross) > eps) ? (currentCross / startCross) : 1.0f;
                (void) startSide;
                (void) currentSide;
                return scaleQuadWithBasis(anchor, crossAxis, sideAxis, scaleCross, 1.0f);
            }
        }

        const bool horizontalEdge = isHorizontalEdge(m_activeHandle);
        const Vector2 drivenAxis = horizontalEdge ? axisY : axisX;
        const float refDrive = dotV(draggedMid - anchor, drivenAxis);
        const float curDrive = dotV(currentVec, drivenAxis);
        float scaleDrive = (std::abs(refDrive) > eps) ? (curDrive / refDrive) : 1.0f;

        if (horizontalEdge) {
            if (freeResize) {
                return scaleCurrentQuad(anchor, 1.0f, scaleDrive);
            }
            return scaleCurrentQuad(anchor, scaleDrive, scaleDrive);
        }

        if (freeResize) {
            return scaleCurrentQuad(anchor, scaleDrive, 1.0f);
        }
        return scaleCurrentQuad(anchor, scaleDrive, scaleDrive);
    }

    bool handleFreeRotateDrag(const Vector2& worldPos, Qt::KeyboardModifiers mods)
    {
        if (!m_dragStartQuad)
            return false;
        const auto& q0 = *m_dragStartQuad;
        Vector2 center = { (q0[0].x + q0[1].x + q0[2].x + q0[3].x) * 0.25f,
            (q0[0].y + q0[1].y + q0[2].y + q0[3].y) * 0.25f };
        float startAngle = std::atan2(m_dragStartWorld.y - center.y, m_dragStartWorld.x - center.x);
        float curAngle = std::atan2(worldPos.y - center.y, worldPos.x - center.x);
        float angle = normalizeAngleDelta(curAngle - startAngle);
        if (mods & Qt::ShiftModifier) {
            angle = snapRotation(angle);
        }

        if ((mods & Qt::ShiftModifier) || m_freeRotateAnimationActive) {
            m_freeRotateAnimationBaseQuad = q0;
            m_freeRotateAnimationCenter = center;
            m_freeRotateTargetAngle = angle;
            m_freeRotateAnimationActive = true;
            return updateAnimation(1.0f / 60.0f);
        }

        m_freeRotateDisplayAngle = angle;
        m_freeRotateTargetAngle = angle;
        m_freeRotateAnimationActive = false;
        m_freeRotateAnimationBaseQuad.reset();
        m_state.freeCorners = rotatedQuad(q0, center, angle);
        return true;
    }

    static int cornerHandleToIndex(TransformHandle h)
    {
        switch (h) {
        case TransformHandle::TopLeft:
            return 0;
        case TransformHandle::TopRight:
            return 1;
        case TransformHandle::BottomRight:
            return 2;
        case TransformHandle::BottomLeft:
            return 3;
        default:
            return -1;
        }
    }

    static std::pair<int, int> sideHandleToCornerIndices(TransformHandle h)
    {
        switch (h) {
        case TransformHandle::Top:
            return { 0, 1 };
        case TransformHandle::Right:
            return { 1, 2 };
        case TransformHandle::Bottom:
            return { 2, 3 };
        case TransformHandle::Left:
            return { 3, 0 };
        default:
            return { -1, -1 };
        }
    }

    static bool isSideHandle(TransformHandle h)
    {
        return h == TransformHandle::Top || h == TransformHandle::Bottom
            || h == TransformHandle::Left || h == TransformHandle::Right;
    }

    // ====================================================================
    //  R E S I Z E   L O G I C
    // ====================================================================
    //
    //  Default   — proportional resize, opposite side/corner stays fixed
    //  Shift     — free (non-proportional), opposite side/corner stays fixed
    //  Alt       — resize from center (symmetric around pivot)
    //  Alt+Shift — free resize from center
    //

    bool updateRotationDragFromWorld(const Vector2& worldPos, Qt::KeyboardModifiers mods)
    {
        Vector2 pivotWorld = m_state.pivot + m_dragStartTranslation;
        float startAngle
            = std::atan2(m_dragStartWorld.y - pivotWorld.y, m_dragStartWorld.x - pivotWorld.x);
        float curAngle = std::atan2(worldPos.y - pivotWorld.y, worldPos.x - pivotWorld.x);
        const float rawRotation = m_dragStartRotation + normalizeAngleDelta(curAngle - startAngle);
        const bool shiftHeld = (mods & Qt::ShiftModifier);
        const float target = shiftHeld ? snapRotation(rawRotation) : rawRotation;
        m_targetRotation = nearestEquivalentAngle(m_state.rotation, target);

        if (shiftHeld || m_rotationAnimationActive) {
            m_rotationAnimationActive = true;
            return updateAnimation(1.0f / 60.0f);
        }

        m_state.rotation = m_targetRotation;
        return true;
    }

    bool handleResize(const Vector2& worldPos, Qt::KeyboardModifiers mods)
    {
        const bool fromCenter = (mods & Qt::AltModifier);
        const bool freeResize = (mods & Qt::ShiftModifier);

        // --- 1. Anchor and handle in content space ---
        Vector2 A = fromCenter ? m_state.pivot : oppositeAnchor(m_activeHandle);
        Vector2 H = handleContentPos(m_activeHandle);
        Vector2 cd = { H.x - A.x, H.y - A.y }; // content delta

        // Anchor's fixed world position (drag-start state)
        Vector2 anchorWorld
            = contentToWorld(A, m_dragStartScale, m_dragStartRotation, m_dragStartTranslation);

        // --- 2. Un-rotate cursor relative to anchor ---
        //   From: cursor = Rotate(scale * (H - A)) + anchorWorld
        //   So:   InvRotate(cursor - anchorWorld) = scale * (H - A)
        Vector2 delta = unrotateRelativeTo(worldPos, anchorWorld, m_dragStartRotation);

        // --- 3. Compute scale ---
        float scaleX = m_dragStartScale.x;
        float scaleY = m_dragStartScale.y;
        constexpr float eps = 0.001f;

        if (freeResize) {
            // Shift: free resize — handle follows cursor exactly
            if (std::abs(cd.x) > eps)
                scaleX = delta.x / cd.x;
            if (std::abs(cd.y) > eps)
                scaleY = delta.y / cd.y;
        } else {
            // Default: proportional
            if (isCornerHandle(m_activeHandle)) {
                // Project cursor onto the diagonal (anchor → handle at drag-start scale)
                Vector2 dir = { m_dragStartScale.x * cd.x, m_dragStartScale.y * cd.y };
                float dirLenSq = dir.x * dir.x + dir.y * dir.y;
                if (dirLenSq > eps) {
                    float r = (delta.x * dir.x + delta.y * dir.y) / dirLenSq;
                    scaleX = m_dragStartScale.x * r;
                    scaleY = m_dragStartScale.y * r;
                }
            } else if (std::abs(cd.y) > eps && std::abs(cd.x) <= eps) {
                // Horizontal edge (Top/Bottom): Y drives, X follows
                scaleY = delta.y / cd.y;
                if (std::abs(m_dragStartScale.y) > eps)
                    scaleX = scaleY * (m_dragStartScale.x / m_dragStartScale.y);
            } else if (std::abs(cd.x) > eps && std::abs(cd.y) <= eps) {
                // Vertical edge (Left/Right): X drives, Y follows
                scaleX = delta.x / cd.x;
                if (std::abs(m_dragStartScale.x) > eps)
                    scaleY = scaleX * (m_dragStartScale.y / m_dragStartScale.x);
            }
        }

        // Prevent zero scale (would collapse geometry), but allow negatives for flip
        if (std::abs(scaleX) < 0.001f)
            scaleX = (scaleX < 0.0f) ? -0.001f : 0.001f;
        if (std::abs(scaleY) < 0.001f)
            scaleY = (scaleY < 0.0f) ? -0.001f : 0.001f;

        // --- 4. Apply ---
        m_state.scale = { scaleX, scaleY };
        m_state.rotation = m_dragStartRotation;

        // --- 5. Fix translation so anchor stays in place ---
        if (fromCenter) {
            m_state.translation = m_dragStartTranslation;
        } else {
            Vector2 anchorWorldNew
                = contentToWorld(A, m_state.scale, m_dragStartRotation, m_dragStartTranslation);
            m_state.translation = { m_dragStartTranslation.x + (anchorWorld.x - anchorWorldNew.x),
                m_dragStartTranslation.y + (anchorWorld.y - anchorWorldNew.y) };
        }

        return true;
    }

    // ---- Helpers ----

    /// Content-space position of the opposite anchor for a given handle
    Vector2 oppositeAnchor(TransformHandle h) const
    {
        float l = m_state.contentBounds.left();
        float r = m_state.contentBounds.right();
        float t = m_state.contentBounds.top();
        float b = m_state.contentBounds.bottom();
        float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
        switch (h) {
        case TransformHandle::TopLeft:
            return { r, b };
        case TransformHandle::TopRight:
            return { l, b };
        case TransformHandle::BottomLeft:
            return { r, t };
        case TransformHandle::BottomRight:
            return { l, t };
        case TransformHandle::Top:
            return { cx, b };
        case TransformHandle::Bottom:
            return { cx, t };
        case TransformHandle::Left:
            return { r, cy };
        case TransformHandle::Right:
            return { l, cy };
        default:
            return { cx, cy };
        }
    }

    /// Content-space position of a handle
    Vector2 handleContentPos(TransformHandle h) const
    {
        float l = m_state.contentBounds.left();
        float r = m_state.contentBounds.right();
        float t = m_state.contentBounds.top();
        float b = m_state.contentBounds.bottom();
        float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
        switch (h) {
        case TransformHandle::TopLeft:
            return { l, t };
        case TransformHandle::TopRight:
            return { r, t };
        case TransformHandle::BottomLeft:
            return { l, b };
        case TransformHandle::BottomRight:
            return { r, b };
        case TransformHandle::Top:
            return { cx, t };
        case TransformHandle::Bottom:
            return { cx, b };
        case TransformHandle::Left:
            return { l, cy };
        case TransformHandle::Right:
            return { r, cy };
        default:
            return { cx, cy };
        }
    }

    /// Transform content-space point → world with given state
    Vector2 contentToWorld(
        const Vector2& p, const Vector2& scale, float rotation, const Vector2& translation) const
    {
        float dx = (p.x - m_state.pivot.x) * scale.x;
        float dy = (p.y - m_state.pivot.y) * scale.y;
        float cosR = std::cos(rotation), sinR = std::sin(rotation);
        return { dx * cosR - dy * sinR + m_state.pivot.x + translation.x,
            dx * sinR + dy * cosR + m_state.pivot.y + translation.y };
    }

    /// Inverse-rotate worldPos around an origin point
    Vector2 unrotateRelativeTo(const Vector2& worldPos, const Vector2& origin, float rotation) const
    {
        float dx = worldPos.x - origin.x;
        float dy = worldPos.y - origin.y;
        float cosR = std::cos(-rotation), sinR = std::sin(-rotation);
        return { dx * cosR - dy * sinR, dx * sinR + dy * cosR };
    }

    static bool isCornerHandle(TransformHandle h)
    {
        return h == TransformHandle::TopLeft || h == TransformHandle::TopRight
            || h == TransformHandle::BottomLeft || h == TransformHandle::BottomRight;
    }
    static bool isHorizontalEdge(TransformHandle h)
    {
        return h == TransformHandle::Top || h == TransformHandle::Bottom;
    }

    // ---- Vector helpers for local-axis scaling ----

    static float dotV(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }

    static bool decomposeInBasis(
        const Vector2& v, const Vector2& basisA, const Vector2& basisB, float* outA, float* outB)
    {
        if (!outA || !outB)
            return false;
        const float det = basisA.x * basisB.y - basisA.y * basisB.x;
        if (std::abs(det) <= 0.0001f) {
            return false;
        }
        *outA = (v.x * basisB.y - v.y * basisB.x) / det;
        *outB = (basisA.x * v.y - basisA.y * v.x) / det;
        return true;
    }

    static ShiftMoveLineSlot pickShiftMoveLineSlot(const Vector2& d)
    {
        const float horiz = std::abs(d.x);
        const float vert = std::abs(d.y);
        return (vert > horiz) ? ShiftMoveLineSlot::Vertical : ShiftMoveLineSlot::Horizontal;
    }

    static Vector2 shiftMoveScreenPoint(
        const Vector2& worldPos, float screenZoom, const Viewport* viewport)
    {
        if (viewport) {
            return viewport->worldToScreen(worldPos);
        }

        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.f;
        return { worldPos.x * z, worldPos.y * z };
    }

    static float screenDistanceToAxis(
        const Vector2& pointScreen, const Vector2& originScreen, const Vector2& axisPointScreen)
    {
        Vector2 dir { axisPointScreen.x - originScreen.x, axisPointScreen.y - originScreen.y };
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len <= 0.0001f) {
            return std::numeric_limits<float>::max();
        }

        dir.x /= len;
        dir.y /= len;
        const Vector2 delta { pointScreen.x - originScreen.x, pointScreen.y - originScreen.y };
        return std::abs(delta.x * dir.y - delta.y * dir.x);
    }

    static bool pickShiftMoveLineSlotFromTriggerZone(const Vector2& worldPos,
        const Vector2& originWorld, float screenZoom, const Viewport* viewport,
        ShiftMoveLineSlot* outSlot)
    {
        if (!outSlot) {
            return false;
        }

        bool inHorizontalZone = false;
        bool inVerticalZone = false;
        if (viewport) {
            const Vector2 pointScreen = viewport->worldToScreen(worldPos);
            const Vector2 originScreen = viewport->worldToScreen(originWorld);
            const Vector2 horizontalAxisScreen
                = viewport->worldToScreen({ originWorld.x + 1.0f, originWorld.y });
            const Vector2 verticalAxisScreen
                = viewport->worldToScreen({ originWorld.x, originWorld.y + 1.0f });
            inHorizontalZone = screenDistanceToAxis(pointScreen, originScreen, horizontalAxisScreen)
                <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
            inVerticalZone = screenDistanceToAxis(pointScreen, originScreen, verticalAxisScreen)
                <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
        } else {
            const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.f;
            const Vector2 dRefWorld { worldPos.x - originWorld.x, worldPos.y - originWorld.y };
            const float dxScreen = dRefWorld.x * z;
            const float dyScreen = dRefWorld.y * z;
            inHorizontalZone = std::abs(dyScreen) <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
            inVerticalZone = std::abs(dxScreen) <= SHIFT_MOVE_AXIS_TRIGGER_SCREEN_PX;
        }

        if (inHorizontalZone == inVerticalZone) {
            return false;
        }

        *outSlot = inHorizontalZone ? ShiftMoveLineSlot::Horizontal : ShiftMoveLineSlot::Vertical;
        return true;
    }

    bool updateShiftMoveAxisSwitchSpeed(
        const Vector2& worldPos, float screenZoom, const Viewport* viewport)
    {
        const Vector2 screenPos = shiftMoveScreenPoint(worldPos, screenZoom, viewport);
        const auto now = std::chrono::steady_clock::now();

        if (!m_moveShiftSpeedSampleValid) {
            m_moveShiftLastScreenPos = screenPos;
            m_moveShiftLastSpeedTime = now;
            m_moveShiftSpeedSampleValid = true;
            return true;
        }

        const float dt = std::chrono::duration<float>(now - m_moveShiftLastSpeedTime).count();
        const Vector2 delta { screenPos.x - m_moveShiftLastScreenPos.x,
            screenPos.y - m_moveShiftLastScreenPos.y };
        const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);

        m_moveShiftLastScreenPos = screenPos;
        m_moveShiftLastSpeedTime = now;

        if (dt <= 0.001f) {
            return dist <= 1.0f;
        }

        const float speed = dist / dt;
        return speed <= SHIFT_MOVE_AXIS_SWITCH_MAX_SPEED_SCREEN_PX_PER_SEC;
    }

    static Vector2 unitVectorForShiftLineSlot(ShiftMoveLineSlot slot)
    {
        switch (slot) {
        case ShiftMoveLineSlot::Horizontal:
            return { 1.f, 0.f };
        case ShiftMoveLineSlot::Vertical:
            return { 0.f, 1.f };
        case ShiftMoveLineSlot::None:
        default:
            return { 1.f, 0.f };
        }
    }

    Vector2 currentTransformContentCenterWorld() const
    {
        if (m_state.hasDeformMesh())
            return m_state.transformedAABB().center();
        if (m_state.hasFreeQuad())
            return quadCenter(*m_state.freeCorners);
        return { m_state.pivot.x + m_state.translation.x, m_state.pivot.y + m_state.translation.y };
    }

    const Vector2& shiftMoveReferenceWorld() const
    {
        return m_moveShiftReferenceValid ? m_moveShiftReferenceWorld
                                         : m_transformModeEntryReferenceWorld;
    }

    void beginShiftMoveReferenceIfNeeded()
    {
        if (m_moveShiftReferenceValid)
            return;
        m_moveShiftReferenceWorld = m_dragStartContentCenter;
        m_moveShiftReferenceValid = true;
        m_moveShiftLineSlot = ShiftMoveLineSlot::None;
        m_moveShiftSpeedSampleValid = false;
    }

    void clearShiftMoveReference() { m_moveShiftReferenceValid = false; }

    /// Shift-move axis: initial axis follows dominant movement; later switches only
    /// when cursor enters a screen-space trigger zone around the other axis.
    void updateShiftMoveLineAxis(
        const Vector2& worldPos, const Vector2& dGrab, float screenZoom, const Viewport* viewport)
    {
        const Vector2& R = shiftMoveReferenceWorld();

        Vector2 dRef = { worldPos.x - R.x, worldPos.y - R.y };
        float lr = dRef.x * dRef.x + dRef.y * dRef.y;
        if (lr < 1.0e-12f) {
            dRef = dGrab;
            lr = dRef.x * dRef.x + dRef.y * dRef.y;
        }
        if (lr < 1.0e-12f) {
            if (m_moveShiftLineSlot == ShiftMoveLineSlot::None) {
                m_moveShiftLineSlot = ShiftMoveLineSlot::Horizontal;
                m_moveShiftLineUnit = { 1.f, 0.f };
            }
            return;
        }

        ShiftMoveLineSlot triggerSlot = ShiftMoveLineSlot::None;
        const bool axisSwitchSpeedAllowed
            = updateShiftMoveAxisSwitchSpeed(worldPos, screenZoom, viewport);
        if (pickShiftMoveLineSlotFromTriggerZone(worldPos, R, screenZoom, viewport, &triggerSlot)) {
            if (m_moveShiftLineSlot == ShiftMoveLineSlot::None || m_moveShiftLineSlot == triggerSlot
                || axisSwitchSpeedAllowed) {
                m_moveShiftLineSlot = triggerSlot;
            }
        } else if (m_moveShiftLineSlot == ShiftMoveLineSlot::None) {
            m_moveShiftLineSlot = pickShiftMoveLineSlot(dRef);
        }
        m_moveShiftLineUnit = unitVectorForShiftLineSlot(m_moveShiftLineSlot);
    }

    /// Shift-move: tracked point stays on line entryRef + s*u (u from updateShiftMoveLineAxis).
    static Vector2 shiftConstrainedMoveAlongFixedAxis(const Vector2& worldPos,
        const Vector2& dragStartWorld, const Vector2& transformEntryRefWorld,
        const Vector2& trackedPointWorldAtDragStart, const Vector2& lineUnit)
    {
        const Vector2 dGrab = { worldPos.x - dragStartWorld.x, worldPos.y - dragStartWorld.y };
        const Vector2 pUncon = { trackedPointWorldAtDragStart.x + dGrab.x,
            trackedPointWorldAtDragStart.y + dGrab.y };
        const float sc = dotV(
            { pUncon.x - transformEntryRefWorld.x, pUncon.y - transformEntryRefWorld.y }, lineUnit);
        return { transformEntryRefWorld.x + sc * lineUnit.x - trackedPointWorldAtDragStart.x,
            transformEntryRefWorld.y + sc * lineUnit.y - trackedPointWorldAtDragStart.y };
    }

    static float distance(const Vector2& a, const Vector2& b)
    {
        float dx = a.x - b.x, dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    static Vector2 normalized(const Vector2& v)
    {
        float len = std::sqrt(v.x * v.x + v.y * v.y);
        return (len > 0.0001f) ? Vector2 { v.x / len, v.y / len } : Vector2 { 1.0f, 0.0f };
    }

    static void clampScale(float& s)
    {
        if (std::abs(s) < 0.001f)
            s = (s < 0.0f) ? -0.001f : 0.001f;
    }

    static float normalizeAngleDelta(float angle)
    {
        constexpr float twoPi = 2.0f * 3.14159265358979323846f;
        while (angle > 3.14159265358979323846f)
            angle -= twoPi;
        while (angle < -3.14159265358979323846f)
            angle += twoPi;
        return angle;
    }

    static float nearestEquivalentAngle(float reference, float target)
    {
        return reference + normalizeAngleDelta(target - reference);
    }

    static Vector2 pixelAlignedMoveOffset(const Vector2& offset)
    {
        return { std::round(offset.x), std::round(offset.y) };
    }

    void resetAutoSnapSpeedGate() { m_autoSnapSpeedSampleValid = false; }

    static bool sameAutoSnapGuideValue(float a, float b, float screenZoom)
    {
        const float z = (screenZoom > 1.0e-6f) ? screenZoom : 1.0f;
        const float tolerance = std::max(0.001f, 0.5f / z);
        return std::abs(a - b) <= tolerance;
    }

    static bool autoSnapStateHasVerticalGuide(
        const TransformAutoSnapGuideState& state, float guideX, float screenZoom)
    {
        return (state.hasVertical && sameAutoSnapGuideValue(state.verticalX, guideX, screenZoom))
            || (state.hasSecondVertical
                && sameAutoSnapGuideValue(state.secondVerticalX, guideX, screenZoom));
    }

    static bool autoSnapStateHasHorizontalGuide(
        const TransformAutoSnapGuideState& state, float guideY, float screenZoom)
    {
        return (state.hasHorizontal
                   && sameAutoSnapGuideValue(state.horizontalY, guideY, screenZoom))
            || (state.hasSecondHorizontal
                && sameAutoSnapGuideValue(state.secondHorizontalY, guideY, screenZoom));
    }

    bool currentAutoSnapHasVerticalGuide(float guideX, float screenZoom) const
    {
        return autoSnapStateHasVerticalGuide(m_autoSnapGuide, guideX, screenZoom);
    }

    bool currentAutoSnapHasHorizontalGuide(float guideY, float screenZoom) const
    {
        return autoSnapStateHasHorizontalGuide(m_autoSnapGuide, guideY, screenZoom);
    }

    AutoSnapCandidate filterAutoSnapCandidateForSpeed(
        const AutoSnapCandidate& candidate, bool allowNewSnapConnections, float screenZoom) const
    {
        if (allowNewSnapConnections) {
            return candidate;
        }

        AutoSnapCandidate filtered = candidate;
        if (filtered.hasX && !currentAutoSnapHasVerticalGuide(filtered.guideX, screenZoom)) {
            filtered.hasX = false;
            filtered.offset.x = 0.0f;
            filtered.screenDistanceXSq = std::numeric_limits<float>::max();
        }
        if (filtered.hasY && !currentAutoSnapHasHorizontalGuide(filtered.guideY, screenZoom)) {
            filtered.hasY = false;
            filtered.offset.y = 0.0f;
            filtered.screenDistanceYSq = std::numeric_limits<float>::max();
        }
        return filtered;
    }

    ResizeAutoSnapCandidate filterResizeAutoSnapCandidateForSpeed(
        const ResizeAutoSnapCandidate& candidate, bool allowNewSnapConnections,
        float screenZoom) const
    {
        if (allowNewSnapConnections) {
            return candidate;
        }

        ResizeAutoSnapCandidate filtered = candidate;
        if (filtered.hasX && !currentAutoSnapHasVerticalGuide(filtered.targetX, screenZoom)) {
            filtered.hasX = false;
            filtered.screenDistanceXSq = std::numeric_limits<float>::max();
        }
        if (filtered.hasY && !currentAutoSnapHasHorizontalGuide(filtered.targetY, screenZoom)) {
            filtered.hasY = false;
            filtered.screenDistanceYSq = std::numeric_limits<float>::max();
        }
        return filtered;
    }

    void pruneAutoSnapGuideStateForSpeed(const TransformAutoSnapGuideState& previousGuide,
        bool allowNewSnapConnections, float screenZoom)
    {
        if (allowNewSnapConnections) {
            return;
        }

        if (m_autoSnapGuide.hasVertical
            && !autoSnapStateHasVerticalGuide(
                previousGuide, m_autoSnapGuide.verticalX, screenZoom)) {
            m_autoSnapGuide.hasVertical = false;
        }
        if (m_autoSnapGuide.hasSecondVertical
            && !autoSnapStateHasVerticalGuide(
                previousGuide, m_autoSnapGuide.secondVerticalX, screenZoom)) {
            m_autoSnapGuide.hasSecondVertical = false;
        }
        if (m_autoSnapGuide.hasHorizontal
            && !autoSnapStateHasHorizontalGuide(
                previousGuide, m_autoSnapGuide.horizontalY, screenZoom)) {
            m_autoSnapGuide.hasHorizontal = false;
        }
        if (m_autoSnapGuide.hasSecondHorizontal
            && !autoSnapStateHasHorizontalGuide(
                previousGuide, m_autoSnapGuide.secondHorizontalY, screenZoom)) {
            m_autoSnapGuide.hasSecondHorizontal = false;
        }
    }

    bool autoSnapSpeedAllowsNewConnections(
        const Vector2& worldPos, const Viewport* viewport, float screenZoom)
    {
        const Vector2 screenPos = screenPointForWorld(worldPos, viewport, screenZoom);
        const auto now = std::chrono::steady_clock::now();

        if (!m_autoSnapSpeedSampleValid) {
            m_autoSnapLastScreenPos = screenPos;
            m_autoSnapLastSpeedTime = now;
            m_autoSnapSpeedSampleValid = true;
            return true;
        }

        const float dt = std::chrono::duration<float>(now - m_autoSnapLastSpeedTime).count();
        const Vector2 delta { screenPos.x - m_autoSnapLastScreenPos.x,
            screenPos.y - m_autoSnapLastScreenPos.y };
        const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);

        m_autoSnapLastScreenPos = screenPos;
        m_autoSnapLastSpeedTime = now;

        if (dt <= 0.001f) {
            return dist <= 1.0f;
        }

        return (dist / dt) <= SHIFT_MOVE_AXIS_SWITCH_MAX_SPEED_SCREEN_PX_PER_SEC;
    }

    void resetMoveAxisGuideState()
    {
        m_moveAxisGuideActive = false;
        m_moveGuideOpacity = 0.f;
        m_moveGuideOpacityTarget = 0.f;
        m_moveGuideDisplayAngle = 0.f;
        m_moveGuideTargetAngle = 0.f;
        clearAutoSnapGuide();
        resetAutoSnapSpeedGate();
    }

    void resetMoveShiftSmoothState()
    {
        m_moveSmoothOffset = { 0.f, 0.f };
        m_moveTargetOffset = { 0.f, 0.f };
        m_moveTranslationAnimActive = false;
        m_wasShiftHeldForMove = false;
        clearShiftMoveReference();
        m_moveShiftLineSlot = ShiftMoveLineSlot::None;
        m_moveShiftLineUnit = { 1.f, 0.f };
        m_moveShiftSpeedSampleValid = false;
    }

    void applyMoveSmoothOffsetToState()
    {
        const Vector2 offset = pixelAlignedMoveOffset(m_moveSmoothOffset);
        if (m_state.hasDeformMesh() && m_dragStartDeformMesh) {
            auto mesh = *m_dragStartDeformMesh;
            for (auto& vertex : mesh.vertices) {
                vertex.target.x += offset.x;
                vertex.target.y += offset.y;
            }
            m_state.deformMesh = mesh;
        } else if (m_state.hasFreeQuad() && m_dragStartQuad) {
            const auto& q0 = *m_dragStartQuad;
            std::array<Vector2, 4> q;
            for (int i = 0; i < 4; ++i) {
                q[i] = { q0[i].x + offset.x, q0[i].y + offset.y };
            }
            m_state.freeCorners = q;
        } else {
            m_state.translation
                = { m_dragStartTranslation.x + offset.x, m_dragStartTranslation.y + offset.y };
        }
    }

    bool stepMoveShiftSmoothing(float /*dt*/, float smoothT)
    {
        if (!m_moveTranslationAnimActive || !m_dragging || m_activeHandle != TransformHandle::Move)
            return false;

        const Vector2 diff = { m_moveTargetOffset.x - m_moveSmoothOffset.x,
            m_moveTargetOffset.y - m_moveSmoothOffset.y };
        const float d2 = diff.x * diff.x + diff.y * diff.y;
        if (d2 <= 1.0e-10f) {
            m_moveSmoothOffset = m_moveTargetOffset;
            applyMoveSmoothOffsetToState();
            if (!m_wasShiftHeldForMove)
                m_moveTranslationAnimActive = false;
            return true;
        }

        m_moveSmoothOffset.x += diff.x * smoothT;
        m_moveSmoothOffset.y += diff.y * smoothT;
        applyMoveSmoothOffsetToState();
        return true;
    }

    bool stepMoveAxisGuideAnimation(float /*dt*/, float smoothT, float opacityT)
    {
        bool changed = false;
        const float oDiff = m_moveGuideOpacityTarget - m_moveGuideOpacity;
        if (std::abs(oDiff) > 0.001f) {
            m_moveGuideOpacity += oDiff * opacityT;
            if (std::abs(m_moveGuideOpacityTarget - m_moveGuideOpacity) < 0.002f)
                m_moveGuideOpacity = m_moveGuideOpacityTarget;
            changed = true;
        }

        if (m_moveGuideOpacity <= 0.001f)
            return changed;

        const float da = normalizeAngleDelta(m_moveGuideTargetAngle - m_moveGuideDisplayAngle);
        if (std::abs(da) <= 0.00015f) {
            if (m_moveGuideDisplayAngle != m_moveGuideTargetAngle) {
                m_moveGuideDisplayAngle = m_moveGuideTargetAngle;
                changed = true;
            }
        } else {
            m_moveGuideDisplayAngle = normalizeAngleDelta(m_moveGuideDisplayAngle + da * smoothT);
            changed = true;
        }
        return changed;
    }

    void updateMoveAxisGuideForShiftMove(Qt::KeyboardModifiers mods)
    {
        if (m_activeHandle != TransformHandle::Move) {
            m_moveShiftLineSlot = ShiftMoveLineSlot::None;
            clearShiftMoveReference();
            m_moveAxisGuideActive = false;
            m_moveGuideOpacityTarget = 0.f;
            return;
        }
        if (mods & Qt::ShiftModifier) {
            m_moveAxisGuideOrigin = shiftMoveReferenceWorld();
            m_moveGuideTargetAngle = std::atan2(m_moveShiftLineUnit.y, m_moveShiftLineUnit.x);
            if (m_moveGuideOpacity < 0.02f)
                m_moveGuideDisplayAngle = m_moveGuideTargetAngle;
            m_moveAxisGuideActive = true;
            m_moveGuideOpacityTarget = 1.f;
        } else {
            m_moveShiftLineSlot = ShiftMoveLineSlot::None;
            m_moveShiftSpeedSampleValid = false;
            clearShiftMoveReference();
            m_moveAxisGuideActive = false;
            m_moveGuideOpacityTarget = 0.f;
        }
    }

    bool stepFreeRotateAnimation(float smoothT)
    {
        if (!m_freeRotateAnimationActive || !m_freeRotateAnimationBaseQuad)
            return false;

        const float delta = normalizeAngleDelta(m_freeRotateTargetAngle - m_freeRotateDisplayAngle);
        if (std::abs(delta) <= 0.0001f) {
            m_freeRotateDisplayAngle = m_freeRotateTargetAngle;
            applyFreeRotateAnimationAngle(m_freeRotateDisplayAngle);
            m_freeRotateAnimationActive = false;
            m_freeRotateAnimationBaseQuad.reset();
            return true;
        }

        const float nextAngle = m_freeRotateDisplayAngle + delta * smoothT;
        if (std::abs(normalizeAngleDelta(m_freeRotateTargetAngle - nextAngle)) <= 0.0005f) {
            m_freeRotateDisplayAngle = m_freeRotateTargetAngle;
            applyFreeRotateAnimationAngle(m_freeRotateDisplayAngle);
            m_freeRotateAnimationActive = false;
            m_freeRotateAnimationBaseQuad.reset();
        } else {
            m_freeRotateDisplayAngle = nextAngle;
            applyFreeRotateAnimationAngle(m_freeRotateDisplayAngle);
        }
        return true;
    }

    void applyFreeRotateAnimationAngle(float angle)
    {
        if (!m_freeRotateAnimationBaseQuad)
            return;
        m_state.freeCorners
            = rotatedQuad(*m_freeRotateAnimationBaseQuad, m_freeRotateAnimationCenter, angle);
    }

    static float snapRotation(float angle)
    {
        return std::round(angle / ROTATION_SNAP_STEP_RADIANS) * ROTATION_SNAP_STEP_RADIANS;
    }

    static std::array<Vector2, 4> rotatedQuad(
        const std::array<Vector2, 4>& q0, const Vector2& center, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        std::array<Vector2, 4> q;
        for (int i = 0; i < 4; ++i) {
            const float dx = q0[i].x - center.x;
            const float dy = q0[i].y - center.y;
            q[i].x = center.x + dx * c - dy * s;
            q[i].y = center.y + dx * s + dy * c;
        }
        return q;
    }

    static float easeOutCubic(float t)
    {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const float inv = 1.0f - clamped;
        return 1.0f - inv * inv * inv;
    }

    static float interpolateScaleComponent(float start, float target, float t)
    {
        constexpr float kMinMagnitude = 0.001f;

        const float startSign = (start < 0.0f) ? -1.0f : 1.0f;
        const float targetSign = (target < 0.0f) ? -1.0f : 1.0f;
        const float startAbs = std::max(std::abs(start), kMinMagnitude);
        const float targetAbs = std::max(std::abs(target), kMinMagnitude);

        if (startSign == targetSign) {
            return startSign * (startAbs + (targetAbs - startAbs) * t);
        }

        if (t < 0.5f) {
            const float localT = t * 2.0f;
            const float magnitude = startAbs + (kMinMagnitude - startAbs) * localT;
            return startSign * magnitude;
        }

        const float localT = (t - 0.5f) * 2.0f;
        const float magnitude = kMinMagnitude + (targetAbs - kMinMagnitude) * localT;
        return targetSign * magnitude;
    }

    bool animateScaleTo(const Vector2& targetScale)
    {
        if (!m_active || m_state.hasFreeQuad() || m_state.hasDeformMesh())
            return false;

        Vector2 normalizedTarget = targetScale;
        clampScale(normalizedTarget.x);
        clampScale(normalizedTarget.y);

        if (std::abs(m_state.scale.x - normalizedTarget.x) < 0.0001f
            && std::abs(m_state.scale.y - normalizedTarget.y) < 0.0001f) {
            m_state.scale = normalizedTarget;
            m_scaleAnimationStartScale = normalizedTarget;
            m_targetScale = normalizedTarget;
            m_scaleAnimationElapsed = 0.0f;
            m_scaleAnimationActive = false;
            return false;
        }

        m_scaleAnimationStartScale = m_state.scale;
        m_targetScale = normalizedTarget;
        m_scaleAnimationElapsed = 0.0f;
        m_scaleAnimationActive = true;
        return true;
    }

    static Vector2 quadCenter(const std::array<Vector2, 4>& q)
    {
        return { (q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f,
            (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f };
    }

    /// Compute local X/Y axes of a quad (average of opposite edge pairs)
    static std::pair<Vector2, Vector2> quadLocalAxes(const std::array<Vector2, 4>& q)
    {
        // X axis: average of top edge (0→1) and bottom edge (3→2)
        Vector2 axX = { ((q[1].x - q[0].x) + (q[2].x - q[3].x)) * 0.5f,
            ((q[1].y - q[0].y) + (q[2].y - q[3].y)) * 0.5f };
        // Y axis: average of left edge (0→3) and right edge (1→2)
        Vector2 axY = { ((q[3].x - q[0].x) + (q[2].x - q[1].x)) * 0.5f,
            ((q[3].y - q[0].y) + (q[2].y - q[1].y)) * 0.5f };
        return { normalized(axX), normalized(axY) };
    }

    /// Scale quad around anchor along two arbitrary axes
    static std::array<Vector2, 4> scaleQuadAlongAxes(const std::array<Vector2, 4>& q0,
        const Vector2& anchor, const Vector2& axisA, const Vector2& axisB, float scaleA,
        float scaleB)
    {
        std::array<Vector2, 4> q;
        const float det = axisA.x * axisB.y - axisA.y * axisB.x;
        const bool validBasis = std::abs(det) > 0.0001f;
        for (int k = 0; k < 4; ++k) {
            Vector2 d = { q0[k].x - anchor.x, q0[k].y - anchor.y };
            float coeffA = 0.0f;
            float coeffB = 0.0f;
            if (validBasis) {
                coeffA = (d.x * axisB.y - d.y * axisB.x) / det;
                coeffB = (axisA.x * d.y - axisA.y * d.x) / det;
            } else {
                coeffA = d.x * axisA.x + d.y * axisA.y;
                coeffB = d.x * axisB.x + d.y * axisB.y;
            }
            q[k] = { anchor.x + coeffA * scaleA * axisA.x + coeffB * scaleB * axisB.x,
                anchor.y + coeffA * scaleA * axisA.y + coeffB * scaleB * axisB.y };
        }
        return q;
    }

    /// Scale quad uniformly around anchor
    static std::array<Vector2, 4> scaleQuadUniform(
        const std::array<Vector2, 4>& q0, const Vector2& anchor, float s)
    {
        std::array<Vector2, 4> q;
        for (int k = 0; k < 4; ++k) {
            q[k] = { anchor.x + (q0[k].x - anchor.x) * s, anchor.y + (q0[k].y - anchor.y) * s };
        }
        return q;
    }

private:
    bool m_active = false;
    QUuid m_layerId;
    TileGrid* m_grid = nullptr;

    TransformState m_state;

    // Before-snapshot for undo
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> m_beforeSnapshot;
    std::unordered_set<TileKey, TileKeyHash> m_beforeKeys;

    // Drag state
    bool m_dragging = false;
    TransformHandle m_activeHandle = TransformHandle::None;
    bool m_classicCornerRotateFromIcon = false;
    int m_activeDeformPoint = -1;
    // Region drag inside the deform mesh: parameter-space basis weights captured
    // at press, used to warp control points with a distance falloff (Photoshop
    // Warp) so nearest points move most and far points stay effectively fixed.
    bool m_deformRegionActive = false;
    std::vector<float> m_deformRegionWeights;
    bool m_ctrlFreeDragActive = false;
    bool m_freeSideShiftHeld = false;
    Vector2 m_dragStartWorld;
    Vector2 m_dragPrevWorld; // For relative side drag (Ctrl+side)
    std::optional<std::array<Vector2, 4>> m_dragStartQuad; // For free-form rotate
    std::optional<TransformState::DeformMesh> m_dragStartDeformMesh;
    bool m_wasAltHeld = false; // Track Alt for rollback when released mid-drag
    Vector2 m_dragStartTranslation;
    Vector2 m_dragStartScale;
    Vector2 m_dragStartContentCenter;
    float m_dragStartRotation = 0.0f;
    float m_targetRotation = 0.0f;
    bool m_rotationAnimationActive = false;
    std::optional<std::array<Vector2, 4>> m_freeRotateAnimationBaseQuad;
    Vector2 m_freeRotateAnimationCenter {};
    float m_freeRotateDisplayAngle = 0.0f;
    float m_freeRotateTargetAngle = 0.0f;
    bool m_freeRotateAnimationActive = false;
    Vector2 m_scaleAnimationStartScale { 1.0f, 1.0f };
    Vector2 m_targetScale { 1.0f, 1.0f };
    float m_scaleAnimationElapsed = 0.0f;
    bool m_scaleAnimationActive = false;
    TransformInteractionMode m_interactionMode = TransformInteractionMode::Classic;
    const TileGrid* m_selectionMask = nullptr;
    bool m_moveOnly = false;

    /// World position of pivot at `enter()`; kept as a fallback for older transform entry alignment
    /// flows.
    Vector2 m_transformModeEntryReferenceWorld {};

    /// Shift-move: reference captured from the content center at the start of the current drag.
    Vector2 m_moveShiftReferenceWorld {};
    bool m_moveShiftReferenceValid = false;

    /// Shift-move: one of two lines through the active reference (H / V), switched by invisible
    /// trigger zones.
    ShiftMoveLineSlot m_moveShiftLineSlot = ShiftMoveLineSlot::None;
    Vector2 m_moveShiftLineUnit { 1.f, 0.f };

    bool m_moveAxisGuideActive = false;
    Vector2 m_moveAxisGuideOrigin {};

    Vector2 m_moveSmoothOffset { 0.f, 0.f };
    Vector2 m_moveTargetOffset { 0.f, 0.f };
    bool m_moveTranslationAnimActive = false;
    bool m_wasShiftHeldForMove = false;
    bool m_moveShiftSpeedSampleValid = false;
    Vector2 m_moveShiftLastScreenPos {};
    std::chrono::steady_clock::time_point m_moveShiftLastSpeedTime {};

    bool m_autoSnapSpeedSampleValid = false;
    Vector2 m_autoSnapLastScreenPos {};
    std::chrono::steady_clock::time_point m_autoSnapLastSpeedTime {};

    float m_moveGuideTargetAngle = 0.f;
    float m_moveGuideDisplayAngle = 0.f;
    float m_moveGuideOpacity = 0.f;
    float m_moveGuideOpacityTarget = 0.f;
    TransformAutoSnapGuideState m_autoSnapGuide;
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMCONTROLLER_H
