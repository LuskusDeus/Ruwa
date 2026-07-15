// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHSETTINGS_H
#define RUWA_CORE_BRUSHES_BRUSHSETTINGS_H

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace ruwa::core::brushes {

enum class BrushDynamicsSettingKey : uint8_t {
    None = 0,
    RadiusMultiplier,
    OpacityMultiplier,
    ShapeFlow,
    ShapeHardness,
    ShapeSpacing,
    ShapeRoundness,
    ShapeAngle,
    TextureAmount,
    TextureScale,
    TextureContrast,
    TextureDepth,
    TextureBlend,
    TextureEdgeBoost,
    ScatterPosition,
    StrokePostCorrection,
    StrokeStabilization,
    StrokeStartTaper,
    StrokeEndTaper,
    StrokeStartCorrectionLength,
    StrokeEndCorrectionLength,
    ColorHue,
    ColorLightness,
    ColorSaturation,
    Count
};

constexpr std::size_t kBrushDynamicsSettingKeyCount
    = static_cast<std::size_t>(BrushDynamicsSettingKey::Count) - 1u;

constexpr float kBrushSpacingMin = 0.005f;
constexpr float kBrushSpacingMax = 5.0f;

enum class BrushInputSourceKey : uint8_t {
    None = 0,
    TabletPressure,
    RandomValue,
    StrokeProgress,
    Time,
    StrokeDirection,
    Count
};

constexpr std::size_t kBrushInputSourceKeyCount
    = static_cast<std::size_t>(BrushInputSourceKey::Count) - 1u;

enum class BrushDynamicsBlendMode : uint8_t { Multiply = 0, Add, Override, Count };

enum class BrushTimeEndAction : uint8_t { Stop = 0, Reverse, Restart, Count };

struct BrushInputContext {
    float pressure = 1.0f;
    float randomValue = 0.0f;
    std::array<float, kBrushDynamicsSettingKeyCount> settingRandomValues {};
    std::array<bool, kBrushDynamicsSettingKeyCount> settingRandomValueAvailable {};
    float strokeProgress = 0.0f;
    float strokeElapsedSeconds = 0.0f;
    bool strokeTimeAvailable = false;
    float strokeDirection = 0.0f;
    bool strokeDirectionAvailable = false;
};

inline float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline float clampSpacingValue(float value)
{
    return std::clamp(value, kBrushSpacingMin, kBrushSpacingMax);
}

inline float clampNonNegative(float value)
{
    return std::max(0.0f, value);
}

inline float clampRange(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

inline float clampBrushTimeDurationSeconds(float value)
{
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, 0.1f, 10.0f);
}

inline float normalizeAngleDegrees(float value)
{
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    float normalized = std::fmod(value, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    return normalized;
}

inline const char* brushDynamicsSettingKeyName(BrushDynamicsSettingKey setting)
{
    switch (setting) {
    case BrushDynamicsSettingKey::RadiusMultiplier:
        return "radius.multiplier";
    case BrushDynamicsSettingKey::OpacityMultiplier:
        return "opacity.multiplier";
    case BrushDynamicsSettingKey::ShapeFlow:
        return "shape.flow";
    case BrushDynamicsSettingKey::ShapeHardness:
        return "shape.hardness";
    case BrushDynamicsSettingKey::ShapeSpacing:
        return "shape.spacing";
    case BrushDynamicsSettingKey::ShapeRoundness:
        return "shape.roundness";
    case BrushDynamicsSettingKey::ShapeAngle:
        return "shape.angle";
    case BrushDynamicsSettingKey::TextureAmount:
        return "texture.amount";
    case BrushDynamicsSettingKey::TextureScale:
        return "texture.scale";
    case BrushDynamicsSettingKey::TextureContrast:
        return "texture.contrast";
    case BrushDynamicsSettingKey::TextureDepth:
        return "texture.depth";
    case BrushDynamicsSettingKey::TextureBlend:
        return "texture.blend";
    case BrushDynamicsSettingKey::TextureEdgeBoost:
        return "texture.edgeBoost";
    case BrushDynamicsSettingKey::ColorHue:
        return "color.hue";
    case BrushDynamicsSettingKey::ColorLightness:
        return "color.lightness";
    case BrushDynamicsSettingKey::ColorSaturation:
        return "color.saturation";
    case BrushDynamicsSettingKey::ScatterPosition:
        return "scatter.position";
    case BrushDynamicsSettingKey::StrokePostCorrection:
        return "stroke.postCorrection";
    case BrushDynamicsSettingKey::StrokeStabilization:
        return "stroke.stabilization";
    case BrushDynamicsSettingKey::StrokeStartTaper:
        return "stroke.startTaper";
    case BrushDynamicsSettingKey::StrokeEndTaper:
        return "stroke.endTaper";
    case BrushDynamicsSettingKey::StrokeStartCorrectionLength:
        return "stroke.startCorrectionLength";
    case BrushDynamicsSettingKey::StrokeEndCorrectionLength:
        return "stroke.endCorrectionLength";
    case BrushDynamicsSettingKey::None:
    case BrushDynamicsSettingKey::Count:
        break;
    }
    return nullptr;
}

inline const char* brushInputSourceKeyName(BrushInputSourceKey source)
{
    switch (source) {
    case BrushInputSourceKey::TabletPressure:
        return "tabletPressure";
    case BrushInputSourceKey::RandomValue:
        return "randomValue";
    case BrushInputSourceKey::StrokeProgress:
        return "strokeProgress";
    case BrushInputSourceKey::Time:
        return "time";
    case BrushInputSourceKey::StrokeDirection:
        return "strokeDirection";
    case BrushInputSourceKey::None:
    case BrushInputSourceKey::Count:
        break;
    }
    return nullptr;
}

inline const char* brushDynamicsBlendModeName(BrushDynamicsBlendMode mode)
{
    switch (mode) {
    case BrushDynamicsBlendMode::Multiply:
        return "multiply";
    case BrushDynamicsBlendMode::Add:
        return "add";
    case BrushDynamicsBlendMode::Override:
        return "override";
    case BrushDynamicsBlendMode::Count:
        break;
    }
    return nullptr;
}

inline const char* brushTimeEndActionName(BrushTimeEndAction action)
{
    switch (action) {
    case BrushTimeEndAction::Stop:
        return "stop";
    case BrushTimeEndAction::Reverse:
        return "reverse";
    case BrushTimeEndAction::Restart:
        return "restart";
    case BrushTimeEndAction::Count:
        break;
    }
    return nullptr;
}

inline BrushDynamicsSettingKey brushDynamicsSettingKeyFromSettingKey(std::string_view settingKey)
{
    if (settingKey == std::string_view("radius.multiplier")) {
        return BrushDynamicsSettingKey::RadiusMultiplier;
    }
    if (settingKey == std::string_view("opacity.multiplier")) {
        return BrushDynamicsSettingKey::OpacityMultiplier;
    }
    if (settingKey == std::string_view("shape.flow")) {
        return BrushDynamicsSettingKey::ShapeFlow;
    }
    if (settingKey == std::string_view("shape.hardness")) {
        return BrushDynamicsSettingKey::ShapeHardness;
    }
    if (settingKey == std::string_view("shape.spacing")) {
        return BrushDynamicsSettingKey::ShapeSpacing;
    }
    if (settingKey == std::string_view("shape.roundness")) {
        return BrushDynamicsSettingKey::ShapeRoundness;
    }
    if (settingKey == std::string_view("shape.angle")) {
        return BrushDynamicsSettingKey::ShapeAngle;
    }
    if (settingKey == std::string_view("texture.amount")) {
        return BrushDynamicsSettingKey::TextureAmount;
    }
    if (settingKey == std::string_view("texture.scale")) {
        return BrushDynamicsSettingKey::TextureScale;
    }
    if (settingKey == std::string_view("texture.contrast")) {
        return BrushDynamicsSettingKey::TextureContrast;
    }
    if (settingKey == std::string_view("texture.depth")) {
        return BrushDynamicsSettingKey::TextureDepth;
    }
    if (settingKey == std::string_view("texture.blend")) {
        return BrushDynamicsSettingKey::TextureBlend;
    }
    if (settingKey == std::string_view("texture.edgeBoost")) {
        return BrushDynamicsSettingKey::TextureEdgeBoost;
    }
    if (settingKey == std::string_view("color.hue")) {
        return BrushDynamicsSettingKey::ColorHue;
    }
    if (settingKey == std::string_view("color.lightness")) {
        return BrushDynamicsSettingKey::ColorLightness;
    }
    if (settingKey == std::string_view("color.saturation")) {
        return BrushDynamicsSettingKey::ColorSaturation;
    }
    if (settingKey == std::string_view("scatter.position")) {
        return BrushDynamicsSettingKey::ScatterPosition;
    }
    if (settingKey == std::string_view("stroke.postCorrection")
        || settingKey == std::string_view("stroke.smoothing")) {
        return BrushDynamicsSettingKey::StrokePostCorrection;
    }
    if (settingKey == std::string_view("stroke.stabilization")
        || settingKey == std::string_view("stroke.stabilizer")) {
        return BrushDynamicsSettingKey::StrokeStabilization;
    }
    if (settingKey == std::string_view("stroke.startTaper")) {
        return BrushDynamicsSettingKey::StrokeStartTaper;
    }
    if (settingKey == std::string_view("stroke.endTaper")) {
        return BrushDynamicsSettingKey::StrokeEndTaper;
    }
    if (settingKey == std::string_view("stroke.startCorrectionLength")) {
        return BrushDynamicsSettingKey::StrokeStartCorrectionLength;
    }
    if (settingKey == std::string_view("stroke.endCorrectionLength")) {
        return BrushDynamicsSettingKey::StrokeEndCorrectionLength;
    }
    return BrushDynamicsSettingKey::None;
}

inline BrushInputSourceKey brushInputSourceKeyFromName(std::string_view sourceName)
{
    if (sourceName == std::string_view("tabletPressure")) {
        return BrushInputSourceKey::TabletPressure;
    }
    if (sourceName == std::string_view("randomValue")) {
        return BrushInputSourceKey::RandomValue;
    }
    if (sourceName == std::string_view("strokeProgress")) {
        return BrushInputSourceKey::StrokeProgress;
    }
    if (sourceName == std::string_view("time")) {
        return BrushInputSourceKey::Time;
    }
    if (sourceName == std::string_view("strokeDirection")) {
        return BrushInputSourceKey::StrokeDirection;
    }
    return BrushInputSourceKey::None;
}

inline BrushTimeEndAction brushTimeEndActionFromName(std::string_view actionName)
{
    if (actionName == std::string_view("reverse")) {
        return BrushTimeEndAction::Reverse;
    }
    if (actionName == std::string_view("restart")) {
        return BrushTimeEndAction::Restart;
    }
    if (actionName == std::string_view("stop")) {
        return BrushTimeEndAction::Stop;
    }
    return BrushTimeEndAction::Stop;
}

inline BrushDynamicsBlendMode brushDynamicsBlendModeFromName(std::string_view modeName)
{
    if (modeName == std::string_view("add")) {
        return BrushDynamicsBlendMode::Add;
    }
    if (modeName == std::string_view("override")) {
        return BrushDynamicsBlendMode::Override;
    }
    return BrushDynamicsBlendMode::Multiply;
}

inline float brushDynamicsValueMin(BrushDynamicsSettingKey setting)
{
    switch (setting) {
    default:
        return 0.0f;
    }
}

inline float brushDynamicsValueMax(BrushDynamicsSettingKey setting)
{
    switch (setting) {
    case BrushDynamicsSettingKey::ShapeSpacing:
        return kBrushSpacingMax;
    case BrushDynamicsSettingKey::ShapeAngle:
    case BrushDynamicsSettingKey::ColorHue:
        return 360.0f;
    case BrushDynamicsSettingKey::TextureScale:
        return 4.0f;
    case BrushDynamicsSettingKey::ColorLightness:
    case BrushDynamicsSettingKey::ColorSaturation:
        return 2.0f;
    case BrushDynamicsSettingKey::StrokeStartCorrectionLength:
    case BrushDynamicsSettingKey::StrokeEndCorrectionLength:
        return 500.0f;
    default:
        return 1.0f;
    }
}

inline float brushDynamicsResultMin(BrushDynamicsSettingKey setting)
{
    switch (setting) {
    case BrushDynamicsSettingKey::ShapeSpacing:
        return kBrushSpacingMin;
    case BrushDynamicsSettingKey::TextureScale:
        return 0.1f;
    default:
        return 0.0f;
    }
}

inline float brushDynamicsResultMax(BrushDynamicsSettingKey setting)
{
    if (setting == BrushDynamicsSettingKey::RadiusMultiplier) {
        return 2.0f;
    }
    return brushDynamicsValueMax(setting);
}

inline float brushDynamicsBindingValueMin(
    BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode)
{
    switch (mode) {
    case BrushDynamicsBlendMode::Multiply:
        return brushDynamicsValueMin(setting);
    case BrushDynamicsBlendMode::Add:
        return -(brushDynamicsResultMax(setting) - brushDynamicsResultMin(setting));
    case BrushDynamicsBlendMode::Override:
        return brushDynamicsResultMin(setting);
    case BrushDynamicsBlendMode::Count:
        break;
    }
    return 0.0f;
}

inline float brushDynamicsBindingValueMax(
    BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode)
{
    switch (mode) {
    case BrushDynamicsBlendMode::Multiply:
        return brushDynamicsValueMax(setting);
    case BrushDynamicsBlendMode::Add:
        return brushDynamicsResultMax(setting) - brushDynamicsResultMin(setting);
    case BrushDynamicsBlendMode::Override:
        return brushDynamicsResultMax(setting);
    case BrushDynamicsBlendMode::Count:
        break;
    }
    return brushDynamicsResultMax(setting);
}

inline float clampBrushDynamicsBindingValue(
    BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode, float value)
{
    return clampRange(value, brushDynamicsBindingValueMin(setting, mode),
        brushDynamicsBindingValueMax(setting, mode));
}

inline float clampBrushDynamicsResultValue(BrushDynamicsSettingKey setting, float value)
{
    return clampRange(value, brushDynamicsResultMin(setting), brushDynamicsResultMax(setting));
}

inline float brushDynamicsRandomAmountMax(BrushDynamicsSettingKey setting)
{
    if (setting == BrushDynamicsSettingKey::RadiusMultiplier) {
        return 1.0f;
    }
    return brushDynamicsResultMax(setting) - brushDynamicsResultMin(setting);
}

inline bool supportsBrushDynamicsSetting(BrushDynamicsSettingKey setting)
{
    return setting != BrushDynamicsSettingKey::None && setting != BrushDynamicsSettingKey::Count;
}

inline BrushDynamicsBlendMode defaultBrushDynamicsBlendMode(BrushDynamicsSettingKey setting)
{
    return (setting == BrushDynamicsSettingKey::ShapeAngle
               || setting == BrushDynamicsSettingKey::ColorHue)
        ? BrushDynamicsBlendMode::Add
        : BrushDynamicsBlendMode::Multiply;
}

inline bool supportsBrushDynamicsBlendMode(
    BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode)
{
    if (!supportsBrushDynamicsSetting(setting) || mode == BrushDynamicsBlendMode::Count) {
        return false;
    }

    if (setting == BrushDynamicsSettingKey::ShapeAngle
        || setting == BrushDynamicsSettingKey::ColorHue) {
        return mode == BrushDynamicsBlendMode::Add || mode == BrushDynamicsBlendMode::Override;
    }

    return mode == BrushDynamicsBlendMode::Multiply || mode == BrushDynamicsBlendMode::Add
        || mode == BrushDynamicsBlendMode::Override;
}

inline BrushDynamicsBlendMode normalizeBrushDynamicsBlendMode(
    BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode)
{
    return supportsBrushDynamicsBlendMode(setting, mode) ? mode
                                                         : defaultBrushDynamicsBlendMode(setting);
}

inline float finalizeBrushDynamicsResultValue(BrushDynamicsSettingKey setting, float value)
{
    if (setting == BrushDynamicsSettingKey::ShapeAngle
        || setting == BrushDynamicsSettingKey::ColorHue) {
        return normalizeAngleDegrees(value);
    }
    return clampBrushDynamicsResultValue(setting, value);
}

inline bool supportsBrushInputSource(BrushInputSourceKey source)
{
    return source != BrushInputSourceKey::None && source != BrushInputSourceKey::Count;
}

inline std::size_t brushDynamicsSettingIndex(BrushDynamicsSettingKey setting)
{
    return static_cast<std::size_t>(setting) - 1u;
}

inline BrushDynamicsSettingKey brushDynamicsSettingFromIndex(std::size_t index)
{
    return static_cast<BrushDynamicsSettingKey>(index + 1u);
}

inline std::size_t brushInputSourceIndex(BrushInputSourceKey source)
{
    return static_cast<std::size_t>(source) - 1u;
}

inline BrushInputSourceKey brushInputSourceFromIndex(std::size_t index)
{
    return static_cast<BrushInputSourceKey>(index + 1u);
}

inline float normalizedBrushStrokeTime(
    float elapsedSeconds, float durationSeconds, BrushTimeEndAction endAction)
{
    const float safeDuration = clampBrushTimeDurationSeconds(durationSeconds);
    const float boundedElapsed = std::max(0.0f, elapsedSeconds);
    const float cycle = boundedElapsed / safeDuration;

    switch (endAction) {
    case BrushTimeEndAction::Stop:
        return clamp01(cycle);
    case BrushTimeEndAction::Reverse: {
        const float phase = std::fmod(cycle, 2.0f);
        if (!std::isfinite(phase)) {
            return 0.0f;
        }
        return (phase <= 1.0f) ? phase : (2.0f - phase);
    }
    case BrushTimeEndAction::Restart: {
        const float phase = std::fmod(cycle, 1.0f);
        if (!std::isfinite(phase)) {
            return 0.0f;
        }
        return clamp01(phase);
    }
    case BrushTimeEndAction::Count:
        break;
    }
    return 0.0f;
}

struct BrushDynamicsBinding;

inline float normalizedBrushStrokeTime(const BrushInputContext& inputContext, float durationSeconds,
    BrushTimeEndAction endAction, bool* available = nullptr)
{
    if (available) {
        *available = inputContext.strokeTimeAvailable;
    }
    if (!inputContext.strokeTimeAvailable) {
        return 0.0f;
    }
    return normalizedBrushStrokeTime(inputContext.strokeElapsedSeconds, durationSeconds, endAction);
}

inline float brushInputSourceValue(const BrushInputContext& inputContext,
    BrushInputSourceKey source, const BrushDynamicsBinding* binding = nullptr,
    bool* available = nullptr);

struct BrushMappingPoint {
    float x = 0.0f;
    float y = 1.0f;
    float smoothness = 1.0f;
};

struct BrushMappingCurve {
    std::vector<BrushMappingPoint> points;

    bool empty() const { return points.empty(); }

    void sortByX()
    {
        std::sort(points.begin(), points.end(),
            [](const BrushMappingPoint& a, const BrushMappingPoint& b) { return a.x < b.x; });
    }

    void normalize(BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None,
        BrushDynamicsBlendMode mode = BrushDynamicsBlendMode::Multiply)
    {
        for (auto& point : points) {
            point.x = clamp01(point.x);
            point.y = clampBrushDynamicsBindingValue(setting, mode, point.y);
            point.smoothness = clamp01(point.smoothness);
        }
        sortByX();
    }

    float evaluate(float inputValue, float fallback = 1.0f,
        BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None,
        BrushDynamicsBlendMode mode = BrushDynamicsBlendMode::Multiply) const
    {
        if (points.empty()) {
            return clampBrushDynamicsBindingValue(setting, mode, fallback);
        }
        if (points.size() == 1) {
            return clampBrushDynamicsBindingValue(setting, mode, points.front().y);
        }

        const float boundedInput = clamp01(inputValue);
        if (boundedInput <= points.front().x) {
            return clampBrushDynamicsBindingValue(setting, mode, points.front().y);
        }
        if (boundedInput >= points.back().x) {
            return clampBrushDynamicsBindingValue(setting, mode, points.back().y);
        }

        for (std::size_t i = 1; i < points.size(); ++i) {
            const BrushMappingPoint& p0 = points[i - 1];
            const BrushMappingPoint& p1 = points[i];
            if (boundedInput > p1.x) {
                continue;
            }

            const float dx = std::max(0.0001f, p1.x - p0.x);
            const float t = (boundedInput - p0.x) / dx;
            return clampBrushDynamicsBindingValue(setting, mode, evaluateSegment(i - 1, t));
        }

        return clampBrushDynamicsBindingValue(setting, mode, points.back().y);
    }

private:
    float evaluateSegment(std::size_t index, float t) const
    {
        const BrushMappingPoint& p0 = points[index];
        const BrushMappingPoint& p1 = points[index + 1];
        const float dx = std::max(0.0001f, p1.x - p0.x);
        const float startTangent = pchipTangent(index);
        const float endTangent = pchipTangent(index + 1);

        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = (2.0f * t3) - (3.0f * t2) + 1.0f;
        const float h10 = t3 - (2.0f * t2) + t;
        const float h01 = (-2.0f * t3) + (3.0f * t2);
        const float h11 = t3 - t2;
        return h00 * p0.y + h10 * dx * startTangent + h01 * p1.y + h11 * dx * endTangent;
    }

    float pchipTangent(std::size_t index) const
    {
        if (points.size() <= 2) {
            return simpleSlope(0, 1);
        }
        if (index == 0) {
            return endpointTangent(0);
        }
        if (index >= points.size() - 1) {
            return endpointTangent(points.size() - 1);
        }

        const float leftSlope = simpleSlope(index - 1, index);
        const float rightSlope = simpleSlope(index, index + 1);
        if (slopeSign(leftSlope) != slopeSign(rightSlope)) {
            return 0.0f;
        }
        if (slopeSign(leftSlope) == 0) {
            return 0.0f;
        }

        const float leftDx = segmentWidth(index - 1, index);
        const float rightDx = segmentWidth(index, index + 1);
        const float w1 = 2.0f * rightDx + leftDx;
        const float w2 = rightDx + 2.0f * leftDx;
        return (w1 + w2) / ((w1 / leftSlope) + (w2 / rightSlope));
    }

    float endpointTangent(std::size_t index) const
    {
        const bool leftEndpoint = index == 0;
        const std::size_t edgeIndex = leftEndpoint ? 0 : points.size() - 2;
        const std::size_t nextIndex = leftEndpoint ? 1 : points.size() - 3;
        const float edgeDx = segmentWidth(edgeIndex, edgeIndex + 1);
        const float nextDx = segmentWidth(nextIndex, nextIndex + 1);
        const float edgeSlope = simpleSlope(edgeIndex, edgeIndex + 1);
        const float nextSlope = simpleSlope(nextIndex, nextIndex + 1);
        const float denominator = std::max(0.0001f, edgeDx + nextDx);
        float tangent = ((2.0f * edgeDx + nextDx) * edgeSlope - edgeDx * nextSlope) / denominator;

        if (slopeSign(tangent) != slopeSign(edgeSlope)) {
            return 0.0f;
        }
        if (slopeSign(edgeSlope) != slopeSign(nextSlope)
            && std::abs(tangent) > std::abs(3.0f * edgeSlope)) {
            return 3.0f * edgeSlope;
        }
        return tangent;
    }

    float simpleSlope(std::size_t leftIndex, std::size_t rightIndex) const
    {
        const BrushMappingPoint& left = points[leftIndex];
        const BrushMappingPoint& right = points[rightIndex];
        return (right.y - left.y) / segmentWidth(leftIndex, rightIndex);
    }

    float segmentWidth(std::size_t leftIndex, std::size_t rightIndex) const
    {
        return std::max(0.0001f, points[rightIndex].x - points[leftIndex].x);
    }

    int slopeSign(float value) const
    {
        if (std::abs(value) <= 0.000001f) {
            return 0;
        }
        return value > 0.0f ? 1 : -1;
    }
};

struct BrushDynamicsBinding {
    BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None;
    BrushInputSourceKey source = BrushInputSourceKey::None;
    BrushDynamicsBlendMode mode = BrushDynamicsBlendMode::Multiply;
    bool enabled = false;
    float durationSec = 1.0f;
    BrushTimeEndAction endAction = BrushTimeEndAction::Stop;
    BrushMappingCurve curve;

    bool hasStoredCurve() const { return !curve.empty(); }

    bool hasNonDefaultTimeSettings() const
    {
        return source == BrushInputSourceKey::Time
            && (std::abs(durationSec - 1.0f) > 0.0001f || endAction != BrushTimeEndAction::Stop);
    }

    bool hasStoredState() const
    {
        return enabled || hasStoredCurve() || hasNonDefaultTimeSettings();
    }

    bool isActive() const
    {
        return enabled && supportsBrushDynamicsSetting(setting) && supportsBrushInputSource(source)
            && !curve.empty();
    }

    float evaluate(const BrushInputContext& inputContext, float fallback = 1.0f) const
    {
        bool sourceAvailable = false;
        const float inputValue
            = brushInputSourceValue(inputContext, source, this, &sourceAvailable);
        return (isActive() && sourceAvailable)
            ? curve.evaluate(inputValue, fallback, setting, mode)
            : clampBrushDynamicsBindingValue(setting, mode, fallback);
    }
};

inline float brushDynamicsRandomAmount(const BrushDynamicsBinding& binding)
{
    float amount = 0.0f;
    for (const auto& point : binding.curve.points) {
        amount = std::max(amount, std::abs(point.y));
    }
    return clampRange(amount, 0.0f, brushDynamicsRandomAmountMax(binding.setting));
}

inline void setBrushDynamicsRandomAmount(BrushDynamicsBinding& binding, float amount)
{
    binding.source = BrushInputSourceKey::RandomValue;
    binding.mode = BrushDynamicsBlendMode::Add;
    const float clampedAmount
        = clampRange(amount, 0.0f, brushDynamicsRandomAmountMax(binding.setting));
    binding.curve.points = {
        { 0.0f, -clampedAmount, 0.65f },
        { 1.0f, clampedAmount, 0.65f },
    };
    binding.curve.normalize(binding.setting, binding.mode);
}

inline float brushInputSourceValue(const BrushInputContext& inputContext,
    BrushInputSourceKey source, const BrushDynamicsBinding* binding, bool* available)
{
    switch (source) {
    case BrushInputSourceKey::TabletPressure:
        if (available) {
            *available = true;
        }
        return clamp01(inputContext.pressure);
    case BrushInputSourceKey::RandomValue:
        if (available) {
            *available = true;
        }
        if (binding && supportsBrushDynamicsSetting(binding->setting)) {
            const std::size_t settingIndex = brushDynamicsSettingIndex(binding->setting);
            if (settingIndex < inputContext.settingRandomValueAvailable.size()
                && inputContext.settingRandomValueAvailable[settingIndex]) {
                return clamp01(inputContext.settingRandomValues[settingIndex]);
            }
        }
        return clamp01(inputContext.randomValue);
    case BrushInputSourceKey::StrokeProgress:
        if (available) {
            *available = true;
        }
        return clamp01(inputContext.strokeProgress);
    case BrushInputSourceKey::Time:
        if (!binding) {
            if (available) {
                *available = false;
            }
            return 0.0f;
        }
        return normalizedBrushStrokeTime(
            inputContext, binding->durationSec, binding->endAction, available);
    case BrushInputSourceKey::StrokeDirection:
        if (available) {
            *available = inputContext.strokeDirectionAvailable;
        }
        return inputContext.strokeDirectionAvailable ? clamp01(inputContext.strokeDirection) : 0.0f;
    case BrushInputSourceKey::None:
    case BrushInputSourceKey::Count:
        break;
    }

    if (available) {
        *available = false;
    }
    return 0.0f;
}

struct BrushDynamicsSlot {
    BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None;
    std::array<BrushDynamicsBinding, kBrushInputSourceKeyCount> bindings {};

    BrushDynamicsSlot()
    {
        for (std::size_t sourceIndex = 0; sourceIndex < bindings.size(); ++sourceIndex) {
            bindings[sourceIndex].source = brushInputSourceFromIndex(sourceIndex);
        }
    }

    BrushDynamicsBinding& binding(BrushInputSourceKey source)
    {
        return bindings[brushInputSourceIndex(source)];
    }

    const BrushDynamicsBinding& binding(BrushInputSourceKey source) const
    {
        return bindings[brushInputSourceIndex(source)];
    }

    bool hasStoredBindings() const
    {
        for (const auto& bindingItem : bindings) {
            if (bindingItem.hasStoredState()) {
                return true;
            }
        }
        return false;
    }

    bool hasActiveBindings() const
    {
        for (const auto& bindingItem : bindings) {
            if (bindingItem.isActive()) {
                return true;
            }
        }
        return false;
    }

    const BrushDynamicsBinding* activeOverrideBinding() const
    {
        for (const auto& bindingItem : bindings) {
            if (bindingItem.isActive() && bindingItem.mode == BrushDynamicsBlendMode::Override) {
                return &bindingItem;
            }
        }
        return nullptr;
    }
};

struct BrushDynamicsModel {
    std::array<BrushDynamicsSlot, kBrushDynamicsSettingKeyCount> settingSlots {};

    BrushDynamicsModel()
    {
        for (std::size_t settingIndex = 0; settingIndex < settingSlots.size(); ++settingIndex) {
            auto& slotItem = settingSlots[settingIndex];
            slotItem.setting = brushDynamicsSettingFromIndex(settingIndex);
            for (std::size_t sourceIndex = 0; sourceIndex < slotItem.bindings.size();
                ++sourceIndex) {
                auto& binding = slotItem.bindings[sourceIndex];
                binding.setting = slotItem.setting;
                binding.source = brushInputSourceFromIndex(sourceIndex);
            }
        }
    }

    BrushDynamicsSlot& slotForSetting(BrushDynamicsSettingKey setting)
    {
        return settingSlots[brushDynamicsSettingIndex(setting)];
    }

    const BrushDynamicsSlot& slotForSetting(BrushDynamicsSettingKey setting) const
    {
        return settingSlots[brushDynamicsSettingIndex(setting)];
    }

    bool hasStoredBindings() const
    {
        for (const auto& slotItem : settingSlots) {
            if (slotItem.hasStoredBindings()) {
                return true;
            }
        }
        return false;
    }

    BrushDynamicsBinding& pressureBinding(BrushDynamicsSettingKey setting)
    {
        return slotForSetting(setting).binding(BrushInputSourceKey::TabletPressure);
    }

    const BrushDynamicsBinding& pressureBinding(BrushDynamicsSettingKey setting) const
    {
        return slotForSetting(setting).binding(BrushInputSourceKey::TabletPressure);
    }

    bool hasStoredPressureBindings() const
    {
        for (const auto& slotItem : settingSlots) {
            const auto& binding = slotItem.binding(BrushInputSourceKey::TabletPressure);
            if (binding.enabled || binding.hasStoredCurve()) {
                return true;
            }
        }
        return false;
    }
};

using BrushPressureBinding = BrushDynamicsBinding;

inline bool supportsPressureDynamics(BrushDynamicsSettingKey setting)
{
    return supportsBrushDynamicsSetting(setting);
}

struct BrushEvaluatedState {
    float radiusMultiplier = 1.0f;
    float opacityMultiplier = 1.0f;
    float hardness = 0.7f;
    float spacing = 0.25f;
    float flow = 1.0f;
    float roundness = 1.0f;
    float angleDegrees = 0.0f;
    float textureAmount = 0.0f;
    float textureScale = 1.0f;
    float textureContrast = 0.5f;
    float textureDepth = 1.0f;
    float textureBlend = 0.5f;
    float textureEdgeBoost = 0.0f;
    float colorHue = 0.0f;
    float colorLightness = 1.0f;
    float colorSaturation = 1.0f;
    float scatterPosition = 0.0f;
    float postCorrection = 0.0f;
    float stabilization = 0.0f;
    float startTaper = 0.0f;
    float endTaper = 0.0f;
    float startCorrectionLength = 0.0f;
    float endCorrectionLength = 0.0f;
};

struct BrushSettingsData {
    enum FlowBlendMode {
        FlowBlendSrcOver = 0,
        FlowBlendMax = 1,
    };

    // ---- Shape ----
    int flowBlendMode = FlowBlendMax;
    float hardness = 0.7f;
    float spacing = 0.25f;
    float flow = 1.0f;
    float roundness = 1.0f;
    float angle = 0.0f;

    // ---- Pressure toggles ----
    bool sizePressureEnabled = false;
    bool opacityPressureEnabled = true;

    // ---- Edge softness ----
    bool brushFeather = true;

    // ---- Legacy pressure ----
    float opacityPressureMin = 0.0f;
    float opacityPressureMax = 1.0f;
    float sizePressureMin = 0.0f;
    float sizePressureMax = 1.0f;
    float flowPressureMin = 1.0f;
    float flowPressureMax = 1.0f;

    // ---- New dynamics ----
    BrushDynamicsModel dynamics;

    // ---- Texture ----
    int textureType = 0;
    float textureAmount = 0.0f;
    float textureScale = 1.0f;
    float textureContrast = 0.5f;
    float textureDepth = 1.0f;
    float textureBlend = 0.5f;
    float textureEdgeBoost = 0.0f;

    // ---- Color ----
    float colorHue = 0.0f;
    float colorLightness = 1.0f;
    float colorSaturation = 1.0f;

    // ---- Dab ----
    enum DabInterpolation {
        DabInterpolationBilinear = 0,
        DabInterpolationNearest = 1,
    };

    int dabType = 0;
    QString dabCustomImagePath;
    float dabXScale = 1.0f;
    float dabYScale = 1.0f;
    float dabRotation = 0.0f;
    float dabThreshold = 0.5f;
    float dabCompression = 1.0f;
    int dabInterpolation = DabInterpolationBilinear;

    // ---- Scatter ----
    float scatterPosition = 0.0f;

    // ---- Stroke ----
    float postCorrection = 0.0f;
    float stabilization = 0.0f;
    float startTaper = 0.0f;
    float endTaper = 0.0f;
    bool adjustCorrectionBySpeed = false;
    bool startCorrectionEnabled = false;
    float startCorrectionLength = 0.0f;
    bool endCorrectionEnabled = false;
    float endCorrectionLength = 0.0f;
    int strokeBlendMode = 0;
    // Smudge pickup rate for the pigment-latent reservoir. 0 keeps the
    // initially loaded content; 1 follows the canvas under every dab.
    // Wet paint uses its dedicated colorBlending/length exchange instead.
    float wetMix = 0.5f;

    // ---- Color mixing (wet brush) ----
    // SAI-style watercolor coefficients. When any is non-zero the brush runs
    // through the canvas-reading reservoir pipeline (like smudge) instead of
    // the plain accumulate-into-stroke-buffer path.
    //   blending: how much canvas color under the dab is mixed into the
    //             reservoir (SAI "Сме-ние цв." / 混色). = pickup rate.
    //   dilution: how much the reservoir loses pigment, so the stroke thins
    //             out and runs dry (SAI "Водность" / 水分量).
    //   spread:   the TARGET FRACTION of the brush's own color in the stroke
    //             (SAI "Расп. цвета" / 色伸び): the reservoir exchanges toward
    //             mix(canvas, pen, spread), so 2% = faint tint of the pen
    //             color, 100% = pure pen. A fraction, not a rate.
    // blending and dilution are exponential mixing rates defined per
    // HALF-RADIUS OF TRAVEL, not per dab: the renderer converts them to
    // per-dab rates from the actual inter-dab distance (wetRatePerDab in
    // GLBrushRenderer.cpp), so the same slider value behaves identically at
    // 1% and 50% spacing.
    // Default OFF: brushes are dry unless the user explicitly raises a wet
    // coefficient (any of blending/dilution/spread > 0 enables wet mode).
    float colorBlending = 0.0f;
    float colorDilution = 0.0f;
    float colorSpread = 0.0f;
    //   length:   reservoir persistence / smear memory (SAI "Стойкость" /
    //             MyPaint "Smudge length"). High = the picked-up color is
    //             dragged far along the stroke before refreshing from the
    //             canvas; low = the reservoir snaps to the canvas under the
    //             brush each dab. Does not by itself enable wet mode.
    float colorLength = 0.0f;
    //   wetFlow:  how loosely the bristles hold paint (the reservoir
    //             advection share, formerly the WET_FLOW shader constant).
    //             0 = paint glued to the brush: rim pickup never reaches the
    //             body, barely mixes; 1 = world-anchored paint: instant full
    //             mix under the stroke, zero carry tail. Does not by itself
    //             enable wet mode.
    float colorWetFlow = 0.75f;
    //   buildup:  per-pass paint layering. The brush carries a thin coat
    //             of body (1 - buildup) per pass: one pass deposits a
    //             translucent layer of the pen color, and where a later
    //             pass overlaps paint laid before it (self-crossing,
    //             scrubbing back and forth, or older content) the new
    //             coat composites OVER the old one — coverage stacks one
    //             layer per pass and saturates AT the pen color, never
    //             beyond. First-contact detection reuses the round-14
    //             advection footprint test (within one pass the leading
    //             crescent only ever reads blank canvas), so no extra
    //             state is needed. 0 = off: full-bodied paint in a single
    //             pass, self-overlaps invisible (legacy behaviour).
    //             Does not by itself enable wet mode.
    float colorBuildup = 0.0f;
    //   dryRate:  paint-supply depletion (drying). The brush carries a
    //             finite supply of its own paint; the supply decays
    //             exponentially with travel at this rate (defined per
    //             half-radius of travel, like blending/dilution) and scales
    //             the effective colorSpread, so the stroke starts at the pen
    //             color and runs dry into a pure smudge/blend. 0 = infinite
    //             paint (constant spread along the whole stroke). Does not
    //             by itself enable wet mode.
    float colorDryRate = 0.0f;
};

inline void normalizePressureRange(float& minValue, float& maxValue)
{
    minValue = std::clamp(minValue, 0.0f, 1.0f);
    maxValue = std::clamp(maxValue, 0.0f, 1.0f);
    if (minValue > maxValue) {
        std::swap(minValue, maxValue);
    }
}

inline float rangeValueForPressure(float minValue, float maxValue, float pressure)
{
    const float p = std::clamp(pressure, 0.0f, 1.0f);
    return minValue + (maxValue - minValue) * p;
}

inline float applyBrushDynamicsBinding(
    float baseValue, const BrushDynamicsBinding& binding, const BrushInputContext& inputContext)
{
    switch (binding.mode) {
    case BrushDynamicsBlendMode::Multiply:
        return baseValue * binding.evaluate(inputContext, 1.0f);
    case BrushDynamicsBlendMode::Add:
        return baseValue + binding.evaluate(inputContext, 0.0f);
    case BrushDynamicsBlendMode::Override:
        return binding.evaluate(inputContext, baseValue);
    case BrushDynamicsBlendMode::Count:
        break;
    }
    return baseValue;
}

inline float evaluateDynamicsSlotValue(
    float baseValue, const BrushDynamicsSlot& slot, const BrushInputContext& inputContext)
{
    if (const auto* overrideBinding = slot.activeOverrideBinding()) {
        bool sourceAvailable = false;
        brushInputSourceValue(
            inputContext, overrideBinding->source, overrideBinding, &sourceAvailable);
        if (sourceAvailable) {
            return finalizeBrushDynamicsResultValue(
                slot.setting, applyBrushDynamicsBinding(baseValue, *overrideBinding, inputContext));
        }
    }

    float multiplyFactor = 1.0f;
    float additiveDelta = 0.0f;
    for (const auto& binding : slot.bindings) {
        if (!binding.isActive()) {
            continue;
        }

        switch (binding.mode) {
        case BrushDynamicsBlendMode::Multiply:
            multiplyFactor *= binding.evaluate(inputContext, 1.0f);
            break;
        case BrushDynamicsBlendMode::Add:
            additiveDelta += binding.evaluate(inputContext, 0.0f);
            break;
        case BrushDynamicsBlendMode::Override:
        case BrushDynamicsBlendMode::Count:
            break;
        }
    }

    return finalizeBrushDynamicsResultValue(
        slot.setting, baseValue * multiplyFactor + additiveDelta);
}

inline float evaluateDynamicsSlotValueExcludingSource(float baseValue,
    const BrushDynamicsSlot& slot, const BrushInputContext& inputContext,
    BrushInputSourceKey excludedSource)
{
    if (const auto* overrideBinding = slot.activeOverrideBinding()) {
        if (overrideBinding->source != excludedSource) {
            bool sourceAvailable = false;
            brushInputSourceValue(
                inputContext, overrideBinding->source, overrideBinding, &sourceAvailable);
            if (sourceAvailable) {
                return finalizeBrushDynamicsResultValue(slot.setting,
                    applyBrushDynamicsBinding(baseValue, *overrideBinding, inputContext));
            }
        }
    }

    float multiplyFactor = 1.0f;
    float additiveDelta = 0.0f;
    for (const auto& binding : slot.bindings) {
        if (!binding.isActive() || binding.source == excludedSource) {
            continue;
        }

        switch (binding.mode) {
        case BrushDynamicsBlendMode::Multiply:
            multiplyFactor *= binding.evaluate(inputContext, 1.0f);
            break;
        case BrushDynamicsBlendMode::Add:
            additiveDelta += binding.evaluate(inputContext, 0.0f);
            break;
        case BrushDynamicsBlendMode::Override:
        case BrushDynamicsBlendMode::Count:
            break;
        }
    }

    return finalizeBrushDynamicsResultValue(
        slot.setting, baseValue * multiplyFactor + additiveDelta);
}

inline void normalizeBrushDynamics(BrushDynamicsModel& dynamics)
{
    for (std::size_t settingIndex = 0; settingIndex < dynamics.settingSlots.size();
        ++settingIndex) {
        auto& slotItem = dynamics.settingSlots[settingIndex];
        slotItem.setting = brushDynamicsSettingFromIndex(settingIndex);

        bool overrideClaimed = false;
        for (std::size_t sourceIndex = 0; sourceIndex < slotItem.bindings.size(); ++sourceIndex) {
            auto& binding = slotItem.bindings[sourceIndex];
            binding.setting = slotItem.setting;
            binding.source = brushInputSourceFromIndex(sourceIndex);
            if (binding.source == BrushInputSourceKey::RandomValue) {
                binding.mode = BrushDynamicsBlendMode::Add;
                if (binding.enabled || binding.hasStoredCurve()) {
                    setBrushDynamicsRandomAmount(binding, brushDynamicsRandomAmount(binding));
                }
            } else if (binding.source == BrushInputSourceKey::StrokeDirection
                && (slotItem.setting == BrushDynamicsSettingKey::ShapeAngle
                    || slotItem.setting == BrushDynamicsSettingKey::ColorHue)) {
                binding.mode = BrushDynamicsBlendMode::Override;
            } else {
                binding.mode = normalizeBrushDynamicsBlendMode(slotItem.setting, binding.mode);
            }
            binding.durationSec = clampBrushTimeDurationSeconds(binding.durationSec);
            if (binding.endAction == BrushTimeEndAction::Count) {
                binding.endAction = BrushTimeEndAction::Stop;
            }
            binding.curve.normalize(binding.setting, binding.mode);

            if (binding.isActive() && binding.mode == BrushDynamicsBlendMode::Override) {
                if (overrideClaimed) {
                    binding.enabled = false;
                } else {
                    overrideClaimed = true;
                }
            }
        }
    }
}

inline BrushEvaluatedState evaluateBrushDynamics(
    const BrushSettingsData& settings, const BrushInputContext& inputContext)
{
    const auto& dynamics = settings.dynamics;

    BrushEvaluatedState out;
    out.radiusMultiplier = clampNonNegative(evaluateDynamicsSlotValue(
        1.0f, dynamics.slotForSetting(BrushDynamicsSettingKey::RadiusMultiplier), inputContext));
    out.opacityMultiplier = clampNonNegative(evaluateDynamicsSlotValue(
        1.0f, dynamics.slotForSetting(BrushDynamicsSettingKey::OpacityMultiplier), inputContext));
    out.hardness = evaluateDynamicsSlotValue(settings.hardness,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeHardness), inputContext);
    out.spacing = evaluateDynamicsSlotValue(settings.spacing,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeSpacing), inputContext);
    out.flow = evaluateDynamicsSlotValue(
        settings.flow, dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeFlow), inputContext);
    out.roundness = evaluateDynamicsSlotValue(settings.roundness,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeRoundness), inputContext);
    out.angleDegrees = evaluateDynamicsSlotValue(
        settings.angle, dynamics.slotForSetting(BrushDynamicsSettingKey::ShapeAngle), inputContext);
    out.textureAmount = evaluateDynamicsSlotValue(settings.textureAmount,
        dynamics.slotForSetting(BrushDynamicsSettingKey::TextureAmount), inputContext);
    out.textureScale = evaluateDynamicsSlotValue(settings.textureScale,
        dynamics.slotForSetting(BrushDynamicsSettingKey::TextureScale), inputContext);
    out.textureContrast = evaluateDynamicsSlotValue(settings.textureContrast,
        dynamics.slotForSetting(BrushDynamicsSettingKey::TextureContrast), inputContext);
    out.textureDepth = evaluateDynamicsSlotValue(settings.textureDepth,
        dynamics.slotForSetting(BrushDynamicsSettingKey::TextureDepth), inputContext);
    out.textureBlend = evaluateDynamicsSlotValue(settings.textureBlend,
        dynamics.slotForSetting(BrushDynamicsSettingKey::TextureBlend), inputContext);
    out.textureEdgeBoost = evaluateDynamicsSlotValue(settings.textureEdgeBoost,
        dynamics.slotForSetting(BrushDynamicsSettingKey::TextureEdgeBoost), inputContext);
    out.colorHue = evaluateDynamicsSlotValue(settings.colorHue,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ColorHue), inputContext);
    out.colorLightness = evaluateDynamicsSlotValue(settings.colorLightness,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ColorLightness), inputContext);
    out.colorSaturation = evaluateDynamicsSlotValue(settings.colorSaturation,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ColorSaturation), inputContext);
    out.scatterPosition = evaluateDynamicsSlotValue(settings.scatterPosition,
        dynamics.slotForSetting(BrushDynamicsSettingKey::ScatterPosition), inputContext);
    out.postCorrection = evaluateDynamicsSlotValue(settings.postCorrection,
        dynamics.slotForSetting(BrushDynamicsSettingKey::StrokePostCorrection), inputContext);
    out.stabilization = evaluateDynamicsSlotValue(settings.stabilization,
        dynamics.slotForSetting(BrushDynamicsSettingKey::StrokeStabilization), inputContext);
    out.startTaper = evaluateDynamicsSlotValue(settings.startTaper,
        dynamics.slotForSetting(BrushDynamicsSettingKey::StrokeStartTaper), inputContext);
    out.endTaper = evaluateDynamicsSlotValue(settings.endTaper,
        dynamics.slotForSetting(BrushDynamicsSettingKey::StrokeEndTaper), inputContext);
    out.startCorrectionLength = evaluateDynamicsSlotValue(settings.startCorrectionLength,
        dynamics.slotForSetting(BrushDynamicsSettingKey::StrokeStartCorrectionLength),
        inputContext);
    out.endCorrectionLength = evaluateDynamicsSlotValue(settings.endCorrectionLength,
        dynamics.slotForSetting(BrushDynamicsSettingKey::StrokeEndCorrectionLength), inputContext);
    return out;
}

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHSETTINGS_H
