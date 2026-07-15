// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHSETTINGDEFS_H
#define RUWA_CORE_BRUSHES_BRUSHSETTINGDEFS_H

#include "features/brush/manager/BrushSettings.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <initializer_list>
#include <optional>

namespace ruwa::core::brushes {

enum class SettingType {
    Slider,
    Toggle,
    ComboBox,
    Segmented,
    DynamicInfo,
    Separator,
};

enum class BrushDynamicsEditorKind : uint8_t {
    Curve = 0,
    Amount,
};

enum class BrushSettingDependencyKind : uint8_t {
    ToggleEnabled = 0,
    ValueGreaterThan,
};

struct BrushDynamicsSourceDef {
    BrushInputSourceKey source = BrushInputSourceKey::None;
    QVector<BrushDynamicsBlendMode> allowedBlendModes;
    BrushDynamicsEditorKind editorKind = BrushDynamicsEditorKind::Curve;
    bool available = false;

    bool supportsMode(BrushDynamicsBlendMode mode) const
    {
        return allowedBlendModes.contains(mode);
    }
};

struct BrushDynamicTargetDef {
    BrushDynamicsSettingKey setting = BrushDynamicsSettingKey::None;
    QVector<BrushDynamicsSourceDef> sources;

    bool isValid() const { return supportsBrushDynamicsSetting(setting); }

    bool hasAvailableSources() const
    {
        for (const auto& sourceDef : sources) {
            if (sourceDef.available && supportsBrushInputSource(sourceDef.source)) {
                return true;
            }
        }
        return false;
    }

    std::optional<BrushDynamicsSourceDef> sourceDef(BrushInputSourceKey source) const
    {
        for (const auto& sourceDef : sources) {
            if (sourceDef.source == source) {
                return sourceDef;
            }
        }
        return std::nullopt;
    }
};

inline BrushDynamicTargetDef pressureOnlyDynamicsTarget(BrushDynamicsSettingKey setting,
    std::initializer_list<BrushDynamicsBlendMode> allowedBlendModes = {
        BrushDynamicsBlendMode::Multiply,
        BrushDynamicsBlendMode::Add,
        BrushDynamicsBlendMode::Override,
    })
{
    BrushDynamicTargetDef target;
    target.setting = setting;

    BrushDynamicsSourceDef pressure;
    pressure.source = BrushInputSourceKey::TabletPressure;
    pressure.available = true;
    pressure.allowedBlendModes.reserve(static_cast<qsizetype>(allowedBlendModes.size()));
    for (const auto mode : allowedBlendModes) {
        pressure.allowedBlendModes.append(mode);
    }

    target.sources.append(pressure);
    return target;
}

inline BrushDynamicTargetDef pressureAndTimeDynamicsTarget(BrushDynamicsSettingKey setting,
    std::initializer_list<BrushDynamicsBlendMode> allowedBlendModes = {
        BrushDynamicsBlendMode::Multiply,
        BrushDynamicsBlendMode::Add,
        BrushDynamicsBlendMode::Override,
    })
{
    BrushDynamicTargetDef target = pressureOnlyDynamicsTarget(setting, allowedBlendModes);

    BrushDynamicsSourceDef time;
    time.source = BrushInputSourceKey::Time;
    time.available = true;
    time.allowedBlendModes.reserve(static_cast<qsizetype>(allowedBlendModes.size()));
    for (const auto mode : allowedBlendModes) {
        time.allowedBlendModes.append(mode);
    }

    target.sources.append(time);
    return target;
}

inline BrushDynamicTargetDef pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey setting,
    std::initializer_list<BrushDynamicsBlendMode> allowedBlendModes = {
        BrushDynamicsBlendMode::Multiply,
        BrushDynamicsBlendMode::Add,
        BrushDynamicsBlendMode::Override,
    })
{
    BrushDynamicTargetDef target = pressureAndTimeDynamicsTarget(setting, allowedBlendModes);

    BrushDynamicsSourceDef random;
    random.source = BrushInputSourceKey::RandomValue;
    random.available = true;
    random.editorKind = BrushDynamicsEditorKind::Amount;
    random.allowedBlendModes.append(BrushDynamicsBlendMode::Add);

    target.sources.append(random);

    BrushDynamicsSourceDef direction;
    direction.source = BrushInputSourceKey::StrokeDirection;
    direction.available = true;
    if (setting == BrushDynamicsSettingKey::ShapeAngle) {
        direction.allowedBlendModes.append(BrushDynamicsBlendMode::Override);
    } else {
        direction.allowedBlendModes.reserve(static_cast<qsizetype>(allowedBlendModes.size()));
        for (const auto mode : allowedBlendModes) {
            direction.allowedBlendModes.append(mode);
        }
    }

    target.sources.append(direction);
    return target;
}

struct BrushSettingDependency {
    const char* key = nullptr;
    BrushSettingDependencyKind kind = BrushSettingDependencyKind::ToggleEnabled;
    QVariant value;

