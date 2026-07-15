// SPDX-License-Identifier: MPL-2.0

// DockLayoutTypes.h
#ifndef RUWA_UI_DOCKING_LAYOUT_TYPES_H
#define RUWA_UI_DOCKING_LAYOUT_TYPES_H

#include <QRect>
#include <QList>
#include <QSize>

namespace ruwa::ui::docking {

/**
 * @brief Direction for splitting
 */
enum class SplitDirection {
    Horizontal, // Children arranged left-to-right
    Vertical // Children arranged top-to-bottom
};

/**
 * @brief Anchor for resize behavior
 *
 * Determines how a node behaves when its parent is resized:
 * - None: Scales proportionally with siblings
 * - Left/Top: Maintains size from left/top edge (shrinks from right/bottom)
 * - Right/Bottom: Maintains size from right/bottom edge (shrinks from left/top)
 */
enum class Anchor {
    None, // Flexible - scales proportionally
    Left, // Anchored to left edge (horizontal split)
    Right, // Anchored to right edge (horizontal split)
    Top, // Anchored to top edge (vertical split)
    Bottom // Anchored to bottom edge (vertical split)
};

/**
 * @brief Check if anchor is valid for a split direction
 */
inline bool isAnchorValidForDirection(Anchor anchor, SplitDirection dir)
{
    if (anchor == Anchor::None)
        return true;
    if (dir == SplitDirection::Horizontal) {
        return anchor == Anchor::Left || anchor == Anchor::Right;
    } else {
        return anchor == Anchor::Top || anchor == Anchor::Bottom;
    }
}

/**
 * @brief Size constraints for a layout node
 *
 * Used for both leaf nodes (panels) and split nodes (aggregated from children).
 */
struct NodeSizeConstraints {
    int minWidth = 100;
    int minHeight = 100;
    int maxWidth = 16777215;
    int maxHeight = 16777215;
    int preferredWidth = 250;
    int preferredHeight = 300;

    bool isValid() const
    {
        return minWidth > 0 && minHeight > 0 && maxWidth >= minWidth && maxHeight >= minHeight;
    }

    QSize minimumSize() const { return QSize(minWidth, minHeight); }
    QSize maximumSize() const { return QSize(maxWidth, maxHeight); }
    QSize preferredSize() const { return QSize(preferredWidth, preferredHeight); }

    int minInDirection(SplitDirection dir) const
    {
        return (dir == SplitDirection::Horizontal) ? minWidth : minHeight;
    }

    int maxInDirection(SplitDirection dir) const
    {
        return (dir == SplitDirection::Horizontal) ? maxWidth : maxHeight;
    }

    int preferredInDirection(SplitDirection dir) const
    {
        return (dir == SplitDirection::Horizontal) ? preferredWidth : preferredHeight;
    }

    int crossMin(SplitDirection dir) const
    {
        return (dir == SplitDirection::Horizontal) ? minHeight : minWidth;
    }

    /**
     * @brief Merge two constraints (for combining sibling nodes in perpendicular direction)
     */
    static NodeSizeConstraints merge(
        const NodeSizeConstraints& a, const NodeSizeConstraints& b, SplitDirection dir)
    {
        NodeSizeConstraints result;
        if (dir == SplitDirection::Horizontal) {
            // Horizontal split: widths add, heights take max of mins
            result.minWidth = a.minWidth + b.minWidth;
            result.minHeight = qMax(a.minHeight, b.minHeight);
            result.maxWidth = qMin(a.maxWidth + b.maxWidth, 16777215);
            result.maxHeight = qMin(a.maxHeight, b.maxHeight);
            result.preferredWidth = a.preferredWidth + b.preferredWidth;
            result.preferredHeight = qMax(a.preferredHeight, b.preferredHeight);
        } else {
            // Vertical split: heights add, widths take max of mins
            result.minWidth = qMax(a.minWidth, b.minWidth);
            result.minHeight = a.minHeight + b.minHeight;
            result.maxWidth = qMin(a.maxWidth, b.maxWidth);
            result.maxHeight = qMin(a.maxHeight + b.maxHeight, 16777215);
            result.preferredWidth = qMax(a.preferredWidth, b.preferredWidth);
            result.preferredHeight = a.preferredHeight + b.preferredHeight;
        }
        return result;
    }
};

/**
 * @brief Constraints for a layout item (legacy, use NodeSizeConstraints for new code)
 */
struct LayoutConstraints {
    int minWidth = 100;
    int minHeight = 100;
    int maxWidth = 16777215;
    int maxHeight = 16777215;
    int preferredWidth = 250;
    int preferredHeight = 300;

    bool isValid() const { return minWidth > 0 && minHeight > 0; }

    int minInDirection(SplitDirection dir) const
    {
        return (dir == SplitDirection::Horizontal) ? minWidth : minHeight;
    }

    int preferredInDirection(SplitDirection dir) const
    {
        return (dir == SplitDirection::Horizontal) ? preferredWidth : preferredHeight;
    }

    NodeSizeConstraints toNodeConstraints() const
    {
        NodeSizeConstraints c;
        c.minWidth = minWidth;
        c.minHeight = minHeight;
        c.maxWidth = maxWidth;
        c.maxHeight = maxHeight;
        c.preferredWidth = preferredWidth;
        c.preferredHeight = preferredHeight;
        return c;
    }

    static LayoutConstraints fromNodeConstraints(const NodeSizeConstraints& nc)
    {
        LayoutConstraints c;
        c.minWidth = nc.minWidth;
        c.minHeight = nc.minHeight;
        c.maxWidth = nc.maxWidth;
        c.maxHeight = nc.maxHeight;
        c.preferredWidth = nc.preferredWidth;
        c.preferredHeight = nc.preferredHeight;
        return c;
    }
};

/**
 * @brief Result of layout calculation for one item
 */
struct LayoutRect {
    QRect rect;
    int index = -1;

    bool isValid() const { return rect.isValid() && index >= 0; }
};

/**
 * @brief Input for layout calculation
 */
struct LayoutInput {
    QRect availableRect;
    SplitDirection direction;
    QList<LayoutConstraints> itemConstraints;
    QList<double> proportions; // 0.0-1.0 for each item, must sum to 1.0
    int handleSize = 6;
};

/**
 * @brief Output of layout calculation
 */
struct LayoutOutput {
    QList<LayoutRect> itemRects;
    QList<QRect> handleRects;
    bool success = false;
    QString errorMessage;
};

/**
 * @brief Item with anchor information for anchored resize calculation
 */
struct AnchoredItem {
    int currentSize = 0; // Current size in split direction
    int minSize = 100; // Minimum allowed size
    int maxSize = 16777215; // Maximum allowed size
    Anchor anchor = Anchor::None;
    int anchoredSize = 0; // Size to maintain when anchored (0 = use current)
};

/**
 * @brief Input for anchored layout calculation
 */
struct AnchoredLayoutInput {
    QList<AnchoredItem> items;
    int totalAvailableSize = 0; // Total space available for items (excluding handles)
    int handleSize = 6;
    SplitDirection direction = SplitDirection::Horizontal;
};

/**
 * @brief Output of anchored layout calculation
 */
struct AnchoredLayoutOutput {
    QList<int> sizes; // Calculated size for each item
    bool success = false;
    QString errorMessage;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_TYPES_H
