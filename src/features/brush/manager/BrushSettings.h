// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHSETTINGS_H
#define RUWA_CORE_BRUSHES_BRUSHSETTINGS_H

#include "features/brush/manager/BrushDynamicsTypes.h"
#include "features/brush/manager/BrushMappingCurve.h"

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

struct BrushDynamicsRandomRange {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

inline BrushDynamicsRandomRange brushDynamicsRandomRange(const BrushDynamicsBinding& binding)
{
    const float neutralValue = binding.mode == BrushDynamicsBlendMode::Add ? 0.0f : 1.0f;
    if (binding.curve.empty()) {
        return { neutralValue, neutralValue };
    }

    BrushDynamicsRandomRange range { std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest() };
    bool hasFiniteValue = false;
    for (const auto& point : binding.curve.points) {
        if (!std::isfinite(point.y)) {
            continue;
        }
        const float value = clampBrushDynamicsBindingValue(binding.setting, binding.mode, point.y);
        range.minimum = std::min(range.minimum, value);
        range.maximum = std::max(range.maximum, value);
        hasFiniteValue = true;
    }
    return hasFiniteValue ? range : BrushDynamicsRandomRange { neutralValue, neutralValue };
}

inline void setBrushDynamicsRandomRange(BrushDynamicsBinding& binding, float minimum, float maximum)
{
    binding.source = BrushInputSourceKey::RandomValue;
    const float neutralValue = binding.mode == BrushDynamicsBlendMode::Add ? 0.0f : 1.0f;
    if (!std::isfinite(minimum)) {
        minimum = neutralValue;
    }
    if (!std::isfinite(maximum)) {
        maximum = neutralValue;
    }
    float clampedMinimum = clampBrushDynamicsBindingValue(binding.setting, binding.mode, minimum);
    float clampedMaximum = clampBrushDynamicsBindingValue(binding.setting, binding.mode, maximum);
    if (clampedMinimum > clampedMaximum) {
        std::swap(clampedMinimum, clampedMaximum);
    }
    binding.curve.points = {
        { 0.0f, clampedMinimum, 0.65f },
        { 1.0f, clampedMaximum, 0.65f },
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
    case BrushInputSourceKey::PenTilt:
        if (available) {
            *available = inputContext.penTiltAvailable;
        }
        return inputContext.penTiltAvailable ? clamp01(inputContext.penTilt) : 0.0f;
    case BrushInputSourceKey::StrokeSpeed:
        if (available) {
            *available = inputContext.strokeSpeedAvailable;
        }
        return inputContext.strokeSpeedAvailable ? clamp01(inputContext.strokeSpeed) : 0.0f;
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
                binding.mode = defaultBrushDynamicsBlendMode(binding.setting, binding.source);
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
    enum TextureMode {
        TextureModeProcedural = 0,
        TextureModeImage = 1,
    };

    enum TextureType {
        TextureTypePencilGrain = 0,
        TextureTypeFractalNoise = 1,
        TextureTypePerlinNoise = 2,
        TextureTypeDots = 3,
        TextureTypeLines = 4,
        TextureTypeCheckerboard = 5,
    };

    int textureMode = TextureModeProcedural;
    int textureType = TextureTypePencilGrain;
    float texturePencilDetail = 0.5f;
    float texturePencilStreakStrength = 0.3f;
    float textureNoiseOctaves = 3.0f;
    float textureNoiseRoughness = 0.55f;
    float texturePerlinOctaves = 2.0f;
    float texturePerlinPersistence = 0.625f;
    float textureDotsSpacing = 28.0f;
    float textureDotsSize = 0.3f;
    float textureDotsJitter = 0.15f;
    float textureLinesSpacing = 24.0f;
    float textureLinesThickness = 0.22f;
    float textureLinesAngle = 45.0f;
    float textureCheckerSize = 24.0f;
    float textureCheckerSoftness = 0.06f;
    float textureCheckerRotation = 0.0f;
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
            binding.mode
                = normalizeBrushDynamicsBlendMode(slotItem.setting, binding.source, binding.mode);
            if (binding.source == BrushInputSourceKey::RandomValue) {
                if (binding.enabled || binding.hasStoredCurve()) {
                    const auto range = brushDynamicsRandomRange(binding);
                    setBrushDynamicsRandomRange(binding, range.minimum, range.maximum);
                }
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
