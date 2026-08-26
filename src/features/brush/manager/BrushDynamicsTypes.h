// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHDYNAMICSTYPES_H
#define RUWA_CORE_BRUSHES_BRUSHDYNAMICSTYPES_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
/// Upper end of the Stroke Speed curve in screen pixels per second. Keeping
/// this in screen space makes a brush react to the same hand motion at every
/// canvas zoom; faster input is intentionally saturated at the curve endpoint.
constexpr float kBrushStrokeSpeedMaxScreenPxPerSecond = 4000.0f;
/// Trailing arc-length measurement window for Stroke Speed. A window spanning
/// several device packets measures the hand motion rather than packet-to-packet
/// quantization, while remaining short enough for deliberate acceleration.
constexpr float kBrushStrokeSpeedFilterTimeSeconds = 0.075f;

enum class BrushInputSourceKey : uint8_t {
    None = 0,
    TabletPressure,
    RandomValue,
    StrokeProgress,
    Time,
    StrokeDirection,
    PenTilt,
    StrokeSpeed,
    Count
};

constexpr std::size_t kBrushInputSourceKeyCount
    = static_cast<std::size_t>(BrushInputSourceKey::Count) - 1u;

enum class BrushDynamicsBlendMode : uint8_t { Multiply = 0, Add, Override, Count };

enum class BrushTimeEndAction : uint8_t { Stop = 0, Reverse, Restart, Count };

/// Device/path signals that travel with one stroke sample. Values use the same
/// normalized domain as curve x coordinates; availability is explicit because
/// an upright pen has no meaningful azimuth and a press has no speed yet.
struct BrushInputDynamics {
    float penTilt = 0.0f;
    bool penTiltAvailable = false;
    float strokeSpeed = 0.0f;
    bool strokeSpeedAvailable = false;
    /// Derivative of normalized strokeSpeed over world-space path length. Live
    /// stroke emission supplies it when neighbouring samples are available so
    /// dab interpolation can remain C1 instead of forming linear-size facets.
    float strokeSpeedSpatialDerivative = 0.0f;
    bool strokeSpeedSpatialDerivativeAvailable = false;
};

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
    /// Azimuth of the pen's projection onto the canvas, normalized from
    /// [0, 360) degrees to [0, 1). Undefined for an upright pen or a device
    /// without tilt support.
    float penTilt = 0.0f;
    bool penTiltAvailable = false;
    /// Smoothed velocity of the resolved stroke cursor (after stabilization),
    /// normalized against kBrushStrokeSpeedMaxScreenPxPerSecond.
    float strokeSpeed = 0.0f;
    bool strokeSpeedAvailable = false;
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

inline float normalizeBrushStrokeSpeed(float screenPixelsPerSecond)
{
    if (!std::isfinite(screenPixelsPerSecond) || screenPixelsPerSecond <= 0.0f) {
        return 0.0f;
    }
    return clamp01(screenPixelsPerSecond / kBrushStrokeSpeedMaxScreenPxPerSecond);
}

inline float interpolateNormalizedAngle(float from, float to, float amount)
{
    from = clamp01(from);
    to = clamp01(to);
    amount = clamp01(amount);
    float delta = to - from;
    if (delta > 0.5f) {
        delta -= 1.0f;
    } else if (delta < -0.5f) {
        delta += 1.0f;
    }
    float result = std::fmod(from + delta * amount, 1.0f);
    if (result < 0.0f) {
        result += 1.0f;
    }
    return result;
}

