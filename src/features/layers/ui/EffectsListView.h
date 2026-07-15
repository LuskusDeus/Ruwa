// SPDX-License-Identifier: MPL-2.0

// EffectsListView.h
#ifndef RUWA_UI_WORKSPACE_EFFECTSLISTVIEW_H
#define RUWA_UI_WORKSPACE_EFFECTSLISTVIEW_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QWidget>

class QParallelAnimationGroup;
class QVariantAnimation;

namespace ruwa::ui::widgets {
class SmoothScrollArea;
class AnimatedListLayout;
class ListDragDrop;
class ReorderableRowWidget;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

/**
 * @brief Compact animated, drag-reorderable list view for the effects panel.
 *
 * A flat-list counterpart to LayerListView built on the same reusable engine
 * (AnimatedListLayout + ListDragDrop). It owns a SmoothScrollArea and positions
 * caller-supplied rows (EffectCards) with slide animations, lets the user
 * reorder them by dragging the card grip, and animates insert/remove.
 *
 * Rows are persistent: the panel reuses the same row object for an id across
 * refreshes and passes the current ordered set to syncRows(), which diffs it
 * against what is shown to drive the animations. The panel owns creation; the
 * view deletes rows it is told were removed once their exit animation ends.
 */
class EffectsListView : public QWidget {
    Q_OBJECT

public:
    explicit EffectsListView(QWidget* parent = nullptr);
    ~EffectsListView() override;

    /**
     * @brief Reconcile the displayed rows to a new ordered set.
     * @param orderedRows  Desired visible rows, in order (persistent objects).
     * @param newIds       Ids in orderedRows that were just created (reveal-in).
     * @param removedRows   Rows removed from the model (roll-up then deleted by
     *                      the view). Rows that vanish only because of filtering
     *                      (absent from orderedRows but not listed here) are just
     *                      hidden, not deleted.
     * @param animate      Whether to animate the transition.
     */
    void syncRows(const QList<ruwa::ui::widgets::ReorderableRowWidget*>& orderedRows,
        const QSet<QUuid>& newIds,
        const QList<ruwa::ui::widgets::ReorderableRowWidget*>& removedRows, bool animate);

public slots:
    /// Begin a reorder drag for the row with the given id (wired to a row's
    /// dragInitiated signal).
    void beginDrag(const QUuid& id, const QPoint& globalPos);

signals:
    /// Emitted when a drag settles on a new position. newIndex is the final
    /// index in the list after the moved item is removed and re-inserted.
    void reordered(const QUuid& movedId, int newIndex);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onContentHeightChanged(int newHeight);
    void onSourceRowCollapseRequested(const QUuid& sourceId);
    void onGhostSettled(const QUuid& movedId, int dropInsertIndex, int targetDepth);
    void onDragCancelled();

private:
    ruwa::ui::widgets::ReorderableRowWidget* rowById(const QUuid& id) const;
    int indexOfRow(const QUuid& id) const;
    QList<QPair<QUuid, ruwa::ui::widgets::ReorderableRowWidget*>> buildEntries() const;
    void relayout(bool animate);
    void stopSyncAnim();
    void endDragSession();

private:
    ruwa::ui::widgets::SmoothScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    ruwa::ui::widgets::AnimatedListLayout* m_layout = nullptr;
    ruwa::ui::widgets::ListDragDrop* m_dragDrop = nullptr;

    QList<ruwa::ui::widgets::ReorderableRowWidget*> m_rows;

    bool m_dragActive = false;
    bool m_dragSettling = false; // release→settle window: layout height changes are ignored
    bool m_dragCursorOverride = false;
    bool m_animating = false;
    bool m_skipNextAnim = false; // set after a drag settle: commit snaps, not slides
    QUuid m_draggingId;

    QPointer<QParallelAnimationGroup> m_syncAnim;
    QPointer<QVariantAnimation> m_sourceCollapseAnim;
    QPointer<QWidget> m_sourceSnapshot; // static fade-out stand-in for the drag source
    QList<QPointer<ruwa::ui::widgets::ReorderableRowWidget>> m_rowsToDelete;
    int m_targetContentHeight = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EFFECTSLISTVIEW_H
