// SPDX-License-Identifier: MPL-2.0

// DockSplitNode.cpp
#include "DockSplitNode.h"
#include "DockLayoutCalculator.h"

#include <algorithm>
#include <numeric>

namespace ruwa::ui::docking {

DockSplitNode::DockSplitNode(SplitDirection direction)
    : m_direction(direction)
{
}

DockSplitNode::~DockSplitNode()
{
    // Children are unique_ptr, auto-deleted
}

void DockSplitNode::setDirection(SplitDirection direction)
{
    if (m_direction == direction) {
        return;
    }

    m_direction = direction;

    // Reset sizes since direction changed
    m_sizes.clear();
    synchronizeSizes();

    // Relayout if we have bounds
    if (m_bounds.isValid()) {
        layoutChildren();
    }
}

DockLayoutNode* DockSplitNode::childAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_children.size())) {
        return nullptr;
    }
    return m_children[index].get();
}

int DockSplitNode::indexOf(DockLayoutNode* child) const
{
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (m_children[i].get() == child) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DockSplitNode::addChild(DockLayoutNodePtr child)
{
    if (!child)
        return;

    child->setParent(this);
    m_children.push_back(std::move(child));
    m_sizes.append(0); // Will be set during layout
    synchronizeSizes();
}

void DockSplitNode::insertChild(int index, DockLayoutNodePtr child)
{
    if (!child)
        return;

    index = qBound(0, index, static_cast<int>(m_children.size()));

    child->setParent(this);
    m_children.insert(m_children.begin() + index, std::move(child));
    m_sizes.insert(index, 0);
    synchronizeSizes();
}

DockLayoutNodePtr DockSplitNode::removeChildAt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_children.size())) {
        return nullptr;
    }

    DockLayoutNodePtr child = std::move(m_children[index]);
    child->setParent(nullptr);
    m_children.erase(m_children.begin() + index);

    // Redistribute the removed child's size ONLY to adjacent children
    int removedSize = (index < m_sizes.size()) ? m_sizes[index] : 0;
    m_sizes.removeAt(index);

    if (!m_sizes.isEmpty() && removedSize > 0) {
        // After removal, the neighbors are:
        // - Left neighbor: was at index-1, still at index-1
        // - Right neighbor: was at index+1, now at index (due to removal)
        int leftIdx = index - 1;
        int rightIdx = index; // This was index+1 before removal

        bool hasLeft = leftIdx >= 0;
        bool hasRight = rightIdx < m_sizes.size();

        if (hasLeft && hasRight) {
            // Both neighbors exist - split between them proportionally
            int leftSize = m_sizes[leftIdx];
            int rightSize = m_sizes[rightIdx];
            int totalNeighbors = leftSize + rightSize;

            if (totalNeighbors > 0) {
                int leftShare = static_cast<int>(
                    removedSize * (static_cast<double>(leftSize) / totalNeighbors));
                m_sizes[leftIdx] += leftShare;
                m_sizes[rightIdx] += removedSize - leftShare;
            } else {
                // Both neighbors have size 0 - split evenly
                int half = removedSize / 2;
                m_sizes[leftIdx] += half;
                m_sizes[rightIdx] += removedSize - half;
            }
        } else if (hasLeft) {
            // Only left neighbor exists - give all to it
            m_sizes[leftIdx] += removedSize;
        } else if (hasRight) {
            // Only right neighbor exists - give all to it
            m_sizes[rightIdx] += removedSize;
        }
        // If neither exists, we have an empty split (shouldn't happen normally)
    }

    return child;
}

DockLayoutNodePtr DockSplitNode::removeChild(DockLayoutNode* child)
{
    int index = indexOf(child);
    if (index < 0) {
        return nullptr;
    }
    return removeChildAt(index);
}

DockLayoutNodePtr DockSplitNode::replaceChild(int index, DockLayoutNodePtr newChild)
{
    if (index < 0 || index >= static_cast<int>(m_children.size()) || !newChild) {
        return nullptr;
    }

    DockLayoutNodePtr oldChild = std::move(m_children[index]);
    oldChild->setParent(nullptr);

    newChild->setParent(this);
    m_children[index] = std::move(newChild);

    // Preserve size for the replaced child
    return oldChild;
}

