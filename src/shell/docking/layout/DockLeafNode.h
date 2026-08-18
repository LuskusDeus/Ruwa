// SPDX-License-Identifier: MPL-2.0

// DockLeafNode.h
#ifndef RUWA_UI_DOCKING_LAYOUT_LEAF_NODE_H
#define RUWA_UI_DOCKING_LAYOUT_LEAF_NODE_H

#include "DockLayoutNode.h"
#include "shell/docking/DockTypes.h"

#include <QList>

class QWidget;

namespace ruwa::ui::docking {

class DockPanel;

/**
 * @brief Leaf node holding one panel, or a tab group of several
 *
 * DockLeafNode is the terminal node in the layout tree: one rectangle of the
 * layout. That rectangle holds either a single DockPanel, or a *tab group* —
 * an ordered list of panels of which exactly one is visible at a time.
 *
 * The single-panel case is not special-cased: a solo panel is simply a group of
 * one. panel() returns the current member, which keeps every existing caller
 * (which predates groups) correct.
 *
 * Geometry:
 * - Solo leaf: the panel gets the leaf's bounds directly.
 * - Group leaf: the group HOST widget gets the bounds; the host owns the header
 *   strip and positions its members. Until a host is attached (the host widget
 *   lands in a later stage) the leaf falls back to giving every member the full
 *   bounds and showing only the current one.
 *
 * Panels are children of the container widget (or, once grouped, of the host);
 * this node never owns them.
 */
class DockLeafNode : public DockLayoutNode {
public:
    explicit DockLeafNode(DockPanel* panel = nullptr);
    ~DockLeafNode() override = default;

    // === Type ===

    Type type() const override { return Type::Leaf; }

    // === Panels ===

    // NOTE: the plain list accessors below are deliberately inline. This header
    // is pulled into a Qt6::Core-only test target through DockSplitNode.h,
    // which links no DockLeafNode.cpp (it would drag in DockPanel and Widgets),
    // so anything that target reaches must be header-only.

    /**
     * @brief Get the current (visible) panel
     *
     * For a solo leaf this is the only panel. Returns nullptr if empty.
     */
    DockPanel* panel() const
    {
        return (m_currentIndex >= 0 && m_currentIndex < m_panels.size()) ? m_panels[m_currentIndex]
                                                                         : nullptr;
    }

    /// Alias of panel(), for call sites where "current" is the point.
    DockPanel* currentPanel() const { return panel(); }

    /**
     * @brief Replace the entire contents with a single panel
     *
     * Any other members are dropped from the node (but not deleted); use
     * removePanel() if you need to know which.
     */
    void setPanel(DockPanel* panel);

    /**
     * @brief Take the current panel (removes it from the node and returns it)
     *
     * For a group, the remaining members stay and the selection moves to a
     * neighbour. Caller takes responsibility for the returned panel.
     */
    DockPanel* takePanel();

    /// All members, in tab order. Returned by value on purpose: callers iterate
    /// it while removing members (sanitation, ungrouping).
    QList<DockPanel*> panels() const { return m_panels; }

    /// Number of members.
    int panelCount() const { return static_cast<int>(m_panels.size()); }

    /// Member at @p index in tab order, or nullptr if out of range.
    DockPanel* panelAt(int index) const
    {
        return (index >= 0 && index < m_panels.size()) ? m_panels[index] : nullptr;
    }

    /// Tab-order index of @p panel, or -1 if it is not a member.
    int indexOfPanel(DockPanel* panel) const
    {
        return panel ? static_cast<int>(m_panels.indexOf(panel)) : -1;
    }

    bool containsPanel(DockPanel* panel) const { return indexOfPanel(panel) >= 0; }

    /// True when this leaf holds more than one panel (i.e. renders a header).
    bool isGroup() const { return panelCount() > 1; }

    /// Check if this node has at least one live panel.
    bool hasPanel() const { return panelCount() > 0; }

    /// Check if this node is empty (no panels).
    bool isEmpty() const { return panelCount() == 0; }

    /**
     * @brief Append a panel to the end of the tab order
     *
     * No-op if already a member. Does not change the selection.
     */
    void addPanel(DockPanel* panel);

    /**
     * @brief Insert a panel at @p index in the tab order
     *
     * @p index is clamped to [0, panelCount()]. No-op if already a member.
     * Does not change the selection (but keeps it pointing at the same panel).
     */
    void insertPanel(int index, DockPanel* panel);

    /**
     * @brief Move an existing member to @p index in the tab order
     *
     * @p index is the final index after the move and is clamped to the current
     * member range. Keeps the same panel selected.
     *
     * @return true if the order actually changed
     */
    bool movePanel(DockPanel* panel, int index);

    /**
     * @brief Remove a panel from the group
     *
     * If it was the current member, the selection moves to the previous
     * neighbour (or the next one, when removing index 0).
     *
     * @return true if the panel was a member
     */
    bool removePanel(DockPanel* panel);

    // === Selection ===

    /// Index of the visible member, or -1 when empty.
    int currentIndex() const { return m_currentIndex; }

    /**
     * @brief Select the member at @p index
     * @return true if the selection actually changed
     */
    bool setCurrentIndex(int index);

    /**
     * @brief Select @p panel
     * @return true if the selection actually changed
     */
    bool setCurrentPanel(DockPanel* panel);

    // === Group host ===

    /**
     * @brief Widget that frames the group (header + member area)
     *
     * Set by DockLayoutRoot when a leaf becomes a group, cleared when it drops
     * back to a single panel. Not owned by the node. Typed as QWidget until the
     * DockGroupHost widget itself lands (stage 2).
     *
     * Raw, like the members below: this header is compiled into a Qt6::Core-only
     * target (tests/), so QPointer on a forward-declared QObject is not
     * available here. Whoever owns the host must clear this before deleting it.
     */
    QWidget* groupHost() const { return m_groupHost; }
    void setGroupHost(QWidget* host) { m_groupHost = host; }

    /// Height reserved for the group header strip (0 for a solo leaf).
    int headerHeight() const;
    void setHeaderHeight(int height) { m_headerHeight = qMax(0, height); }

    // === Layout ===

    void setBounds(const QRect& bounds) override;
    NodeSizeConstraints sizeConstraints() const override;

    // === Debug ===

    QString debugString() const override;

private:
    void clampCurrentIndex();

private:
    /// Members, in tab order. Raw pointers, as the single-panel node held
    /// before: removal is always explicit (DockLayoutRoot::removePanel, driven
    /// by DockManager's panel lifetime signals), never by pointer expiry.
    QList<DockPanel*> m_panels;
    int m_currentIndex = -1;
    QWidget* m_groupHost = nullptr;
    int m_headerHeight = kDefaultGroupHeaderHeight;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_LEAF_NODE_H
