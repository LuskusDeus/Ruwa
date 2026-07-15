// SPDX-License-Identifier: MPL-2.0

// DockSplitNode.h
#ifndef RUWA_UI_DOCKING_LAYOUT_SPLIT_NODE_H
#define RUWA_UI_DOCKING_LAYOUT_SPLIT_NODE_H

#include "DockLayoutNode.h"
#include "DockLeafNode.h"

#include <QList>
#include <vector>
#include <functional>

namespace ruwa::ui::docking {

/**
 * @brief Callback for handle geometry updates
 *
 * @param handleIndex Index of the handle (0 = between child 0 and 1)
 * @param geometry Handle geometry in container coordinates
 */
using HandleGeometryCallback = std::function<void(int handleIndex, const QRect& geometry)>;

/**
 * @brief Split node containing 2+ child nodes
 *
 * DockSplitNode arranges its children either horizontally or vertically,
 * with handles between them for user resizing.
 *
 * Key features:
 * - Anchored resize: Some children can preserve their size while others flex
 * - Push-through drag: Dragging past minimum pushes adjacent items
 * - Proportional fallback: Non-anchored items scale proportionally
 *
 * Handle geometry is communicated via callback, not owned widgets.
 * The DockLayoutRoot creates actual handle widgets based on these callbacks.
 */
class DockSplitNode : public DockLayoutNode {
public:
    explicit DockSplitNode(SplitDirection direction = SplitDirection::Horizontal);
    ~DockSplitNode() override;

    // === Type ===

    Type type() const override { return Type::Split; }

    // === Direction ===

    /**
     * @brief Get split direction
     */
    SplitDirection direction() const { return m_direction; }

    /**
     * @brief Set split direction
     *
     * Changing direction triggers a relayout.
     */
    void setDirection(SplitDirection direction);

    // === Children ===

    /**
     * @brief Get number of children
     */
    int childCount() const { return static_cast<int>(m_children.size()); }

    /**
     * @brief Check if empty
     */
    bool isEmpty() const { return m_children.empty(); }

    /**
     * @brief Get child at index
     */
    DockLayoutNode* childAt(int index) const;

    /**
     * @brief Get index of child (-1 if not found)
     */
    int indexOf(DockLayoutNode* child) const;

    /**
     * @brief Get all children
     */
    const std::vector<DockLayoutNodePtr>& children() const { return m_children; }

    /**
     * @brief Add child at end
     *
     * @param child Child to add (takes ownership)
     */
    void addChild(DockLayoutNodePtr child);

    /**
     * @brief Insert child at index
     *
     * @param index Position to insert at
     * @param child Child to insert (takes ownership)
     */
    void insertChild(int index, DockLayoutNodePtr child);

    /**
     * @brief Remove child at index
     *
     * @param index Index to remove
     * @return Removed child (caller takes ownership)
     */
    DockLayoutNodePtr removeChildAt(int index);

    /**
     * @brief Remove specific child
     *
     * @param child Child to remove
     * @return Removed child (caller takes ownership), or nullptr if not found
     */
    DockLayoutNodePtr removeChild(DockLayoutNode* child);

    /**
     * @brief Replace child at index
     *
     * @param index Index to replace
     * @param newChild New child (takes ownership)
     * @return Old child (caller takes ownership)
     */
    DockLayoutNodePtr replaceChild(int index, DockLayoutNodePtr newChild);

    // === Sizes ===

    /**
     * @brief Get current sizes of children (in split direction)
     */
    const QList<int>& sizes() const { return m_sizes; }

    /**
     * @brief Set sizes directly
     *
     * Sizes are clamped to constraints. Total should match available space.
     */
    void setSizes(const QList<int>& sizes);

    /**
     * @brief Get handle size (pixels)
     */
    int handleSize() const { return m_handleSize; }

    /**
     * @brief Set handle size
     */
    void setHandleSize(int size) { m_handleSize = qMax(1, size); }

    // === Handle Drag ===

    /**
     * @brief Handle drag operation
     *
     * @param handleIndex Index of handle being dragged
     * @param delta Pixels to move (positive = grow first item)
     */
    void handleDrag(int handleIndex, int delta);

    // === Layout ===

    void setBounds(const QRect& bounds) override;
    NodeSizeConstraints sizeConstraints() const override;

    // === Handle Geometry Callback ===

    /**
     * @brief Set callback for handle geometry updates
     *
     * Called during setBounds() for each handle with its new geometry.
     */
    void setHandleGeometryCallback(HandleGeometryCallback callback)
    {
        m_handleGeometryCallback = std::move(callback);
    }

    // === Utility ===

    /**
     * @brief Find leaf node containing a panel
     */
    DockLeafNode* findLeafForPanel(DockPanel* panel) const;

    /**
     * @brief Find all leaf nodes
     */
    QList<DockLeafNode*> allLeaves() const;

    /**
     * @brief Collapse unnecessary split nodes
     *
     * If this node has only one child, it should be replaced by that child.
     * Returns true if this node should be collapsed.
     */
    bool shouldCollapse() const { return m_children.size() <= 1; }

    // === Debug ===

    QString debugString() const override;

private:
    /**
     * @brief Calculate child bounds from sizes
     */
    void layoutChildren();

    /**
     * @brief Check whether current sizes are incompatible with available space or constraints
     */
    bool hasInvalidSizes() const;

    /**
     * @brief Repair current sizes so children fully consume available space without violating
     * minimums
     */
    void normalizeSizesToAvailableSpace();

    /**
     * @brief Ensure sizes array matches children count
     */
    void synchronizeSizes();

    /**
     * @brief Calculate available space for children (total - handles)
     */
    int availableSpace() const;

    /**
     * @brief Get size in split direction
     */
    int sizeInDirection(const QRect& rect) const;

    /**
     * @brief Get size perpendicular to split direction
     */
    int crossSize(const QRect& rect) const;

    /**
     * @brief Build AnchoredItem list from children for calculator
     */
    QList<AnchoredItem> buildAnchoredItems() const;

    /**
     * @brief Apply calculated sizes from anchored resize
     */
    void applySizes(const QList<int>& newSizes);

private:
    SplitDirection m_direction = SplitDirection::Horizontal;
    std::vector<DockLayoutNodePtr> m_children;
    QList<int> m_sizes; // Size of each child in split direction
    int m_handleSize = 6;
    HandleGeometryCallback m_handleGeometryCallback;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_SPLIT_NODE_H
