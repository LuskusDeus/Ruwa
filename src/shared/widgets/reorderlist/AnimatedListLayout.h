// SPDX-License-Identifier: MPL-2.0

// AnimatedListLayout.h
#ifndef RUWA_SHARED_WIDGETS_REORDERLIST_ANIMATEDLISTLAYOUT_H
#define RUWA_SHARED_WIDGETS_REORDERLIST_ANIMATEDLISTLAYOUT_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QRectF>
#include <QPointer>

namespace ruwa::ui::widgets {

class ReorderableRowWidget;

/**
 * @brief Animated position data for a single row
 */
struct RowLayoutEntry {
    QPointer<ReorderableRowWidget> widget;
    QUuid itemId;

    // Current animated position
    qreal currentY = 0;
    qreal targetY = 0;

    // Per-row height (groups are shorter)
    int rowHeight = 36;

    // For clip masking (group collapse)
    qreal clipTop = 0; // -1 = no clip
    qreal clipBottom = 99999;

    bool visible = true; // Should be visible after animation completes
};

/**
 * @brief Custom animated layout manager for a reorderable list.
 *
 * Does NOT use QLayout. Positions widgets manually with animations.
 *
 * Responsibilities:
 *   - Calculate target Y positions from an ordered (id, widget) list
 *   - Animate rows to their target positions
 *   - Manage clip regions for group collapse/expand (tree lists only)
 *   - Provide gap insertion for drag & drop
 *   - Calculate total content height
 *
 * The layout operates on a container widget. It moves child
 * ReorderableRowWidgets to their calculated positions. Rows are identified by
 * a QUuid so the same engine drives layers (LayerId == QUuid) and effects.
 */
class AnimatedListLayout : public QObject {
    Q_OBJECT

public:
    explicit AnimatedListLayout(QWidget* container, QObject* parent = nullptr);
    ~AnimatedListLayout() override;

    // === Configuration ===

    void setRowHeight(int h) { m_rowHeight = h; }
    int rowHeight() const { return m_rowHeight; }

    void setRowSpacing(int s) { m_rowSpacing = s; }
    int rowSpacing() const { return m_rowSpacing; }

    void setAnimationDuration(int ms) { m_animDuration = ms; }
    int animationDuration() const { return m_animDuration; }

    /**
     * @brief Whether per-row heights (from ReorderableRowWidget::effectiveRowHeight)
     * are multiplied by the UI scale factor.
     *
     * Fixed-metric rows (layers: 44/36 logical px) report logical heights and
     * want scaling ON (default). Content-measured rows (effect cards) report
     * heights already in device pixels and must set this OFF to avoid
     * double-scaling. Spacing and drop-gap defaults always scale.
     */
    void setScaleRowHeights(bool scale) { m_scaleRowHeights = scale; }
    bool scaleRowHeights() const { return m_scaleRowHeights; }

    // === Layout Calculation ===

    /**
     * @brief Recalculate all row positions.
     * @param rows Ordered list of (itemId, widget) entries
     * @param animate Whether to animate to new positions
     * @param newEntryIds IDs of newly created entries (fade in instead of slide)
     * @param instantPlaceIds IDs that should be placed instantly at target with no animation at all
     */
    void updateLayout(const QList<QPair<QUuid, ReorderableRowWidget*>>& rows, bool animate = true,
        const QSet<QUuid>& newEntryIds = {}, const QSet<QUuid>& instantPlaceIds = {});

    /**
     * @brief Total content height (for scroll area)
     */
    int contentHeight() const { return m_contentHeight; }

    /**
     * @brief Whether layout animations are currently running
     */
    bool isAnimating() const { return m_animGroup != nullptr; }

    // === Drag & Drop Gap ===

    /**
     * @brief Insert an animated gap at the given flat index.
     * Other rows shift down. Set -1 to clear gap.
     */
    void setDropGapIndex(int flatIndex);
    int dropGapIndex() const { return m_dropGapIndex; }

    /**
     * @brief Height of the drop gap
     */
    void setDropGapHeight(int h) { m_dropGapHeight = h; }

    /**
     * @brief Apply drag-end layout: recalculate all positions as if the
     * excluded entries don't exist, with a gap at the adjusted drop position.
     *
     * The excluded entries' widgets are NOT touched by the layout —
     * the view manages their collapse animations separately.
     *
     * Other entries animate to fill the source's old space and make room for the gap.
     *
     * @param excludeIds  Items being dragged (excluded from position calc)
     * @param dropInsertIndex  Original flat index where to insert
     */
    void applyDragEndState(const QSet<QUuid>& excludeIds, int dropInsertIndex);
    void applyCopyDragEndState(int dropInsertIndex, int gapHeight);

    /**
     * @brief Y position of the gap set by applyDragEndState.
     * The ghost should fly to this position.
     */
    qreal dragEndGapY() const { return m_dragEndGapY; }

    /**
     * @brief Clear the drag-end state (excluded id, gap, etc.)
     */
    void clearDragEndState();

    // === Clip Masking (group collapse) ===

    /**
     * @brief Set clip region for children of a collapsing group.
     * @param groupId The group being collapsed/expanded
     * @param clipBottom Y-coordinate of the group row bottom (children clip above this)
     * @param expanding true = expanding (children appear), false = collapsing (children disappear)
     */
    void setGroupClip(const QUuid& groupId, qreal clipBottom, bool expanding);

    /**
     * @brief Clear all clip constraints
     */
    void clearClips();

    // === Query ===

    /**
     * @brief Get row index at Y position (in container coords)
     */
    int rowIndexAtY(int y) const;

    /**
     * @brief Get the drop insert index for a Y position.
     * Returns the flat index where an item should be inserted.
     */
    int dropInsertIndexAtY(int y) const;

    /**
     * @brief Get target Y for a given flat index
     */
    qreal targetYForIndex(int index) const;

    /**
     * @brief Get row widget at flat index (or nullptr)
     */
    ReorderableRowWidget* rowWidgetAtIndex(int index) const;

    /**
     * @brief Get scaled row height at flat index
     */
    int scaledRowHeightAtIndex(int index) const;

    /**
     * @brief Number of layout entries
     */
    int entryCount() const { return m_entries.size(); }

signals:
    void contentHeightChanged(int newHeight);
    void layoutAnimationFinished();

private:
    void applyPositions(
        bool animate, const QSet<QUuid>& newEntryIds = {}, const QSet<QUuid>& instantPlaceIds = {});
    void finishAnimations();
    /// Scale a per-row height honouring m_scaleRowHeights (see setScaleRowHeights).
    int scaleRow(int h) const;

private:
    QWidget* m_container = nullptr;

    QList<RowLayoutEntry> m_entries;
    int m_contentHeight = 0;

    int m_rowHeight = 36;
    int m_rowSpacing = 2;
    int m_animDuration = 300;
    bool m_scaleRowHeights = true;

    // Drop gap
    int m_dropGapIndex = -1;
    int m_dropGapHeight = 36;

    // Drag-end state
    QSet<QUuid> m_excludeIds; // entries excluded from position calc
    qreal m_dragEndGapY = -1; // Y position of gap for ghost target

    // Group clipping
    struct GroupClipInfo {
        qreal clipBottom;
        bool expanding;
    };
    QHash<QUuid, GroupClipInfo> m_groupClips;

    // Animations
    QPointer<QParallelAnimationGroup> m_animGroup;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_REORDERLIST_ANIMATEDLISTLAYOUT_H
