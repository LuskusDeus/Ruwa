// SPDX-License-Identifier: MPL-2.0

#include "features/brush/engine/PixelBrushModule.h"

#include "features/brush/engine/StrokeStabilizer.h"
#include "features/layers/model/BlendModeUtils.h"
#include "features/brush/rendering/DabShapeCache.h"
#include "shared/tiles/TileBrush.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileTypes.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace ruwa::core::brushes {

namespace {

constexpr uint32_t TILE_SIZE = aether::TILE_SIZE;

float clampUnit(double value, float fallback)
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(static_cast<float>(value), 0.0f, 1.0f);
}

float clampSpacing(double value, float fallback)
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(static_cast<float>(value), kBrushSpacingMin, kBrushSpacingMax);
}

void upgradeStrokeSettingsToV5(const QVariantMap& settings, BrushSettingsData& out)
{
    if (!settings.contains(QStringLiteral("stroke.postCorrection"))
        && settings.contains(QStringLiteral("stroke.smoothing"))) {
        out.postCorrection = clampUnit(
            settings.value(QStringLiteral("stroke.smoothing"), out.postCorrection).toDouble(),
            out.postCorrection);
    }

    if (!settings.contains(QStringLiteral("stroke.stabilization"))
        && settings.contains(QStringLiteral("stroke.stabilizer"))) {
        out.stabilization = clampUnit(
            settings.value(QStringLiteral("stroke.stabilizer"), out.stabilization).toDouble(),
            out.stabilization);
    }
}

QVariantMap serializeCurvePoint(const BrushMappingPoint& point)
{
    return {
        { QStringLiteral("x"), point.x },
        { QStringLiteral("y"), point.y },
        { QStringLiteral("smoothness"), point.smoothness },
    };
}

QVariantList serializeCurve(const BrushMappingCurve& curve)
{
    QVariantList points;
    points.reserve(static_cast<qsizetype>(curve.points.size()));
    for (const auto& point : curve.points) {
        points.append(serializeCurvePoint(point));
    }
    return points;
}

QVariantMap serializeDynamicsBinding(const BrushDynamicsBinding& binding)
{
    QVariantMap map;
    const char* blendMode = brushDynamicsBlendModeName(binding.mode);
    if (!blendMode) {
        return map;
    }

    map.insert(QStringLiteral("mode"), QString::fromLatin1(blendMode));
    map.insert(QStringLiteral("enabled"), binding.enabled);
    map.insert(QStringLiteral("points"), serializeCurve(binding.curve));
    if (binding.source == BrushInputSourceKey::Time) {
        const char* endAction = brushTimeEndActionName(binding.endAction);
        map.insert(
            QStringLiteral("durationSec"), clampBrushTimeDurationSeconds(binding.durationSec));
        map.insert(
            QStringLiteral("endAction"), QString::fromLatin1(endAction ? endAction : "stop"));
    }
    return map;
}

QVariantMap serializeDynamicsBindings(const BrushDynamicsModel& dynamics)
{
    QVariantMap settingsMap;
    for (const auto& slotItem : dynamics.settingSlots) {
        const char* settingKey = brushDynamicsSettingKeyName(slotItem.setting);
        if (!settingKey || !slotItem.hasStoredBindings()) {
            continue;
        }

        QVariantMap sourcesMap;
        for (const auto& binding : slotItem.bindings) {
            if (!binding.hasStoredState()) {
                continue;
            }

            const char* sourceKey = brushInputSourceKeyName(binding.source);
            if (!sourceKey) {
                continue;
            }

            const QVariantMap serialized = serializeDynamicsBinding(binding);
            if (!serialized.isEmpty()) {
                sourcesMap.insert(QString::fromLatin1(sourceKey), serialized);
            }
        }

        if (!sourcesMap.isEmpty()) {
            settingsMap.insert(QString::fromLatin1(settingKey), sourcesMap);
        }
    }
    return settingsMap;
}

BrushMappingCurve deserializeCurve(
    const QVariant& value, BrushDynamicsSettingKey setting, BrushDynamicsBlendMode mode)
{
    BrushMappingCurve curve;
    const QVariantList points = value.toList();
    curve.points.reserve(static_cast<std::size_t>(points.size()));
    for (const QVariant& pointValue : points) {
        const QVariantMap pointMap = pointValue.toMap();
        if (pointMap.isEmpty()) {
            continue;
        }

        BrushMappingPoint point;
        point.x = clamp01(pointMap.value(QStringLiteral("x"), point.x).toFloat());
        point.y = clampBrushDynamicsBindingValue(
            setting, mode, pointMap.value(QStringLiteral("y"), point.y).toFloat());
        point.smoothness
            = clamp01(pointMap.value(QStringLiteral("smoothness"), point.smoothness).toFloat());
        curve.points.push_back(point);
    }
    curve.normalize(setting, mode);
    return curve;
}

BrushDynamicsModel deserializeDynamicsBindings(const QVariant& value)
{
    BrushDynamicsModel dynamics;
    const QVariantMap settingsMap = value.toMap();
    for (auto settingIt = settingsMap.constBegin(); settingIt != settingsMap.constEnd();
        ++settingIt) {
        const BrushDynamicsSettingKey dynamicsKey
            = brushDynamicsSettingKeyFromSettingKey(settingIt.key().toStdString());
        if (!supportsBrushDynamicsSetting(dynamicsKey)) {
            continue;
        }

        const QVariantMap sourcesMap = settingIt.value().toMap();
        for (auto sourceIt = sourcesMap.constBegin(); sourceIt != sourcesMap.constEnd();
            ++sourceIt) {
            const BrushInputSourceKey source
                = brushInputSourceKeyFromName(sourceIt.key().toStdString());
            if (!supportsBrushInputSource(source)) {
                continue;
            }

            const QVariantMap bindingMap = sourceIt.value().toMap();
            if (bindingMap.isEmpty()) {
                continue;
            }

            auto& binding = dynamics.slotForSetting(dynamicsKey).binding(source);
            binding.mode = normalizeBrushDynamicsBlendMode(dynamicsKey,
                brushDynamicsBlendModeFromName(
                    bindingMap.value(QStringLiteral("mode")).toString().toStdString()));
            binding.enabled = bindingMap.value(QStringLiteral("enabled"), binding.enabled).toBool();
            if (source == BrushInputSourceKey::Time) {
                binding.durationSec = clampBrushTimeDurationSeconds(static_cast<float>(
                    bindingMap.value(QStringLiteral("durationSec"), binding.durationSec)
                        .toDouble()));
                binding.endAction = brushTimeEndActionFromName(
                    bindingMap.value(QStringLiteral("endAction")).toString().toStdString());
            }
            binding.curve = deserializeCurve(
                bindingMap.value(QStringLiteral("points")), dynamicsKey, binding.mode);
        }
    }
    normalizeBrushDynamics(dynamics);
    return dynamics;
}

