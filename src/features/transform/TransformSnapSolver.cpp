// SPDX-License-Identifier: MPL-2.0

#include "features/transform/TransformSnapSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace aether {
namespace {

float anchorCoordinate(const Rect& bounds, SnapAxis axis, SnapAnchor anchor)
{
    const float start = axis == SnapAxis::X ? bounds.left() : bounds.top();
    const float end = axis == SnapAxis::X ? bounds.right() : bounds.bottom();
    if (anchor == SnapAnchor::Start) {
        return start;
    }
    if (anchor == SnapAnchor::End) {
        return end;
    }
    return (start + end) * 0.5f;
}

QString relationKey(SnapRelationType type, SnapAxis axis, SnapAnchor sourceAnchor,
    SnapAnchor targetAnchor, const QUuid& id, float target)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(static_cast<int>(type))
        .arg(static_cast<int>(axis))
        .arg(static_cast<int>(sourceAnchor))
        .arg(static_cast<int>(targetAnchor))
        .arg(id.toString(QUuid::WithoutBraces))
        .arg(target, 0, 'g', 9);
}

void addAlignment(std::vector<SnapRelation>& out, const Rect& sourceBounds,
    const SnapTarget& target, SnapAxis axis, SnapAnchor sourceAnchor, SnapAnchor targetAnchor,
    const std::optional<QUuid>& sourceParentId)
{
    SnapRelation relation;
    relation.axis = axis;
    relation.sourceAnchor = sourceAnchor;
    relation.targetAnchor = targetAnchor;
    relation.targetId = target.id;
    relation.targetBounds = target.bounds;
    relation.targetType = target.type;
    relation.sourceCoordinate = anchorCoordinate(sourceBounds, axis, sourceAnchor);
    relation.targetCoordinate = anchorCoordinate(target.bounds, axis, targetAnchor);
    relation.correction = relation.targetCoordinate - relation.sourceCoordinate;
    relation.hierarchyPriority = target.type != SnapTargetType::Canvas && sourceParentId
            && *sourceParentId == target.parentId
        ? 1
        : 0;
    relation.stableKey = relationKey(
        relation.type, axis, sourceAnchor, targetAnchor, target.id, relation.targetCoordinate);
    out.push_back(std::move(relation));
}

bool overlapsOrNear(const Rect& a, const Rect& b, SnapAxis perpendicularAxis)
{
    const float aStart = perpendicularAxis == SnapAxis::X ? a.left() : a.top();
    const float aEnd = perpendicularAxis == SnapAxis::X ? a.right() : a.bottom();
    const float bStart = perpendicularAxis == SnapAxis::X ? b.left() : b.top();
    const float bEnd = perpendicularAxis == SnapAxis::X ? b.right() : b.bottom();
    const float extent = std::max(aEnd - aStart, bEnd - bStart);
    const float nearDistance = std::max(24.0f, extent * 0.5f);
    return aStart <= bEnd + nearDistance && bStart <= aEnd + nearDistance;
}

float axisStart(const Rect& bounds, SnapAxis axis)
{
    return axis == SnapAxis::X ? bounds.left() : bounds.top();
}

float axisEnd(const Rect& bounds, SnapAxis axis)
{
    return axis == SnapAxis::X ? bounds.right() : bounds.bottom();
}

int matchingAdjacentGapCount(
    const std::vector<const SnapTarget*>& targets, SnapAxis axis, float expectedGap)
{
    int confirmations = 0;
    for (size_t i = 1; i < targets.size(); ++i) {
        const float gap
            = axisStart(targets[i]->bounds, axis) - axisEnd(targets[i - 1]->bounds, axis);
        if (gap >= 0.0f && std::abs(gap - expectedGap) <= 0.001f) {
            ++confirmations;
        }
    }
    return std::max(1, confirmations);
}

SnapTarget canvasTarget(const SnapScene& scene)
{
    SnapTarget target;
    target.bounds = { 0.0f, 0.0f, scene.canvasSize.x, scene.canvasSize.y };
    target.type = SnapTargetType::Canvas;
    return target;
}

void addSpacingRelation(std::vector<SnapRelation>& out, SnapAxis axis, float correction,
    float spacing, const SnapTarget& target, const std::optional<QUuid>& sourceParentId,
    int confirmations, const QString& suffix)
{
    SnapRelation relation;
    relation.type = SnapRelationType::EqualSpacing;
    relation.axis = axis;
    relation.targetId = target.id;
    relation.targetBounds = target.bounds;
    relation.targetType = target.type;
    relation.correction = correction;
    relation.spacing = spacing;
    relation.confirmationCount = confirmations;
    relation.hierarchyPriority = sourceParentId && *sourceParentId == target.parentId ? 1 : 0;
    relation.stableKey = QStringLiteral("spacing:%1:%2:%3")
                             .arg(static_cast<int>(axis))
                             .arg(target.id.toString(QUuid::WithoutBraces))
                             .arg(suffix);
    out.push_back(std::move(relation));
}

} // namespace

