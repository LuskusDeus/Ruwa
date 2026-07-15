// SPDX-License-Identifier: MPL-2.0

// DockLeafNode.cpp
#include "DockLeafNode.h"
#include "shell/docking/widgets/DockPanel.h"

namespace ruwa::ui::docking {

DockLeafNode::DockLeafNode(DockPanel* panel)
    : m_panel(panel)
{
}

void DockLeafNode::setPanel(DockPanel* panel)
{
    m_panel = panel;
}

DockPanel* DockLeafNode::takePanel()
{
    DockPanel* panel = m_panel;
    m_panel = nullptr;
    return panel;
}

void DockLeafNode::setBounds(const QRect& bounds)
{
    m_bounds = bounds;

    // Update panel geometry if we have a valid panel
    if (m_panel && bounds.isValid()) {
        m_panel->setGeometry(bounds);
    }
}

NodeSizeConstraints DockLeafNode::sizeConstraints() const
{
    NodeSizeConstraints constraints;

    if (m_panel) {
        // Get constraints from panel's size hints
        PanelSizeHints hints = m_panel->sizeHints();

        constraints.minWidth = hints.minWidth;
        constraints.minHeight = hints.minHeight;
        constraints.maxWidth = hints.maxWidth;
        constraints.maxHeight = hints.maxHeight;

        // Use direction-specific effective docked sizes:
        // - preferredWidth is used for horizontal splits (Left/Right positioning)
        // - preferredHeight is used for vertical splits (Top/Bottom positioning)
        // This ensures that docking to Top doesn't affect the remembered width for Left/Right
        constraints.preferredWidth = hints.effectiveHorizontalDockedWidth();
        constraints.preferredHeight = hints.effectiveVerticalDockedHeight();

        // Also respect Qt's minimum size hint
        QSize minHint = m_panel->minimumSizeHint();
        if (minHint.isValid()) {
            constraints.minWidth = qMax(constraints.minWidth, minHint.width());
            constraints.minHeight = qMax(constraints.minHeight, minHint.height());
        }
    }

    return constraints;
}

QString DockLeafNode::debugString() const
{
    QString indent(depth() * 2, ' ');
    QString panelName = m_panel ? m_panel->title() : QStringLiteral("(empty)");
    return QStringLiteral("%1Leaf[%2] bounds=%3,%4 %5x%6")
        .arg(indent)
        .arg(panelName)
        .arg(m_bounds.x())
        .arg(m_bounds.y())
        .arg(m_bounds.width())
        .arg(m_bounds.height());
}

} // namespace ruwa::ui::docking
