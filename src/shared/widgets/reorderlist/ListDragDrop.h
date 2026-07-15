// SPDX-License-Identifier: MPL-2.0

// ListDragDrop.h
#ifndef RUWA_SHARED_WIDGETS_REORDERLIST_LISTDRAGDROP_H
#define RUWA_SHARED_WIDGETS_REORDERLIST_LISTDRAGDROP_H

#include <QObject>
#include <QWidget>
#include <QPoint>
#include <QPointF>
#include <QPropertyAnimation>
#include <QPointer>
#include <QList>
#include <QSet>
#include <QUuid>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>

#include <functional>

namespace ruwa::ui::widgets {

class ReorderableRowWidget;
class AnimatedListLayout;

/**
 * @brief Ghost widget shown during drag.
 *
 * For single drag: renders a snapshot of the dragged row.
 * For multi-drag: renders "Dragging N layers" label, then morphs
 * into individual row snapshots via animateToSnapshots().
 */
class DragGhostWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal ghostOpacity READ ghostOpacity WRITE setGhostOpacity)
    Q_PROPERTY(qreal ghostScale READ ghostScale WRITE setGhostScale)
    Q_PROPERTY(qreal ghostRotation READ ghostRotation WRITE setGhostRotation)
    Q_PROPERTY(qreal morphProgress READ morphProgress WRITE setMorphProgress)
    Q_PROPERTY(qreal backdropOpacity READ backdropOpacity WRITE setBackdropOpacity)

public:
    explicit DragGhostWidget(QWidget* parent = nullptr);

    void setSnapshot(const QPixmap& pixmap);
    /** @param width Layer row width (viewport width); -1 = use default pill width */
    void setMultiDragCount(int count, int width = -1);
    QSize visualContentSize() const { return m_visualContentSize; }
    void setVisualContentSize(const QSize& size);
    QPoint contentTopLeft() const;

    qreal ghostOpacity() const { return m_opacity; }
    void setGhostOpacity(qreal v);
    qreal ghostScale() const { return m_scale; }
    void setGhostScale(qreal v);
    qreal ghostRotation() const { return m_rotation; }
    void setGhostRotation(qreal v);

    // --- Multi-drag morph to rows ---

    /**
     * @brief Provide individual row snapshots (top-to-bottom order) and their
     *        target Y-positions in the parent (topLevel window) coordinates.
     *        Call before the morph animation starts.
     *        Pass empty targetYsInParent to update Ys only (snapshots kept).
     */
    void setRowSnapshots(const QList<QPixmap>& snapshots, const QList<int>& targetYsInParent);

    /** Returns the stored row snapshots. */
    const QList<QPixmap>& rowSnapshots() const { return m_rowSnapshots; }

    /**
     * @brief Per-row flags: if true, row skips fly-from-pill and starts at target.
     *        Used when layer is already in correct Y position (depth-only change).
     */
    void setRowSkipPositioning(const QList<bool>& skip);

    /**
     * @brief morphProgress in [0..1]: 0 = pill label, 1 = fully spread rows.
     */
    qreal morphProgress() const { return m_morphProgress; }
    void setMorphProgress(qreal v);

    /** True once setRowSnapshots() has been called. */
    bool hasMorphData() const { return !m_rowSnapshots.isEmpty(); }

    /**
     * @brief Set the pre-blurred backdrop pixmap (full parent-window grab, blurred).
     *        Painted under the snapshot for a "frosted glass" effect.
     *        The pixmap is in parent (topLevel window) coordinates; the ghost
     *        samples the region that corresponds to its current position.
     */
    void setBlurredBackdrop(const QPixmap& fullParentBlurred);

    qreal backdropOpacity() const { return m_backdropOpacity; }
    void setBackdropOpacity(qreal v);

signals:
    void morphFinished();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int visualPadding() const;

    QPixmap m_snapshot;
    QSize m_visualContentSize;
    int m_multiCount = 0;
    qreal m_opacity = 0.85;
    qreal m_scale = 1.0;
    qreal m_rotation = 0.0;

    // Morph data
    QList<QPixmap> m_rowSnapshots;
    QList<int> m_targetYsInParent; // target Y of each snapshot in parent widget coords
    QList<bool> m_rowSkipPositioning; // if true, row starts at target (no fly from pill)
    qreal m_morphProgress = 0.0;

    // Frosted-glass backdrop
    QPixmap m_blurredBackdrop; // full topLevel window pixmap, blurred
    qreal m_backdropOpacity = 0.0;
};

/**
 * @brief Drop indicator line widget.
 */
class DropIndicatorWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal indicatorOpacity READ indicatorOpacity WRITE setIndicatorOpacity)