inline BrushInputDynamics interpolateBrushInputDynamics(
    const BrushInputDynamics& from, const BrushInputDynamics& to, float amount,
    float travelDistance = 0.0f)
{
    BrushInputDynamics result;
    amount = clamp01(amount);
    result.penTiltAvailable = from.penTiltAvailable || to.penTiltAvailable;
    if (from.penTiltAvailable && to.penTiltAvailable) {
        result.penTilt = interpolateNormalizedAngle(from.penTilt, to.penTilt, amount);
    } else {
        result.penTilt = to.penTiltAvailable ? clamp01(to.penTilt) : clamp01(from.penTilt);
    }
    result.strokeSpeedAvailable = from.strokeSpeedAvailable || to.strokeSpeedAvailable;
    if (from.strokeSpeedAvailable && to.strokeSpeedAvailable) {
        const bool cubicAvailable = from.strokeSpeedSpatialDerivativeAvailable
            && to.strokeSpeedSpatialDerivativeAvailable && std::isfinite(travelDistance)
            && std::isfinite(from.strokeSpeedSpatialDerivative)
            && std::isfinite(to.strokeSpeedSpatialDerivative) && travelDistance > 0.000001f;
        if (cubicAvailable) {
            const float fromValue = clamp01(from.strokeSpeed);
            const float toValue = clamp01(to.strokeSpeed);
            const float delta = toValue - fromValue;
            float fromTangent = from.strokeSpeedSpatialDerivative * travelDistance;
            float toTangent = to.strokeSpeedSpatialDerivative * travelDistance;

            // The live path supplies monotone PCHIP tangents. Keep endpoint
            // tangents intact so adjacent segments remain C1; only reject a
            // tangent that contradicts this segment or belongs to a flat span.
            if (std::abs(delta) <= 0.000001f) {
                fromTangent = 0.0f;
                toTangent = 0.0f;
            } else {
                if (fromTangent * delta <= 0.0f) {
                    fromTangent = 0.0f;
                }
                if (toTangent * delta <= 0.0f) {
                    toTangent = 0.0f;
                }
            }

            const float amount2 = amount * amount;
            const float amount3 = amount2 * amount;
            const float h00 = 2.0f * amount3 - 3.0f * amount2 + 1.0f;
            const float h10 = amount3 - 2.0f * amount2 + amount;
            const float h01 = -2.0f * amount3 + 3.0f * amount2;
            const float h11 = amount3 - amount2;
            result.strokeSpeed = clamp01(h00 * fromValue + h10 * fromTangent + h01 * toValue
                + h11 * toTangent);

            const float dh00 = 6.0f * amount2 - 6.0f * amount;
            const float dh10 = 3.0f * amount2 - 4.0f * amount + 1.0f;
            const float dh01 = -dh00;
            const float dh11 = 3.0f * amount2 - 2.0f * amount;
            result.strokeSpeedSpatialDerivative = (dh00 * fromValue + dh10 * fromTangent
                + dh01 * toValue + dh11 * toTangent)
                / travelDistance;
            result.strokeSpeedSpatialDerivativeAvailable = true;
        } else {
            result.strokeSpeed
                = clamp01(from.strokeSpeed + (to.strokeSpeed - from.strokeSpeed) * amount);
            result.strokeSpeedSpatialDerivative = from.strokeSpeedSpatialDerivative
                + (to.strokeSpeedSpatialDerivative - from.strokeSpeedSpatialDerivative) * amount;
            result.strokeSpeedSpatialDerivativeAvailable
                = from.strokeSpeedSpatialDerivativeAvailable
                && to.strokeSpeedSpatialDerivativeAvailable;
        }
    } else {
        result.strokeSpeed
            = to.strokeSpeedAvailable ? clamp01(to.strokeSpeed) : clamp01(from.strokeSpeed);
        result.strokeSpeedSpatialDerivative = to.strokeSpeedAvailable
            ? to.strokeSpeedSpatialDerivative
            : from.strokeSpeedSpatialDerivative;
        result.strokeSpeedSpatialDerivativeAvailable = to.strokeSpeedAvailable
            ? to.strokeSpeedSpatialDerivativeAvailable
            : from.strokeSpeedSpatialDerivativeAvailable;
    }
    return result;
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
    case BrushInputSourceKey::PenTilt:
        return "penTilt";
    case BrushInputSourceKey::StrokeSpeed:
        return "strokeSpeed";
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
    if (sourceName == std::string_view("penTilt")) {
        return BrushInputSourceKey::PenTilt;
    }
    if (sourceName == std::string_view("strokeSpeed")) {
        return BrushInputSourceKey::StrokeSpeed;
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

inline BrushDynamicsBlendMode defaultBrushDynamicsBlendMode(
    BrushDynamicsSettingKey setting, BrushInputSourceKey source)
{
    if (source == BrushInputSourceKey::RandomValue) {
        return BrushDynamicsBlendMode::Add;
    }
    if ((setting == BrushDynamicsSettingKey::ShapeAngle
            || setting == BrushDynamicsSettingKey::ColorHue)
        && (source == BrushInputSourceKey::StrokeDirection
            || source == BrushInputSourceKey::PenTilt)) {
        return BrushDynamicsBlendMode::Override;
    }
    return defaultBrushDynamicsBlendMode(setting);
}

inline bool supportsBrushDynamicsBlendMode(
    BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode)
{
    if (!supportsBrushDynamicsSetting(setting) || mode == BrushDynamicsBlendMode::Count) {
        return false;
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

inline BrushDynamicsBlendMode normalizeBrushDynamicsBlendMode(
    BrushDynamicsSettingKey setting, BrushInputSourceKey source, BrushDynamicsBlendMode mode)
{
    return supportsBrushDynamicsBlendMode(setting, mode)
        ? mode
        : defaultBrushDynamicsBlendMode(setting, source);
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

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHDYNAMICSTYPES_H
