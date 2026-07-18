// SPDX-License-Identifier: MPL-2.0

// LayerListView.h
#ifndef RUWA_UI_WIDGETS_LAYERSYSTEM_LAYERLISTVIEW_H
#define RUWA_UI_WIDGETS_LAYERSYSTEM_LAYERLISTVIEW_H

#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"

#include <QWidget>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QVariantAnimation>
#include <QParallelAnimationGroup>
#include <QSize>
#include <QTimer>

class QImage;
template <typename T> class QFutureWatcher;

namespace ruwa::ui::widgets {

class LayerRowWidget;
class AnimatedListLayout;
class ListDragDrop;
class DropIndicatorWidget;
class SmoothScrollArea;

/**
 * @brief Main layer list view widget.
 *
 * Manages:
 *   - Pool of LayerRowWidget instances
 *   - Sync with LayerModel
 *   - Animated layout via AnimatedListLayout
 *   - Drag & drop via ListDragDrop
 *   - Group collapse/expand with clip-mask animation
 *   - Scrolling via SmoothScrollArea
 *
 * This widget owns a SmoothScrollArea internally.
 * Set it as a regular widget in your layout.
 */
class LayerListView : public QWidget {
    Q_OBJECT

public:
    explicit LayerListView(QWidget* parent = nullptr);
    ~LayerListView() override;

    // === Model ===
    void setModel(ruwa::core::layers::LayerModel* model);
    void setInsertAnimationsEnabled(bool enabled) { m_insertAnimationsEnabled = enabled; }
    void setCanvasSize(const QSize& size);
    void setDisplayFrame(const QRect& frame);
    void refreshThumbnailPreviews();
    void invalidateVisibleThumbnails();
    void invalidateThumbnails(
        const QList<ruwa::core::layers::LayerId>& ids, bool prioritize = true);
    void setThumbnailLoadingMode(bool active);
    void setForcedThumbnailLoadingLayer(const ruwa::core::layers::LayerId& id);
    bool isThumbnailLoadingMode() const { return m_thumbnailLoadingMode; }
    ruwa::core::layers::LayerModel* model() const { return m_model; }

    /** @brief Called by LayersPanel when drop is rejected (invalid move). Triggers ghost fade-out.
     */
    void setDropRejected(bool rejected) { m_dropRejected = rejected; }

signals:
    void layerSelected(const ruwa::core::layers::LayerId& id, Qt::KeyboardModifiers mods);
    void layerPaintTargetSelected(
        const ruwa::core::layers::LayerId& id, bool maskTarget, Qt::KeyboardModifiers mods);
    void layerContentSelectionRequested(const ruwa::core::layers::LayerId& id);
    void layerTextEditRequested(const ruwa::core::layers::LayerId& id);
    void layerExpandToggled(const ruwa::core::layers::LayerId& id);
    void layerVisibilityToggled(const ruwa::core::layers::LayerId& id);
    void layerDragDropped(
        const ruwa::core::layers::LayerId& id, int dropInsertIndex, int targetDepth);
    void layerDragCopyDropped(
        const ruwa::core::layers::LayerId& id, int dropInsertIndex, int targetDepth);
    void layerRenamed(const ruwa::core::layers::LayerId& id, const QString& newName);
    void clipSelectionRequested(const ruwa::core::layers::LayerId& baseLayerId);
    void clipSwipeRequested(const ruwa::core::layers::LayerId& baseLayerId, bool leftToRight);
    void layerAlphaLockClicked(const ruwa::core::layers::LayerId& id);
    void layerLockClicked(const ruwa::core::layers::LayerId& id);
    void layerDuplicateRequested(const ruwa::core::layers::LayerId& id);
    void layerDeleteRequested(const ruwa::core::layers::LayerId& id);
    void layerQuickClippingMaskRequested(const ruwa::core::layers::LayerId& id);
    void layerClearPixelsRequested(const ruwa::core::layers::LayerId& id);
    void layerRasterizeSmartRequested(const ruwa::core::layers::LayerId& id);
    void layerApplyMaskRequested(const ruwa::core::layers::LayerId& id);
    void layerInvertMaskRequested(const ruwa::core::layers::LayerId& id);
    void layerApplyEffectsRequested(const ruwa::core::layers::LayerId& id);
    void layerToggleAlphaLockRequested(const ruwa::core::layers::LayerId& id);
    void layerToggleLockRequested(const ruwa::core::layers::LayerId& id);

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    // Model signals
    void onLayersChanged();
    void onSelectionChanged(const ruwa::core::layers::LayerId& primaryId);
    void onLayerSelectionStateChanged(const ruwa::core::layers::LayerId& id, bool selected);
    void onGroupExpansionChanged(const ruwa::core::layers::LayerId& id, bool expanded);