BrushDynamicsModel deserializeLegacyPressureBindings(const QVariant& value)
{
    BrushDynamicsModel dynamics;
    const QVariantList bindings = value.toList();
    for (const QVariant& bindingValue : bindings) {
        const QVariantMap bindingMap = bindingValue.toMap();
        if (bindingMap.isEmpty()) {
            continue;
        }

        const QString settingKey = bindingMap.value(QStringLiteral("settingKey")).toString();
        const BrushDynamicsSettingKey dynamicsKey
            = brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
        if (!supportsBrushDynamicsSetting(dynamicsKey)) {
            continue;
        }

        auto& binding
            = dynamics.slotForSetting(dynamicsKey).binding(BrushInputSourceKey::TabletPressure);
        binding.mode = BrushDynamicsBlendMode::Multiply;
        binding.enabled = bindingMap.value(QStringLiteral("enabled"), binding.enabled).toBool();
        binding.curve = deserializeCurve(
            bindingMap.value(QStringLiteral("points")), dynamicsKey, binding.mode);
    }
    normalizeBrushDynamics(dynamics);
    return dynamics;
}

bool hasExplicitPressureBinding(const BrushDynamicsModel& dynamics, BrushDynamicsSettingKey setting)
{
    const auto& binding
        = dynamics.slotForSetting(setting).binding(BrushInputSourceKey::TabletPressure);
    return binding.enabled || binding.hasStoredCurve();
}

BrushDynamicsBinding makeLegacyRangeBinding(
    BrushDynamicsSettingKey setting, float minValue, float maxValue)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::TabletPressure;
    binding.mode = BrushDynamicsBlendMode::Multiply;
    binding.enabled = true;
    binding.curve.points = {
        { 0.0f, minValue, 0.65f },
        { 1.0f, maxValue, 0.65f },
    };
    binding.curve.normalize(setting, binding.mode);
    return binding;
}

void migrateLegacyPressureBinding(BrushDynamicsModel& dynamics, BrushDynamicsSettingKey setting,
    float minValue, float maxValue, bool shouldMigrate)
{
    if (!shouldMigrate || hasExplicitPressureBinding(dynamics, setting)) {
        return;
    }
    dynamics.slotForSetting(setting).binding(BrushInputSourceKey::TabletPressure)
        = makeLegacyRangeBinding(setting, minValue, maxValue);
}

float bindingEndpointValue(const BrushDynamicsBinding& binding, float inputValue)
{
    if (binding.curve.empty()) {
        return (binding.mode == BrushDynamicsBlendMode::Add) ? 0.0f : 1.0f;
    }
    return binding.curve.evaluate(inputValue,
        (binding.mode == BrushDynamicsBlendMode::Add) ? 0.0f : 1.0f, binding.setting, binding.mode);
}

