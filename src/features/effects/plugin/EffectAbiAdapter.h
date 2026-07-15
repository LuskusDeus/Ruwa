// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTABIADAPTER_H
#define RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTABIADAPTER_H

// Translation between the plain-C SDK descriptor / parameter model and Ruwa's
// internal LayerEffectDescriptor / LayerEffectState. This is the ONLY place that
// knows both sides; it preserves the existing document format (Color -> string,
// Choice -> string, Position -> paired keys).

#include "features/effects/LayerEffectTypes.h"

#include <ruwa/effect/ruwa_effect_sdk.h>

#include <QString>
#include <QVariantMap>

#include <vector>

namespace ruwa::core::effects::plugin {

/// Build a host descriptor mirroring one plugin effect. The plugin descriptor
/// pointer must outlive the returned descriptor (its callbacks capture it).
ruwa::core::effects::LayerEffectDescriptor buildLayerEffectDescriptor(
    const RuwaEffectDescriptor* effect);

/// Resolve a state's params into ABI param values, in the effect's declared
/// parameter order. `keysOut` is filled with pointers into the plugin's own
/// (stable) key strings; both outputs stay valid as long as `effect` does.
std::vector<RuwaEffectParamValue> makeParamValues(const RuwaEffectDescriptor* effect,
    const QVariantMap& params, std::vector<const char*>& keysOut);

/// Apply the effect's sequential schema migrations to `state` in place, up to
/// the effect's current version. No-op without a migration callback (the version
/// is simply adopted) or when already current.
void applyMigrations(
    const RuwaEffectDescriptor* effect, ruwa::core::effects::LayerEffectState& state);

/// The single host API table handed to every plugin's query entry point.
const RuwaEffectHostApi* hostApiTable();

} // namespace ruwa::core::effects::plugin

#endif // RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTABIADAPTER_H
