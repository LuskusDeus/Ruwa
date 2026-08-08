// SPDX-License-Identifier: MPL-2.0

// DockGroupHost.h
#ifndef RUWA_UI_DOCKING_WIDGETS_DOCKGROUPHOST_H
#define RUWA_UI_DOCKING_WIDGETS_DOCKGROUPHOST_H

#include "shell/docking/DockTypes.h"
// Full definition, not a forward declaration: QPointer<DockPanel>::data() casts
// from QObject*, which needs the complete type at every inline use below.
#include "DockPanel.h"
#include "features/theme/manager/ThemeColors.h"

#include <QWidget>
#include <QList>
#include <QPointer>
#include <QRect>

class QVariantAnimation;

namespace ruwa::ui::docking {

class DockGroupHeader;
class DockLeafNode;

/**
 * @brief Frame around one tab group: header strip + the member area
 *
 * A DockGroupHost is a child of the DockContainerWidget and receives the
 * geometry of its layout cell (DockLeafNode::setBounds). Members are reparented
 * into its clipping viewport, so the slide between two members is simply the
 * two panels moving inside a clip rect — the same explicit-geometry +
 * QVariantAnimation pattern the rest of the docking system uses.
 *
 * A QStackedWidget (AnimatedStackedWidget) was the obvious alternative, but its
 * layout would own the members' geometry and fight both DockLeafNode and
 * DockPanel::animateDocking, which is exactly what the drop animation needs to
 * drive by hand.
 *
 * The host never mutates the layout tree: it reports intent and repaints from
 * syncFromLeaf().
 */
class DockGroupHost : public QWidget {
    Q_OBJECT

public:
    /// Which side the incoming panel enters from.
    enum class SlideDirection {
        Auto, ///< Derive from tab order (later tab enters from the right)
        FromRight,
        FromLeft
    };

    DockGroupHost(DockLeafNode* leaf, QWidget* parent);
    ~DockGroupHost() override;

    // === Model ===

    DockLeafNode* leaf() const { return m_leaf; }

    /**
     * @brief Re-read members and selection from the leaf
     *
     * Adopts members that are not hosted yet, releases ones that left the
     * group, and rebuilds the header. Does not animate.
     */
    void syncFromLeaf();

    /// Members currently hosted, in tab order.
    QList<DockPanel*> hostedPanels() const;

    DockPanel* currentPanel() const { return m_currentPanel.data(); }

    /**
     * @brief Show @p panel, sliding the previous member out
     *
     * @param animated false snaps immediately (restore, theme reload, teardown)
     * @param direction Auto derives the direction from tab order
     */
    void setCurrentPanel(
        DockPanel* panel, bool animated = true, SlideDirection direction = SlideDirection::Auto);

    // === Membership ===

    /// Reparent @p panel into the viewport (no-op if already hosted).
    void adoptPanel(DockPanel* panel);

    /**
     * @brief Select the member @p delta places away in tab order
     *
     * Reports the choice through panelActivationRequested rather than switching
     * directly: the leaf owns the selection, and the layout root is what moves
     * it (and then animates this host). @p wrap runs off the end back to the
     * other side — what a keyboard cycle does, and what a wheel must not.
     */
    void activateRelativeMember(int delta, bool wrap);

    /**
     * @brief Hand @p panel back to the container widget
     *
     * The panel keeps its screen position, so a caller that is about to float
     * or re-dock it can animate from where it visually was. It also keeps its
     * visibility: a member released because it was CLOSED must not reappear,
     * and hidden is exactly the state a non-current member is already in.
     */
    void releasePanel(DockPanel* panel);

    // === Drop choreography (used by the container when a panel is dropped in) ===

    /**
     * @brief Slide the current member out to clear the incoming panel's slot
     *
     * Runs for @p duration ms so it can be started next to the dropped panel's
     * own DockPanel::animateDocking and land on the same frame.
     */
    void runInsertionSlide(GroupInsertSide side, int duration);

    /**
     * @brief Move the tab highlight ahead of the model, to a member in flight
     *
     * The strip and the stage answer to a drop on different clocks: the model
     * keeps the incumbent current so it can slide out, while the tab of the
     * arriving panel is already in the strip and is what the user aimed at.
     * finishInsertion() lands on the same tab, so nothing snaps.
     */
    void previewCurrentTab(DockPanel* panel);

    /**
     * @brief Adopt the dropped panel once its docking animation has landed
     *
     * Takes over from runInsertionSlide(): the panel becomes the current member
     * at rest, with no further animation.
     */
    void finishInsertion(DockPanel* panel);