void syncLegacyPressureMirror(BrushSettingsData& settings, BrushDynamicsSettingKey setting)
{
    const auto& binding
        = settings.dynamics.slotForSetting(setting).binding(BrushInputSourceKey::TabletPressure);
    const bool mirrorable = binding.isActive() && binding.mode == BrushDynamicsBlendMode::Multiply;

    const float minValue = mirrorable ? bindingEndpointValue(binding, 0.0f) : 1.0f;
    const float maxValue = mirrorable ? bindingEndpointValue(binding, 1.0f) : 1.0f;

    switch (setting) {
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

void syncLegacyPressureMirrors(BrushSettingsData& settings)
{
    syncLegacyPressureMirror(settings, BrushDynamicsSettingKey::RadiusMultiplier);
    syncLegacyPressureMirror(settings, BrushDynamicsSettingKey::OpacityMultiplier);
    syncLegacyPressureMirror(settings, BrushDynamicsSettingKey::ShapeFlow);
}

void normalizeMinMax(float& minValue, float& maxValue)
{
    normalizePressureRange(minValue, maxValue);
}

int normalizeFlowBlendMode(int rawValue, float flowValue)
{
    if (rawValue == BrushSettingsData::FlowBlendSrcOver
        || rawValue == BrushSettingsData::FlowBlendMax) {
        return rawValue;
    }

    return (flowValue >= 0.999f) ? BrushSettingsData::FlowBlendMax
                                 : BrushSettingsData::FlowBlendSrcOver;
}

int normalizeStrokeBlendMode(int rawValue)
{
    return ruwa::core::layers::isValidBlendModeValue(rawValue)
        ? rawValue
        : static_cast<int>(ruwa::core::layers::BlendMode::Normal);
}

QStringList layerBlendModeOptions()
{
    QStringList options;
    options.reserve(ruwa::core::layers::kBlendModeCount);
    for (int i = 0; i < ruwa::core::layers::kBlendModeCount; ++i) {
        const auto mode = static_cast<ruwa::core::layers::BlendMode>(i);
        options.append(ruwa::core::layers::blendModeDisplayName(mode, "ruwa::core::brushes"));
    }
    return options;
}

QVector<BrushTabDef> pixelBrushTabDefinitions()
{
    return {
        { "shape", QT_TR_NOOP("Shape"), QT_TR_NOOP("Core brush tip parameters"),
            {
                dynamicInfoDef("radius.multiplier", QT_TR_NOOP("Size"),
                    QT_TR_NOOP("This parameter is dynamic"),
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::RadiusMultiplier)),
                dynamicInfoDef("opacity.multiplier", QT_TR_NOOP("Opacity"),
                    QT_TR_NOOP("This parameter is dynamic"),
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::OpacityMultiplier)),
                separatorDef(),
                segmentedDef("shape.flowBlendMode", QT_TR_NOOP("Flow Blend"),
                    BrushSettingsData::FlowBlendMax, { QT_TR_NOOP("src_over"), QT_TR_NOOP("max") }),
                sliderDef("shape.hardness", QT_TR_NOOP("Hardness"), 0.7f, 0.0f, 1.0f, 0.01f, 100, 0,
                    "%", pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ShapeHardness)),
                sliderDef("shape.spacing", QT_TR_NOOP("Spacing"), 0.25f, kBrushSpacingMin,
                    kBrushSpacingMax, 0.001f, 100, 1, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ShapeSpacing)),
                sliderDef("shape.flow", QT_TR_NOOP("Flow"), 1.0f, 0.0f, 1.0f, 0.01f, 100, 0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ShapeFlow)),
                sliderDef("shape.roundness", QT_TR_NOOP("Roundness"), 1.0f, 0.0f, 1.0f, 0.01f, 100,
                    0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ShapeRoundness)),
                sliderDef("shape.angle", QT_TR_NOOP("Angle"), 0.0f, 0.0f, 360.0f, 1.0f, 1, 0,
                    "\u00B0",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ShapeAngle,
                        { BrushDynamicsBlendMode::Add, BrushDynamicsBlendMode::Override })),
                toggleDef("shape.brushFeather", QT_TR_NOOP("Brush Feather"), true,
                    QT_TR_NOOP("Slight edge softening so brush is not pixel-perfect")),
            } },
        { "color", QT_TR_NOOP("Color"), QT_TR_NOOP("Brush color adjustments"),
            {
                comboDef("stroke.blendMode", QT_TR_NOOP("Blend Mode"),
                    static_cast<int>(ruwa::core::layers::BlendMode::Normal),
                    layerBlendModeOptions()),
                sliderDef("color.hue", QT_TR_NOOP("HUE"), 0.0f, 0.0f, 360.0f, 1.0f, 1, 0, "\u00B0",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ColorHue,
                        { BrushDynamicsBlendMode::Add, BrushDynamicsBlendMode::Override })),
                sliderDef("color.lightness", QT_TR_NOOP("Lightness"), 1.0f, 0.0f, 2.0f, 0.01f, 100,
                    0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ColorLightness)),
                sliderDef("color.saturation", QT_TR_NOOP("Saturation"), 1.0f, 0.0f, 2.0f, 0.01f,
                    100, 0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ColorSaturation)),
            } },
        { "mixing", QT_TR_NOOP("Mixing"), QT_TR_NOOP("Wet color blending"),
            {
                sliderDef("mixing.blending", QT_TR_NOOP("Color Blending"), 0.5f, 0.0f, 1.0f, 0.01f,
                    100, 0, "%"),
                sliderDef(
                    "mixing.length", QT_TR_NOOP("Length"), 0.5f, 0.0f, 1.0f, 0.01f, 100, 0, "%"),
                sliderDef("mixing.dilution", QT_TR_NOOP("Dilution"), 0.0f, 0.0f, 1.0f, 0.01f, 100,
                    0, "%"),
                sliderDef("mixing.spread", QT_TR_NOOP("Color Spread"), 0.0f, 0.0f, 1.0f, 0.01f, 100,
                    0, "%"),
                sliderDef(
                    "mixing.buildup", QT_TR_NOOP("Buildup"), 0.0f, 0.0f, 1.0f, 0.01f, 100, 0, "%"),
                sliderDef(
                    "mixing.dryRate", QT_TR_NOOP("Drying"), 0.0f, 0.0f, 1.0f, 0.01f, 100, 0, "%"),
                sliderDef("mixing.wetFlow", QT_TR_NOOP("Wet Flow"), 0.75f, 0.0f, 1.0f, 0.01f, 100,
                    0, "%"),
            } },
        { "texture", QT_TR_NOOP("Texture"), QT_TR_NOOP("Texture overlay on brush stamps"),
            {
                comboDef("texture.type", QT_TR_NOOP("Type"), 0,
                    { QT_TR_NOOP("Procedural"), QT_TR_NOOP("Noise"), QT_TR_NOOP("Perlin") }),
                sliderDef("texture.amount", QT_TR_NOOP("Amount"), 0.0f, 0.0f, 1.0f, 0.01f, 100, 0,
                    "%", pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::TextureAmount)),
                sliderDef("texture.scale", QT_TR_NOOP("Scale"), 1.0f, 0.1f, 4.0f, 0.1f, 1, 1, "x",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::TextureScale)),
                sliderDef("texture.contrast", QT_TR_NOOP("Contrast"), 0.5f, 0.0f, 1.0f, 0.01f, 100,
                    0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::TextureContrast)),
                sliderDef("texture.depth", QT_TR_NOOP("Depth"), 1.0f, 0.0f, 1.0f, 0.01f, 100, 0,
                    "%", pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::TextureDepth)),
                sliderDef("texture.blend", QT_TR_NOOP("Blend"), 0.5f, 0.0f, 1.0f, 0.01f, 100, 0,
                    "%", pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::TextureBlend)),
                sliderDef("texture.edgeBoost", QT_TR_NOOP("Edge Boost"), 0.0f, 0.0f, 1.0f, 0.01f,
                    100, 0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::TextureEdgeBoost)),
            } },
        { "dab", QT_TR_NOOP("Dab"), QT_TR_NOOP("Dab shape and type"),
            {
                comboDef("dab.type", QT_TR_NOOP("Type"), 0,
                    { QT_TR_NOOP("0"), QT_TR_NOOP("1"), QT_TR_NOOP("2"), QT_TR_NOOP("3"),
                        QT_TR_NOOP("4"), QT_TR_NOOP("5") }),
            } },
        { "scatter", QT_TR_NOOP("Scatter"), QT_TR_NOOP("Dab placement variation"),
            {
                sliderDef("scatter.position", QT_TR_NOOP("Position Scatter"), 0.0f, 0.0f, 1.0f,
                    0.01f, 100, 0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::ScatterPosition)),
            } },
        { "stroke", QT_TR_NOOP("Stroke"), QT_TR_NOOP("Stroke taper"),
            {
                sliderDef("stroke.startTaper", QT_TR_NOOP("Start Taper"), 0.0f, 0.0f, 1.0f, 0.01f,
                    100, 0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::StrokeStartTaper)),
                sliderDef("stroke.endTaper", QT_TR_NOOP("End Taper"), 0.0f, 0.0f, 1.0f, 0.01f, 100,
                    0, "%",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::StrokeEndTaper)),
            } },
        { "stabilization", QT_TR_NOOP("Stabilization"),
            QT_TR_NOOP("Realtime stabilization and post-stroke correction"),
            {
                sliderDef("stroke.stabilization", QT_TR_NOOP("Stabilization"), 0.0f, 0.0f, 1.0f,
                    0.01f, 100, 0, "",
                    pressureTimeRandomDynamicsTarget(BrushDynamicsSettingKey::StrokeStabilization)),
                sliderDef("stroke.postCorrection", QT_TR_NOOP("Post Correction"), 0.0f, 0.0f, 1.0f,
                    0.01f, 100, 0, "",
                    pressureTimeRandomDynamicsTarget(
                        BrushDynamicsSettingKey::StrokePostCorrection)),
                withEnabledWhen(
                    toggleDef("stroke.adjustBySpeed", QT_TR_NOOP("Adjust by Speed"), false,
                        QT_TR_NOOP(
                            "Slow strokes receive stronger post correction than fast strokes")),
                    { enabledWhenValueAbove("stroke.postCorrection", 0.0001) }),
                toggleDef("stroke.startCorrectionEnabled", QT_TR_NOOP("Start"), false,
                    QT_TR_NOOP("Enable endpoint correction near the beginning of the stroke")),
                withEnabledWhen(
                    sliderDef("stroke.startCorrectionLength", QT_TR_NOOP("Start Length"), 0.0f,
                        0.0f, 500.0f, 1.0f, 1, 0, "px",
                        pressureTimeRandomDynamicsTarget(
                            BrushDynamicsSettingKey::StrokeStartCorrectionLength)),
                    { enabledWhenToggleOn("stroke.startCorrectionEnabled") }),
                toggleDef("stroke.endCorrectionEnabled", QT_TR_NOOP("End"), false,
                    QT_TR_NOOP("Enable endpoint correction near the end of the stroke")),
                withEnabledWhen(sliderDef("stroke.endCorrectionLength", QT_TR_NOOP("End Length"),
                                    0.0f, 0.0f, 500.0f, 1.0f, 1, 0, "px",
                                    pressureTimeRandomDynamicsTarget(
                                        BrushDynamicsSettingKey::StrokeEndCorrectionLength)),
                    { enabledWhenToggleOn("stroke.endCorrectionEnabled") }),
            } },
    };
}

