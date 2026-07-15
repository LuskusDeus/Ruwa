// SPDX-License-Identifier: MPL-2.0

// DockLayoutCalculator.h
#ifndef RUWA_UI_DOCKING_LAYOUT_CALCULATOR_H
#define RUWA_UI_DOCKING_LAYOUT_CALCULATOR_H

#include "shell/docking/DockLayoutTypes.h"

namespace ruwa::ui::docking {

/**
 * @brief Pure functions for layout calculation
 *
 * No state, no side effects - just math.
 * Easy to test and reason about.
 */
class DockLayoutCalculator {
public:
    // ==================== Legacy API ====================

    /**
     * @brief Calculate layout for items in a container
     *
     * @param input Layout parameters
     * @return Calculated rectangles for items and handles
     */
    static LayoutOutput calculate(const LayoutInput& input);

    /**
     * @brief Calculate new proportions after handle drag
     *
     * @param currentProportions Current proportions
     * @param handleIndex Index of handle being dragged (0 = between item 0 and 1)
     * @param delta Pixels to move (positive = increase left/top item)
     * @param totalSize Total available size
     * @param constraints Constraints for each item
     * @return New proportions respecting constraints
     */
    static QList<double> calculateDragResult(const QList<double>& currentProportions,
        int handleIndex, int delta, int totalSize, const QList<LayoutConstraints>& constraints,
        SplitDirection direction);

    /**
     * @brief Normalize proportions to sum to 1.0
     */
    static QList<double> normalizeProportions(const QList<double>& proportions);

    /**
     * @brief Create equal proportions for N items
     */
    static QList<double> equalProportions(int count);

    /**
     * @brief Calculate minimum total size needed
     */
    static int calculateMinimumSize(
        const QList<LayoutConstraints>& constraints, SplitDirection direction, int handleSize);

    // ==================== Anchored Resize API ====================

    /**
     * @brief Calculate sizes after container resize with anchored items
     *
     * Algorithm:
     * 1. Anchored items keep their anchoredSize (or current size if anchoredSize == 0)
     * 2. Flexible items (Anchor::None) share remaining space proportionally
     * 3. If not enough space, anchored items are shrunk to fit (respecting minimums)
     *
     * @param input Anchored layout parameters
     * @return Calculated sizes for each item
     */
    static AnchoredLayoutOutput calculateAnchoredResize(const AnchoredLayoutInput& input);

    /**
     * @brief Calculate sizes after handle drag with push effect
     *
     * When dragging a handle, only the two adjacent items are affected initially.
     * If one item reaches its minimum, the drag "pushes" through to the next item.
     *
     * @param currentSizes Current sizes of all items
     * @param handleIndex Index of handle being dragged (between item[index] and item[index+1])
     * @param delta Pixels to move (positive = grow item[handleIndex], shrink item[handleIndex+1])
     * @param constraints Size constraints for each item
     * @param direction Split direction
     * @return New sizes respecting all constraints
     */
    static QList<int> calculateDragWithPush(const QList<int>& currentSizes, int handleIndex,
        int delta, const QList<NodeSizeConstraints>& constraints, SplitDirection direction);

    /**
     * @brief Distribute available space among flexible items
     *
     * @param totalSpace Total space to distribute
     * @param itemCount Number of items
     * @param fixedSizes Map of index -> fixed size (for anchored items)
     * @param constraints Constraints for all items
     * @param direction Split direction
     * @return Sizes for all items
     */
    static QList<int> distributeFlexibleSpace(int totalSpace, int itemCount,
        const QMap<int, int>& fixedSizes, const QList<NodeSizeConstraints>& constraints,
        SplitDirection direction);

    /**
     * @brief Calculate minimum size for NodeSizeConstraints
     */
    static int calculateMinimumSize(
        const QList<NodeSizeConstraints>& constraints, SplitDirection direction, int handleSize);

    /**
     * @brief Clamp a size to constraints
     */
    static int clampSize(
        int size, const NodeSizeConstraints& constraints, SplitDirection direction);

private:
    /**
     * @brief Distribute space among items respecting minimums (legacy)
     */
    static QList<int> distributeSpace(int availableSpace, const QList<double>& proportions,
        const QList<LayoutConstraints>& constraints, SplitDirection direction);

    /**
     * @brief Apply push effect in one direction
     *
     * @param sizes Current sizes (modified in place)
     * @param startIndex Index to start pushing from
     * @param direction +1 for pushing right/down, -1 for pushing left/up
     * @param deficit Amount of space needed
     * @param constraints Size constraints
     * @param splitDir Split direction
     * @return Amount of deficit that couldn't be resolved
     */
    static int applyPush(QList<int>& sizes, int startIndex, int pushDirection, int deficit,
        const QList<NodeSizeConstraints>& constraints, SplitDirection splitDir);
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_CALCULATOR_H
