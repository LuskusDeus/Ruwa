// SPDX-License-Identifier: MPL-2.0

// DockLeafNode.h
#ifndef RUWA_UI_DOCKING_LAYOUT_LEAF_NODE_H
#define RUWA_UI_DOCKING_LAYOUT_LEAF_NODE_H

#include "DockLayoutNode.h"

namespace ruwa::ui::docking {

class DockPanel;

/**
 * @brief Leaf node containing a single DockPanel
 *
 * DockLeafNode is the terminal node in the layout tree.
 * It holds a reference to a DockPanel and manages its geometry.
 *
 * When setBounds() is called, the leaf node updates the panel's geometry.
 * The panel widget itself remains a child of the container widget,
 * not of this node (which is not a QWidget).
 */
class DockLeafNode : public DockLayoutNode {
public:
    explicit DockLeafNode(DockPanel* panel = nullptr);
    ~DockLeafNode() override = default;

    // === Type ===

    Type type() const override { return Type::Leaf; }

    // === Panel ===

    /**
     * @brief Get the contained panel
     */
    DockPanel* panel() const { return m_panel; }

    /**
     * @brief Set the contained panel
     *
     * @param panel The panel to contain (may be nullptr)
     */
    void setPanel(DockPanel* panel);

    /**
     * @brief Take the panel (removes and returns it)
     *
     * After this call, panel() returns nullptr.
     * Caller takes ownership of the panel.
     */
    DockPanel* takePanel();

    /**
     * @brief Check if this node has a valid panel
     */
    bool hasPanel() const { return m_panel != nullptr; }

    /**
     * @brief Check if this node is empty (no panel)
     */
    bool isEmpty() const { return m_panel == nullptr; }

    // === Layout ===

    void setBounds(const QRect& bounds) override;
    NodeSizeConstraints sizeConstraints() const override;

    // === Debug ===

    QString debugString() const override;

private:
    DockPanel* m_panel = nullptr;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_LEAF_NODE_H
