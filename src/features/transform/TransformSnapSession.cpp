// SPDX-License-Identifier: MPL-2.0

#include "features/transform/TransformSnapSession.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aether {
namespace {

Vector2 projectedPoint(const Vector2& point, const Viewport* viewport, float screenZoom)
{
    if (viewport) {
        return viewport->worldToScreen(point);
    }
    const float zoom = screenZoom > 1.0e-6f ? screenZoom : 1.0f;
    return { point.x * zoom, point.y * zoom };
}

bool betterRelation(const SnapRelation& a, const SnapRelation& b, float aDistance, float bDistance)
{
    if (a.confirmationCount != b.confirmationCount) {
        return a.confirmationCount > b.confirmationCount;
    }
    if (a.hierarchyPriority != b.hierarchyPriority) {
        return a.hierarchyPriority > b.hierarchyPriority;
    }
    if (std::abs(aDistance - bDistance) > 0.0001f) {
        return aDistance < bDistance;
    }
    const QString aId = a.targetId.toString(QUuid::WithoutBraces);
    const QString bId = b.targetId.toString(QUuid::WithoutBraces);
    if (aId != bId) {
        return aId < bId;
    }
    return a.stableKey < b.stableKey;
}

} // namespace

TransformSnapSession::TransformSnapSession(SnapSettings settings, SnapScene scene,
    SnapCoordinatePolicy coordinatePolicy, std::optional<QUuid> sourceParentId)
    : m_settings(std::move(settings))
    , m_scene(std::move(scene))
    , m_coordinatePolicy(coordinatePolicy)
    , m_sourceParentId(std::move(sourceParentId))
{
    auto buildIndex = [this](SnapAxis axis, std::vector<SnapAxisEntry>& index) {
        index.clear();
        index.reserve(m_scene.targets.size() * 3);
        for (size_t i = 0; i < m_scene.targets.size(); ++i) {
            const Rect& bounds = m_scene.targets[i].bounds;
            const float start = axis == SnapAxis::X ? bounds.left() : bounds.top();
            const float end = axis == SnapAxis::X ? bounds.right() : bounds.bottom();
            index.push_back({ start, SnapAnchor::Start, i });
            index.push_back({ (start + end) * 0.5f, SnapAnchor::Center, i });
            index.push_back({ end, SnapAnchor::End, i });
        }
        std::sort(index.begin(), index.end(), [](const SnapAxisEntry& a, const SnapAxisEntry& b) {
            if (a.coordinate != b.coordinate) {
                return a.coordinate < b.coordinate;
            }
            if (a.targetIndex != b.targetIndex) {
                return a.targetIndex < b.targetIndex;
            }
            return static_cast<int>(a.anchor) < static_cast<int>(b.anchor);
        });
    };
    buildIndex(SnapAxis::X, m_scene.xIndex);
    buildIndex(SnapAxis::Y, m_scene.yIndex);
}

