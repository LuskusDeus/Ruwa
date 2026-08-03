// SPDX-License-Identifier: MPL-2.0

#include "features/brush/manager/BrushDynamicsSlotUtils.h"

#include "features/brush/engine/BrushEngineRegistry.h"

namespace ruwa::core::brushes {

BrushDynamicsSlot defaultDynamicsSlotForSetting(const QString& settingKey)
{
    BrushDynamicsSlot slot;
    slot.setting = brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    if (!supportsBrushDynamicsSetting(slot.setting)) {
        return slot;
    }

    auto& binding = slot.binding(BrushInputSourceKey::TabletPressure);
    binding.setting = slot.setting;
    binding.source = BrushInputSourceKey::TabletPressure;
    binding.mode = defaultBrushDynamicsBlendMode(slot.setting, binding.source);
    if (slot.setting == BrushDynamicsSettingKey::ShapeAngle
        || slot.setting == BrushDynamicsSettingKey::ColorHue) {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 0.0f, 0.65f },
        };
    } else {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 1.0f, 0.65f },
        };
    }
    binding.curve.normalize(binding.setting, binding.mode);

    return slot;
}

BrushDynamicsBinding legacyPressureBindingForSetting(
    const BrushSettingsData& settings, BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::TabletPressure;
    binding.mode = BrushDynamicsBlendMode::Multiply;

    float minValue = 1.0f;
    float maxValue = 1.0f;
    bool enabled = false;
    bool hasLegacyState = false;

    switch (setting) {
    case BrushDynamicsSettingKey::RadiusMultiplier:
        minValue = settings.sizePressureMin;
        maxValue = settings.sizePressureMax;
        enabled = settings.sizePressureEnabled;
        hasLegacyState = enabled || minValue != 1.0f || maxValue != 1.0f;
        break;
    case BrushDynamicsSettingKey::OpacityMultiplier:
        minValue = settings.opacityPressureMin;
        maxValue = settings.opacityPressureMax;
        enabled = settings.opacityPressureEnabled;
        hasLegacyState = enabled || minValue != 1.0f || maxValue != 1.0f;
        break;
    case BrushDynamicsSettingKey::ShapeFlow:
        minValue = settings.flowPressureMin;
        maxValue = settings.flowPressureMax;
        enabled = minValue < 0.999f || maxValue < 0.999f;
        hasLegacyState = enabled;
        break;
    default:
        break;
    }

    if (!hasLegacyState) {
        return binding;
    }

    binding.enabled = enabled;
    binding.curve.points = {
        { 0.0f, minValue, 0.65f },
        { 1.0f, maxValue, 0.65f },
    };
    binding.curve.normalize(setting, binding.mode);
    return binding;
}

BrushDynamicsSlot dynamicsSlotForSetting(
    const BrushSettingsData& settings, const QString& settingKey)
{
    const auto dynamicsKey = brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    if (!supportsBrushDynamicsSetting(dynamicsKey)) {
        return {};
    }

    auto slot = settings.dynamics.slotForSetting(dynamicsKey);
    if (slot.hasStoredBindings()) {
        return slot;
    }

    const auto legacyBinding = legacyPressureBindingForSetting(settings, dynamicsKey);
    if (legacyBinding.hasStoredCurve()) {
        slot.binding(BrushInputSourceKey::TabletPressure) = legacyBinding;
        return slot;
    }

    auto fallbackSlot = defaultDynamicsSlotForSetting(settingKey);
    fallbackSlot.binding(BrushInputSourceKey::TabletPressure).enabled
        = slot.binding(BrushInputSourceKey::TabletPressure).enabled;
    return fallbackSlot;
}

std::optional<BrushSettingDef> findSettingDef(
    const BrushEngineDescriptor& descriptor, const QString& settingKey)
{
    for (const auto& tab : descriptor.settingsTabs) {
        for (const auto& def : tab.settings) {
            if (def.key != nullptr && settingKey == QLatin1String(def.key)) {
                return def;
            }
        }
    }
    return std::nullopt;
}

BrushDynamicTargetDef dynamicsTargetForSetting(const QString& engineId, const QString& settingKey)
{
    const auto dynamicsKey = brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    const auto* module = BrushEngineRegistry::instance().moduleOrPixelFallback(engineId);
    const auto fallbackTarget = [&]() -> BrushDynamicTargetDef {
        if (!supportsBrushDynamicsSetting(dynamicsKey)) {
            return {};
        }
        return pressureTimeRandomDynamicsTarget(dynamicsKey);
    };
    if (!module) {
        return fallbackTarget();
    }

    const auto def = findSettingDef(module->descriptor(), settingKey);
    if (def.has_value()) {
        return def->dynamicTarget;
    }
    return fallbackTarget();
}

