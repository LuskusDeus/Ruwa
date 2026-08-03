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
        && source == BrushInputSourceKey::StrokeDirection) {
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
