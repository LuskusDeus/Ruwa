// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMSNAPSOLVER_H
#define RUWA_CORE_TRANSFORM_TRANSFORMSNAPSOLVER_H

#include "features/transform/TransformSnapTypes.h"

#include <limits>
#include <vector>

namespace aether {

class TransformSnapSolver {
public:
    static std::vector<SnapRelation> alignmentCandidates(const SnapScene& scene,
        const SnapSettings& settings, const Rect& sourceBounds,
        const std::optional<QUuid>& sourceParentId, bool canvasOnly = false,
        float coordinateSearchRadius = std::numeric_limits<float>::infinity());

    static std::vector<SnapRelation> pointCandidates(const SnapScene& scene,
        const SnapSettings& settings, const Vector2& point,
        const std::optional<QUuid>& sourceParentId, bool canvasOnly = false,
        float coordinateSearchRadius = std::numeric_limits<float>::infinity());

    static std::vector<SnapRelation> spacingCandidates(const SnapScene& scene,
        const SnapSettings& settings, const Rect& sourceBounds,
        const std::optional<QUuid>& sourceParentId);

    static std::vector<size_t> orderedSpacingTargetIndices(
        const SnapScene& scene, const Rect& sourceBounds, SnapAxis axis);
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMSNAPSOLVER_H