    // Row signals
    void onRowClicked(const ruwa::core::layers::LayerId& id, Qt::KeyboardModifiers mods);
    void onRowPaintTargetClicked(
        const ruwa::core::layers::LayerId& id, bool maskTarget, Qt::KeyboardModifiers mods);
    void onRowThumbnailCtrlClicked(const ruwa::core::layers::LayerId& id);
    void onRowTextEditRequested(const ruwa::core::layers::LayerId& id);
    void onRowDoubleClicked(const ruwa::core::layers::LayerId& id);
    void onRowExpandToggled(const ruwa::core::layers::LayerId& id);
    void onRowVisibilityToggled(const ruwa::core::layers::LayerId& id);
    void onRowDragInitiated(const ruwa::core::layers::LayerId& id, const QPoint& globalPos);
    void onRowRenameFinished(const ruwa::core::layers::LayerId& id, const QString& newName);
    void onRowEyePressed(const ruwa::core::layers::LayerId& id, bool wasVisible);
    void onRowAlphaLockClicked(const ruwa::core::layers::LayerId& id);
    void onRowLockClicked(const ruwa::core::layers::LayerId& id);
    void onRowClipSwipeRequested(const ruwa::core::layers::LayerId& id, bool leftToRight);
    void onRowRightExpandDuplicateClicked(const ruwa::core::layers::LayerId& id);
    void onRowRightExpandDeleteClicked(const ruwa::core::layers::LayerId& id);
    void onRowQuickClippingMaskRequested(const ruwa::core::layers::LayerId& id);
    void onRowClearPixelsRequested(const ruwa::core::layers::LayerId& id);
    void onRowRasterizeSmartRequested(const ruwa::core::layers::LayerId& id);
    void onRowApplyMaskRequested(const ruwa::core::layers::LayerId& id);
    void onRowInvertMaskRequested(const ruwa::core::layers::LayerId& id);
    void onRowApplyEffectsRequested(const ruwa::core::layers::LayerId& id);
    void onRowToggleAlphaLockRequested(const ruwa::core::layers::LayerId& id);
    void onRowToggleLockRequested(const ruwa::core::layers::LayerId& id);

    // Drag signals
    void onDragCompleted(const ruwa::core::layers::LayerId& movedId, int dropInsertIndex);
    void onDragCancelled();

    // New drag flow: source collapse + ghost settle
    void onSourceRowCollapseRequested(const ruwa::core::layers::LayerId& sourceId);
    void onGhostSettled(
        const ruwa::core::layers::LayerId& movedId, int dropInsertIndex, int targetDepth);

    // Multi-drag flow
    void onMultiDragCompleted(const ruwa::core::layers::LayerId& sourceId,
        const QSet<ruwa::core::layers::LayerId>& draggedIds, int dropInsertIndex, int targetDepth);

    // Content height
    void onContentHeightChanged(int newHeight);
    void onLayerAboutToBeRemoved(const ruwa::core::layers::LayerId& id);

private:
    void rebuildRowWidgets();
    void syncSelectionState();
    void connectRowSignals(LayerRowWidget* row);
    void scheduleThumbnailBatch();
    void processThumbnailBatch();
    void applyThumbnailRenderResult();
    void enqueueThumbnail(const ruwa::core::layers::LayerId& id, bool prioritize);
    void queueThumbnailsForActiveRows();
    bool isRowInViewport(const LayerRowWidget* row) const;
    bool shouldShowThumbnailLoading(const ruwa::core::layers::LayerData* data) const;

    LayerRowWidget* getOrCreateRow();
    void recycleRow(LayerRowWidget* row);

    // Collapse animation
    void animateGroupCollapse(const ruwa::core::layers::LayerId& groupId);
    void animateGroupExpand(const ruwa::core::layers::LayerId& groupId);

    // Source row collapse for drag (handles multiple rows for group drag)
    void animateSourceRowCollapse(const QList<LayerRowWidget*>& rows);
    void animateCopyDragSourceRestore();
    int draggedRowsGapHeight(const QSet<ruwa::core::layers::LayerId>& ids) const;

    // Multi-drag two-phase animation (collapse at source, expand at target)
    void animateMultiDragCollapse(const QSet<ruwa::core::layers::LayerId>& draggedIds,
        const ruwa::core::layers::LayerId& sourceId, int dropInsertIndex, int targetDepth,
        bool layoutHandlesSlide = false);
    void animateMultiDragExpand(const QSet<ruwa::core::layers::LayerId>& movedIds,
        const QHash<ruwa::core::layers::LayerId, int>& prePositions, int preContentH);

    // Force content widget height to match layout
    void syncContentHeight();
    void forceContentHeightImmediate(int height);

    // New layer creation animation (expand from height 0)
    void animateNewLayerInsert(const QSet<ruwa::core::layers::LayerId>& newIds,
        const QHash<ruwa::core::layers::LayerId, int>& oldPositions, int oldContentH);