void applyDabShapeMaskFromSettings(aether::TileBrush& brush, const BrushSettingsData& settings)
{
    if (!settings.dabCustomImagePath.isEmpty()) {
        auto grid
            = aether::DabShapeCache::instance().getCustomAlphaGrid(settings.dabCustomImagePath,
                settings.dabThreshold, settings.dabCompression, settings.dabInterpolation);
        if (!grid.data.empty()) {
            brush.setDabType(1);
            brush.setDabShapeMask(grid.data.data(),
                grid.softAlpha.empty() ? nullptr : grid.softAlpha.data(), grid.width, grid.height);
            return;
        }
    }

    if (settings.dabType > 0) {
        auto grid = aether::DabShapeCache::instance().getAlphaGrid(settings.dabType);
        brush.setDabShapeMask(grid.data.empty() ? nullptr : grid.data.data(),
            grid.softAlpha.empty() ? nullptr : grid.softAlpha.data(), grid.width, grid.height);
    } else {
        brush.setDabShapeMask(nullptr, 0, 0);
    }
}

void configureBrushFromSettings(aether::TileBrush& brush, const BrushSettingsData& settings,
    qreal opacityNorm, const QColor& color)
{
    brush.setColor(static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.blue()),
        static_cast<uint8_t>(std::clamp(static_cast<int>(opacityNorm * 255.0), 0, 255)));
    brush.setBrushSettings(settings);
    applyDabShapeMaskFromSettings(brush, settings);
}

QImage tileGridToQImage(const aether::TileGrid& grid, int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const float wx = static_cast<float>(px) + 0.5f;
            const float wy = static_cast<float>(py) + 0.5f;

            aether::TileKey key = aether::worldToTile(wx, wy);
            const aether::TileData* tile = grid.getTile(key);
            if (!tile) {
                continue;
            }

            float tileOx = 0.0f;
            float tileOy = 0.0f;
            aether::tileWorldOrigin(key, tileOx, tileOy);
            const int lx = static_cast<int>(wx - tileOx);
            const int ly = static_cast<int>(wy - tileOy);
            if (lx < 0 || lx >= static_cast<int>(TILE_SIZE) || ly < 0
                || ly >= static_cast<int>(TILE_SIZE)) {
                continue;
            }

            const uint8_t* pixels = tile->pixels();
            const uint32_t idx
                = (static_cast<uint32_t>(ly) * TILE_SIZE + static_cast<uint32_t>(lx)) * 4;
            const uint8_t a = pixels[idx + 3];
            if (a == 0) {
                continue;
            }

            image.setPixel(px, py, qRgba(pixels[idx + 0], pixels[idx + 1], pixels[idx + 2], a));
        }
    }

    return image;
}

BrushSettingsData renderableSettings(const QVariantMap& settingsMap)
{
    return PixelBrushModule::settingsFromVariantMap(settingsMap);
}

void configureBrushToolMode(aether::TileBrush& brush, BrushToolMode toolMode)
{
    brush.setEraseMode(toolMode == BrushToolMode::Erase);
    brush.setBlurMode(toolMode == BrushToolMode::Blur);
    brush.setSmudgeMode(toolMode == BrushToolMode::Smudge);
}

class PixelBrushStrokeReplayData final : public IEditableBrushStrokeReplayData {
public:
    explicit PixelBrushStrokeReplayData(aether::TileBrush* brush)
        : m_liveBrush(brush)
    {
    }

    PixelBrushStrokeReplayData(std::vector<aether::TileBrush::DabPoint> dabs, int dabType)
        : m_snapshotDabs(std::move(dabs))
        , m_snapshotDabType(dabType)
    {
    }

    bool empty() const override { return currentDabs().empty(); }

    size_t size() const override { return currentDabs().size(); }

    std::vector<BrushStrokeReplayPoint> points() const override
    {
        const auto& dabs = currentDabs();
        std::vector<BrushStrokeReplayPoint> replayPoints;
        replayPoints.reserve(dabs.size());
        for (const auto& dab : dabs) {
            replayPoints.push_back(toReplayPoint(dab));
        }
        return replayPoints;
    }

    std::shared_ptr<IBrushStrokeReplayData> clone() const override
    {
        return std::make_shared<PixelBrushStrokeReplayData>(snapshotDabs(), currentDabType());
    }

    bool replacePoints(const std::vector<BrushStrokeReplayPoint>& points) override
    {
        if (!m_liveBrush) {
            return false;
        }

        auto& liveDabs = m_liveBrush->strokeDabs();
        liveDabs.clear();
        liveDabs.reserve(points.size());
        for (const auto& point : points) {
            liveDabs.push_back(toDabPoint(point));
        }
        return true;
    }

    bool translate(float dx, float dy) override
    {
        if (!m_liveBrush) {
            return false;
        }

        for (auto& dab : m_liveBrush->strokeDabs()) {
            dab.worldX += dx;
            dab.worldY += dy;
            dab.baseWorldX += dx;
            dab.baseWorldY += dy;
        }
        return true;
    }

private:
    static BrushStrokeReplayPoint toReplayPoint(const aether::TileBrush::DabPoint& dab)
    {
        BrushStrokeReplayPoint point;
        point.worldX = dab.worldX;
        point.worldY = dab.worldY;
        point.baseWorldX = dab.baseWorldX;
        point.baseWorldY = dab.baseWorldY;
        point.pressure = dab.pressure;
        point.strokeElapsedSeconds = dab.strokeElapsedSeconds;
        point.textureAmount = dab.textureAmount;
        point.textureScale = dab.textureScale;
        point.textureContrast = dab.textureContrast;
        point.textureDepth = dab.textureDepth;
        point.textureBlend = dab.textureBlend;
        point.textureEdgeBoost = dab.textureEdgeBoost;
        point.startTaper = dab.startTaper;
        point.endTaper = dab.endTaper;
        point.postCorrection = dab.postCorrection;
        point.startCorrectionLength = dab.startCorrectionLength;
        point.endCorrectionLength = dab.endCorrectionLength;
        point.scatterPosition = dab.scatterPosition;
        point.radius = dab.radius;
        point.baseRadius = dab.baseRadius;
        point.hardness = dab.hardness;
        point.roundness = dab.roundness;
        point.angleDegrees = dab.angleDegrees;
        point.useMaxBlend = dab.useMaxBlend;
        point.strokeTimeAvailable = dab.strokeTimeAvailable;
        point.colorR = dab.colorR;
        point.colorG = dab.colorG;
        point.colorB = dab.colorB;
        point.alpha = dab.alpha;
        point.baseAlpha = dab.baseAlpha;
        return point;
    }

