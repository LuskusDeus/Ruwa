// SPDX-License-Identifier: MPL-2.0

// DockLayoutCalculator.cpp
#include "DockLayoutCalculator.h"
#include <QtMath>
#include <algorithm>
#include <QMap>

namespace ruwa::ui::docking {

LayoutOutput DockLayoutCalculator::calculate(const LayoutInput& input)
{
    LayoutOutput output;

    const int itemCount = input.itemConstraints.size();

    // Validate input
    if (itemCount == 0) {
        output.success = true;
        return output;
    }

    if (input.proportions.size() != itemCount) {
        output.errorMessage = "Proportions count mismatch";
        return output;
    }

    if (!input.availableRect.isValid()) {
        output.errorMessage = "Invalid available rect";
        return output;
    }

    const bool horizontal = (input.direction == SplitDirection::Horizontal);
    const int totalSize = horizontal ? input.availableRect.width() : input.availableRect.height();
    const int crossSize = horizontal ? input.availableRect.height() : input.availableRect.width();
    const int handleCount = itemCount - 1;
    const int totalHandleSize = handleCount * input.handleSize;
    const int availableForItems = totalSize - totalHandleSize;

    // Check minimum space
    int minRequired
        = calculateMinimumSize(input.itemConstraints, input.direction, input.handleSize);
    if (totalSize < minRequired) {
        output.errorMessage
            = QString("Not enough space: need %1, have %2").arg(minRequired).arg(totalSize);
        return output;
    }

    // Distribute space
    QList<int> sizes = distributeSpace(
        availableForItems, input.proportions, input.itemConstraints, input.direction);

    // Build output rectangles
    int pos = horizontal ? input.availableRect.left() : input.availableRect.top();

    for (int i = 0; i < itemCount; ++i) {
        LayoutRect itemRect;
        itemRect.index = i;

        if (horizontal) {
            itemRect.rect = QRect(pos, input.availableRect.top(), sizes[i], crossSize);
        } else {
            itemRect.rect = QRect(input.availableRect.left(), pos, crossSize, sizes[i]);
        }

        output.itemRects.append(itemRect);
        pos += sizes[i];

        // Add handle rect (except after last item)
        if (i < handleCount) {
            QRect handleRect;
            if (horizontal) {
                handleRect = QRect(pos, input.availableRect.top(), input.handleSize, crossSize);
            } else {
                handleRect = QRect(input.availableRect.left(), pos, crossSize, input.handleSize);
            }
            output.handleRects.append(handleRect);
            pos += input.handleSize;
        }
    }

    output.success = true;
    return output;
}

QList<double> DockLayoutCalculator::calculateDragResult(const QList<double>& currentProportions,
    int handleIndex, int delta, int totalSize, const QList<LayoutConstraints>& constraints,
    SplitDirection direction)
{
    if (handleIndex < 0 || handleIndex >= currentProportions.size() - 1) {
        return currentProportions;
    }

    if (delta == 0) {
        return currentProportions;
    }

    const int itemCount = currentProportions.size();
    QList<double> result = currentProportions;

    // Convert delta to proportion change
    double deltaProportion = static_cast<double>(delta) / totalSize;

    // Items affected by this handle
    int leftIndex = handleIndex; // Will grow if delta > 0
    int rightIndex = handleIndex + 1; // Will shrink if delta > 0

    if (delta < 0) {
        std::swap(leftIndex, rightIndex);
        deltaProportion = -deltaProportion;
    }

    // Calculate minimum proportions for constraints
    auto minProportion = [&](int index) -> double {
        int minSize = constraints[index].minInDirection(direction);
        return static_cast<double>(minSize) / totalSize;
    };

    // How much can rightIndex shrink?
    double rightCurrent = result[rightIndex];
    double rightMin = minProportion(rightIndex);
    double maxShrink = rightCurrent - rightMin;

    // Apply as much delta as possible
    double actualDelta = qMin(deltaProportion, maxShrink);

    if (actualDelta > 0) {
        result[leftIndex] += actualDelta;
        result[rightIndex] -= actualDelta;

        // If we couldn't apply full delta, try to push next items
        double remaining = deltaProportion - actualDelta;
        if (remaining > 0.001) { // Small threshold to avoid floating point issues
            // Try to shrink the next item in chain
            int nextIndex = (delta > 0) ? rightIndex + 1 : rightIndex - 1;
            if (nextIndex >= 0 && nextIndex < itemCount && nextIndex != leftIndex) {
                // Recursive push - shrink next item
                double nextCurrent = result[nextIndex];
                double nextMin = minProportion(nextIndex);
                double nextMaxShrink = nextCurrent - nextMin;
                double nextActual = qMin(remaining, nextMaxShrink);

                if (nextActual > 0) {
                    result[leftIndex] += nextActual;
                    result[nextIndex] -= nextActual;
                }
            }
        }
    }

    return normalizeProportions(result);
}

QList<double> DockLayoutCalculator::normalizeProportions(const QList<double>& proportions)
{
    if (proportions.isEmpty()) {
        return proportions;
    }

    double sum = 0;
    for (double p : proportions) {
        sum += p;
    }

    if (qFuzzyIsNull(sum)) {
        // All zeros - return equal proportions
        return equalProportions(proportions.size());
    }

    QList<double> result;
    result.reserve(proportions.size());

    for (double p : proportions) {
        result.append(p / sum);
    }

    return result;
}

QList<double> DockLayoutCalculator::equalProportions(int count)
{
    QList<double> result;
    if (count <= 0)
        return result;

    double each = 1.0 / count;
    result.reserve(count);

    for (int i = 0; i < count; ++i) {
        result.append(each);
    }

    return result;
}

int DockLayoutCalculator::calculateMinimumSize(
    const QList<LayoutConstraints>& constraints, SplitDirection direction, int handleSize)
{
    int total = 0;

    for (const auto& c : constraints) {
        total += c.minInDirection(direction);
    }

    // Add handles
    int handleCount = qMax(0, constraints.size() - 1);
    total += handleCount * handleSize;

    return total;
}

QList<int> DockLayoutCalculator::distributeSpace(int availableSpace,
    const QList<double>& proportions, const QList<LayoutConstraints>& constraints,
    SplitDirection direction)
{
    const int count = proportions.size();
    QList<int> sizes;
    sizes.reserve(count);

    if (count == 0) {
        return sizes;
    }

    // First pass: allocate by proportion
    int allocated = 0;
    for (int i = 0; i < count; ++i) {
        int size;
        if (i == count - 1) {
            // Last item gets remaining space
            size = availableSpace - allocated;
        } else {
            size = qRound(availableSpace * proportions[i]);
        }
        sizes.append(size);
        allocated += size;
    }

    // Second pass: enforce minimums
    // If any item is below minimum, steal from larger items
    bool changed = true;
    int iterations = 0;
    const int maxIterations = count * 2; // Prevent infinite loop

    while (changed && iterations < maxIterations) {
        changed = false;
        iterations++;

        for (int i = 0; i < count; ++i) {
            int minSize = constraints[i].minInDirection(direction);

            if (sizes[i] < minSize) {
                int deficit = minSize - sizes[i];

                // Find donor - largest item that can spare space
                int donorIndex = -1;
                int donorExcess = 0;

                for (int j = 0; j < count; ++j) {
                    if (j == i)
                        continue;

                    int jMin = constraints[j].minInDirection(direction);
                    int jExcess = sizes[j] - jMin;

                    if (jExcess > donorExcess) {
                        donorExcess = jExcess;
                        donorIndex = j;
                    }
                }

                if (donorIndex >= 0 && donorExcess > 0) {
                    int transfer = qMin(deficit, donorExcess);
                    sizes[i] += transfer;
                    sizes[donorIndex] -= transfer;
                    changed = true;
                }
            }
        }
    }

    // Final pass: ensure all items are at least at minimum
    // (may slightly exceed total if not enough space, but layout will clip)
    for (int i = 0; i < count; ++i) {
        int minSize = constraints[i].minInDirection(direction);
        sizes[i] = qMax(sizes[i], minSize);
    }

    return sizes;
}

// ==================== Anchored Resize API Implementation ====================

AnchoredLayoutOutput DockLayoutCalculator::calculateAnchoredResize(const AnchoredLayoutInput& input)
{
    AnchoredLayoutOutput output;
    const int count = input.items.size();

    if (count == 0) {
        output.success = true;
        return output;
    }

    // Calculate total minimum size required
    int totalMinRequired = 0;
    for (const auto& item : input.items) {
        totalMinRequired += item.minSize;
    }

    if (input.totalAvailableSize < totalMinRequired) {
        output.errorMessage = QString("Not enough space: need %1, have %2")
                                  .arg(totalMinRequired)
                                  .arg(input.totalAvailableSize);
        return output;
    }

    // Separate anchored and flexible items
    QList<int> anchoredIndices;
    QList<int> flexibleIndices;
    int totalAnchoredSize = 0;

    for (int i = 0; i < count; ++i) {
        const auto& item = input.items[i];
        if (item.anchor != Anchor::None) {
            anchoredIndices.append(i);
            int anchorSize = (item.anchoredSize > 0) ? item.anchoredSize : item.currentSize;
            totalAnchoredSize += qMax(anchorSize, item.minSize);
        } else {
            flexibleIndices.append(i);
        }
    }

    // Calculate space available for flexible items
    int flexibleSpace = input.totalAvailableSize - totalAnchoredSize;

    // If flexible space is negative, we need to shrink anchored items
    if (flexibleSpace < 0 && !flexibleIndices.isEmpty()) {
        // Calculate minimum space needed for flexible items
        int flexMinRequired = 0;
        for (int idx : flexibleIndices) {
            flexMinRequired += input.items[idx].minSize;
        }

        // Shrink anchored items proportionally to free up space
        int deficit = flexMinRequired - flexibleSpace;
        int anchoredExcess = totalAnchoredSize - [&]() {
            int minSum = 0;
            for (int idx : anchoredIndices) {
                minSum += input.items[idx].minSize;
            }
            return minSum;
        }();

        if (anchoredExcess > 0) {
            double shrinkRatio = qMin(1.0, static_cast<double>(deficit) / anchoredExcess);
            totalAnchoredSize = 0;
            for (int idx : anchoredIndices) {
                const auto& item = input.items[idx];
                int anchorSize = (item.anchoredSize > 0) ? item.anchoredSize : item.currentSize;
                int excess = anchorSize - item.minSize;
                int newSize = anchorSize - static_cast<int>(excess * shrinkRatio);
                totalAnchoredSize += qMax(newSize, item.minSize);
            }
            flexibleSpace = input.totalAvailableSize - totalAnchoredSize;
        }
    }

    // Initialize output sizes
    output.sizes.resize(count);

    // Assign anchored sizes
    for (int idx : anchoredIndices) {
        const auto& item = input.items[idx];
        int anchorSize = (item.anchoredSize > 0) ? item.anchoredSize : item.currentSize;
        output.sizes[idx] = clampSize(anchorSize,
            NodeSizeConstraints {
                item.minSize, item.minSize, item.maxSize, item.maxSize, anchorSize, anchorSize },
            input.direction);
    }

    // Distribute remaining space to flexible items
    // IMPORTANT: Keep current sizes for all flexible items except ONE that absorbs the delta.
    // This ensures resize only affects neighbors, not all panels proportionally.
    if (!flexibleIndices.isEmpty()) {
        int remainingSpace = qMax(0, flexibleSpace);

        // First pass: keep current sizes for all flexible items (clamped to constraints)
        int allocated = 0;
        for (int i = 0; i < flexibleIndices.size() - 1; ++i) {
            int idx = flexibleIndices[i];
            const auto& item = input.items[idx];

            // Keep current size, clamped to constraints
            int size = qBound(item.minSize, item.currentSize, item.maxSize);

            // Don't exceed remaining space (leave room for others)
            int minForRest = 0;
            for (int j = i + 1; j < flexibleIndices.size(); ++j) {
                minForRest += input.items[flexibleIndices[j]].minSize;
            }
            size = qMin(size, remainingSpace - allocated - minForRest);
            size = qMax(size, item.minSize);

            output.sizes[idx] = size;
            allocated += size;
        }

        // Last flexible item absorbs all remaining space (the "resize absorber")
        int lastFlexIdx = flexibleIndices.last();
        const auto& lastItem = input.items[lastFlexIdx];
        int lastSize = remainingSpace - allocated;
        lastSize = qBound(lastItem.minSize, lastSize, lastItem.maxSize);
        output.sizes[lastFlexIdx] = lastSize;
    }

    // Verify total (may need minor adjustment due to rounding)
    int total = 0;
    for (int s : output.sizes) {
        total += s;
    }

    if (total != input.totalAvailableSize && !flexibleIndices.isEmpty()) {
        // Adjust last flexible item
        int lastFlexIdx = flexibleIndices.last();
        int adjustment = input.totalAvailableSize - total;
        int newSize = output.sizes[lastFlexIdx] + adjustment;
        output.sizes[lastFlexIdx] = qMax(newSize, input.items[lastFlexIdx].minSize);
    }

    output.success = true;
    return output;
}

QList<int> DockLayoutCalculator::calculateDragWithPush(const QList<int>& currentSizes,
    int handleIndex, int delta, const QList<NodeSizeConstraints>& constraints,
    SplitDirection direction)
{
    if (handleIndex < 0 || handleIndex >= currentSizes.size() - 1) {
        return currentSizes;
    }

    if (delta == 0) {
        return currentSizes;
    }

    QList<int> sizes = currentSizes;
    const int count = sizes.size();

    // Determine grow and shrink indices
    int growIdx = (delta > 0) ? handleIndex : handleIndex + 1;
    int shrinkIdx = (delta > 0) ? handleIndex + 1 : handleIndex;
    int absDelta = qAbs(delta);

    // Get constraints
    int growMin = constraints[growIdx].minInDirection(direction);
    int growMax = constraints[growIdx].maxInDirection(direction);
    int shrinkMin = constraints[shrinkIdx].minInDirection(direction);

    // Calculate how much the shrink item can give
    int shrinkAvailable = sizes[shrinkIdx] - shrinkMin;

    if (shrinkAvailable >= absDelta) {
        // Simple case: direct transfer between neighbors
        int growSpace = growMax - sizes[growIdx];
        int actualDelta = qMin(absDelta, qMin(shrinkAvailable, growSpace));

        sizes[growIdx] += actualDelta;
        sizes[shrinkIdx] -= actualDelta;
    } else {
        // Complex case: need to push through
        // First, take what we can from the immediate neighbor
        sizes[growIdx] += shrinkAvailable;
        sizes[shrinkIdx] = shrinkMin;

        int remaining = absDelta - shrinkAvailable;

        // Push effect: try to get more space from items beyond
        int pushDirection = (delta > 0) ? 1 : -1;
        int nextShrinkIdx = shrinkIdx + pushDirection;

        while (remaining > 0 && nextShrinkIdx >= 0 && nextShrinkIdx < count) {
            int nextMin = constraints[nextShrinkIdx].minInDirection(direction);
            int nextAvailable = sizes[nextShrinkIdx] - nextMin;

            if (nextAvailable > 0) {
                int take = qMin(remaining, nextAvailable);

                // Check grow item max constraint
                int growSpace = growMax - sizes[growIdx];
                take = qMin(take, growSpace);

                if (take > 0) {
                    sizes[growIdx] += take;
                    sizes[nextShrinkIdx] -= take;
                    remaining -= take;
                }
            }

            nextShrinkIdx += pushDirection;
        }
    }

    return sizes;
}

QList<int> DockLayoutCalculator::distributeFlexibleSpace(int totalSpace, int itemCount,
    const QMap<int, int>& fixedSizes, const QList<NodeSizeConstraints>& constraints,
    SplitDirection direction)
{
    QList<int> sizes;
    sizes.resize(itemCount);

    if (itemCount == 0) {
        return sizes;
    }

    // Calculate space used by fixed items
    int fixedTotal = 0;
    for (auto it = fixedSizes.begin(); it != fixedSizes.end(); ++it) {
        fixedTotal += it.value();
        sizes[it.key()] = it.value();
    }

    // Calculate flexible space and count
    int flexibleSpace = totalSpace - fixedTotal;
    int flexibleCount = itemCount - fixedSizes.size();

    if (flexibleCount <= 0 || flexibleSpace <= 0) {
        // No flexible items or no space - just set fixed sizes
        return sizes;
    }

    // Distribute among flexible items equally, then adjust for constraints
    int baseSize = flexibleSpace / flexibleCount;
    int remainder = flexibleSpace % flexibleCount;

    QList<int> flexibleIndices;
    for (int i = 0; i < itemCount; ++i) {
        if (!fixedSizes.contains(i)) {
            flexibleIndices.append(i);
        }
    }

    // First pass: equal distribution
    int allocated = 0;
    for (int i = 0; i < flexibleIndices.size(); ++i) {
        int idx = flexibleIndices[i];
        int size = baseSize + (i < remainder ? 1 : 0);
        sizes[idx] = size;
        allocated += size;
    }

    // Second pass: enforce minimums
    bool changed = true;
    int iterations = 0;
    while (changed && iterations < flexibleCount * 2) {
        changed = false;
        iterations++;

        for (int idx : flexibleIndices) {
            int minSize = constraints[idx].minInDirection(direction);
            if (sizes[idx] < minSize) {
                int deficit = minSize - sizes[idx];

                // Find a donor
                for (int donorIdx : flexibleIndices) {
                    if (donorIdx == idx)
                        continue;

                    int donorMin = constraints[donorIdx].minInDirection(direction);
                    int donorExcess = sizes[donorIdx] - donorMin;

                    if (donorExcess > 0) {
                        int transfer = qMin(deficit, donorExcess);
                        sizes[idx] += transfer;
                        sizes[donorIdx] -= transfer;
                        deficit -= transfer;
                        changed = true;

                        if (deficit <= 0)
                            break;
                    }
                }
            }
        }
    }

    return sizes;
}

int DockLayoutCalculator::calculateMinimumSize(
    const QList<NodeSizeConstraints>& constraints, SplitDirection direction, int handleSize)
{
    int total = 0;

    for (const auto& c : constraints) {
        total += c.minInDirection(direction);
    }

    // Add handles
    int handleCount = qMax(0, constraints.size() - 1);
    total += handleCount * handleSize;

    return total;
}

int DockLayoutCalculator::clampSize(
    int size, const NodeSizeConstraints& constraints, SplitDirection direction)
{
    int minVal = constraints.minInDirection(direction);
    int maxVal = constraints.maxInDirection(direction);
    return qBound(minVal, size, maxVal);
}

int DockLayoutCalculator::applyPush(QList<int>& sizes, int startIndex, int pushDirection,
    int deficit, const QList<NodeSizeConstraints>& constraints, SplitDirection splitDir)
{
    int remaining = deficit;
    int idx = startIndex;

    while (remaining > 0 && idx >= 0 && idx < sizes.size()) {
        int minSize = constraints[idx].minInDirection(splitDir);
        int available = sizes[idx] - minSize;

        if (available > 0) {
            int take = qMin(remaining, available);
            sizes[idx] -= take;
            remaining -= take;
        }

        idx += pushDirection;
    }

    return remaining;
}

} // namespace ruwa::ui::docking