public:
    enum class Style { DragLine, ClippingBlock };

    explicit DropIndicatorWidget(QWidget* parent = nullptr);

    void setIndentLevel(int depth);
    int indentLevel() const { return m_depth; }
    void setStyle(Style style);
    void setIndicatorIcon(const QPixmap& pixmap);

    /**
     * @brief Per-level indent width and left padding used to position the line.
     * Flat lists can leave depth at 0 so these have no visible effect.
     * Values are in unscaled (logical) pixels; scaled via ThemeManager at paint.
     */
    void setIndentMetrics(int indentPerLevel, int basePad);

    qreal indicatorOpacity() const { return m_opacity; }
    void setIndicatorOpacity(qreal v);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int m_depth = 0;
    qreal m_opacity = 0.0;
    Style m_style = Style::DragLine;
    QPixmap m_icon;
    int m_indentPerLevel = 20;
    int m_basePad = 6;
};

/**
 * @brief Manages the full drag & drop lifecycle for layer rows.
 *
 * Phases:
 *   1. Drag start: creates ghost, marks source row as dragging
 *   2. Drag move: updates ghost position, calculates drop target,
 *      shows indicator line, shifts rows via layout gap
 *   3. Drag end: animates ghost to target position, then commits move
 *   4. Cancel: animates ghost back to source, restores state
 *
 * Supports multi-drag (drags all selected layers).
 */
class ListDragDrop : public QObject {
    Q_OBJECT

public:
    explicit ListDragDrop(QWidget* viewport, AnimatedListLayout* layout, QObject* parent = nullptr);
    ~ListDragDrop() override;

    bool isDragging() const { return m_dragging; }
    bool isMultiDrag() const { return m_draggedIds.size() > 1; }
    void setCopyMode(bool copyMode) { m_copyMode = copyMode; }

    // === Drop-target resolution strategy ===

    /// Result of resolving a raw pointer position into a concrete drop target.
    struct DropResolution {
        int insertIndex = 0; ///< Flat index where the item(s) will be inserted
        int depth = 0; ///< Target nesting depth (0 for flat lists)
    };

    /**
     * @brief Strategy that maps a raw drop position to a concrete target.
     *
     * Flat lists leave this unset: the target is simply the raw insert index at
     * depth 0. Tree lists (layers) install a resolver that clamps the index
     * (e.g. never below a fixed Background row) and derives a nesting depth from
     * the suggested X-depth plus neighbouring rows.
     *
     * @param rawInsertIndex Flat index under the pointer (from the layout).
     * @param suggestedDepth Depth implied by the pointer's X (0 if no indent metrics).
     * @param draggedIds     Items currently being dragged (excluded neighbours).
     */
    using DropResolveFn = std::function<DropResolution(
        int rawInsertIndex, int suggestedDepth, const QSet<QUuid>& draggedIds)>;
    void setDropResolver(DropResolveFn fn) { m_dropResolver = std::move(fn); }

    /// Indent metrics used to derive the suggested depth from the pointer X and
    /// to position the indicator line. Leave at 0 for flat lists (no depth).
    void setIndentMetrics(int indentPerLevel, int basePad)
    {
        m_indentPerLevel = indentPerLevel;
        m_basePad = basePad;
    }

    /// Row height (unscaled) used as a fallback when no rows exist yet.
    void setFallbackRowHeight(int h) { m_fallbackRowHeight = h; }

    /**
     * @brief Start drag operation
     * @param sourceId The primary dragged layer
     * @param allSelectedIds All selected layers (for multi-drag)
     * @param sourceWidget The row widget that initiated drag
     * @param globalPos Starting mouse position
     * @param descendantIds Visible descendants of source (for group drag)
     */
    void startDrag(const QUuid& sourceId, const QSet<QUuid>& allSelectedIds,
        ReorderableRowWidget* sourceWidget, const QPoint& globalPos,
        const QSet<QUuid>& descendantIds = {}, bool copyMode = false);

    /**
     * @brief Update drag position (call from viewport's mouseMoveEvent)
     */
    void updateDrag(const QPoint& globalPos);

    /**
     * @brief Re-evaluate the drop target after the viewport scrolls while dragging.
     *
     * The drag cursor stays at the same screen position, but its coordinates in
     * the scrolling content change.
     */
    void refreshDropTarget(const QPoint& globalPos);

    /**
     * @brief End drag (mouse release) - animate to final position
     */
    void endDrag(const QPoint& globalPos);

    /**
     * @brief Cancel drag - animate back to source
     */
    void cancelDrag();

    /**
     * @brief Current drop insert index (flat index in visible list)
     */
    int dropInsertIndex() const { return m_dropInsertIndex; }

    /**
     * @brief Target nesting depth based on X-coordinate during drag
     */
    int dropTargetDepth() const { return m_dropTargetDepth; }

    /**
     * @brief All IDs that should be excluded from layout (source + descendants)
     */
    QSet<QUuid> allExcludeIds() const;

    /**
     * @brief All directly dragged layer IDs (selected layers, no descendants)
     */
    const QSet<QUuid>& draggedIds() const { return m_draggedIds; }

    /**
     * @brief Destroy ghost and indicator widgets. Called by view after rebuild.
     */
    void destroyGhost();