void syncLegacyPressureState(BrushSettingsData& settings, const BrushDynamicsSlot& slot)
{
    const auto& binding = slot.binding(BrushInputSourceKey::TabletPressure);
    const bool mirrorable = binding.isActive() && binding.mode == BrushDynamicsBlendMode::Multiply;
    const auto endpointValue = [&binding](float inputValue) {
        if (binding.curve.empty()) {
            return (binding.mode == BrushDynamicsBlendMode::Add) ? 0.0f : 1.0f;
        }
        return binding.curve.evaluate(inputValue,
            (binding.mode == BrushDynamicsBlendMode::Add) ? 0.0f : 1.0f, binding.setting,
            binding.mode);
    };
    const float minValue = mirrorable ? endpointValue(0.0f) : 1.0f;
    const float maxValue = mirrorable ? endpointValue(1.0f) : 1.0f;

    switch (slot.setting) {
    case BrushDynamicsSettingKey::RadiusMultiplier:
        settings.sizePressureEnabled = mirrorable;
        settings.sizePressureMin = minValue;
        settings.sizePressureMax = maxValue;
        break;
    case BrushDynamicsSettingKey::OpacityMultiplier:
        settings.opacityPressureEnabled = mirrorable;
        settings.opacityPressureMin = minValue;
        settings.opacityPressureMax = maxValue;
        break;
    case BrushDynamicsSettingKey::ShapeFlow:
        settings.flowPressureMin = minValue;
        settings.flowPressureMax = maxValue;
        break;
    default:
        break;
    }
}

bool applyDynamicsSlotForSetting(
    BrushSettingsData& settings, const QString& settingKey, const BrushDynamicsSlot& slot)
{
    const auto dynamicsKey = brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    if (!supportsBrushDynamicsSetting(dynamicsKey)) {
        return false;
    }

    auto normalizedSlot = slot;
    normalizedSlot.setting = dynamicsKey;
    for (std::size_t sourceIndex = 0; sourceIndex < normalizedSlot.bindings.size(); ++sourceIndex) {
        auto& binding = normalizedSlot.bindings[sourceIndex];
        binding.setting = dynamicsKey;
        binding.source = brushInputSourceFromIndex(sourceIndex);
        binding.mode = normalizeBrushDynamicsBlendMode(dynamicsKey, binding.source, binding.mode);
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
    }

    bool overrideClaimed = false;
    for (auto& binding : normalizedSlot.bindings) {
        if (binding.isActive() && binding.mode == BrushDynamicsBlendMode::Override) {
            if (overrideClaimed) {
                binding.enabled = false;
            } else {
                overrideClaimed = true;
            }
        }
    }

    settings.dynamics.slotForSetting(dynamicsKey) = normalizedSlot;
    syncLegacyPressureState(settings, normalizedSlot);
    return true;
}

bool hasEffectiveDynamics(const BrushSettingsData& settings, BrushDynamicsSettingKey setting)
{
    if (!supportsBrushDynamicsSetting(setting)) {
        return false;
    }

    if (settings.dynamics.slotForSetting(setting).hasActiveBindings()) {
        return true;
    }

    switch (setting) {
    case BrushDynamicsSettingKey::RadiusMultiplier:
        return settings.sizePressureEnabled;
    case BrushDynamicsSettingKey::OpacityMultiplier:
        return settings.opacityPressureEnabled;
    case BrushDynamicsSettingKey::ShapeFlow:
        return settings.flowPressureMin < 0.999f || settings.flowPressureMax < 0.999f;
    default:
        return false;
    }
}

float baseValueForDynamicsSetting(
    const BrushSettingsData& settings, BrushDynamicsSettingKey setting)
{
    switch (setting) {
    case BrushDynamicsSettingKey::RadiusMultiplier:
        return 1.0f;
    case BrushDynamicsSettingKey::OpacityMultiplier:
        return 1.0f;
    case BrushDynamicsSettingKey::ShapeFlow:
        return settings.flow;
    case BrushDynamicsSettingKey::ShapeHardness:
        return settings.hardness;
    case BrushDynamicsSettingKey::ShapeSpacing:
        return settings.spacing;
    case BrushDynamicsSettingKey::ShapeRoundness:
        return settings.roundness;
    case BrushDynamicsSettingKey::ShapeAngle:
        return settings.angle;
    case BrushDynamicsSettingKey::TextureAmount:
        return settings.textureAmount;
    case BrushDynamicsSettingKey::TextureScale:
        return settings.textureScale;
    case BrushDynamicsSettingKey::TextureContrast:
        return settings.textureContrast;
    case BrushDynamicsSettingKey::TextureDepth:
        return settings.textureDepth;
    case BrushDynamicsSettingKey::TextureBlend:
        return settings.textureBlend;
    case BrushDynamicsSettingKey::TextureEdgeBoost:
        return settings.textureEdgeBoost;
    case BrushDynamicsSettingKey::ColorHue:
        return settings.colorHue;
    case BrushDynamicsSettingKey::ColorLightness:
        return settings.colorLightness;
    case BrushDynamicsSettingKey::ColorSaturation:
        return settings.colorSaturation;
    case BrushDynamicsSettingKey::ScatterPosition:
        return settings.scatterPosition;
    case BrushDynamicsSettingKey::StrokePostCorrection:
        return settings.postCorrection;
    case BrushDynamicsSettingKey::StrokeStabilization:
        return settings.stabilization;
    case BrushDynamicsSettingKey::StrokeStartTaper:
        return settings.startTaper;
    case BrushDynamicsSettingKey::StrokeEndTaper:
        return settings.endTaper;
    case BrushDynamicsSettingKey::StrokeStartCorrectionLength:
        return settings.startCorrectionLength;
    case BrushDynamicsSettingKey::StrokeEndCorrectionLength:
        return settings.endCorrectionLength;
    case BrushDynamicsSettingKey::None:
    case BrushDynamicsSettingKey::Count:
        break;
    }
    return 1.0f;
}

} // namespace ruwa::core::brushes
