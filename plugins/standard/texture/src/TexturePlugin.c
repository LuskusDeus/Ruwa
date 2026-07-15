// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   Ruwa.Standard.Texture — standard Texture effect package (SDK plugin, ABI v1)
 * ==========================================================================
 *
 *   An ordinary Effect SDK plugin, built against ONLY sdk/include. Discovered by
 *   the host's EffectPluginManager beside the executable and driven through the
 *   one ruwa_effect_plugin_query entry point — no privileged path.
 *
 *   Ported effects (the whole former in-executable Texture group):
 *     - "texture.noise" (Noise) — procedural value noise, document-stable.
 *     - "texture.grain" (Grain) — film-grain variant of the same pass.
 *   Both are positional (region frame) and share the one Noise/Grain pass (see
 *   effects/NoiseGrain.c), selected by a `mode` carried per descriptor through
 *   `user_data`. Faithful ports: same shader, same parameters, same clamps. This
 *   file only carries the package metadata and descriptor tables.
 * ========================================================================== */

#include "TextureCommon.h"
#include "TextureEffects.h"

#include <stdint.h>

/* ==========================================================================
 *   Descriptor tables
 * ========================================================================== */

/* Both are pure per-fragment procedural textures: no bounds expansion. */
static const RuwaEffectCapabilities k_texture_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE, RUWA_TRUE, RUWA_FALSE, RUWA_FALSE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE
};

static const RuwaEffectParamDef k_noise_params[] = {
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.08, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "scale", .label = "Scale",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 1.0, .min_value = 1.0, .max_value = 128.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "monochrome", .label = "Monochrome",
      .type = RUWA_EFFECT_PARAM_BOOL, .default_value = 0.0 },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "seed", .label = "Seed",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 0.0, .min_value = 0.0, .max_value = 10000.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
};

static const RuwaEffectParamDef k_grain_params[] = {
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.12, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "size", .label = "Size",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 2.0, .min_value = 1.0, .max_value = 64.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "contrast", .label = "Contrast",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.35, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "seed", .label = "Seed",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 0.0, .min_value = 0.0, .max_value = 10000.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
};

static const RuwaEffectDescriptor k_effects[] = {
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "texture.noise",
      .display_name = "Noise", .category = "Texture", .version = 1u,
      .capabilities = &k_texture_caps, .params = k_noise_params, .param_count = 4u,
      .user_data = (void*)(intptr_t)0, .pixel_expansion_radius = NULL, .resolve_coverage = NULL,
      .migrate_state = NULL, .create_pass = texture_create_pass, .render_pass = texture_render_pass,
      .destroy_pass = texture_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "texture.grain",
      .display_name = "Grain", .category = "Texture", .version = 1u,
      .capabilities = &k_texture_caps, .params = k_grain_params, .param_count = 4u,
      .user_data = (void*)(intptr_t)1, .pixel_expansion_radius = NULL, .resolve_coverage = NULL,
      .migrate_state = NULL, .create_pass = texture_create_pass, .render_pass = texture_render_pass,
      .destroy_pass = texture_destroy_pass },
};

static const RuwaEffectPluginApi k_plugin = {
    .struct_size    = sizeof(RuwaEffectPluginApi),
    .abi_major      = RUWA_EFFECT_ABI_MAJOR,
    .abi_minor      = RUWA_EFFECT_ABI_MINOR,
    .plugin_id      = "com.ruwa.standard.texture",
    .plugin_name    = "Ruwa Standard Texture",
    .plugin_version = "1.0",
    .vendor         = "Ruwa",
    .effect_count   = 2u,
    .effects        = k_effects,
    .shutdown       = NULL,
};

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host)
{
    if (requested_abi_major != RUWA_EFFECT_ABI_MAJOR) {
        return NULL; /* incompatible ABI major — the host rejects us cleanly */
    }
    texture_set_host_api(host); /* store for the plugin's lifetime; do not call it here yet */
    return &k_plugin;
}