float TransformSnapSession::screenDistance(const SnapRelation& relation,
    const Vector2& referencePoint, const Viewport* viewport, float screenZoom) const
{
    Vector2 corrected = referencePoint;
    if (relation.axis == SnapAxis::X) {
        corrected.x += relation.correction;
    } else {
        corrected.y += relation.correction;
    }
    const Vector2 a = projectedPoint(referencePoint, viewport, screenZoom);
    const Vector2 b = projectedPoint(corrected, viewport, screenZoom);
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::optional<SnapRelation> TransformSnapSession::choose(SnapAxis axis,
    std::vector<SnapRelation> candidates, const Vector2& referencePoint, const Viewport* viewport,
    float screenZoom, bool axisAllowed, std::vector<SnapRelation>& merged)
{
    std::optional<SnapRelation>& latch = axis == SnapAxis::X ? m_xLatch : m_yLatch;
    merged.clear();
    if (!axisAllowed) {
        latch.reset();
        return std::nullopt;
    }

    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                         [axis](const SnapRelation& relation) { return relation.axis != axis; }),
        candidates.end());

    for (SnapRelation& candidate : candidates) {
        std::vector<QString> confirmingTargets;
        for (const SnapRelation& other : candidates) {
            if (other.axis != candidate.axis || other.type != candidate.type
                || std::abs(other.correction - candidate.correction) > 0.001f) {
                continue;
            }
            const QString targetKey = other.targetId.isNull()
                ? QStringLiteral("canvas")
                : other.targetId.toString(QUuid::WithoutBraces);
            if (std::find(confirmingTargets.begin(), confirmingTargets.end(), targetKey)
                == confirmingTargets.end()) {
                confirmingTargets.push_back(targetKey);
            }
        }
        candidate.confirmationCount
            = std::max(candidate.confirmationCount, static_cast<int>(confirmingTargets.size()));
    }

    auto collectMatchingRelations = [&](const SnapRelation& primary) {
        for (const SnapRelation& candidate : candidates) {
            if (std::abs(candidate.correction - primary.correction) <= 0.001f) {
                merged.push_back(candidate);
            }
        }
    };

    if (latch) {
        const auto it = std::find_if(candidates.begin(), candidates.end(),
            [&](const SnapRelation& candidate) { return candidate.stableKey == latch->stableKey; });
        if (it != candidates.end()
            && screenDistance(*it, referencePoint, viewport, screenZoom)
                <= m_settings.releaseThresholdScreenPx) {
            latch = *it;
            collectMatchingRelations(*latch);
            return latch;
        }
        latch.reset();
    }

    std::optional<SnapRelation> best;
    float bestDistance = 0.0f;
    for (SnapRelation& candidate : candidates) {
        const float distance = screenDistance(candidate, referencePoint, viewport, screenZoom);
        if (distance > m_settings.captureThresholdScreenPx) {
            continue;
        }
        if (!best || betterRelation(candidate, *best, distance, bestDistance)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    latch = best;
    if (best) {
        collectMatchingRelations(*best);
    }
    return best;
}

SnapResult TransformSnapSession::solveMove(
    const Rect& sourceBounds, const Viewport* viewport, float screenZoom, bool allowX, bool allowY)
{
    const float zoom
        = viewport ? viewport->camera().zoom() : (screenZoom > 1.0e-6f ? screenZoom : 1.0f);
    const float searchRadius = m_settings.releaseThresholdScreenPx / std::abs(zoom);
    std::vector<SnapRelation> candidates = TransformSnapSolver::alignmentCandidates(
        m_scene, m_settings, sourceBounds, m_sourceParentId, false, searchRadius);
    std::vector<SnapRelation> spacing = TransformSnapSolver::spacingCandidates(
        m_scene, m_settings, sourceBounds, m_sourceParentId);
    candidates.insert(candidates.end(), spacing.begin(), spacing.end());

    const Vector2 reference = sourceBounds.center();
    SnapResult result;
    result.xRelation = choose(
        SnapAxis::X, candidates, reference, viewport, screenZoom, allowX, result.xRelations);
    result.yRelation = choose(
        SnapAxis::Y, candidates, reference, viewport, screenZoom, allowY, result.yRelations);
    if (result.xRelation) {
        result.correction.x = result.xRelation->correction;
    }
    if (result.yRelation) {
        result.correction.y = result.yRelation->correction;
    }
    buildVisualState(sourceBounds, result);
    m_visualState = result.visualState;
    return result;
}

SnapResult TransformSnapSession::solvePoint(const Vector2& point, const Viewport* viewport,
    float screenZoom, bool canvasOnly, bool allowX, bool allowY)
{
    const float zoom
        = viewport ? viewport->camera().zoom() : (screenZoom > 1.0e-6f ? screenZoom : 1.0f);
    const float searchRadius = m_settings.releaseThresholdScreenPx / std::abs(zoom);
    std::vector<SnapRelation> candidates = TransformSnapSolver::pointCandidates(
        m_scene, m_settings, point, m_sourceParentId, canvasOnly, searchRadius);
    SnapResult result;
    result.xRelation = choose(
        SnapAxis::X, candidates, point, viewport, screenZoom, allowX, result.xRelations);
    result.yRelation = choose(
        SnapAxis::Y, candidates, point, viewport, screenZoom, allowY, result.yRelations);
    if (result.xRelation) {
        result.correction.x = result.xRelation->correction;
    }
    if (result.yRelation) {
        result.correction.y = result.yRelation->correction;
    }
    buildVisualState({ point.x, point.y, 0.0f, 0.0f }, result);
    m_visualState = result.visualState;
    return result;
}

void TransformSnapSession::buildVisualState(const Rect& sourceBounds, SnapResult& result)
{
    result.visualState = {};
    const Rect snapped { sourceBounds.x + result.correction.x, sourceBounds.y + result.correction.y,
        sourceBounds.width, sourceBounds.height };

    auto append = [&](const SnapRelation& relation) {
        if (relation.type == SnapRelationType::EqualSpacing) {
            constexpr float spacingEpsilon = 0.001f;
            if (std::abs(relation.spacing) <= spacingEpsilon) {
                return;
            }

            struct SpacingItem {
                Rect bounds {};
                bool source = false;
            };
            std::vector<SpacingItem> items;
            const std::vector<size_t> targetIndices
                = TransformSnapSolver::orderedSpacingTargetIndices(m_scene, snapped, relation.axis);
            items.reserve(targetIndices.size() + 1);
            for (size_t targetIndex : targetIndices) {
                items.push_back({ m_scene.targets[targetIndex].bounds, false });
            }
            items.push_back({ snapped, true });

            const auto start = [&](const Rect& bounds) {
                return relation.axis == SnapAxis::X ? bounds.left() : bounds.top();
            };
            const auto end = [&](const Rect& bounds) {
                return relation.axis == SnapAxis::X ? bounds.right() : bounds.bottom();
            };
            std::sort(items.begin(), items.end(), [&](const SpacingItem& a, const SpacingItem& b) {
                const float av = start(a.bounds);
                const float bv = start(b.bounds);
                if (av != bv) {
                    return av < bv;
                }
                return a.source && !b.source;
            });

            const auto sourceIt = std::find_if(
                items.begin(), items.end(), [](const SpacingItem& item) { return item.source; });
            if (sourceIt == items.end()) {
                return;
            }
            size_t chainStart = static_cast<size_t>(sourceIt - items.begin());
            size_t chainEnd = chainStart;
            const auto gapMatches = [&](size_t leftIndex, size_t rightIndex) {
                const float gap = start(items[rightIndex].bounds) - end(items[leftIndex].bounds);
                return std::abs(gap - relation.spacing) <= spacingEpsilon;
            };
            while (chainStart > 0 && gapMatches(chainStart - 1, chainStart)) {
                --chainStart;
            }
            while (chainEnd + 1 < items.size() && gapMatches(chainEnd, chainEnd + 1)) {
                ++chainEnd;
            }

            const auto samePoint = [](const Vector2& a, const Vector2& b) {
                return std::abs(a.x - b.x) <= spacingEpsilon
                    && std::abs(a.y - b.y) <= spacingEpsilon;
            };
            for (size_t i = chainStart; i < chainEnd; ++i) {
                SnapSpacingDimension dimension;
                if (relation.axis == SnapAxis::X) {
                    dimension.from = { items[i].bounds.right(), snapped.center().y };
                    dimension.to = { items[i + 1].bounds.left(), snapped.center().y };
                } else {
                    dimension.from = { snapped.center().x, items[i].bounds.bottom() };
                    dimension.to = { snapped.center().x, items[i + 1].bounds.top() };
                }
                dimension.value = std::abs(relation.spacing);
                const bool duplicate = std::any_of(result.visualState.spacingDimensions.begin(),
                    result.visualState.spacingDimensions.end(),
                    [&](const SnapSpacingDimension& existing) {
                        return samePoint(existing.from, dimension.from)
                            && samePoint(existing.to, dimension.to);
                    });
                if (duplicate) {
                    continue;
                }
                result.visualState.spacingDimensions.push_back(dimension);
                result.visualState.labels.push_back(
                    { { (dimension.from.x + dimension.to.x) * 0.5f,
                          (dimension.from.y + dimension.to.y) * 0.5f },
                        formatMetric(dimension.value) });
            }
            return;
        }

        SnapGuideSegment segment;
        if (relation.axis == SnapAxis::X) {
            const float top = relation.targetType == SnapTargetType::Canvas
                ? snapped.top()
                : std::min(snapped.top(), relation.targetBounds.top());
            const float bottom = relation.targetType == SnapTargetType::Canvas
                ? snapped.bottom()
                : std::max(snapped.bottom(), relation.targetBounds.bottom());
            segment = { { relation.targetCoordinate, top }, { relation.targetCoordinate, bottom } };
        } else {
            const float left = relation.targetType == SnapTargetType::Canvas
                ? snapped.left()
                : std::min(snapped.left(), relation.targetBounds.left());
            const float right = relation.targetType == SnapTargetType::Canvas
                ? snapped.right()
                : std::max(snapped.right(), relation.targetBounds.right());
            segment = { { left, relation.targetCoordinate }, { right, relation.targetCoordinate } };
        }
        segment.axis = relation.axis;
        const auto samePoint = [](const Vector2& a, const Vector2& b) {
            return std::abs(a.x - b.x) <= 0.001f && std::abs(a.y - b.y) <= 0.001f;
        };
        if (std::none_of(result.visualState.alignmentSegments.begin(),
                result.visualState.alignmentSegments.end(), [&](const SnapGuideSegment& existing) {
                    return existing.axis == segment.axis && samePoint(existing.from, segment.from)
                        && samePoint(existing.to, segment.to);
                })) {
            result.visualState.alignmentSegments.push_back(segment);
        }
        const SnapGuideMarker marker { relation.axis == SnapAxis::X
                ? Vector2 { relation.targetCoordinate, snapped.center().y }
                : Vector2 { snapped.center().x, relation.targetCoordinate },
            relation.targetAnchor == SnapAnchor::Center };
        if (std::none_of(result.visualState.markers.begin(), result.visualState.markers.end(),
                [&](const SnapGuideMarker& existing) {
                    return existing.center == marker.center
                        && samePoint(existing.position, marker.position);
                })) {
            result.visualState.markers.push_back(marker);
        }
    };

    for (const SnapRelation& relation : result.xRelations) {
        append(relation);
    }
    for (const SnapRelation& relation : result.yRelations) {
        append(relation);
    }
}

QString TransformSnapSession::formatMetric(float value)
{
    if (std::abs(value - std::round(value)) <= 0.001f) {
        return QStringLiteral("%1 px").arg(static_cast<int>(std::round(value)));
    }
    QString text = QString::number(value, 'f', 2);
    while (text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text + QStringLiteral(" px");
}

void TransformSnapSession::clear()
{
    m_xLatch.reset();
    m_yLatch.reset();
    m_visualState = {};
}

} // namespace aether
