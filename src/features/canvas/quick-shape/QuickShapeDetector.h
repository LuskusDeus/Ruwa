// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   Q U I C K   S H A P E   D E T E C T O R
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_QUICK_SHAPE_QUICKSHAPEDETECTOR_H
#define RUWA_FEATURES_CANVAS_QUICK_SHAPE_QUICKSHAPEDETECTOR_H

#include "features/brush/engine/BrushStrokeReplay.h"

#include <vector>
#include <cstddef>

namespace aether {

// ==========================================================================
//   C O N S T A N T S
// ==========================================================================

namespace QuickShapeConstants {
constexpr int kHoldDelayMs = 420;
constexpr int kMorphIntervalMs = 16;
constexpr float kMorphDurationMs = 240.0f;
constexpr float kMinPathLength = 14.0f;
constexpr float kLineMinLength = 6.0f;
constexpr float kLineStraightnessThreshold = 0.64f;
constexpr float kLineAvgDeviationFactor = 0.90f;
constexpr float kLineMaxDeviationFactor = 1.35f;
constexpr float kLineDeviationByLength = 0.09f;
constexpr float kCircleTau = 6.28318530718f;
constexpr float kCircleMinRadius = 5.0f;
constexpr float kCircleMaxStraightness = 0.90f;
constexpr float kCircleMaxAspectRatio = 1.95f;
constexpr float kCircleClosedLoopAspectRatio = 2.35f;
constexpr float kCircleClosedLoopMaxChordByAxis = 0.48f;
constexpr float kCircleMinPathByRadius = 2.0f;
constexpr float kCircleMaxRadialDeviationFactor = 0.78f;
constexpr float kCircleAvgRadialDeviationFactor = 0.42f;
constexpr float kCircleClosedLoopDeviationBoost = 1.35f;
constexpr float kCircleMaxChordByDiameter = 0.42f;
constexpr float kCircleMinPerimeterCoverage = 0.72f;
constexpr float kCircleMaxPerimeterCoverage = 1.25f;
constexpr float kCircleMaxAvgDeviationNorm = 0.22f;
constexpr float kCircleMaxMaxDeviationNorm = 0.62f;
constexpr float kCircleLargeTurnAngle = 1.55f;
constexpr float kCircleVeryLargeTurnAngle = 2.35f;
constexpr float kCircleMaxLargeTurnRatio = 0.45f;
constexpr int kCircleMaxVeryLargeTurns = 4;
constexpr float kTriangleMinAxis = 10.0f;
constexpr float kTriangleMaxStraightness = 0.93f;
constexpr float kTriangleMaxChordByAxis = 0.52f;
constexpr float kTriangleMinPerimeterCoverage = 0.48f;
constexpr float kTriangleMaxPerimeterCoverage = 1.55f;
constexpr float kTriangleMaxAvgDeviationNorm = 0.34f;
constexpr float kTriangleMaxMaxDeviationNorm = 0.82f;
constexpr float kTriangleVertexHitDistanceNorm = 0.62f;
constexpr float kSquareMinAxis = 10.0f;
constexpr float kSquareMaxStraightness = 0.95f;
constexpr float kSquareMaxAspectRatio = 1.22f;
constexpr float kSquareMaxChordByAxis = 0.45f;
constexpr float kSquareMinPerimeterCoverage = 0.55f;
constexpr float kSquareMaxPerimeterCoverage = 1.55f;
constexpr float kSquareMaxAvgDeviationNorm = 0.24f;
constexpr float kSquareMaxMaxDeviationNorm = 0.60f;
constexpr float kResampleMinSpacing = 0.75f;
constexpr size_t kResampleMaxDabMultiplier = 8;
constexpr size_t kResampleHardCap = 4096;
} // namespace QuickShapeConstants

// ==========================================================================
//   D E B U G   I N F O
// ==========================================================================

struct QuickCircleDebugInfo {
    const char* rejectReason = "not_evaluated";
    bool isCandidate = false;
    size_t dabCount = 0;
    float totalPath = 0.0f;
    float chordLength = 0.0f;
    float straightness = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minAxis = 0.0f;
    float maxAxis = 0.0f;
    float aspectRatio = 0.0f;
    float aspectLimit = 0.0f;
    float chordByAxis = 0.0f;
    bool closedLoopLike = false;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float avgRadius = 0.0f;
    float avgDev = 0.0f;
    float maxDev = 0.0f;
    float avgDevLimit = 0.0f;
    float maxDevLimit = 0.0f;
    float chordByDiameter = 0.0f;
    float perimeterCoverage = 0.0f;
    float avgDeviationNorm = 0.0f;
    float maxDeviationNorm = 0.0f;
    int turnSamples = 0;
    int largeTurns = 0;
    int veryLargeTurns = 0;
    float largeTurnRatio = 0.0f;
    int prominentCorners = 0;
    int strongCorners = 0;
    float maxCornerAngle = 0.0f;
    float score = 0.0f;
};

enum class QuickTriangleDirection { Up, Down, Left, Right };

struct QuickShapeDetectionResult {
    enum class Kind { None, Line, Circle, Triangle, Square };

    Kind kind = Kind::None;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float squareHalfExtent = 0.0f;
    float triangleHalfWidth = 0.0f;
    float triangleHalfHeight = 0.0f;
    QuickTriangleDirection triangleDirection = QuickTriangleDirection::Up;
    QuickCircleDebugInfo circleDebug;
};

void logQuickShapeHoldDecision(const QuickCircleDebugInfo& d, bool lineCandidate);

// ==========================================================================
//   D E T E C T I O N   F U N C T I O N S
// ==========================================================================

bool isQuickLineCandidate(const std::vector<ruwa::core::brushes::BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength);

float computeQuickCircleAngleDirection(
    const std::vector<ruwa::core::brushes::BrushStrokeReplayPoint>& dabs, float centerX,
    float centerY);

bool isQuickCircleCandidate(const std::vector<ruwa::core::brushes::BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, float& outCenterX, float& outCenterY,
    QuickCircleDebugInfo* debugInfo = nullptr);

bool isQuickTriangleCandidate(const std::vector<ruwa::core::brushes::BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, float& outCenterX, float& outCenterY, float& outHalfWidth,
    float& outHalfHeight, QuickTriangleDirection& outDirection);

bool isQuickSquareCandidate(const std::vector<ruwa::core::brushes::BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, float& outCenterX, float& outCenterY, float& outHalfExtent);

bool detectQuickShapeCandidate(const std::vector<ruwa::core::brushes::BrushStrokeReplayPoint>& dabs,
    float totalPath, float chordLength, QuickShapeDetectionResult& outResult);

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_QUICK_SHAPE_QUICKSHAPEDETECTOR_H
