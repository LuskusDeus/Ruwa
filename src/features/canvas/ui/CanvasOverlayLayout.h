// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   L A Y O U T   ( E N G I N E )
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASOVERLAYLAYOUT_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASOVERLAYLAYOUT_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QSize>

#include <optional>

class QWidget;
class QPropertyAnimation;
class QTimer;

namespace ruwa::ui::workspace {

/**
 * @brief CanvasPanel-agnostic layout engine for floating canvas overlay widgets.
 *
 * Owns the positioning policy for every overlay parented to the canvas content
 * widget: edge-inset bounds, single clamp path, wall-style anti-overlap between
 * "solid" items, edge/corner snapping, normalized-position tracking across
 * content resizes, and short settle animations.
 *
 * The engine never reaches into CanvasPanel members; callers register a widget
 * with an anchor + capability flags and drive it through a small API. Per-item
 * layout state (user-moved, normalized position, pending restore, animation)
 * lives here, so the three historical copies of clamp/reposition logic collapse
 * into one.
 *
 * Movement contract (decided with the user):
 *   - Live drag is 1:1 with no animation; only wall-collision + bounds clamp.
 *   - Snap-on-release, collision push, and discrete relayouts may animate.
 *   - Continuous content resize moves directly (frame-coherent with the canvas).
 */
class CanvasOverlayLayout : public QObject {
    Q_OBJECT

public:
    /// Default placement of an item when the user has never moved it.
    enum class Anchor {
        TopLeft,
        TopCenter,
        TopRight,
        CenterLeft,
        Center,
        CenterRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    /// Per-item capabilities. Combine with operator|.
    enum Cap {
        None = 0,
        Draggable = 1 << 0, ///< User can drag it (1:1, wall + clamp).
        Solid = 1 << 1, ///< Acts as an obstacle others cannot overlap.
        SnapEdges = 1 << 2, ///< Magnetizes to content edges/corners on release.
        ResizeTracked = 1 << 3, ///< Keeps its track-normalized position across resizes.
        BoundsClamped = 1 << 4, ///< Always kept fully inside the content bounds.
        Transient = 1 << 5, ///< Popup-like; positioned only on show, not relaid out.
        NoDragGlide
        = 1 << 6, ///< Never animate the post-collision catch-up (item has its own drag logic).
    };
    Q_DECLARE_FLAGS(Caps, Cap)

    explicit CanvasOverlayLayout(QWidget* content, QObject* parent = nullptr);
    ~CanvasOverlayLayout() override;

    // --- registration -----------------------------------------------------
    void registerItem(QWidget* w, Anchor anchor, Caps caps, int priority = 0);
    void unregisterItem(QWidget* w);
    bool isRegistered(QWidget* w) const;

    void setContentWidget(QWidget* content);
    QWidget* contentWidget() const { return m_content; }

    // --- tunables ---------------------------------------------------------
    int margin() const { return m_margin; }
    void setMargin(int px);
    int snapThreshold() const { return m_snapThreshold; }
    void setSnapThreshold(int px);
    int animationDurationMs() const { return m_animMs; }
    void setAnimationDurationMs(int ms);

    // --- geometry ---------------------------------------------------------
    /// Content rect inset by the edge margin (the legal region for an overlay's top-left/size).
    QRect contentBounds() const;
    bool hasValidContent() const;
    /// Clamp @p pos so @p w stays fully inside contentBounds().
    QPoint clampToBounds(QWidget* w, const QPoint& pos) const;

    // --- drag (1:1, no animation) ----------------------------------------
    /// Resolve a drag target: clamp + wall-collision against solids. Moves @p w and returns the
    /// final pos.
    QPoint applyDrag(QWidget* w, const QPoint& desiredTopLeft);
    /// Finish a drag: edge/corner snap (animated) and persist the normalized position.
    void endDrag(QWidget* w);
    /// Programmatic absolute placement: clamp + wall + persist (marks user-moved). Returns final
    /// pos.
    QPoint setItemPosition(QWidget* w, const QPoint& topLeft, bool animate = false);

    // --- programmatic placement ------------------------------------------
    /// Place @p w at its anchor default (clears user-moved). Optionally animated.
    void placeDefault(QWidget* w, bool animate = false);
    /// Re-resolve every non-transient item for a content resize. Direct move (resize-coherent)
    /// unless @p animate.
    void relayout(bool animate = false);
    /// Re-resolve a single registered item (its tracked/anchor position + collisions).
    void relayoutItem(QWidget* w, bool animate = false);

    // --- persisted state --------------------------------------------------
    void markUserMoved(QWidget* w, bool moved = true);
    bool isUserMoved(QWidget* w) const;
    /// Track-normalized position (0..1 along the free travel range), or (-1,-1) if unavailable.
    QPointF normalizedPosition(QWidget* w) const;
    /// Apply a previously saved normalized position; if content not ready yet, keep it pending.
    void setNormalizedPosition(QWidget* w, const QPointF& norm);

signals:
    /// Emitted whenever the engine moves an item (drag, snap, relayout). pos is the final top-left.
    void itemMoved(QWidget* widget, const QPoint& pos);

private:
    struct Item {
        QPointer<QWidget> widget;
        Anchor anchor = Anchor::TopCenter;
        Caps caps = None;
        int priority = 0;
        bool userMoved = false;
        std::optional<QPointF> pendingNorm; ///< Restored norm waiting for valid content.
        QPropertyAnimation* anim = nullptr;
    };

    Item* find(QWidget* w);
    const Item* find(QWidget* w) const;

    QPoint anchorPosition(const Item& it) const;
    QPoint resolveSolid(const Item& it, const QPoint& cur, const QPoint& desired) const;
    QPoint applySnap(const Item& it, const QPoint& pos) const;
    QPointF toNormalized(const Item& it, const QPoint& pos) const;
    QPoint fromNormalized(const Item& it, const QPointF& norm) const;
    void placeItem(Item& it, bool animate);
    void moveItem(Item& it, const QPoint& pos, bool animate, int durationMs = -1);
    QList<QRect> solidObstacles(const Item& exclude) const;

    // Frame-rate-independent low-pass follow: a dragged item trails the cursor with a
    // constant, smooth lag for the whole drag (instead of snapping 1:1). Driven by a
    // dt-based timer so it is unaffected by event/timer jitter.
    void startDragFollow(QWidget* w, const QPoint& target);
    void stopDragFollow();
    void updateDragFollow();

    QWidget* m_content = nullptr;
    QList<Item> m_items;
    QPointer<QWidget> m_activeDrag; ///< Item under an active drag; skipped during relayout.
    QPoint m_dragLogicalPos; ///< Drag target in logical space (wall is resolved here, not on the
                             ///< lagging visual).

    QTimer* m_dragFollowTimer = nullptr;
    QPointer<QWidget> m_dragFollowWidget; ///< Item currently trailing the cursor.
    QPoint m_dragFollowTarget; ///< Live cursor-driven target the widget eases toward.
    QElapsedTimer m_dragFollowClock; ///< dt source for frame-rate-independent smoothing.

    int m_margin = 6;
    int m_snapThreshold = 16;
    int m_animMs = 160;
};

} // namespace ruwa::ui::workspace

Q_DECLARE_OPERATORS_FOR_FLAGS(ruwa::ui::workspace::CanvasOverlayLayout::Caps)

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASOVERLAYLAYOUT_H
