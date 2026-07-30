// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMSNAPSESSION_H
#define RUWA_CORE_TRANSFORM_TRANSFORMSNAPSESSION_H

#include "features/canvas/scene/Viewport.h"
#include "features/transform/TransformSnapSolver.h"

#include <optional>

namespace aether {

class TransformSnapSession {
public:
    TransformSnapSession(SnapSettings settings, SnapScene scene,
        SnapCoordinatePolicy coordinatePolicy, std::optional<QUuid> sourceParentId = std::nullopt);

    const SnapSettings& settings() const { return m_settings; }
    const SnapScene& scene() const { return m_scene; }
    SnapCoordinatePolicy coordinatePolicy() const { return m_coordinatePolicy; }

    SnapResult solveMove(const Rect& sourceBounds, const Viewport* viewport, float screenZoom,
        bool allowX = true, bool allowY = true);
    SnapResult solvePoint(const Vector2& point, const Viewport* viewport, float screenZoom,
        bool canvasOnly = false, bool allowX = true, bool allowY = true);

    void clear();
    const TransformSnapVisualState& visualState() const { return m_visualState; }

private:
    std::optional<SnapRelation> choose(SnapAxis axis, std::vector<SnapRelation> candidates,
        const Vector2& referencePoint, const Viewport* viewport, float screenZoom,
        bool axisAllowed, bool enforceCoordinatePolicy, std::vector<SnapRelation>& merged);
    float screenDistance(const SnapRelation& relation, const Vector2& referencePoint,
        const Viewport* viewport, float screenZoom) const;
    bool coordinatePolicyAllows(SnapRelation& relation) const;
    void buildVisualState(const Rect& sourceBounds, SnapResult& result);
    static QString formatMetric(float value);

    SnapSettings m_settings;
    SnapScene m_scene;
    SnapCoordinatePolicy m_coordinatePolicy = SnapCoordinatePolicy::Continuous;
    std::optional<QUuid> m_sourceParentId;
    std::optional<SnapRelation> m_xLatch;
    std::optional<SnapRelation> m_yLatch;
    TransformSnapVisualState m_visualState;
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMSNAPSESSION_H