    static aether::TileBrush::DabPoint toDabPoint(const BrushStrokeReplayPoint& point)
    {
        aether::TileBrush::DabPoint dab;
        dab.worldX = point.worldX;
        dab.worldY = point.worldY;
        dab.baseWorldX = point.baseWorldX;
        dab.baseWorldY = point.baseWorldY;
        dab.pressure = point.pressure;
        dab.strokeElapsedSeconds = point.strokeElapsedSeconds;
        dab.textureAmount = point.textureAmount;
        dab.textureScale = point.textureScale;
        dab.textureContrast = point.textureContrast;
        dab.textureDepth = point.textureDepth;
        dab.textureBlend = point.textureBlend;
        dab.textureEdgeBoost = point.textureEdgeBoost;
        dab.startTaper = point.startTaper;
        dab.endTaper = point.endTaper;
        dab.postCorrection = point.postCorrection;
        dab.startCorrectionLength = point.startCorrectionLength;
        dab.endCorrectionLength = point.endCorrectionLength;
        dab.scatterPosition = point.scatterPosition;
        dab.radius = point.radius;
        dab.baseRadius = point.baseRadius;
        dab.hardness = point.hardness;
        dab.roundness = point.roundness;
        dab.angleDegrees = point.angleDegrees;
        dab.useMaxBlend = point.useMaxBlend;
        dab.strokeTimeAvailable = point.strokeTimeAvailable;
        dab.colorR = point.colorR;
        dab.colorG = point.colorG;
        dab.colorB = point.colorB;
        dab.alpha = point.alpha;
        dab.baseAlpha = point.baseAlpha;
        return dab;
    }

    const std::vector<aether::TileBrush::DabPoint>& currentDabs() const
    {
        return m_liveBrush ? m_liveBrush->strokeDabs() : m_snapshotDabs;
    }

    int currentDabType() const { return m_liveBrush ? m_liveBrush->dabType() : m_snapshotDabType; }

    std::vector<aether::TileBrush::DabPoint> snapshotDabs() const
    {
        const auto& dabs = currentDabs();
        return { dabs.begin(), dabs.end() };
    }

    aether::TileBrush* m_liveBrush = nullptr;
    std::vector<aether::TileBrush::DabPoint> m_snapshotDabs;
    int m_snapshotDabType = 0;
};

} // namespace

PixelBrushSession::PixelBrushSession(const BrushSessionConfig& config)
    : m_engineVersion(std::max(config.engineVersion, 1))
{
    const BrushSettingsData settings = PixelBrushModule::settingsFromVariantMap(config.settings);
    m_brush.setBrushSettings(settings);
    applyDabShapeMaskFromSettings(m_brush, settings);
    configureBrushToolMode(m_brush, config.toolMode);
    m_activeReplayData = std::make_shared<PixelBrushStrokeReplayData>(&m_brush);
}

BrushEngineId PixelBrushSession::engineId() const
{
    return m_engineId;
}

int PixelBrushSession::engineVersion() const
{
    return m_engineVersion;
}

std::shared_ptr<IEditableBrushStrokeReplayData> PixelBrushSession::activeStrokeReplayData()
{
    return m_activeReplayData;
}

BrushEngineDescriptor PixelBrushModule::descriptor() const
{
    return { QLatin1String(kPixelBrushEngineId), QStringLiteral("Pixel"),
        pixelBrushTabDefinitions(), { true, true, true } };
}

int PixelBrushModule::currentVersion() const
{
    return kPixelBrushEngineVersion;
}

QVariantMap PixelBrushModule::defaultSettings() const
{
    return settingsToVariantMap(BrushSettingsData {});
}

QVariantMap PixelBrushModule::normalizeSettings(const QVariantMap& settings) const
{
    return settingsToVariantMap(settingsFromVariantMap(settings));
}

QVariantMap PixelBrushModule::upgradeSettings(int fromVersion, const QVariantMap& settings) const
{
    BrushSettingsData upgraded = settingsFromVariantMap(settings);
    if (fromVersion < 5) {
        upgradeStrokeSettingsToV5(settings, upgraded);
    }
    if (fromVersion < 2) {
        migrateLegacyPressureBinding(upgraded.dynamics, BrushDynamicsSettingKey::ShapeFlow,
            upgraded.flowPressureMin, upgraded.flowPressureMax,
            upgraded.flowPressureMin < 0.999f || upgraded.flowPressureMax < 0.999f);
        migrateLegacyPressureBinding(upgraded.dynamics, BrushDynamicsSettingKey::RadiusMultiplier,
            upgraded.sizePressureMin, upgraded.sizePressureMax, upgraded.sizePressureEnabled);
        migrateLegacyPressureBinding(upgraded.dynamics, BrushDynamicsSettingKey::OpacityMultiplier,
            upgraded.opacityPressureMin, upgraded.opacityPressureMax,
            upgraded.opacityPressureEnabled);
    }
    return settingsToVariantMap(normalizeCompatibilitySettings(upgraded));
}

std::unique_ptr<IBrushEngineSession> PixelBrushModule::createSession(
    const BrushSessionConfig& config) const
{
    BrushSessionConfig normalizedConfig = config;
    normalizedConfig.engineVersion = currentVersion();
    normalizedConfig.settings = normalizeSettings(config.settings);
    return std::make_unique<PixelBrushSession>(normalizedConfig);
}

