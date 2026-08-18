// SPDX-License-Identifier: MPL-2.0

// DockLayoutRoot.h
#ifndef RUWA_UI_DOCKING_LAYOUT_ROOT_H
#define RUWA_UI_DOCKING_LAYOUT_ROOT_H

#include "DockLayoutNode.h"
#include "DockLeafNode.h"
#include "DockSplitNode.h"
#include "shell/docking/DockTypes.h"
#include "shell/docking/state/DockLayoutPreset.h"
#include "features/theme/manager/ThemeColors.h"

#include <QObject>
#include <QMap>
#include <QSet>
#include <QPointer>
#include <QPoint>
#include <QRect>
#include <QVariantAnimation>
#include <QJsonObject>
#include <functional>
#include <optional>

namespace ruwa::ui::docking {

class DockPanel;
class DockSplitHandle;
class DockGroupHost;

/**
 * @brief Coordinator between layout tree and Qt widgets
 *
 * DockLayoutRoot is the main entry point for the new layout system.
 * It manages:
 * - The root node of the layout tree
 * - Creation/destruction of handle widgets
 * - Synchronization between tree state and widget geometry
 * - Panel add/remove operations
 *
 * Panels are direct children of the container widget, not of layout nodes.
 * Handle widgets are also direct children of the container.
 * The layout tree is a pure data structure that computes geometry.
 */
class DockLayoutRoot : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Create a layout root
     *
     * @param containerWidget The widget that contains all panels and handles
     * @param parent QObject parent
     */
    explicit DockLayoutRoot(QWidget* containerWidget, QObject* parent = nullptr);
    ~DockLayoutRoot() override;

    // === Container ===

    /**
     * @brief Get the container widget
     */
    QWidget* containerWidget() const { return m_containerWidget; }

    // === Root Node ===

    /**
     * @brief Get the root node (may be nullptr if empty)
     */
    DockLayoutNode* rootNode() const { return m_root.get(); }

    /**
     * @brief Check if layout is empty
     */
    bool isEmpty() const
    {
        return !m_root
            || (m_root->isLeaf() && !static_cast<DockLeafNode*>(m_root.get())->hasPanel());
    }

    // === Panel Operations ===

    /**
     * @brief Add panel to the layout
     *
     * @param panel Panel to add (must already be a child of containerWidget)
     * @param position Position to add at (Left, Right, Top, Bottom)
     * @return Leaf node containing the panel
     */
    DockLeafNode* addPanel(DockPanel* panel, DockPosition position);

    /**
     * @brief Add panel relative to another panel
     *
     * @param panel Panel to add
     * @param relativeTo Panel to add relative to
     * @param position Position relative to relativeTo
     * @return Leaf node containing the panel, or nullptr if relativeTo not found
     */
    DockLeafNode* addPanelRelativeTo(
        DockPanel* panel, DockPanel* relativeTo, DockPosition position);

    /**
     * @brief Add panel into the same layout cell as another panel, as a tab group
     *
     * Forms a group if @p relativeTo is currently solo, or joins the existing
     * one. The inserted panel becomes the group's current member.
     *
     * @param panel Panel to add
     * @param relativeTo Panel whose cell is joined
     * @param insertIndex Tab-order index, or -1 for "right after relativeTo"
     * @param makeCurrent false leaves the selection on the incumbent member.
     *        The drop animation needs this: the incumbent has to stay visible
     *        while it slides out from under the arriving panel, which only
     *        becomes current once its docking animation lands.
     * @return The group leaf, or nullptr if the panels refuse to be grouped
     *         (PanelFeature::Groupable) or relativeTo is not in the tree
     */
    DockLeafNode* addPanelToGroup(
        DockPanel* panel, DockPanel* relativeTo, int insertIndex = -1, bool makeCurrent = true);

    /**
     * @brief Would addPanelToGroup() accept these two panels?
     *
     * Asked by the overlay before it offers a group drop zone at all, so the
     * user never sees a target that the drop would then refuse.
     */
    bool canGroupPanels(DockPanel* panel, DockPanel* relativeTo) const;

    /**
     * @brief Make @p panel the visible member of its group
     * @return true if the selection changed
     */
    bool setCurrentPanelInGroup(DockPanel* panel);

    /**
     * @brief Remove panel from the layout
     *
     * @param panel Panel to remove
     * @return true if panel was found and removed
     */
    bool removePanel(DockPanel* panel);

    /**
     * @brief Find the leaf node containing a panel
     *
     * @param panel Panel to find
     * @return Leaf node, or nullptr if not found
     */
    DockLeafNode* findLeafForPanel(DockPanel* panel) const;

    /**
     * @brief Get all panels in the layout, including hidden group members
     */
    QList<DockPanel*> allPanels() const;

    /**
     * @brief Get one panel per layout cell: the current member of each group
     */
    QList<DockPanel*> visiblePanels() const;

    /**
     * @brief Find panel at a given position
     *
     * @param globalPos Position in global coordinates
     * @return Panel at position, or nullptr if not found
     */
    DockPanel* findPanelAt(const QPoint& globalPos) const;