void DockSplitNode::setSizes(const QList<int>& sizes)
{
    if (sizes.size() != static_cast<int>(m_children.size())) {
        return;
    }

    m_sizes = sizes;

    // Clamp to constraints
    for (size_t i = 0; i < m_children.size(); ++i) {
        NodeSizeConstraints constraints = m_children[i]->sizeConstraints();
        int minSize = constraints.minInDirection(m_direction);
        int maxSize = constraints.maxInDirection(m_direction);
        m_sizes[static_cast<int>(i)] = qBound(minSize, m_sizes[static_cast<int>(i)], maxSize);
    }

    layoutChildren();
}

void DockSplitNode::handleDrag(int handleIndex, int delta)
{
    if (handleIndex < 0 || handleIndex >= static_cast<int>(m_children.size()) - 1) {
        return;
    }

    if (delta == 0) {
        return;
    }

    // Build constraints list
    QList<NodeSizeConstraints> constraints;
    for (const auto& child : m_children) {
        constraints.append(child->sizeConstraints());
    }

    // Calculate new sizes with push effect
    QList<int> newSizes = DockLayoutCalculator::calculateDragWithPush(
        m_sizes, handleIndex, delta, constraints, m_direction);

    // Apply new sizes
    applySizes(newSizes);
}

void DockSplitNode::setBounds(const QRect& bounds)
{
    QRect oldBounds = m_bounds;
    m_bounds = bounds;

    if (m_children.empty()) {
        return;
    }

    if (!bounds.isValid()) {
        return;
    }

    // Ensure sizes array is correct length
    synchronizeSizes();

    // Check if any size is zero (new child added) or all sizes are zero (initial)
    bool hasZeroSize = std::any_of(m_sizes.begin(), m_sizes.end(), [](int s) { return s == 0; });
    bool needsRedistribute = m_sizes.isEmpty() || hasZeroSize;
    const bool needsRepair = !needsRedistribute && hasInvalidSizes();

    // Check if this is a resize (bounds changed, not initial layout)
    bool isResize = oldBounds.isValid() && (sizeInDirection(oldBounds) != sizeInDirection(bounds))
        && !needsRedistribute;

    if (isResize) {
        // Apply anchored resize algorithm
        AnchoredLayoutInput input;
        input.items = buildAnchoredItems();
        input.totalAvailableSize = availableSpace();
        input.handleSize = m_handleSize;
        input.direction = m_direction;

        AnchoredLayoutOutput output = DockLayoutCalculator::calculateAnchoredResize(input);

        if (output.success) {
            m_sizes = output.sizes;
        }
    } else if (needsRepair) {
        normalizeSizesToAvailableSpace();
    } else if (needsRedistribute) {
        // Initial layout or new child added - distribute space
        int space = availableSpace();
        int childCount = static_cast<int>(m_children.size());

        // Calculate total of existing non-zero sizes
        int existingTotal = 0;
        int existingCount = 0;
        for (int s : m_sizes) {
            if (s > 0) {
                existingTotal += s;
                existingCount++;
            }
        }

        int newCount = childCount - existingCount;

        if (existingCount == 0 || existingTotal == 0) {
            // All new - use anchored layout if any child has anchor (respects fixed sizes),
            // otherwise distribute proportionally to preferred sizes
            QList<AnchoredItem> items = buildAnchoredItems();
            const bool hasAnchoredItems = std::any_of(items.begin(), items.end(),
                [](const AnchoredItem& item) { return item.anchor != Anchor::None; });

            if (hasAnchoredItems) {
                AnchoredLayoutInput input;
                input.items = std::move(items);
                input.totalAvailableSize = space;
                input.handleSize = m_handleSize;
                input.direction = m_direction;

                AnchoredLayoutOutput output = DockLayoutCalculator::calculateAnchoredResize(input);
                if (output.success) {
                    m_sizes = output.sizes;
                }
            } else {
                // Proportional distribution based on preferred sizes
                int totalPreferred = 0;
                for (int i = 0; i < childCount; ++i) {
                    NodeSizeConstraints c = m_children[i]->sizeConstraints();
                    int pref = (m_direction == SplitDirection::Horizontal) ? c.preferredWidth
                                                                           : c.preferredHeight;
                    totalPreferred += qMax(1, pref);
                }

                m_sizes.clear();
                int remaining = space;
                for (int i = 0; i < childCount; ++i) {
                    NodeSizeConstraints c = m_children[i]->sizeConstraints();
                    int pref = (m_direction == SplitDirection::Horizontal) ? c.preferredWidth
                                                                           : c.preferredHeight;
                    pref = qMax(1, pref);

                    int size;
                    if (i == childCount - 1) {
                        size = remaining;
                    } else {
                        size = (totalPreferred > 0)
                            ? static_cast<int>(static_cast<double>(pref) / totalPreferred * space)
                            : space / childCount;
                    }

                    int minSize = c.minInDirection(m_direction);
                    int maxSize = c.maxInDirection(m_direction);
                    size = qBound(minSize, size, maxSize);
                    size = qMax(1, size);

                    m_sizes.append(size);
                    remaining -= size;
                }
            }
        } else if (newCount > 0) {
            // Some existing, some new - take space ONLY from neighbors of new panels
            // Non-adjacent panels should not change size unless neighbors are at minimum

            // Process each new child (where m_sizes[i] == 0)
            for (int i = 0; i < childCount; ++i) {
                if (m_sizes[i] != 0)
                    continue; // Skip existing children

                NodeSizeConstraints c = m_children[i]->sizeConstraints();
                int pref = (m_direction == SplitDirection::Horizontal) ? c.preferredWidth
                                                                       : c.preferredHeight;
                int minSize = c.minInDirection(m_direction);
                int needed = qMax(minSize, pref);

                int taken = 0;

                // Try to take from LEFT neighbors first (closest first)
                for (int leftIdx = i - 1; leftIdx >= 0 && taken < needed; --leftIdx) {
                    if (m_sizes[leftIdx] <= 0)
                        continue; // Skip other new panels

                    int leftMin
                        = m_children[leftIdx]->sizeConstraints().minInDirection(m_direction);
                    int canTake = m_sizes[leftIdx] - leftMin;
                    if (canTake > 0) {
                        int take = qMin(canTake, needed - taken);
                        m_sizes[leftIdx] -= take;
                        taken += take;
                    }
                }

                // Try to take from RIGHT neighbors (closest first)
                for (int rightIdx = i + 1; rightIdx < childCount && taken < needed; ++rightIdx) {
                    if (m_sizes[rightIdx] <= 0)
                        continue; // Skip other new panels

                    int rightMin
                        = m_children[rightIdx]->sizeConstraints().minInDirection(m_direction);
                    int canTake = m_sizes[rightIdx] - rightMin;
                    if (canTake > 0) {
                        int take = qMin(canTake, needed - taken);
                        m_sizes[rightIdx] -= take;
                        taken += take;
                    }
                }

                // Assign the taken space to the new child
                m_sizes[i] = qMax(taken, minSize);
            }

            // Final adjustment: ensure total matches available space
            int totalUsed = 0;
            for (int s : m_sizes) {
                totalUsed += s;
            }

            int diff = space - totalUsed;
            if (diff != 0) {
                // Find a flexible panel to adjust (prefer existing panels)
                for (int i = childCount - 1; i >= 0 && diff != 0; --i) {
                    if (m_sizes[i] > 0) {
                        NodeSizeConstraints c = m_children[i]->sizeConstraints();
                        int minSize = c.minInDirection(m_direction);
                        int maxSize = c.maxInDirection(m_direction);

                        int newSize = m_sizes[i] + diff;
                        newSize = qBound(minSize, newSize, maxSize);
                        int actualDiff = newSize - m_sizes[i];
                        m_sizes[i] = newSize;
                        diff -= actualDiff;
                    }
                }
            }
        }
    }

    // Ensure no sizes are zero after distribution (prevents re-triggering needsRedistribute)
    for (int i = 0; i < m_sizes.size(); ++i) {
        if (m_sizes[i] <= 0) {
            int minSize = m_children[i]->sizeConstraints().minInDirection(m_direction);
            m_sizes[i] = qMax(1, minSize);
        }
    }

    // Layout children with current sizes
    layoutChildren();
}