    // Layer removal animation (shrink + fade out)
    void animateLayerRemoval(const QSet<ruwa::core::layers::LayerId>& removedIds);

    // Mouse forwarding for drag
    void setupMouseTracking();

    // Eye swipe helper
    LayerRowWidget* rowAtGlobalPos(const QPoint& globalPos) const;

    // Clipping preview helper
    bool resolveClipPreviewTarget(const ruwa::core::layers::LayerId& hoveredId, int* outInsertIndex,
        ruwa::core::layers::LayerId* outBaseLayerId) const;
    void updateClipPreviewAt(const QPoint& globalPos);
    void clearClipPreview();

private:
    ruwa::core::layers::LayerModel* m_model = nullptr;

    // Scroll
    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;

    // Layout
    AnimatedListLayout* m_layout = nullptr;

    // Drag & drop
    ListDragDrop* m_dragDrop = nullptr;

    // Active row widgets (in display order)
    QList<LayerRowWidget*> m_activeRows;

    // Pool of recycled row widgets
    QList<LayerRowWidget*> m_rowPool;

    // Map layerId -> active row widget for fast lookup
    QHash<ruwa::core::layers::LayerId, LayerRowWidget*> m_rowMap;

    // Deferred thumbnail generation
    QTimer m_thumbnailBatchTimer;
    QList<ruwa::core::layers::LayerId> m_thumbnailQueue;
    QSet<ruwa::core::layers::LayerId> m_thumbnailQueuedIds;
    QHash<ruwa::core::layers::LayerId, quint64> m_thumbnailGenerations;
    bool m_thumbnailLoadingMode = false;
    ruwa::core::layers::LayerId m_forcedThumbnailLoadingLayerId;
    QFutureWatcher<QImage>* m_thumbnailRenderWatcher = nullptr;
    ruwa::core::layers::LayerId m_thumbnailRenderingLayerId;
    quint64 m_thumbnailRenderingGeneration = 0;
    quint64 m_nextThumbnailGeneration = 1;

    // Flag to prevent re-entrant rebuilds
    bool m_rebuilding = false;

    // Flag to track if rebuild happened during drag completion
    bool m_rebuildPending = false;

    // Skip animation on next rebuild (used after drag-drop settle)
    bool m_skipNextAnimation = false;
    bool m_insertAnimationsEnabled = true;

    // True while drag-end settle is in progress (blocks new interactions)
    bool m_settlingDrag = false;
    bool m_deferCopyDragRebuild = false;

    // Animated content height
    QVariantAnimation* m_heightAnim = nullptr;
    int m_targetContentHeight = 0; // Target height (for correct comparison during animation)

    // Drag active flag (for event filter)
    bool m_dragActive = false;
    bool m_copyDragActive = false;

    // Eye swipe state
    bool m_eyeSwiping = false;
    bool m_eyeSwipeToVisible = false; // true = making layers visible, false = hiding
    QSet<ruwa::core::layers::LayerId> m_eyeSwipedIds;

    // Source row collapse animation during drag
    QPointer<QVariantAnimation> m_collapseAnim;
    QPointer<QParallelAnimationGroup> m_copyRestoreAnim;

    // IDs to place instantly (no animation) on next rebuild
    QSet<ruwa::core::layers::LayerId> m_instantPlaceIds;

    // Group expand/collapse animation
    bool m_groupAnimating = false;
    QPointer<QParallelAnimationGroup> m_groupAnim;

    // New layer creation animation
    bool m_creationAnimating = false;
    QPointer<QParallelAnimationGroup> m_creationAnim;

    // Layer removal animation
    bool m_deleteAnimating = false;
    QPointer<QParallelAnimationGroup> m_deleteAnim;

    // Multi-drag animation state
    bool m_multiDragAnimating = false;
    QPointer<QParallelAnimationGroup> m_multiDragAnim;

    // Pending data for the ghost-settle phase of multi-drag
    ruwa::core::layers::LayerId m_multiDragPendingSourceId;
    QSet<ruwa::core::layers::LayerId> m_multiDragPendingDragIds;
    int m_multiDragPendingDropIdx = -1;
    int m_multiDragPendingDepth = 0;

    // Alt+click clipping preview
    bool m_clipPreviewActive = false;
    int m_clipPreviewInsertIndex = -1;
    ruwa::core::layers::LayerId m_clipPreviewBaseLayerId;
    QPointer<DropIndicatorWidget> m_clipPreviewIndicator;
    bool m_clipPreviewFilterInstalled = false;

    QRect m_displayFrame;

    // Set by LayersPanel when drop is invalid; checked in onGhostSettled to use fade animation
    bool m_dropRejected = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LAYERSYSTEM_LAYERLISTVIEW_H
