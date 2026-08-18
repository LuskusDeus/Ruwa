// SPDX-License-Identifier: MPL-2.0

// DockLeafNode.cpp
#include "DockLeafNode.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QStringList>

namespace ruwa::ui::docking {

DockLeafNode::DockLeafNode(DockPanel* panel)
{
    if (panel) {
        m_panels.append(panel);
        m_currentIndex = 0;
    }
}

// ============================================================================
// Members
// ============================================================================

void DockLeafNode::clampCurrentIndex()
{
    if (m_panels.isEmpty()) {
        m_currentIndex = -1;
        return;
    }
    m_currentIndex = qBound(0, m_currentIndex, static_cast<int>(m_panels.size()) - 1);
}

void DockLeafNode::setPanel(DockPanel* panel)
{
    m_panels.clear();
    if (panel) {
        m_panels.append(panel);
        m_currentIndex = 0;
    } else {
        m_currentIndex = -1;
    }
}

DockPanel* DockLeafNode::takePanel()
{
    DockPanel* current = panel();
    if (current) {
        removePanel(current);
    }
    return current;
}

void DockLeafNode::addPanel(DockPanel* panel)
{
    insertPanel(panelCount(), panel);
}

void DockLeafNode::insertPanel(int index, DockPanel* panel)
{
    if (!panel || containsPanel(panel)) {
        return;
    }

    index = qBound(0, index, static_cast<int>(m_panels.size()));
    m_panels.insert(index, panel);

    if (m_currentIndex < 0) {
        m_currentIndex = index;
    } else if (index <= m_currentIndex) {
        // Keep pointing at the same panel.
        ++m_currentIndex;
    }
}

bool DockLeafNode::movePanel(DockPanel* panel, int index)
{
    const int from = indexOfPanel(panel);
    if (from < 0 || m_panels.size() < 2) {
        return false;
    }

    index = qBound(0, index, static_cast<int>(m_panels.size()) - 1);
    if (from == index) {
        return false;
    }

    DockPanel* current = currentPanel();
    m_panels.move(from, index);
    m_currentIndex = static_cast<int>(m_panels.indexOf(current));
    return true;
}

bool DockLeafNode::removePanel(DockPanel* panel)
{
    const int index = indexOfPanel(panel);
    if (index < 0) {
        return false;
    }

    m_panels.removeAt(index);

    if (m_panels.isEmpty()) {
        m_currentIndex = -1;
    } else if (index < m_currentIndex) {
        --m_currentIndex;
    } else if (index == m_currentIndex) {
        // Fall back to the previous neighbour, or the new first member.
        m_currentIndex = qMax(0, index - 1);
    }
    clampCurrentIndex();
    return true;
}

// ============================================================================
// Selection
// ============================================================================

bool DockLeafNode::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_panels.size() || index == m_currentIndex) {
        return false;
    }
    m_currentIndex = index;
    return true;
}

bool DockLeafNode::setCurrentPanel(DockPanel* panel)
{
    const int index = indexOfPanel(panel);
    if (index < 0) {
        return false;
    }
    return setCurrentIndex(index);
}

int DockLeafNode::headerHeight() const
{
    return isGroup() ? m_headerHeight : 0;
}

// ============================================================================
// Layout
// ============================================================================

void DockLeafNode::setBounds(const QRect& bounds)
{
    m_bounds = bounds;

    if (!bounds.isValid()) {
        return;
    }

    // Grouped: the host owns the header strip and lays out its members.
    if (QWidget* host = groupHost()) {
        host->setGeometry(bounds);
        return;
    }

    // Ungrouped fallback. Also covers a group whose host has not been attached
    // yet: every member fills the cell, only the current one is visible.
    const QList<DockPanel*> members = panels();
    if (members.size() <= 1) {
        if (DockPanel* p = panel()) {
            p->setGeometry(bounds);
        }
        return;
    }

    DockPanel* current = panel();
    for (DockPanel* member : members) {
        if (!member) {
            continue;
        }
        member->setGeometry(bounds);
        member->setVisible(member == current);
    }
}

NodeSizeConstraints DockLeafNode::sizeConstraints() const
{
    NodeSizeConstraints constraints;

    const QList<DockPanel*> members = panels();
    if (members.isEmpty()) {
        return constraints;
    }

    bool first = true;
    for (DockPanel* member : members) {
        if (!member) {
            continue;
        }

        const PanelSizeHints hints = member->sizeHints();

        NodeSizeConstraints c;
        c.minWidth = hints.minWidth;
        c.minHeight = hints.minHeight;
        c.maxWidth = hints.maxWidth;
        c.maxHeight = hints.maxHeight;

        // Direction-specific effective docked sizes:
        // - preferredWidth is used for horizontal splits (Left/Right positioning)
        // - preferredHeight is used for vertical splits (Top/Bottom positioning)
        // This ensures that docking to Top doesn't affect the remembered width
        // for Left/Right.
        c.preferredWidth = hints.effectiveHorizontalDockedWidth();
        c.preferredHeight = hints.effectiveVerticalDockedHeight();

        // Also respect Qt's minimum size hint
        const QSize minHint = member->minimumSizeHint();
        if (minHint.isValid()) {
            c.minWidth = qMax(c.minWidth, minHint.width());
            c.minHeight = qMax(c.minHeight, minHint.height());
        }

        if (first) {
            constraints = c;
            first = false;
            continue;
        }

        // A group cell must satisfy every member it can show: the tightest
        // floor and the tightest ceiling win.
        constraints.minWidth = qMax(constraints.minWidth, c.minWidth);
        constraints.minHeight = qMax(constraints.minHeight, c.minHeight);
        constraints.maxWidth = qMin(constraints.maxWidth, c.maxWidth);
        constraints.maxHeight = qMin(constraints.maxHeight, c.maxHeight);
        constraints.preferredWidth = qMax(constraints.preferredWidth, c.preferredWidth);
        constraints.preferredHeight = qMax(constraints.preferredHeight, c.preferredHeight);
    }

    // The header strip eats vertical space no member can use.
    const int header = headerHeight();
    if (header > 0) {
        constraints.minHeight += header;
        constraints.preferredHeight += header;
        if (constraints.maxHeight < 16777215 - header) {
            constraints.maxHeight += header;
        }
    }

    // A ceiling that dropped below the floor would make the cell unlayoutable.
    constraints.maxWidth = qMax(constraints.maxWidth, constraints.minWidth);
    constraints.maxHeight = qMax(constraints.maxHeight, constraints.minHeight);

    return constraints;
}

// ============================================================================
// Debug
// ============================================================================

QString DockLeafNode::debugString() const
{
    const QString indent(depth() * 2, ' ');

    QString contents;
    const QList<DockPanel*> members = panels();
    if (members.isEmpty()) {
        contents = QStringLiteral("(empty)");
    } else if (members.size() == 1) {
        contents = members.first()->title();
    } else {
        QStringList titles;
        titles.reserve(members.size());
        for (int i = 0; i < members.size(); ++i) {
            const QString title = members[i] ? members[i]->title() : QStringLiteral("(null)");
            titles << ((i == m_currentIndex) ? QStringLiteral("*%1").arg(title) : title);
        }
        contents = QStringLiteral("group{%1}").arg(titles.join(QStringLiteral(", ")));
    }

    return QStringLiteral("%1Leaf[%2] bounds=%3,%4 %5x%6")
        .arg(indent)
        .arg(contents)
        .arg(m_bounds.x())
        .arg(m_bounds.y())
        .arg(m_bounds.width())
        .arg(m_bounds.height());
}

} // namespace ruwa::ui::docking
