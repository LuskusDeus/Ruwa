// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMSNAPTYPES_H
#define RUWA_CORE_TRANSFORM_TRANSFORMSNAPTYPES_H

#include "features/transform/TransformState.h"

#include <QUuid>
#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace aether {

enum class SnapAxis { X, Y };
enum class SnapAnchor { Start, Center, End };
enum class SnapTargetType { Canvas, Raster, IsolatedPixel, Text, Group };
enum class SnapRelationType { Alignment, EqualSpacing };
enum class SnapCoordinatePolicy { PixelAligned, Continuous };

inline constexpr float kSnapCaptureThresholdScreenPx = 8.0f;
inline constexpr float kSnapReleaseThresholdScreenPx = 14.0f;

struct SnapSettings {
    bool canvasEnabled = true;
    bool layersEnabled = true;
    bool equalSpacingEnabled = true;
    /// Allow center anchors on both the moving geometry and snap targets.
    bool centerAlignmentEnabled = true;
    bool pixelAlignRasterMovesEnabled = true;
    float captureThresholdScreenPx = kSnapCaptureThresholdScreenPx;
    float releaseThresholdScreenPx = kSnapReleaseThresholdScreenPx;
};

struct SnapTarget {
    Rect bounds {};
    QUuid id;
    QUuid parentId;
    SnapTargetType type = SnapTargetType::Raster;
};

struct SnapAxisEntry {
    float coordinate = 0.0f;
    SnapAnchor anchor = SnapAnchor::Start;
    size_t targetIndex = 0;
};

struct SnapScene {
    Vector2 canvasSize {};
    bool finiteCanvas = true;
    std::vector<SnapTarget> targets;
    std::vector<SnapAxisEntry> xIndex;
    std::vector<SnapAxisEntry> yIndex;
};

struct SnapRelation {
    SnapRelationType type = SnapRelationType::Alignment;
    SnapAxis axis = SnapAxis::X;
    SnapAnchor sourceAnchor = SnapAnchor::Start;
    SnapAnchor targetAnchor = SnapAnchor::Start;
    QUuid targetId;
    Rect targetBounds {};
    SnapTargetType targetType = SnapTargetType::Raster;
    float sourceCoordinate = 0.0f;
    float targetCoordinate = 0.0f;
    float correction = 0.0f;
    float spacing = 0.0f;
    int confirmationCount = 1;
    int hierarchyPriority = 0;
    QString stableKey;
};

} // namespace aether

// Boundary value types (plan 7.16.3): the visual snap state crosses the
// renderer-neutral presentation capability, so it is defined in the
// workspace namespace; the `aether` aliases below keep the legacy snap
// engine and overlay internals building unchanged.

namespace ruwa::ui::workspace {

struct SnapGuideSegment {
    aether::Vector2 from {};
    aether::Vector2 to {};
    aether::SnapAxis axis = aether::SnapAxis::X;
};

struct SnapGuideMarker {
    aether::Vector2 position {};
    bool center = false;
};

struct SnapSpacingDimension {
    aether::Vector2 from {};
    aether::Vector2 to {};
    float value = 0.0f;
};

struct SnapMetricLabel {
    aether::Vector2 position {};
    QString text;
};

struct TransformSnapVisualState {
    std::vector<SnapGuideSegment> alignmentSegments;
    std::vector<SnapGuideMarker> markers;
    std::vector<SnapSpacingDimension> spacingDimensions;
    std::vector<SnapMetricLabel> labels;

    bool active() const
    {
        return !alignmentSegments.empty() || !markers.empty() || !spacingDimensions.empty()
            || !labels.empty();
    }
};

} // namespace ruwa::ui::workspace

namespace aether {

using ruwa::ui::workspace::SnapGuideMarker;
using ruwa::ui::workspace::SnapGuideSegment;
using ruwa::ui::workspace::SnapMetricLabel;
using ruwa::ui::workspace::SnapSpacingDimension;
using ruwa::ui::workspace::TransformSnapVisualState;

struct SnapResult {
    Vector2 correction {};
    std::optional<SnapRelation> xRelation;
    std::optional<SnapRelation> yRelation;
    std::vector<SnapRelation> xRelations;
    std::vector<SnapRelation> yRelations;
    TransformSnapVisualState visualState;

    bool active() const { return xRelation.has_value() || yRelation.has_value(); }
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMSNAPTYPES_H