    /**
     * @brief Animate ghost fade-out and shrink, then destroy. Used when drop is rejected.
     */
    void animateGhostFadeOut();

signals:
    void dragStarted();
    void dragMoved(int dropInsertIndex);
    void dragCompleted(const QUuid& movedId, int dropInsertIndex);
    void dragCancelled();

    /**
     * @brief Emitted when drag ends successfully - source row should collapse.
     * The view should animate the source row height to 0.
     */
    void sourceRowCollapseRequested(const QUuid& sourceId);

    /**
     * @brief Emitted when ghost settle + opacity animation finishes.
     * The view should hide/destroy the ghost and commit the move.
     * @param movedId The layer being moved
     * @param dropInsertIndex Flat index for ghost positioning
     * @param targetDepth Target nesting depth based on X position
     */
    void ghostSettled(const QUuid& movedId, int dropInsertIndex, int targetDepth);

    /**
     * @brief Emitted when multi-drag ends.
     *
     * For the new animation flow the view should:
     *  1. Snapshot each dragged row and call feedMultiDragSnapshots().
     *  2. Call animateMultiGhostSettle() which flies the ghost to target
     *     and morphs it into individual rows.
     *  3. On ghostSettled signal commit the move.
     */
    void multiDragCompleted(
        const QUuid& sourceId, const QSet<QUuid>& draggedIds, int dropInsertIndex, int targetDepth);

public:
    // --- Multi-drag settle helpers (called by LayerListView) ---

    /**
     * @brief Feed individual row snapshots and their pre-move positions so
     *        the ghost can morph into them.
     * @param snapshots  Pixmaps for each dragged row in flat visual order.
     * @param sourceYs   Current Y of each row in the content widget.
     */
    void feedMultiDragSnapshots(const QList<QPixmap>& snapshots, const QList<int>& sourceYs);

    /**
     * @brief Start the multi-drag settle animation.
     *        Ghost flies to target gap, then morphs into row cards.
     *        Emits ghostSettled() when complete.
     * @return true if immediate commit (all layers already in position) — caller should skip
     * collapse.
     */
    bool animateMultiGhostSettle(
        int targetGapY, int rowHeight, int rowSpacing, bool layoutAlreadyApplied = false);

private:
    void createGhost(ReorderableRowWidget* sourceWidget, const QPoint& globalPos);
    QPoint mapGhostTargetPos(const QPoint& globalPos) const;
    void startGhostFollow();
    void stopGhostFollow();
    void tickGhostFollow();
    void updateDropTarget(const QPoint& viewportPos);
    void animateGhostToTarget();
    void animateGhostToSource();
    void onSettleAnimFinished();

    /**
     * @brief Calculate the correct target Y for the ghost after drop.
     * Accounts for the source row collapsing (shift correction).
     */
    int calculateGhostTargetY() const;

    /**
     * @brief Get the flat index of the source row in the current layout.
     */
    int sourceFlatIndex() const;
    int draggedGapHeight() const;

private:
    QWidget* m_viewport = nullptr;
    AnimatedListLayout* m_layout = nullptr;

    bool m_dragging = false;
    bool m_copyMode = false;

    // Drag source info
    QUuid m_sourceId;
    QSet<QUuid> m_draggedIds;
    QSet<QUuid> m_descendantIds; // visible descendants for group drag
    QPoint m_dragOffset; // Offset from ghost top-left to mouse
    QPoint m_sourcePos; // Original position of source row
    int m_sourceFlatIndex = -1; // Flat index of source row at drag start

    // Ghost
    QPointer<DragGhostWidget> m_ghost;
    QPointer<DropIndicatorWidget> m_indicator;
    QTimer m_dragFollowTimer;
    QElapsedTimer m_dragFollowElapsed;
    QPointF m_dragGhostPos;
    QPointF m_dragGhostTargetPos;
    QPointF m_dragGhostVelocity;

    // Drop-target resolution strategy + metrics
    DropResolveFn m_dropResolver;
    int m_indentPerLevel = 0; // 0 = flat list (no depth from X)
    int m_basePad = 0;
    int m_fallbackRowHeight = 36;

    // Drop target
    int m_dropInsertIndex = -1;
    int m_dropTargetDepth = 0;
    QElapsedTimer m_dropTargetThrottle;
    QPoint m_pendingDropViewportPos;
    bool m_hasDropTargetThrottle = false;

    // Settle animation
    QPointer<QPropertyAnimation> m_settleAnim;
    QPointer<QPropertyAnimation> m_ghostOpacityAnim;
    QPointer<QPropertyAnimation> m_morphAnim;
    QPointer<QPropertyAnimation> m_backdropAnim;

    // Multi-drag settle data
    QList<int> m_multiSourceYs; // pre-collapse Y of each dragged row
    int m_multiSettleTargetGapY = 0;
    int m_multiSettleRowHeight = 0;
    int m_multiSettleRowSpacing = 0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_REORDERLIST_LISTDRAGDROP_H