NodeSizeConstraints DockSplitNode::sizeConstraints() const
{
    if (m_children.empty()) {
        return NodeSizeConstraints();
    }

    // Start with first child's constraints
    NodeSizeConstraints result = m_children[0]->sizeConstraints();

    // Merge with remaining children
    for (size_t i = 1; i < m_children.size(); ++i) {
        NodeSizeConstraints childConstraints = m_children[i]->sizeConstraints();
        result = NodeSizeConstraints::merge(result, childConstraints, m_direction);
    }

    // Add handle sizes to minimum
    int totalHandleSize = (static_cast<int>(m_children.size()) - 1) * m_handleSize;
    if (m_direction == SplitDirection::Horizontal) {
        result.minWidth += totalHandleSize;
        result.preferredWidth += totalHandleSize;
    } else {
        result.minHeight += totalHandleSize;
        result.preferredHeight += totalHandleSize;
    }

    return result;
}

DockLeafNode* DockSplitNode::findLeafForPanel(DockPanel* panel) const
{
    for (const auto& child : m_children) {
        if (child->isLeaf()) {
            auto* leaf = static_cast<DockLeafNode*>(child.get());
            if (leaf->panel() == panel) {
                return leaf;
            }
        } else if (child->isSplit()) {
            auto* split = static_cast<DockSplitNode*>(child.get());
            DockLeafNode* found = split->findLeafForPanel(panel);
            if (found) {
                return found;
            }
        }
    }
    return nullptr;
}