std::vector<SnapRelation> TransformSnapSolver::alignmentCandidates(const SnapScene& scene,
    const SnapSettings& settings, const Rect& sourceBounds,
    const std::optional<QUuid>& sourceParentId, bool canvasOnly, float coordinateSearchRadius)
{
    std::vector<SnapRelation> result;
    constexpr std::array<SnapAnchor, 3> anchors { SnapAnchor::Start, SnapAnchor::Center,
        SnapAnchor::End };

    const auto alignmentAnchorEnabled = [&settings](SnapAnchor anchor) {
        return anchor != SnapAnchor::Center || settings.centerAlignmentEnabled;
    };

    if (settings.canvasEnabled && scene.canvasSize.x > 0.0f && scene.canvasSize.y > 0.0f) {
        const SnapTarget canvas = canvasTarget(scene);
        for (SnapAxis axis : { SnapAxis::X, SnapAxis::Y }) {
            for (SnapAnchor sourceAnchor : anchors) {
                if (!alignmentAnchorEnabled(sourceAnchor)) {
                    continue;
                }
                if (settings.centerAlignmentEnabled) {
                    addAlignment(result, sourceBounds, canvas, axis, sourceAnchor,
                        SnapAnchor::Center, sourceParentId);
                }
                if (scene.finiteCanvas) {
                    addAlignment(result, sourceBounds, canvas, axis, sourceAnchor,
                        SnapAnchor::Start, sourceParentId);
                    addAlignment(result, sourceBounds, canvas, axis, sourceAnchor, SnapAnchor::End,
                        sourceParentId);
                }
            }
        }
    }

    if (!canvasOnly && settings.layersEnabled) {
        auto appendIndex = [&](SnapAxis axis, const std::vector<SnapAxisEntry>& index) {
            for (SnapAnchor sourceAnchor : anchors) {
                if (!alignmentAnchorEnabled(sourceAnchor)) {
                    continue;
                }
                const float sourceCoordinate = anchorCoordinate(sourceBounds, axis, sourceAnchor);
                auto it = index.begin();
                if (std::isfinite(coordinateSearchRadius)) {
                    it = std::lower_bound(index.begin(), index.end(),
                        sourceCoordinate - coordinateSearchRadius,
                        [](const SnapAxisEntry& entry, float coordinate) {
                            return entry.coordinate < coordinate;
                        });
                }
                for (; it != index.end(); ++it) {
                    const SnapAxisEntry& entry = *it;
                    if (std::isfinite(coordinateSearchRadius)
                        && entry.coordinate > sourceCoordinate + coordinateSearchRadius) {
                        break;
                    }
                    if (entry.targetIndex >= scene.targets.size()) {
                        continue;
                    }
                    if (!alignmentAnchorEnabled(entry.anchor)) {
                        continue;
                    }
                    addAlignment(result, sourceBounds, scene.targets[entry.targetIndex], axis,
                        sourceAnchor, entry.anchor, sourceParentId);
                }
            }
        };
        if (!scene.xIndex.empty() || !scene.yIndex.empty()) {
            appendIndex(SnapAxis::X, scene.xIndex);
            appendIndex(SnapAxis::Y, scene.yIndex);
        } else {
            for (const SnapTarget& target : scene.targets) {
                for (SnapAxis axis : { SnapAxis::X, SnapAxis::Y }) {
                    for (SnapAnchor sourceAnchor : anchors) {
                        if (!alignmentAnchorEnabled(sourceAnchor)) {
                            continue;
                        }
                        for (SnapAnchor targetAnchor : anchors) {
                            if (!alignmentAnchorEnabled(targetAnchor)) {
                                continue;
                            }
                            addAlignment(result, sourceBounds, target, axis, sourceAnchor,
                                targetAnchor, sourceParentId);
                        }
                    }
                }
            }
        }
    }
    return result;
}

std::vector<SnapRelation> TransformSnapSolver::pointCandidates(const SnapScene& scene,
    const SnapSettings& settings, const Vector2& point, const std::optional<QUuid>& sourceParentId,
    bool canvasOnly, float coordinateSearchRadius)
{
    const Rect pointBounds { point.x, point.y, 0.0f, 0.0f };
    std::vector<SnapRelation> all = alignmentCandidates(
        scene, settings, pointBounds, sourceParentId, canvasOnly, coordinateSearchRadius);
    all.erase(std::remove_if(all.begin(), all.end(),
                  [](const SnapRelation& relation) {
                      return relation.sourceAnchor != SnapAnchor::Start;
                  }),
        all.end());
    return all;
}