    /**
     * @brief Find the layout cell (leaf, possibly a group) at a given position
     */
    DockLeafNode* findLeafAt(const QPoint& globalPos) const;

    // === Geometry ===

    /**
     * @brief Set root bounds (call from container's resizeEvent)
     *
     * This triggers the entire layout calculation.
     */
    void setRootBounds(const QRect& bounds);

    /**
     * @brief Get current root bounds
     */
    QRect rootBounds() const { return m_rootBounds; }

    // === Handle Management ===

    /**
     * @brief Synchronize handle widgets with current tree structure
     *
     * Call after tree structure changes (add/remove operations).
     * Creates/destroys handle widgets as needed.
     */
    void syncHandleWidgets();

    // === Group Hosts ===

    /**
     * @brief Create/destroy group frames so they match the tree
     *
     * Called from syncHandleWidgets(): both are "widgets that must follow the
     * tree's structure". A leaf that became a group gains a DockGroupHost and
     * hands its panels to it; one that dropped back to a single panel gives the
     * survivor back to the container widget and loses the host.
     */
    void syncGroupHosts();

    /// Group frame of the cell holding @p panel, or nullptr if it is not grouped.
    DockGroupHost* groupHostForPanel(DockPanel* panel) const;

    /**
     * @brief Group frame whose HEADER STRIP contains @p globalPos
     *
     * The header is the one part of a cell that belongs to no panel, so
     * findPanelAt() misses it entirely — hit-testing it needs the frames.
     */
    DockGroupHost* groupHostAtHeader(const QPoint& globalPos) const;

    /**
     * @brief Raise all handle widgets above panels
     *
     * Call after panel z-order changes (e.g., after docking animation finishes)
     * to restore proper z-order where handles are above panels.
     */
    void raiseHandles();

    // === Theme ===

    /**
     * @brief Apply theme to handles
     */
    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    // === Layout Animation ===

    /**
     * @brief Enable/disable layout animations
     */
    void setLayoutAnimationEnabled(bool enabled);
    bool isLayoutAnimationEnabled() const { return m_layoutAnimationEnabled; }

    /**
     * @brief Set layout animation duration
     */
    void setLayoutAnimationDuration(int ms);
    int layoutAnimationDuration() const { return m_layoutAnimationDuration; }

    /**
     * @brief Check if layout animation is running
     */
    bool isAnimatingLayout() const { return m_animatingLayout; }

    /**
     * @brief Capture current panel geometries before layout change
     * Call this before any layout-modifying operation
     */
    void captureLayoutState();

    /**
     * @brief Animate panels to their new positions after layout change
     * Call this after layout-modifying operation completes
     * @param excludePanel Optional panel to exclude (e.g., panel being docked that has its own
     * animation)
     */
    void animateLayoutChange(DockPanel* excludePanel = nullptr);

    // === Debug ===

    /**
     * @brief Get debug string of entire tree
     */
    QString debugString() const;

    /**
     * @brief Get placement info for a panel (for restoring after hide)
     *
     * Returns the relative position needed to add the panel back to the same place.
     * Returns nullopt if panel is not in the layout (e.g. floating or already hidden).
     */
    std::optional<PanelPlacement> getPanelPlacement(DockPanel* panel) const;

    // === Serialization ===
    /**
     * @brief Serialize current layout tree to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Restore layout tree from JSON
     * @param json Serialized layout JSON
     * @param panelResolver Resolves panel by saved id/title
     * @return true if layout restored successfully
     */
    bool fromJson(const QJsonObject& json,
        const std::function<DockPanel*(const QString& panelId, const QString& panelTitle)>&
            panelResolver);

    /**
     * @brief Sanitize the layout tree and re-apply bounds.
     *
     * Removes
     * empty/duplicate leaves, collapses redundant split nodes and
     * forces a fresh geometry
     * pass. Returns true if the tree was modified.
     */
    bool repairLayout();

signals:
    /**
     * @brief Emitted when layout structure changes
     */
    void layoutChanged();

    /**
     * @brief Emitted when a panel is added
     */
    void panelAdded(DockPanel* panel);

    /**
     * @brief Emitted when a panel is removed
     */
    void panelRemoved(DockPanel* panel);

    /// A group frame appeared / is about to go away (for higher layers that
    /// need to hook into it, e.g. DockManager's drag handling).
    void groupHostCreated(DockGroupHost* host);
    void groupHostAboutToBeDestroyed(DockGroupHost* host);

    /// Forwarded from a group header. Acted on by DockContainerWidget /
    /// DockManager, which own panel lifetime.
    void groupPanelCloseRequested(DockPanel* panel);

private slots:
    void onHandleDragStarted(DockSplitNode* node, int handleIndex);
    void onHandleDragMoved(DockSplitNode* node, int handleIndex, int delta);
    void onHandleDragFinished(DockSplitNode* node, int handleIndex);
    void onLayoutAnimationValueChanged(const QVariant& value);
    void onLayoutAnimationFinished();

private:
    /**
     * @brief Convert DockPosition to SplitDirection
     */
    static SplitDirection positionToDirection(DockPosition position);

