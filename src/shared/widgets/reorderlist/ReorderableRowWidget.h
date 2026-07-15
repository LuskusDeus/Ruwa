// SPDX-License-Identifier: MPL-2.0

// ReorderableRowWidget.h
#ifndef RUWA_SHARED_WIDGETS_REORDERLIST_REORDERABLEROWWIDGET_H
#define RUWA_SHARED_WIDGETS_REORDERLIST_REORDERABLEROWWIDGET_H

#include <QUuid>
#include <QWidget>

namespace ruwa::ui::widgets {

/**
 * @brief Abstract base for a row in an animated, drag-reorderable list.
 *
 * This is the contract the reusable list engine (AnimatedListLayout /
 * ListDragDrop / ReorderableListView) relies on. Concrete rows —
 * LayerRowWidget for the layers panel, EffectCard for the effects panel —
 * subclass this and provide their own painting and interaction.
 *
 * Identity is a QUuid so a single engine can drive heterogeneous item kinds
 * (layers use LayerId == QUuid; effects use their instance QUuid).
 *
 * The `rowOpacity` property lives here because the layout/drag animations
 * drive it via QPropertyAnimation on the "rowOpacity" property name. Concrete
 * rows read m_rowOpacity in their paintEvent to honour it.
 *
 * Tree/nesting hooks (itemDepth / isReorderBlocked) default to a flat list;
 * the layers panel overrides them to expose group nesting and the immovable
 * Background layer. Flat lists (effects) can ignore them entirely.
 */
class ReorderableRowWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal rowOpacity READ rowOpacity WRITE setRowOpacity)

public:
    explicit ReorderableRowWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
    }
    ~ReorderableRowWidget() override = default;

    /// Stable identity of the item this row represents.
    virtual QUuid itemId() const = 0;

    /// Laid-out height of this row (groups/backgrounds may differ from default).
    virtual int effectiveRowHeight() const = 0;

    /// Enter/leave the "being dragged" visual state (dim, clear hover, etc.).
    virtual void setDragging(bool dragging) = 0;

    /// Nesting depth for tree lists. Flat lists always report 0.
    virtual int itemDepth() const { return 0; }

    /// True if this item cannot be moved/reparented (e.g. Background layer).
    virtual bool isReorderBlocked() const { return false; }

    // --- Animation property (driven by the list engine) ---
    qreal rowOpacity() const { return m_rowOpacity; }
    virtual void setRowOpacity(qreal v);

protected:
    qreal m_rowOpacity = 1.0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_REORDERLIST_REORDERABLEROWWIDGET_H