    // === Teardown ===

    /**
     * @brief Slide the last survivor into view before the group is taken apart
     *
     * The group has just dropped to a single member. If the one that LEFT was
     * the visible one, the survivor would otherwise blink into place — so it
     * slides in here, inside the viewport that still clips it, and the frame
     * asks to be destroyed (collapseFinished) only once it has landed.
     *
     * @return true if a slide is playing and the caller must wait for
     *         collapseFinished(); false if there is nothing to show and the
     *         frame can be destroyed right away.
     */
    bool beginCollapse();
    bool isCollapsing() const { return m_collapsing; }

    /**
     * @brief Play the group's disappearance, then delete this frame
     *
     * The header strip collapses upward over @p duration while the survivor —
     * already released to the container by the caller — grows into exactly the
     * space being vacated. The frame is emptied first, so nothing it still owns
     * can be disturbed by the layout settling around it.
     *
     * The caller must have cleared the leaf's pointer to this host already:
     * a frame in farewell is no longer part of the layout.
     */
    void runFarewell(int duration);

    /// Geometry a member occupies, in this host's coordinates.
    QRect memberAreaRect() const;

    /// Same rect mapped into the container widget's coordinates — the target
    /// for a dropped panel's docking animation.
    QRect memberAreaInParent() const;

    int headerHeight() const;

    // === Theme ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    // === Animation settings ===

    void setAnimationDuration(int ms) { m_animationDuration = qMax(0, ms); }
    int animationDuration() const { return m_animationDuration; }
    void setAnimationsEnabled(bool enabled) { m_animationsEnabled = enabled; }

    DockGroupHeader* header() const { return m_header; }

signals:
    /// The collapse slide has landed: this frame is ready to be destroyed.
    void collapseFinished();
    /// User picked a tab; the layout root should move the leaf's selection.
    void panelActivationRequested(DockPanel* panel);
    /// User clicked a tab's ×.
    void panelCloseRequested(DockPanel* panel);
    /// User dragged a tab out of the strip; the panel should leave the group.
    void panelDragStarted(DockPanel* panel, const QPoint& globalPos);
    void panelDragMoved(DockPanel* panel, const QPoint& globalPos);
    void panelDragFinished(DockPanel* panel, const QPoint& globalPos);

protected:
    void resizeEvent(QResizeEvent* event) override;
    /// Fills the member area with the header's surface, so a member in flight
    /// never uncovers the container background behind the cell.
    void paintEvent(QPaintEvent* event) override;

private:
    /**
     * @brief A tab's × was clicked: start the content transition now
     *
     * The panel is still alive and still the visible member, so the successor
     * slides in OVER it while the tab fades — the two animations run together
     * rather than one after the other.
     */
    void onPanelCloseStarted(DockPanel* closing, DockPanel* successor);
    /// The tab has finished fading; the close lands once its slide has too.
    void onPanelFarewellFinished(DockPanel* panel);
    /// The content transition has landed (or was cut short).
    void markPendingCloseSlidesDone();
    /// Emit panelCloseRequested for every close whose animations have all run.
    void reportSettledCloses();

    void setupShortcuts();
    void layoutChildren();
    /// Park every member except the current one out of sight.
    void applyRestingGeometry();
    void stopSlide();
    void onSlideValue(qreal progress);
    void onSlideFinished();
    /// Report a finished (or cut short) collapse exactly once.
    void finishCollapse();

private:
    DockLeafNode* m_leaf = nullptr;
    QWidget* m_viewport = nullptr;
    DockGroupHeader* m_header = nullptr;

    QList<QPointer<DockPanel>> m_hosted;
    QPointer<DockPanel> m_currentPanel;

    // Slide state
    QPointer<QVariantAnimation> m_slideAnimation;
    QPointer<DockPanel> m_slideOutgoing;
    QPointer<DockPanel> m_slideIncoming;
    int m_slideOutgoingTargetX = 0;
    int m_slideIncomingStartX = 0;

    // A close in flight: reported only once BOTH the tab's farewell and the
    // content transition it kicked off have landed, so the closing member keeps
    // painting for the whole thing instead of blinking out at frame one.
    struct PendingClose {
        QPointer<DockPanel> panel;
        bool slideDone = false;
        bool farewellDone = false;
    };
    QList<PendingClose> m_pendingCloses;

    bool m_animationsEnabled = true;
    bool m_collapsing = false;
    int m_animationDuration = 250;

    ruwa::ui::core::ThemeColors m_colors;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_WIDGETS_DOCKGROUPHOST_H