    /**
     * @brief Check if position is "first" (left or top)
     */
    static bool isFirstPosition(DockPosition position);

    /**
     * @brief Add panel to an empty layout
     */
    DockLeafNode* addPanelToEmpty(DockPanel* panel);

    /**
     * @brief Add panel by splitting the root
     */
    DockLeafNode* addPanelAtRoot(DockPanel* panel, DockPosition position);

    /**
     * @brief Add panel by splitting a leaf node
     */
    DockLeafNode* addPanelByLeafSplit(DockPanel* panel, DockLeafNode* leaf, DockPosition position);

    /**
     * @brief Create handle widgets for a split node
     */
    void createHandlesForSplit(DockSplitNode* split);

    /**
     * @brief Remove handle widgets for a split node
     */
    void removeHandlesForSplit(DockSplitNode* split);

    /**
     * @brief Cleanup empty nodes after panel removal
     */
    void cleanupEmptyNodes();

    /**
     * @brief Collapse single-child split nodes
     */
    void collapseSingleChildSplits(DockLayoutNode*& node);

    /**
     * @brief Recursively sync handles for all split nodes
     */
    void syncHandlesRecursive(DockLayoutNode* node);

    /**
     * @brief Setup handle geometry callback for a split node
     */
    void setupHandleCallback(DockSplitNode* split);

    /**
     * @brief Get first panel in a node (leaf or nested split)
     */
    DockPanel* getFirstPanelInNode(DockLayoutNode* node) const;

    /**
     * @brief Widget that occupies a leaf's cell
     *
     * The group frame for a grouped leaf, the panel itself otherwise. This is
     * the widget the layout animation moves — a grouped panel's own geometry is
     * relative to its host's viewport, not to the container.
     */
    QWidget* cellWidget(DockLeafNode* leaf) const;

    /// Cell widget of every leaf in the tree.
    QList<QWidget*> allCellWidgets() const;

    /**
     * @brief Cell widget touching one edge of a node, in a split's direction
     *
     * Identifies a handle by the two cells it separates, which survives handle
     * recreation. Was three copies of the same lambda before groups; a group
     * must resolve to its frame here, never to whichever member is on top.
     */
    QWidget* edgeCellWidget(DockLayoutNode* node, bool rightEdge, SplitDirection splitDir) const;

    /// Every leaf in the tree, root included.
    QList<DockLeafNode*> allLeafNodes() const;

    void destroyGroupHost(DockLeafNode* leaf);
    void connectGroupHost(DockGroupHost* host);

    QJsonObject serializeNode(const DockLayoutNode* node) const;
    DockLayoutNodePtr deserializeNode(const QJsonObject& nodeObj,
        const std::function<DockPanel*(const QString& panelId, const QString& panelTitle)>&
            panelResolver) const;
    bool sanitizeNodeRecursive(DockLayoutNodePtr& node, QSet<DockPanel*>& seenPanels);
    bool sanitizeLeafNode(DockLeafNode* leaf, QSet<DockPanel*>& seenPanels);
    bool sanitizeSplitNode(DockSplitNode* split, QSet<DockPanel*>& seenPanels);

private:
    QWidget* m_containerWidget = nullptr;
    DockLayoutNodePtr m_root;
    QRect m_rootBounds;

    // Handle widgets keyed by split node
    QMap<DockSplitNode*, QList<DockSplitHandle*>> m_handles;

    // Live group frames. Deliberately NOT keyed by leaf: a leaf can be
    // destroyed by tree surgery, and looking up a dangling key to find the
    // orphaned host would be undefined. Orphans are found by walking the tree
    // for live hosts and deleting whatever is left in this set.
    QSet<DockGroupHost*> m_groupHosts;

    // Theme colors
    ruwa::ui::core::ThemeColors m_colors;

    // State flags
    bool m_destroying = false;
    bool m_inOperation = false;

    // Layout animation
    bool m_layoutAnimationEnabled = true;
    int m_layoutAnimationDuration = 250; // ms
    bool m_animatingLayout = false;
    QPointer<QVariantAnimation> m_layoutAnimation;
    // Keyed by CELL widget (panel, or group frame for a grouped cell): a
    // grouped panel's geometry lives in its host's viewport coordinates and
    // must never be interpolated in container space.
    QMap<QWidget*, QRect> m_capturedGeometries; // Old geometries
    QMap<QWidget*, QRect> m_targetGeometries; // New geometries
    QWidget* m_excludedCell = nullptr; // Cell excluded from animation

    // Handle animation (animated alongside panels)
    QMap<DockSplitHandle*, QRect> m_capturedHandleGeometries;
    QMap<DockSplitHandle*, QRect> m_targetHandleGeometries;

    // Handle positions keyed by adjacent cell widgets (survives handle
    // recreation, and stays stable when a group switches its visible member)
    QMap<QPair<QWidget*, QWidget*>, QRect> m_capturedHandlePositions;

    // Flag to defer showing new handles until animation starts
    bool m_preparingAnimation = false;
    QSet<DockSplitHandle*> m_deferredHandles; // Handles to show when animation starts
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_ROOT_H