QImage PixelBrushModule::renderPreview(const BrushPreviewRequest& request) const
{
    const BrushSettingsData settings = renderableSettings(request.settings);
    if (request.width <= 0 || request.height <= 0) {
        return {};
    }

    const float maxRadius = static_cast<float>(std::min(request.width, request.height) * 0.35);
    const float baseRadius = 3.0f + static_cast<float>(request.sizeNorm) * (maxRadius - 3.0f);

    aether::TileGrid grid;
    aether::TileBrush brush;
    configureBrushFromSettings(brush, settings, request.opacityNorm, request.color);
    brush.setRadius(baseRadius);

    if (!request.strokePreview) {
        brush.setPressure(1.0f);
        brush.setStrokeElapsedSeconds(0.0f, true);
        brush.beginStroke();
        const float cx = request.width * 0.5f;
        const float cy = request.height * 0.5f;
        for (int i = 0; i < 8; ++i) {
            brush.stamp(grid, cx, cy, nullptr);
        }
        brush.endStroke(grid);
        return tileGridToQImage(grid, request.width, request.height);
    }

    brush.beginStroke();
    const float marginX = request.width * 0.06f;
    const int waveCount = 2;
    const float waveAmp = request.height * 0.16f;
    auto pathX = [&](float t) { return marginX + t * (request.width - 2.0f * marginX); };
    auto pathY = [&](float t) {
        return request.height * 0.5f + std::sin(t * 6.28318530718f * waveCount) * waveAmp;
    };
    auto pathPressure = [](float t) { return std::sin(t * 3.14159265359f); };
    constexpr float kPreviewStrokeDurationSec = 2.4f;
    auto pathElapsedSeconds = [kPreviewStrokeDurationSec](float t) {
        return std::max(0.0f, t) * kPreviewStrokeDurationSec;
    };

    StrokeStabilizerState stabilizationState;
    auto stabilizedPoint = [&](float targetX, float targetY, float pressure,
                               float strokeElapsedSeconds, bool reset) {
        BrushInputContext inputContext;
        inputContext.pressure = pressure;
        inputContext.strokeElapsedSeconds = strokeElapsedSeconds;
        inputContext.strokeTimeAvailable = true;
        const float tauMs
            = stabilizationTauMs(evaluateBrushDynamics(settings, inputContext).stabilization);
        const StrokeStabilizerPoint point = sampleStrokeStabilizer(stabilizationState, targetX,
            targetY, tauMs, static_cast<double>(strokeElapsedSeconds) * 1000.0, reset);
        return std::pair<float, float> { point.x, point.y };
    };

    const int segmentCount = request.strokeSegmentCountHint > 0
        ? std::max(8, request.strokeSegmentCountHint)
        : std::max(96, static_cast<int>((request.width + request.height) * 0.2f));
    float prevPressure = pathPressure(0.0f);
    float prevElapsedSeconds = pathElapsedSeconds(0.0f);
    auto [startX, startY]
        = stabilizedPoint(pathX(0.0f), pathY(0.0f), prevPressure, prevElapsedSeconds, true);
    brush.setPressure(prevPressure);
    brush.setStrokeElapsedSeconds(prevElapsedSeconds, true);
    brush.stamp(grid, startX, startY, nullptr);

    for (int i = 1; i <= segmentCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segmentCount);
        const float pressure = pathPressure(t);
        const float strokeElapsedSeconds = pathElapsedSeconds(t);
        auto [toX, toY]
            = stabilizedPoint(pathX(t), pathY(t), pressure, strokeElapsedSeconds, false);
        brush.strokeToInterpolatedSize(grid, startX, startY, toX, toY, prevPressure, pressure,
            nullptr, prevElapsedSeconds, strokeElapsedSeconds, true);
        startX = toX;
        startY = toY;
        prevPressure = pressure;
        prevElapsedSeconds = strokeElapsedSeconds;
    }

    bool rebuildNeeded = false;
    if (brush.hasTaperEffect()) {
        rebuildNeeded |= brush.applyStrokeTaperToDabs();
    }
    if (brush.hasStrokePostProcessingEffect()) {
        rebuildNeeded |= brush.applyPostCorrectionToDabs();
        rebuildNeeded |= brush.applyEndpointCorrectionToDabs();
        if (brush.hasTaperEffect()) {
            rebuildNeeded |= brush.applyStrokeTaperToDabs();
        }
    }
    if (rebuildNeeded) {
        brush.rebuildStrokeBufferFromDabs(nullptr);
    }

    brush.endStroke(grid);
    return tileGridToQImage(grid, request.width, request.height);
}

QImage PixelBrushModule::buildCursorStamp(const BrushCursorRequest& request) const
{
    BrushPreviewRequest previewRequest;
    previewRequest.engineId = request.engineId;
    previewRequest.engineVersion = request.engineVersion;
    previewRequest.settings = request.settings;
    previewRequest.sizeNorm = request.sizeNorm;
    previewRequest.opacityNorm = 1.0;
    previewRequest.color = request.color;
    previewRequest.width = request.size;
    previewRequest.height = request.size;
    previewRequest.strokePreview = false;
    return renderPreview(previewRequest);
}

QVariantMap PixelBrushModule::settingsToVariantMap(const BrushSettingsData& settings)
{
    QVariantMap map = {
        { QStringLiteral("shape.flowBlendMode"), settings.flowBlendMode },
        { QStringLiteral("shape.hardness"), settings.hardness },
        { QStringLiteral("shape.spacing"), settings.spacing },
        { QStringLiteral("shape.flow"), settings.flow },
        { QStringLiteral("shape.roundness"), settings.roundness },
        { QStringLiteral("shape.angle"), settings.angle },
        { QStringLiteral("shape.sizePressureEnabled"), settings.sizePressureEnabled },
        { QStringLiteral("shape.opacityPressureEnabled"), settings.opacityPressureEnabled },
        { QStringLiteral("shape.brushFeather"), settings.brushFeather },
        { QStringLiteral("dynamics.opacityPressureMin"), settings.opacityPressureMin },
        { QStringLiteral("dynamics.opacityPressureMax"), settings.opacityPressureMax },
        { QStringLiteral("dynamics.sizePressureMin"), settings.sizePressureMin },
        { QStringLiteral("dynamics.sizePressureMax"), settings.sizePressureMax },
        { QStringLiteral("dynamics.flowPressureMin"), settings.flowPressureMin },
        { QStringLiteral("dynamics.flowPressureMax"), settings.flowPressureMax },
        { QStringLiteral("texture.type"), settings.textureType },
        { QStringLiteral("texture.amount"), settings.textureAmount },
        { QStringLiteral("texture.scale"), settings.textureScale },
        { QStringLiteral("texture.contrast"), settings.textureContrast },
        { QStringLiteral("texture.depth"), settings.textureDepth },
        { QStringLiteral("texture.blend"), settings.textureBlend },
        { QStringLiteral("texture.edgeBoost"), settings.textureEdgeBoost },
        { QStringLiteral("color.hue"), settings.colorHue },
        { QStringLiteral("color.lightness"), settings.colorLightness },
        { QStringLiteral("color.saturation"), settings.colorSaturation },
        { QStringLiteral("dab.type"), settings.dabType },
        { QStringLiteral("dab.customImage"), settings.dabCustomImagePath },
        { QStringLiteral("dab.xScale"), settings.dabXScale },
        { QStringLiteral("dab.yScale"), settings.dabYScale },
        { QStringLiteral("dab.rotation"), settings.dabRotation },
        { QStringLiteral("dab.threshold"), settings.dabThreshold },
        { QStringLiteral("dab.compression"), settings.dabCompression },
        { QStringLiteral("dab.interpolation"), settings.dabInterpolation },
        { QStringLiteral("scatter.position"), settings.scatterPosition },
        { QStringLiteral("stroke.postCorrection"), settings.postCorrection },
        { QStringLiteral("stroke.stabilization"), settings.stabilization },
        { QStringLiteral("stroke.startTaper"), settings.startTaper },
        { QStringLiteral("stroke.endTaper"), settings.endTaper },
        { QStringLiteral("stroke.adjustBySpeed"), settings.adjustCorrectionBySpeed },
        { QStringLiteral("stroke.startCorrectionEnabled"), settings.startCorrectionEnabled },
        { QStringLiteral("stroke.startCorrectionLength"), settings.startCorrectionLength },
        { QStringLiteral("stroke.endCorrectionEnabled"), settings.endCorrectionEnabled },
        { QStringLiteral("stroke.endCorrectionLength"), settings.endCorrectionLength },
        { QStringLiteral("stroke.blendMode"), normalizeStrokeBlendMode(settings.strokeBlendMode) },
        { QStringLiteral("stroke.wetMix"), settings.wetMix },
        { QStringLiteral("mixing.blending"), settings.colorBlending },
        { QStringLiteral("mixing.length"), settings.colorLength },
        { QStringLiteral("mixing.dilution"), settings.colorDilution },
        { QStringLiteral("mixing.spread"), settings.colorSpread },
        { QStringLiteral("mixing.dryRate"), settings.colorDryRate },
        { QStringLiteral("mixing.wetFlow"), settings.colorWetFlow },
        { QStringLiteral("mixing.buildup"), settings.colorBuildup },
    };

    if (settings.dynamics.hasStoredBindings()) {
        map.insert(
            QStringLiteral("dynamics.bindings"), serializeDynamicsBindings(settings.dynamics));
    }

    return map;
}