QList<DockLeafNode*> DockSplitNode::allLeaves() const
{
    QList<DockLeafNode*> result;

    for (const auto& child : m_children) {
        if (child->isLeaf()) {
            result.append(static_cast<DockLeafNode*>(child.get()));
        } else if (child->isSplit()) {
            result.append(static_cast<DockSplitNode*>(child.get())->allLeaves());
        }
    }

    return result;
}

QString DockSplitNode::debugString() const
{
    QString indent(depth() * 2, ' ');
    QString dirStr = (m_direction == SplitDirection::Horizontal) ? "H" : "V";

    QString result = QStringLiteral("%1Split[%2] bounds=%3,%4 %5x%6 sizes=[")
                         .arg(indent)
                         .arg(dirStr)
                         .arg(m_bounds.x())
                         .arg(m_bounds.y())
                         .arg(m_bounds.width())
                         .arg(m_bounds.height());

    for (int i = 0; i < m_sizes.size(); ++i) {
        if (i > 0)
            result += ",";
        result += QString::number(m_sizes[i]);
    }
    result += "]\n";

    for (const auto& child : m_children) {
        result += child->debugString() + "\n";
    }

    return result;
}

void DockSplitNode::layoutChildren()
{
    if (m_children.empty() || !m_bounds.isValid()) {
        return;
    }

    const bool horizontal = (m_direction == SplitDirection::Horizontal);
    const int crossSz = crossSize(m_bounds);
    const int childCount = static_cast<int>(m_children.size());
    const int extent = horizontal ? m_bounds.right() : m_bounds.bottom();

    int pos = horizontal ? m_bounds.left() : m_bounds.top();

    for (int i = 0; i < childCount; ++i) {
        // Last child extends to bounds edge to avoid empty space from rounding
        int size = m_sizes.value(i, 0);
        if (i == childCount - 1 && size > 0) {
            int remaining = extent - pos + 1;
            if (remaining > 0) {
                size = remaining;
            }
        }

        // Calculate child bounds
        QRect childBounds;
        if (horizontal) {
            childBounds = QRect(pos, m_bounds.top(), size, crossSz);
        } else {
            childBounds = QRect(m_bounds.left(), pos, crossSz, size);
        }

        // Set child bounds (recursive)
        m_children[i]->setBounds(childBounds);

        pos += m_sizes[i];

        // Handle geometry callback (except after last child)
        if (i < childCount - 1 && m_handleGeometryCallback) {
            QRect handleRect;
            if (horizontal) {
                handleRect = QRect(pos, m_bounds.top(), m_handleSize, crossSz);
            } else {
                handleRect = QRect(m_bounds.left(), pos, crossSz, m_handleSize);
            }
            m_handleGeometryCallback(i, handleRect);
            pos += m_handleSize;
        }
    }
}