std::vector<size_t> TransformSnapSolver::orderedSpacingTargetIndices(
    const SnapScene& scene, const Rect& sourceBounds, SnapAxis axis)
{
    const SnapAxis perpendicular = axis == SnapAxis::X ? SnapAxis::Y : SnapAxis::X;
    std::vector<size_t> result;
    const std::vector<SnapAxisEntry>& index = axis == SnapAxis::X ? scene.xIndex : scene.yIndex;
    if (!index.empty()) {
        for (const SnapAxisEntry& entry : index) {
            if (entry.anchor != SnapAnchor::Start || entry.targetIndex >= scene.targets.size()) {
                continue;
            }
            if (overlapsOrNear(
                    sourceBounds, scene.targets[entry.targetIndex].bounds, perpendicular)) {
                result.push_back(entry.targetIndex);
            }
        }
        return result;
    }

    for (size_t i = 0; i < scene.targets.size(); ++i) {
        if (overlapsOrNear(sourceBounds, scene.targets[i].bounds, perpendicular)) {
            result.push_back(i);
        }
    }
    std::sort(result.begin(), result.end(), [&](size_t a, size_t b) {
        const float av = axisStart(scene.targets[a].bounds, axis);
        const float bv = axisStart(scene.targets[b].bounds, axis);
        if (av != bv) {
            return av < bv;
        }
        return scene.targets[a].id.toString() < scene.targets[b].id.toString();
    });
    return result;
}

std::vector<SnapRelation> TransformSnapSolver::spacingCandidates(const SnapScene& scene,
    const SnapSettings& settings, const Rect& sourceBounds,
    const std::optional<QUuid>& sourceParentId)
{
    std::vector<SnapRelation> result;
    if (!settings.layersEnabled || !settings.equalSpacingEnabled || scene.targets.size() < 2) {
        return result;
    }

    for (SnapAxis axis : { SnapAxis::X, SnapAxis::Y }) {
        std::vector<const SnapTarget*> targets;
        const std::vector<size_t> targetIndices
            = orderedSpacingTargetIndices(scene, sourceBounds, axis);
        targets.reserve(targetIndices.size());
        for (size_t targetIndex : targetIndices) {
            targets.push_back(&scene.targets[targetIndex]);
        }

        const float sourceStart = axis == SnapAxis::X ? sourceBounds.left() : sourceBounds.top();
        const float sourceEnd = axis == SnapAxis::X ? sourceBounds.right() : sourceBounds.bottom();
        const SnapTarget* left = nullptr;
        const SnapTarget* right = nullptr;
        int leftIndex = -1;
        int rightIndex = -1;
        for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
            const Rect& bounds = targets[static_cast<size_t>(i)]->bounds;
            const float start = axis == SnapAxis::X ? bounds.left() : bounds.top();
            const float end = axis == SnapAxis::X ? bounds.right() : bounds.bottom();
            if (end <= sourceStart
                && (!left
                    || end
                        > (axis == SnapAxis::X ? left->bounds.right() : left->bounds.bottom()))) {
                left = targets[static_cast<size_t>(i)];
                leftIndex = i;
            }
            if (start >= sourceEnd
                && (!right
                    || start
                        < (axis == SnapAxis::X ? right->bounds.left() : right->bounds.top()))) {
                right = targets[static_cast<size_t>(i)];
                rightIndex = i;
            }
        }

        if (left && right) {
            const float leftEnd
                = axis == SnapAxis::X ? left->bounds.right() : left->bounds.bottom();
            const float rightStart
                = axis == SnapAxis::X ? right->bounds.left() : right->bounds.top();
            const float leftGap = sourceStart - leftEnd;
            const float rightGap = rightStart - sourceEnd;
            addSpacingRelation(result, axis, (rightGap - leftGap) * 0.5f,
                (leftGap + rightGap) * 0.5f, *left, sourceParentId, 2,
                QStringLiteral("symmetric-left"));
            addSpacingRelation(result, axis, (rightGap - leftGap) * 0.5f,
                (leftGap + rightGap) * 0.5f, *right, sourceParentId, 2,
                QStringLiteral("symmetric-right"));
        }

        if (left && leftIndex > 0) {
            const SnapTarget& previous = *targets[static_cast<size_t>(leftIndex - 1)];
            const float previousEnd
                = axis == SnapAxis::X ? previous.bounds.right() : previous.bounds.bottom();
            const float leftStart = axis == SnapAxis::X ? left->bounds.left() : left->bounds.top();
            const float leftEnd
                = axis == SnapAxis::X ? left->bounds.right() : left->bounds.bottom();
            const float gap = leftStart - previousEnd;
            addSpacingRelation(result, axis, leftEnd + gap - sourceStart, gap, *left,
                sourceParentId, matchingAdjacentGapCount(targets, axis, gap),
                QStringLiteral("after"));
        }

        if (right && rightIndex + 1 < static_cast<int>(targets.size())) {
            const SnapTarget& next = *targets[static_cast<size_t>(rightIndex + 1)];
            const float rightStart
                = axis == SnapAxis::X ? right->bounds.left() : right->bounds.top();
            const float rightEnd
                = axis == SnapAxis::X ? right->bounds.right() : right->bounds.bottom();
            const float nextStart = axis == SnapAxis::X ? next.bounds.left() : next.bounds.top();
            const float gap = nextStart - rightEnd;
            addSpacingRelation(result, axis, rightStart - gap - sourceEnd, gap, *right,
                sourceParentId, matchingAdjacentGapCount(targets, axis, gap),
                QStringLiteral("before"));
        }
    }
    return result;
}

} // namespace aether