BrushSettingsData PixelBrushModule::settingsFromVariantMap(const QVariantMap& settings)
{
    BrushSettingsData out;
    out.flowBlendMode
        = settings.value(QStringLiteral("shape.flowBlendMode"), out.flowBlendMode).toInt();
    out.hardness = clampUnit(
        settings.value(QStringLiteral("shape.hardness"), out.hardness).toDouble(), out.hardness);
    out.spacing = clampSpacing(
        settings.value(QStringLiteral("shape.spacing"), out.spacing).toDouble(), out.spacing);
    out.flow
        = clampUnit(settings.value(QStringLiteral("shape.flow"), out.flow).toDouble(), out.flow);
    out.roundness = clampUnit(
        settings.value(QStringLiteral("shape.roundness"), out.roundness).toDouble(), out.roundness);
    out.angle = normalizeAngleDegrees(
        static_cast<float>(settings.value(QStringLiteral("shape.angle"), out.angle).toDouble()));
    out.sizePressureEnabled
        = settings.value(QStringLiteral("shape.sizePressureEnabled"), out.sizePressureEnabled)
              .toBool();
    out.opacityPressureEnabled
        = settings.value(QStringLiteral("shape.opacityPressureEnabled"), out.opacityPressureEnabled)
              .toBool();
    out.brushFeather
        = settings.value(QStringLiteral("shape.brushFeather"), out.brushFeather).toBool();
    out.opacityPressureMin = clampUnit(
        settings.value(QStringLiteral("dynamics.opacityPressureMin"), out.opacityPressureMin)
            .toDouble(),
        out.opacityPressureMin);
    out.opacityPressureMax = clampUnit(
        settings.value(QStringLiteral("dynamics.opacityPressureMax"), out.opacityPressureMax)
            .toDouble(),
        out.opacityPressureMax);
    out.sizePressureMin = clampUnit(
        settings.value(QStringLiteral("dynamics.sizePressureMin"), out.sizePressureMin).toDouble(),
        out.sizePressureMin);
    out.sizePressureMax = clampUnit(
        settings.value(QStringLiteral("dynamics.sizePressureMax"), out.sizePressureMax).toDouble(),
        out.sizePressureMax);
    out.flowPressureMin = clampUnit(
        settings.value(QStringLiteral("dynamics.flowPressureMin"), out.flowPressureMin).toDouble(),
        out.flowPressureMin);
    out.flowPressureMax = clampUnit(
        settings.value(QStringLiteral("dynamics.flowPressureMax"), out.flowPressureMax).toDouble(),
        out.flowPressureMax);
    const bool hasGenericBindings = settings.contains(QStringLiteral("dynamics.bindings"));
    const bool hasLegacyPressureBindings
        = settings.contains(QStringLiteral("dynamics.pressureBindings"));
    if (hasGenericBindings) {
        out.dynamics
            = deserializeDynamicsBindings(settings.value(QStringLiteral("dynamics.bindings")));
    } else if (hasLegacyPressureBindings) {
        out.dynamics = deserializeLegacyPressureBindings(
            settings.value(QStringLiteral("dynamics.pressureBindings")));
    }
    out.textureType
        = std::clamp(settings.value(QStringLiteral("texture.type"), out.textureType).toInt(), 0, 2);
    out.textureAmount
        = clampUnit(settings.value(QStringLiteral("texture.amount"), out.textureAmount).toDouble(),
            out.textureAmount);
    out.textureScale = clampRange(
        static_cast<float>(
            settings.value(QStringLiteral("texture.scale"), out.textureScale).toDouble()),
        0.1f, 4.0f);
    out.textureContrast = clampUnit(
        settings.value(QStringLiteral("texture.contrast"), out.textureContrast).toDouble(),
        out.textureContrast);
    out.textureDepth
        = clampUnit(settings.value(QStringLiteral("texture.depth"), out.textureDepth).toDouble(),
            out.textureDepth);
    out.textureBlend
        = clampUnit(settings.value(QStringLiteral("texture.blend"), out.textureBlend).toDouble(),
            out.textureBlend);
    out.textureEdgeBoost = clampUnit(
        settings.value(QStringLiteral("texture.edgeBoost"), out.textureEdgeBoost).toDouble(),
        out.textureEdgeBoost);
    out.colorHue = normalizeAngleDegrees(
        static_cast<float>(settings.value(QStringLiteral("color.hue"), out.colorHue).toDouble()));
    out.colorLightness = clampRange(
        static_cast<float>(
            settings.value(QStringLiteral("color.lightness"), out.colorLightness).toDouble()),
        0.0f, 2.0f);
    out.colorSaturation = clampRange(
        static_cast<float>(
            settings.value(QStringLiteral("color.saturation"), out.colorSaturation).toDouble()),
        0.0f, 2.0f);
    out.dabType = std::clamp(settings.value(QStringLiteral("dab.type"), out.dabType).toInt(), 0, 5);
    out.dabCustomImagePath
        = settings.value(QStringLiteral("dab.customImage"), out.dabCustomImagePath).toString();
    out.dabXScale = clampUnit(
        settings.value(QStringLiteral("dab.xScale"), out.dabXScale).toDouble(), out.dabXScale);
    out.dabYScale = clampUnit(
        settings.value(QStringLiteral("dab.yScale"), out.dabYScale).toDouble(), out.dabYScale);
    out.dabRotation = normalizeAngleDegrees(static_cast<float>(
        settings.value(QStringLiteral("dab.rotation"), out.dabRotation).toDouble()));
    out.dabThreshold
        = clampUnit(settings.value(QStringLiteral("dab.threshold"), out.dabThreshold).toDouble(),
            out.dabThreshold);
    out.dabCompression = clampUnit(
        settings.value(QStringLiteral("dab.compression"), out.dabCompression).toDouble(),
        out.dabCompression);
    out.dabInterpolation = std::clamp(
        settings.value(QStringLiteral("dab.interpolation"), out.dabInterpolation).toInt(), 0, 1);
    out.scatterPosition = clampUnit(
        settings.value(QStringLiteral("scatter.position"), out.scatterPosition).toDouble(),
        out.scatterPosition);
    out.postCorrection
        = clampUnit(settings
                        .value(QStringLiteral("stroke.postCorrection"),
                            settings.value(QStringLiteral("stroke.smoothing"), out.postCorrection))
                        .toDouble(),
            out.postCorrection);
    out.stabilization
        = clampUnit(settings
                        .value(QStringLiteral("stroke.stabilization"),
                            settings.value(QStringLiteral("stroke.stabilizer"), out.stabilization))
                        .toDouble(),
            out.stabilization);
    out.startTaper
        = clampUnit(settings.value(QStringLiteral("stroke.startTaper"), out.startTaper).toDouble(),
            out.startTaper);
    out.endTaper = clampUnit(
        settings.value(QStringLiteral("stroke.endTaper"), out.endTaper).toDouble(), out.endTaper);
    out.adjustCorrectionBySpeed
        = settings.value(QStringLiteral("stroke.adjustBySpeed"), out.adjustCorrectionBySpeed)
              .toBool();
    out.startCorrectionEnabled
        = settings
              .value(QStringLiteral("stroke.startCorrectionEnabled"), out.startCorrectionEnabled)
              .toBool();
    out.startCorrectionLength = clampRange(
        static_cast<float>(settings
                .value(QStringLiteral("stroke.startCorrectionLength"), out.startCorrectionLength)
                .toDouble()),
        0.0f, 500.0f);
    out.endCorrectionEnabled
        = settings.value(QStringLiteral("stroke.endCorrectionEnabled"), out.endCorrectionEnabled)
              .toBool();
    out.endCorrectionLength = clampRange(
        static_cast<float>(
            settings.value(QStringLiteral("stroke.endCorrectionLength"), out.endCorrectionLength)
                .toDouble()),
        0.0f, 500.0f);
    out.strokeBlendMode = normalizeStrokeBlendMode(
        settings.value(QStringLiteral("stroke.blendMode"), out.strokeBlendMode).toInt());
    out.wetMix = clampUnit(
        settings.value(QStringLiteral("stroke.wetMix"), out.wetMix).toDouble(), out.wetMix);
    out.colorBlending
        = clampUnit(settings.value(QStringLiteral("mixing.blending"), out.colorBlending).toDouble(),
            out.colorBlending);
    out.colorLength
        = clampUnit(settings.value(QStringLiteral("mixing.length"), out.colorLength).toDouble(),
            out.colorLength);
    out.colorDilution
        = clampUnit(settings.value(QStringLiteral("mixing.dilution"), out.colorDilution).toDouble(),
            out.colorDilution);
    out.colorSpread
        = clampUnit(settings.value(QStringLiteral("mixing.spread"), out.colorSpread).toDouble(),
            out.colorSpread);
    out.colorDryRate
        = clampUnit(settings.value(QStringLiteral("mixing.dryRate"), out.colorDryRate).toDouble(),
            out.colorDryRate);
    out.colorWetFlow
        = clampUnit(settings.value(QStringLiteral("mixing.wetFlow"), out.colorWetFlow).toDouble(),
            out.colorWetFlow);
    out.colorBuildup
        = clampUnit(settings.value(QStringLiteral("mixing.buildup"), out.colorBuildup).toDouble(),
            out.colorBuildup);
    return normalizeCompatibilitySettings(out);
}