void DockSplitNode::synchronizeSizes()
{
    const int childCount = static_cast<int>(m_children.size());
    while (m_sizes.size() < childCount) {
        m_sizes.append(0);
    }
    while (m_sizes.size() > childCount) {
        m_sizes.removeLast();
    }
}

bool DockSplitNode::hasInvalidSizes() const
{
    if (m_children.empty() || m_sizes.size() != static_cast<int>(m_children.size())) {
        return true;
    }

    const int space = availableSpace();
    if (space <= 0) {
        return true;
    }

    int total = 0;
    for (int i = 0; i < m_sizes.size(); ++i) {
        const NodeSizeConstraints constraints
            = m_children[static_cast<size_t>(i)]->sizeConstraints();
        const int minSize = constraints.minInDirection(m_direction);
        const int maxSize = constraints.maxInDirection(m_direction);
        const int size = m_sizes[i];

        if (size < minSize || size > maxSize) {
            return true;
        }

        total += size;
    }

    return total != space;
}

void DockSplitNode::normalizeSizesToAvailableSpace()
{
    if (m_children.empty()) {
        m_sizes.clear();
        return;
    }

    synchronizeSizes();

    const int childCount = static_cast<int>(m_children.size());
    const int space = qMax(0, availableSpace());
    QList<int> minimums;
    QList<int> maximums;
    minimums.reserve(childCount);
    maximums.reserve(childCount);

    int total = 0;
    for (int i = 0; i < childCount; ++i) {
        const NodeSizeConstraints constraints
            = m_children[static_cast<size_t>(i)]->sizeConstraints();
        const int minSize = constraints.minInDirection(m_direction);
        const int maxSize = constraints.maxInDirection(m_direction);
        minimums.append(minSize);
        maximums.append(maxSize);

        int size = m_sizes.value(i, 0);
        size = qBound(minSize, size, maxSize);
        m_sizes[i] = size;
        total += size;
    }

    if (total > space) {
        int overflow = total - space;
        for (int i = childCount - 1; i >= 0 && overflow > 0; --i) {
            const int shrinkable = m_sizes[i] - minimums[i];
            if (shrinkable <= 0) {
                continue;
            }

            const int take = qMin(shrinkable, overflow);
            m_sizes[i] -= take;
            overflow -= take;
        }
    } else if (total < space) {
        int remainder = space - total;
        for (int i = childCount - 1; i >= 0 && remainder > 0; --i) {
            const int growable = maximums[i] - m_sizes[i];
            if (growable <= 0) {
                continue;
            }

            const int add = qMin(growable, remainder);
            m_sizes[i] += add;
            remainder -= add;
        }

        if (remainder > 0 && !m_sizes.isEmpty()) {
            m_sizes.last() += remainder;
        }
    }
}

int DockSplitNode::availableSpace() const
{
    int total = sizeInDirection(m_bounds);
    int handles = qMax(0, static_cast<int>(m_children.size()) - 1) * m_handleSize;
    return total - handles;
}

int DockSplitNode::sizeInDirection(const QRect& rect) const
{
    return (m_direction == SplitDirection::Horizontal) ? rect.width() : rect.height();
}

int DockSplitNode::crossSize(const QRect& rect) const
{
    return (m_direction == SplitDirection::Horizontal) ? rect.height() : rect.width();
}

QList<AnchoredItem> DockSplitNode::buildAnchoredItems() const
{
    QList<AnchoredItem> items;
    const int childCount = static_cast<int>(m_children.size());

    for (int i = 0; i < childCount; ++i) {
        const auto& child = m_children[i];
        NodeSizeConstraints constraints = child->sizeConstraints();

        AnchoredItem item;
        item.currentSize = m_sizes[i];
        item.minSize = constraints.minInDirection(m_direction);
        item.maxSize = constraints.maxInDirection(m_direction);
        item.anchor = child->anchor();
        item.anchoredSize = child->anchoredSize();

        items.append(item);
    }

    return items;
}

void DockSplitNode::applySizes(const QList<int>& newSizes)
{
    if (newSizes.size() != static_cast<int>(m_children.size())) {
        return;
    }

    m_sizes = newSizes;
    layoutChildren();
}

} // namespace ruwa::ui::docking
