// SPDX-License-Identifier: MPL-2.0

#include "features/effects/plugin/EffectAbiAdapter.h"

#include "features/effects/plugin/EffectHostGpuContext.h" // gpuApiTable
#include "RuwaBuildConfig.h"

#include <QColor>
#include <QDebug>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Backing objects for the SDK's opaque coverage-input / mutable-state handles.
// Only the host ever constructs or dereferences these; plugins hold the opaque
// pointer and reach the data through the host API functions below.
struct RuwaEffectCoverageInput_s {
    const std::vector<RuwaTileKey>* tiles = nullptr;
};

struct RuwaEffectMutableState_s {
    QVariantMap* params = nullptr;
    const RuwaEffectDescriptor* effect = nullptr;
};

namespace ruwa::core::effects::plugin {

namespace {

using ruwa::core::effects::EffectParamDefinition;
using ruwa::core::effects::EffectParamType;
using ruwa::core::effects::LayerEffectDescriptor;
using ruwa::core::effects::LayerEffectState;

template <typename T> const T* abiArrayElement(const T* first, uint32_t index)
{
    if (!first || first->struct_size < sizeof(uint32_t)) {
        return nullptr;
    }
    if (index > std::numeric_limits<std::size_t>::max() / first->struct_size) {
        return nullptr;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(first);
    return reinterpret_cast<const T*>(bytes + static_cast<std::size_t>(index) * first->struct_size);
}

float clampUnitF(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

EffectParamType mapType(RuwaEffectParamType type)
{
    switch (type) {
    case RUWA_EFFECT_PARAM_BOOL:
        return EffectParamType::Bool;
    case RUWA_EFFECT_PARAM_INT:
        return EffectParamType::Int;
    case RUWA_EFFECT_PARAM_COLOR:
        return EffectParamType::Color;
    case RUWA_EFFECT_PARAM_CHOICE:
        return EffectParamType::Choice;
    case RUWA_EFFECT_PARAM_REAL:
    default:
        return EffectParamType::Real;
    }
}

ruwa::core::effects::EffectParamEditorHint mapEditor(RuwaEffectParamEditor editor)
{
    return editor == RUWA_EFFECT_EDITOR_NUMBER_FIELD
        ? ruwa::core::effects::EffectParamEditorHint::NumberField
        : ruwa::core::effects::EffectParamEditorHint::Slider;
}

ruwa::core::effects::EffectParamPositionAxis mapAxis(RuwaEffectPositionAxis axis)
{
    return axis == RUWA_EFFECT_AXIS_Y ? ruwa::core::effects::EffectParamPositionAxis::Y
                                      : ruwa::core::effects::EffectParamPositionAxis::X;
}

ruwa::core::effects::EffectParamDefaultBinding mapBinding(RuwaEffectDefaultBinding binding)
{
    using B = ruwa::core::effects::EffectParamDefaultBinding;
    switch (binding) {
    case RUWA_EFFECT_BIND_CANVAS_WIDTH:
        return B::CanvasWidth;
    case RUWA_EFFECT_BIND_CANVAS_HEIGHT:
        return B::CanvasHeight;
    case RUWA_EFFECT_BIND_CANVAS_HALF_WIDTH:
        return B::CanvasHalfWidth;
    case RUWA_EFFECT_BIND_CANVAS_HALF_HEIGHT:
        return B::CanvasHalfHeight;
    case RUWA_EFFECT_BIND_NONE:
    default:
        return B::None;
    }
}

QColor colorFromFloats(const float c[4])
{
    return QColor::fromRgbF(clampUnitF(c[0]), clampUnitF(c[1]), clampUnitF(c[2]), clampUnitF(c[3]));
}

// The exact on-disk form effects already use (see EffectCard's colour editor):
// #AARRGGBB when translucent, #RRGGBB when opaque, uppercase. Renderers parse it
// back with QColor(text). Keeping this identical is what preserves document
// round-trip.
QString colorToHex(const QColor& color)
{
    return color.alpha() < 255 ? color.name(QColor::HexArgb).toUpper()
                               : color.name(QColor::HexRgb).toUpper();
}

const RuwaEffectParamDef* findDef(const RuwaEffectDescriptor* effect, const char* key)
{
    if (!effect || !key) {
        return nullptr;
    }
    for (uint32_t i = 0; i < effect->param_count; ++i) {
        const RuwaEffectParamDef* def = abiArrayElement(effect->params, i);
        if (def && def->key && std::strcmp(def->key, key) == 0) {
            return def;
        }
    }
    return nullptr;
}

QStringList choiceList(const RuwaEffectParamDef& def)
{
    QStringList out;
    out.reserve(static_cast<int>(def.choice_count));
    for (uint32_t i = 0; i < def.choice_count; ++i) {
        out << (def.choices && def.choices[i] ? QString::fromUtf8(def.choices[i]) : QString());
    }
    return out;
}

// Resolve one param definition + stored QVariant into an ABI value. `has` is
// whether the state actually carried the key (else the default is used).
RuwaEffectParamValue toParamValue(const RuwaEffectParamDef& def, const QVariant& q, bool has)
{
    RuwaEffectParamValue v {};
    v.type = def.type;
    switch (def.type) {
    case RUWA_EFFECT_PARAM_BOOL:
        v.value.as_bool = (has ? q.toBool() : (def.default_value != 0.0)) ? RUWA_TRUE : RUWA_FALSE;
        break;
    case RUWA_EFFECT_PARAM_INT:
        v.value.as_int = has ? q.toInt() : static_cast<int32_t>(std::llround(def.default_value));
        break;
    case RUWA_EFFECT_PARAM_REAL:
        v.value.as_real = has ? q.toDouble() : def.default_value;
        break;
    case RUWA_EFFECT_PARAM_COLOR: {
        QColor c = has ? QColor(q.toString().trimmed()) : QColor();
        if (!c.isValid()) {
            c = colorFromFloats(def.default_color);
        }
        v.value.as_color[0] = static_cast<float>(c.redF());
        v.value.as_color[1] = static_cast<float>(c.greenF());
        v.value.as_color[2] = static_cast<float>(c.blueF());
        v.value.as_color[3] = static_cast<float>(c.alphaF());
        break;
    }
    case RUWA_EFFECT_PARAM_CHOICE: {
        int idx = -1;
        if (has) {
            const QString s = q.toString();
            for (uint32_t i = 0; i < def.choice_count; ++i) {
                if (def.choices && def.choices[i] && s == QString::fromUtf8(def.choices[i])) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
        }
        if (idx < 0) {
            idx = def.default_choice;
        }
        if (idx < 0) {
            idx = 0;
        }
        if (def.choice_count > 0 && idx >= static_cast<int>(def.choice_count)) {
            idx = static_cast<int>(def.choice_count) - 1;
        }
        v.value.as_choice = idx;
        break;
    }
    }
    return v;
}

// Inverse of toParamValue: write an ABI value back into the stored QVariant
// form, keeping Color as a hex string and Choice as its label string.
QVariant toVariant(const RuwaEffectParamValue& v, const RuwaEffectParamDef* def)
{
    switch (v.type) {
    case RUWA_EFFECT_PARAM_BOOL:
        return v.value.as_bool != RUWA_FALSE;
    case RUWA_EFFECT_PARAM_INT:
        return v.value.as_int;
    case RUWA_EFFECT_PARAM_REAL:
        return v.value.as_real;
    case RUWA_EFFECT_PARAM_COLOR:
        return colorToHex(colorFromFloats(v.value.as_color));
    case RUWA_EFFECT_PARAM_CHOICE:
        if (def && v.value.as_choice >= 0
            && static_cast<uint32_t>(v.value.as_choice) < def->choice_count && def->choices
            && def->choices[v.value.as_choice]) {
            return QString::fromUtf8(def->choices[v.value.as_choice]);
        }
        return v.value.as_choice;
    }
    return {};
}

EffectParamDefinition mapParamDef(const RuwaEffectParamDef* d)
{
    EffectParamDefinition p;
    p.key = QString::fromUtf8(d->key ? d->key : "");
    p.label = d->label ? QString::fromUtf8(d->label) : p.key;
    p.type = mapType(d->type);
    switch (d->type) {
    case RUWA_EFFECT_PARAM_BOOL:
        p.defaultValue = (d->default_value != 0.0);
        break;
    case RUWA_EFFECT_PARAM_INT:
        p.defaultValue = static_cast<int>(std::llround(d->default_value));
        p.minimumValue = static_cast<int>(std::llround(d->min_value));
        p.maximumValue = static_cast<int>(std::llround(d->max_value));
        p.stepValue = static_cast<int>(std::llround(d->step_value));
        break;
    case RUWA_EFFECT_PARAM_REAL:
        p.defaultValue = d->default_value;
        p.minimumValue = d->min_value;
        p.maximumValue = d->max_value;
        p.stepValue = d->step_value;
        break;
    case RUWA_EFFECT_PARAM_COLOR:
        p.defaultValue = colorToHex(colorFromFloats(d->default_color));
        break;
    case RUWA_EFFECT_PARAM_CHOICE: {
        const QStringList choices = choiceList(*d);
        p.choices = choices;
        int idx = d->default_choice;
        if (idx < 0 || idx >= choices.size()) {
            idx = 0;
        }
        p.defaultValue = choices.value(idx);
        break;
    }
    }
    p.preferredEditor = mapEditor(d->preferred_editor);
    if (d->position_pair_key && d->position_pair_key[0] != '\0') {
        p.positionPairKey = QString::fromUtf8(d->position_pair_key);
        p.positionAxis = mapAxis(d->position_axis);
    }
    p.defaultBinding = mapBinding(d->default_binding);
    return p;
}

// --- host API trampolines -------------------------------------------------

void RUWA_EFFECT_CALL hLog(RuwaEffectLogLevel level, const char* message)
{
    if (!message) {
        return;
    }
    switch (level) {
    case RUWA_EFFECT_LOG_DEBUG:
        qDebug("[effect-plugin] %s", message);
        break;
    case RUWA_EFFECT_LOG_INFO:
        qInfo("[effect-plugin] %s", message);
        break;
    case RUWA_EFFECT_LOG_WARNING:
        qWarning("[effect-plugin] %s", message);
        break;
    case RUWA_EFFECT_LOG_ERROR:
    default:
        qCritical("[effect-plugin] %s", message);
        break;
    }
}

uint32_t RUWA_EFFECT_CALL hCoverageCount(RuwaEffectCoverageInput input)
{
    return input && input->tiles ? static_cast<uint32_t>(input->tiles->size()) : 0;
}

RuwaTileKey RUWA_EFFECT_CALL hCoverageAt(RuwaEffectCoverageInput input, uint32_t index)
{
    if (input && input->tiles && index < input->tiles->size()) {
        return (*input->tiles)[index];
    }
    return RuwaTileKey { 0, 0 };
}

RuwaEffectParamValue RUWA_EFFECT_CALL hStateGet(RuwaEffectMutableState state, const char* key)
{
    RuwaEffectParamValue v {};
    if (!state || !state->params || !key) {
        return v;
    }
    const RuwaEffectParamDef* def = findDef(state->effect, key);
    if (!def) {
        return v;
    }
    const QString k = QString::fromUtf8(key);
    return toParamValue(*def, state->params->value(k), state->params->contains(k));
}

void RUWA_EFFECT_CALL hStateSet(
    RuwaEffectMutableState state, const char* key, RuwaEffectParamValue value)
{
    if (!state || !state->params || !key) {
        return;
    }
    const RuwaEffectParamDef* def = findDef(state->effect, key);
    state->params->insert(QString::fromUtf8(key), toVariant(value, def));
}

const char* RUWA_EFFECT_CALL hSharedVertexSource(void)
{
    // The host links the shared fullscreen vertex shader automatically; plugins
    // never need the source directly.
    return nullptr;
}

} // namespace

std::vector<RuwaEffectParamValue> makeParamValues(const RuwaEffectDescriptor* effect,
    const QVariantMap& params, std::vector<const char*>& keysOut)
{
    std::vector<RuwaEffectParamValue> values;
    keysOut.clear();
    if (!effect) {
        return values;
    }
    values.reserve(effect->param_count);
    keysOut.reserve(effect->param_count);
    for (uint32_t i = 0; i < effect->param_count; ++i) {
        const RuwaEffectParamDef* def = abiArrayElement(effect->params, i);
        if (!def) {
            break;
        }
        keysOut.push_back(def->key ? def->key : "");
        const QString key = QString::fromUtf8(def->key ? def->key : "");
        values.push_back(toParamValue(*def, params.value(key), params.contains(key)));
    }
    return values;
}

LayerEffectDescriptor buildLayerEffectDescriptor(const RuwaEffectDescriptor* effect)
{
    LayerEffectDescriptor d;
    if (!effect) {
        return d;
    }
    d.typeId = QString::fromUtf8(effect->type_id ? effect->type_id : "");
    d.displayName = effect->display_name ? QString::fromUtf8(effect->display_name) : d.typeId;
    d.category = effect->category ? QString::fromUtf8(effect->category) : QString();
    d.version = effect->version ? effect->version : 1;

    const RuwaEffectCapabilities& caps = *effect->capabilities;
    d.capabilities.supportsDocumentTile = caps.supports_document_tile != RUWA_FALSE;
    d.capabilities.supportsViewportScreen = caps.supports_viewport_screen != RUWA_FALSE;
    d.capabilities.expandsBounds = caps.expands_bounds != RUWA_FALSE;
    d.capabilities.requiresNeighborTiles = caps.requires_neighbor_tiles != RUWA_FALSE;
    d.capabilities.requiresBackdrop = caps.requires_backdrop != RUWA_FALSE;
    d.capabilities.orderDependent = caps.order_dependent != RUWA_FALSE;
    d.capabilities.readsWholeLayer = caps.reads_whole_layer != RUWA_FALSE;

    for (uint32_t i = 0; i < effect->param_count; ++i) {
        d.params.append(mapParamDef(abiArrayElement(effect->params, i)));
    }

    if (effect->pixel_expansion_radius) {
        const RuwaEffectDescriptor* eff = effect;
        d.pixelExpansionRadius = [eff](const LayerEffectState& st) -> int {
            std::vector<const char*> keys;
            std::vector<RuwaEffectParamValue> vals = makeParamValues(eff, st.params, keys);
            RuwaEffectStateView sv { sizeof(RuwaEffectStateView), st.version,
                static_cast<uint32_t>(keys.size()), keys.data(), vals.data() };
            try {
                return std::max(0, eff->pixel_expansion_radius(eff->user_data, &sv));
            } catch (...) {
                qWarning("Ruwa effect plugin: pixel_expansion_radius threw for '%s'",
                    eff->type_id ? eff->type_id : "<unnamed>");
                return 0;
            }
        };
    }

    if (effect->resolve_coverage) {
        const RuwaEffectDescriptor* eff = effect;
        d.coverageResolver = [eff](const LayerEffectState& st,
                                 const ruwa::core::effects::EffectTileCoverage& input)
            -> ruwa::core::effects::EffectTileCoverage {
            std::vector<RuwaTileKey> in;
            in.reserve(input.size());
            for (const aether::TileKey& k : input) {
                in.push_back(RuwaTileKey { k.x, k.y });
            }
            RuwaEffectCoverageInput_s wrap;
            wrap.tiles = &in;

            ruwa::core::effects::EffectTileCoverage out;
            // Not named `emit`: that identifier is a Qt macro.
            RuwaEffectCoverageEmit emitTile = [](void* ctx, RuwaTileKey key) {
                static_cast<ruwa::core::effects::EffectTileCoverage*>(ctx)->insert(
                    aether::TileKey { key.x, key.y });
            };

            std::vector<const char*> keys;
            std::vector<RuwaEffectParamValue> vals = makeParamValues(eff, st.params, keys);
            RuwaEffectStateView sv { sizeof(RuwaEffectStateView), st.version,
                static_cast<uint32_t>(keys.size()), keys.data(), vals.data() };

            try {
                const RuwaBool handled = eff->resolve_coverage(eff->user_data, &sv,
                    reinterpret_cast<RuwaEffectCoverageInput>(&wrap), emitTile, &out);
                return handled != RUWA_FALSE ? out : input;
            } catch (...) {
                qWarning("Ruwa effect plugin: resolve_coverage threw for '%s'",
                    eff->type_id ? eff->type_id : "<unnamed>");
                return input;
            }
        };
    }

    return d;
}

void applyMigrations(const RuwaEffectDescriptor* effect, LayerEffectState& state)
{
    if (!effect) {
        return;
    }
    const uint32_t target = effect->version ? effect->version : 1;
    if (state.version >= target) {
        return;
    }
    if (!effect->migrate_state) {
        state.version = target;
        return;
    }
    RuwaEffectMutableState_s mutableState;
    mutableState.params = &state.params;
    mutableState.effect = effect;
    while (state.version < target) {
        try {
            effect->migrate_state(effect->user_data, state.version,
                reinterpret_cast<RuwaEffectMutableState>(&mutableState));
        } catch (...) {
            qWarning("Ruwa effect plugin: migrate_state threw for '%s' at version %u",
                effect->type_id ? effect->type_id : "<unnamed>", state.version);
            return;
        }
        state.version += 1;
    }
}

const RuwaEffectHostApi* hostApiTable()
{
    // Function-local static: constructed on first use, so gpuApiTable()'s table
    // (in another TU) is guaranteed ready — no static-init-order hazard.
    static const RuwaEffectHostApi api = {
        sizeof(RuwaEffectHostApi),
        RUWA_EFFECT_ABI_MAJOR,
        RUWA_EFFECT_ABI_MINOR,
        "Ruwa",
        RUWA_APPLICATION_VERSION,
        hLog,
        gpuApiTable(),
        hCoverageCount,
        hCoverageAt,
        hStateGet,
        hStateSet,
        hSharedVertexSource,
    };
    return &api;
}

} // namespace ruwa::core::effects::plugin