BrushSettingsData PixelBrushModule::normalizeCompatibilitySettings(
    const BrushSettingsData& settings)
{
    BrushSettingsData out = settings;
    out.flowBlendMode = normalizeFlowBlendMode(out.flowBlendMode, out.flow);
    normalizeMinMax(out.opacityPressureMin, out.opacityPressureMax);
    normalizeMinMax(out.sizePressureMin, out.sizePressureMax);
    normalizeMinMax(out.flowPressureMin, out.flowPressureMax);
    if (!out.dynamics.hasStoredBindings()) {
        migrateLegacyPressureBinding(out.dynamics, BrushDynamicsSettingKey::ShapeFlow,
            out.flowPressureMin, out.flowPressureMax,
            out.flowPressureMin < 0.999f || out.flowPressureMax < 0.999f);
        migrateLegacyPressureBinding(out.dynamics, BrushDynamicsSettingKey::RadiusMultiplier,
            out.sizePressureMin, out.sizePressureMax, out.sizePressureEnabled);
        migrateLegacyPressureBinding(out.dynamics, BrushDynamicsSettingKey::OpacityMultiplier,
            out.opacityPressureMin, out.opacityPressureMax, out.opacityPressureEnabled);
    }
    normalizeBrushDynamics(out.dynamics);
    syncLegacyPressureMirrors(out);
    out.hardness = std::clamp(out.hardness, 0.0f, 1.0f);
    out.spacing = clampSpacingValue(out.spacing);
    out.flow = std::clamp(out.flow, 0.0f, 1.0f);
    out.roundness = std::clamp(out.roundness, 0.0f, 1.0f);
    out.angle = normalizeAngleDegrees(out.angle);
    out.textureAmount = std::clamp(out.textureAmount, 0.0f, 1.0f);
    out.textureScale = std::clamp(out.textureScale, 0.1f, 4.0f);
    out.textureContrast = std::clamp(out.textureContrast, 0.0f, 1.0f);
    out.textureDepth = std::clamp(out.textureDepth, 0.0f, 1.0f);
    out.textureBlend = std::clamp(out.textureBlend, 0.0f, 1.0f);
    out.textureEdgeBoost = std::clamp(out.textureEdgeBoost, 0.0f, 1.0f);
    out.colorHue = normalizeAngleDegrees(out.colorHue);
    out.colorLightness = std::clamp(out.colorLightness, 0.0f, 2.0f);
    out.colorSaturation = std::clamp(out.colorSaturation, 0.0f, 2.0f);
    out.textureType = std::clamp(out.textureType, 0, 2);
    out.dabType = std::clamp(out.dabType, 0, 5);
    out.dabXScale = std::clamp(out.dabXScale, 0.0f, 1.0f);
    out.dabYScale = std::clamp(out.dabYScale, 0.0f, 1.0f);
    out.dabRotation = normalizeAngleDegrees(out.dabRotation);
    out.dabThreshold = std::clamp(out.dabThreshold, 0.0f, 1.0f);
    out.dabCompression = std::clamp(out.dabCompression, 0.0f, 1.0f);
    out.dabInterpolation = std::clamp(out.dabInterpolation, 0, 1);
    out.scatterPosition = std::clamp(out.scatterPosition, 0.0f, 1.0f);
    out.postCorrection = std::clamp(out.postCorrection, 0.0f, 1.0f);
    out.stabilization = std::clamp(out.stabilization, 0.0f, 1.0f);
    out.startTaper = std::clamp(out.startTaper, 0.0f, 1.0f);
    out.endTaper = std::clamp(out.endTaper, 0.0f, 1.0f);
    out.startCorrectionLength = std::clamp(out.startCorrectionLength, 0.0f, 500.0f);
    out.endCorrectionLength = std::clamp(out.endCorrectionLength, 0.0f, 500.0f);
    out.strokeBlendMode = normalizeStrokeBlendMode(out.strokeBlendMode);
    out.wetMix = std::clamp(out.wetMix, 0.0f, 1.0f);
    out.colorBlending = std::clamp(out.colorBlending, 0.0f, 1.0f);
    out.colorDilution = std::clamp(out.colorDilution, 0.0f, 1.0f);
    out.colorSpread = std::clamp(out.colorSpread, 0.0f, 1.0f);
    out.colorLength = std::clamp(out.colorLength, 0.0f, 1.0f);
    out.colorDryRate = std::clamp(out.colorDryRate, 0.0f, 1.0f);
    out.colorWetFlow = std::clamp(out.colorWetFlow, 0.0f, 1.0f);
    out.colorBuildup = std::clamp(out.colorBuildup, 0.0f, 1.0f);
    return out;
}

} // namespace ruwa::core::brushes
