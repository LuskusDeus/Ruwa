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
     * @brief Get all panels in the layout
     */
    QList<DockPanel*> allPanels() const;

    /**
     * @brief Find panel at a given position
     *
     * @param globalPos Position in global coordinates
     * @return Panel at position, or nullptr if not found
     */
    DockPanel* findPanelAt(const QPoint& globalPos) const;

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
    void setLayoutAnimationEnabled(bool enabled) { m_layoutAnimationEnabled = enabled; }
    bool isLayoutAnimationEnabled() const { return m_layoutAnimationEnabled; }

    /**
     * @brief Set layout animation duration
     */
    void setLayoutAnimationDuration(int ms) { m_layoutAnimationDuration = qMax(0, ms); }
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

    QJsonObject serializeNode(const DockLayoutNode* node) const;
    DockLayoutNodePtr deserializeNode(const QJsonObject& nodeObj,
        const std::function<DockPanel*(const QString& panelId, const QString& panelTitle)>&
            panelResolver) const;
    bool sanitizeNodeRecursive(DockLayoutNodePtr& node, QSet<DockPanel*>& seenPanels);
    bool sanitizeSplitNode(DockSplitNode* split, QSet<DockPanel*>& seenPanels);

private:
    QWidget* m_containerWidget = nullptr;
    DockLayoutNodePtr m_root;
    QRect m_rootBounds;

    // Handle widgets keyed by split node
    QMap<DockSplitNode*, QList<DockSplitHandle*>> m_handles;

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
    QMap<DockPanel*, QRect> m_capturedGeometries; // Old geometries
    QMap<DockPanel*, QRect> m_targetGeometries; // New geometries
    DockPanel* m_excludedPanel = nullptr; // Panel excluded from animation

    // Handle animation (animated alongside panels)
    QMap<DockSplitHandle*, QRect> m_capturedHandleGeometries;
    QMap<DockSplitHandle*, QRect> m_targetHandleGeometries;

    // Handle positions keyed by adjacent panels (survives handle recreation)
    QMap<QPair<DockPanel*, DockPanel*>, QRect> m_capturedHandlePositions;

    // Flag to defer showing new handles until animation starts
    bool m_preparingAnimation = false;
    QSet<DockSplitHandle*> m_deferredHandles; // Handles to show when animation starts
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_ROOT_H
