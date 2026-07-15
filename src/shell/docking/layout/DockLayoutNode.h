// SPDX-License-Identifier: MPL-2.0

// DockLayoutNode.h
#ifndef RUWA_UI_DOCKING_LAYOUT_NODE_H
#define RUWA_UI_DOCKING_LAYOUT_NODE_H

#include "shell/docking/DockLayoutTypes.h"

#include <QRect>
#include <memory>

namespace ruwa::ui::docking {

class DockLayoutNode;
class DockPanel;

using DockLayoutNodePtr = std::unique_ptr<DockLayoutNode>;

/**
 * @brief Abstract base class for layout tree nodes
 *
 * The layout tree is a pure data structure - no Qt widgets here.
 * Two concrete types:
 * - DockLeafNode: Contains a single DockPanel
 * - DockSplitNode: Contains 2+ children arranged horizontally or vertically
 *
 * Layout flow:
 * - setBounds() called top-down from root
 * - sizeConstraints() called bottom-up to get minimum/preferred sizes
 * - Anchored resize preserves sizes of anchored nodes
 */
class DockLayoutNode {
public:
    /**
     * @brief Node type discriminator
     */
    enum class Type {
        Leaf, // Contains a DockPanel
        Split // Contains child nodes
    };

    virtual ~DockLayoutNode() = default;

    // === Type ===

    virtual Type type() const = 0;

    bool isLeaf() const { return type() == Type::Leaf; }
    bool isSplit() const { return type() == Type::Split; }

    // === Bounds ===

    /**
     * @brief Get current bounds
     */
    const QRect& bounds() const { return m_bounds; }

    /**
     * @brief Set bounds from parent (triggers layout of children)
     *
     * This is the main layout entry point. When called:
     * - Leaf nodes update their panel's geometry
     * - Split nodes distribute space among children and recurse
     */
    virtual void setBounds(const QRect& bounds) = 0;

    // === Size Constraints ===

    /**
     * @brief Get aggregated size constraints
     *
     * - Leaf nodes return their panel's constraints
     * - Split nodes aggregate children's constraints
     */
    virtual NodeSizeConstraints sizeConstraints() const = 0;

    // === Anchor ===

    /**
     * @brief Get anchor mode
     */
    Anchor anchor() const { return m_anchor; }

    /**
     * @brief Set anchor mode
     *
     * Anchor determines behavior when parent is resized:
     * - None: Scale proportionally with siblings
     * - Left/Top: Maintain size from that edge
     * - Right/Bottom: Maintain size from that edge
     */
    void setAnchor(Anchor anchor) { m_anchor = anchor; }

    /**
     * @brief Get anchored size (size to maintain when anchored)
     *
     * @return Anchored size, or 0 if using current size
     */
    int anchoredSize() const { return m_anchoredSize; }

    /**
     * @brief Set anchored size
     *
     * When anchor != None, this size is preserved during parent resize.
     * If 0, current size is used.
     */
    void setAnchoredSize(int size) { m_anchoredSize = qMax(0, size); }

    /**
     * @brief Capture current size as anchored size
     *
     * Call this after a resize operation to "lock in" the current size.
     */
    void captureAnchoredSize(SplitDirection parentDirection)
    {
        if (parentDirection == SplitDirection::Horizontal) {
            m_anchoredSize = m_bounds.width();
        } else {
            m_anchoredSize = m_bounds.height();
        }
    }

    // === Parent ===

    /**
     * @brief Get parent node (nullptr for root)
     */
    DockLayoutNode* parent() const { return m_parent; }

    /**
     * @brief Set parent (called internally during tree operations)
     */
    void setParent(DockLayoutNode* parent) { m_parent = parent; }

    // === Debug ===

    /**
     * @brief Get depth in tree (0 for root)
     */
    int depth() const
    {
        int d = 0;
        DockLayoutNode* p = m_parent;
        while (p) {
            ++d;
            p = p->m_parent;
        }
        return d;
    }

    /**
     * @brief Debug string representation
     */
    virtual QString debugString() const = 0;

protected:
    DockLayoutNode() = default;

    QRect m_bounds;
    DockLayoutNode* m_parent = nullptr;
    Anchor m_anchor = Anchor::None;
    int m_anchoredSize = 0;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_NODE_H