    bool isSatisfied(const QVariant& currentValue) const
    {
        switch (kind) {
        case BrushSettingDependencyKind::ToggleEnabled:
            return currentValue.toBool();
        case BrushSettingDependencyKind::ValueGreaterThan:
            return currentValue.toDouble() > value.toDouble();
        }
        return true;
    }
};

struct BrushSettingDef {
    const char* key = nullptr;
    const char* label = nullptr;
    SettingType type = SettingType::Slider;

    QVariant defaultValue;

    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    int displayScale = 100;
    int displayDecimals = 0;
    const char* suffix = "%";
    BrushDynamicTargetDef dynamicTarget;

    const char* description = nullptr;
    QStringList comboOptions;
    QStringList segmentedOptions;
    QVector<BrushSettingDependency> enabledWhen;

    bool supportsDynamics() const
    {
        return (type == SettingType::Slider || type == SettingType::DynamicInfo)
            && dynamicTarget.isValid() && dynamicTarget.hasAvailableSources();
    }
};

inline BrushSettingDef sliderDef(const char* key, const char* label, float defaultValue, float min,
    float max, float step = 0.01f, int displayScale = 100, int displayDecimals = 0,
    const char* suffix = "%", BrushDynamicTargetDef dynamicTarget = {})
{
    BrushSettingDef def;
    def.key = key;
    def.label = label;
    def.type = SettingType::Slider;
    def.defaultValue = defaultValue;
    def.min = min;
    def.max = max;
    def.step = step;
    def.displayScale = displayScale;
    def.displayDecimals = displayDecimals;
    def.suffix = suffix;
    def.dynamicTarget = dynamicTarget;
    return def;
}

inline BrushSettingDef toggleDef(
    const char* key, const char* label, bool defaultValue, const char* description = nullptr)
{
    BrushSettingDef def;
    def.key = key;
    def.label = label;
    def.type = SettingType::Toggle;
    def.defaultValue = defaultValue;
    def.description = description;
    return def;
}

inline BrushSettingDef comboDef(
    const char* key, const char* label, int defaultValue, const QStringList& options)
{
    BrushSettingDef def;
    def.key = key;
    def.label = label;
    def.type = SettingType::ComboBox;
    def.defaultValue = defaultValue;
    def.comboOptions = options;
    return def;
}

inline BrushSettingDef segmentedDef(
    const char* key, const char* label, int defaultValue, const QStringList& options)
{
    BrushSettingDef def;
    def.key = key;
    def.label = label;
    def.type = SettingType::Segmented;
    def.defaultValue = defaultValue;
    def.segmentedOptions = options;
    return def;
}

inline BrushSettingDef dynamicInfoDef(const char* key, const char* label, const char* description,
    BrushDynamicTargetDef dynamicTarget)
{
    BrushSettingDef def;
    def.key = key;
    def.label = label;
    def.type = SettingType::DynamicInfo;
    def.description = description;
    def.dynamicTarget = dynamicTarget;
    return def;
}

inline BrushSettingDef separatorDef()
{
    BrushSettingDef def;
    def.type = SettingType::Separator;
    return def;
}

inline BrushSettingDependency enabledWhenToggleOn(const char* key)
{
    BrushSettingDependency dependency;
    dependency.key = key;
    dependency.kind = BrushSettingDependencyKind::ToggleEnabled;
    return dependency;
}

inline BrushSettingDependency enabledWhenValueAbove(const char* key, double threshold)
{
    BrushSettingDependency dependency;
    dependency.key = key;
    dependency.kind = BrushSettingDependencyKind::ValueGreaterThan;
    dependency.value = threshold;
    return dependency;
}

inline BrushSettingDef withEnabledWhen(
    BrushSettingDef def, std::initializer_list<BrushSettingDependency> dependencies)
{
    def.enabledWhen.reserve(static_cast<qsizetype>(dependencies.size()));
    for (const BrushSettingDependency& dependency : dependencies) {
        def.enabledWhen.append(dependency);
    }
    return def;
}

struct BrushTabDef {
    const char* id = nullptr;
    const char* label = nullptr;
    const char* description = nullptr;
    QVector<BrushSettingDef> settings;
};

inline QVector<BrushSettingDef> starredSettingDefs(
    const QVector<BrushTabDef>& tabs, const QSet<QString>& starredKeys)
{
    QVector<BrushSettingDef> result;
    for (const auto& tab : tabs) {
        for (const auto& def : tab.settings) {
            if (def.key != nullptr && starredKeys.contains(QLatin1String(def.key))) {
                result.append(def);
            }
        }
    }
    return result;
}

inline QSet<QString> defaultStarredKeys(const QVector<BrushTabDef>& tabs)
{
    QSet<QString> keys;
    if (tabs.isEmpty()) {
        return keys;
    }

    for (const auto& def : tabs.first().settings) {
        if (def.key != nullptr) {
            keys.insert(QLatin1String(def.key));
        }
    }
    return keys;
}

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHSETTINGDEFS_H
