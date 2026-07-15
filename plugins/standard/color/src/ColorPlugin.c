// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   Ruwa.Standard.Color — standard Color effect package (SDK plugin, ABI v1)
 * ==========================================================================
 *
 *   An ordinary Effect SDK plugin. Built against ONLY sdk/include: no Qt, no
 *   src/, no Ruwa-internal headers. The host's EffectPluginManager discovers it
 *   in the "effects" folder beside the executable and drives it through the one
 *   exported ruwa_effect_plugin_query entry point — with no privileged path over
 *   any third-party plugin.
 *
 *   Standard color adjustments share one cached GPU pass. Existing effects:
 *     - "brightness.contrast" (Brightness / Contrast)
 *     - "hue.saturation"      (Hue / Saturation)
 *   New adjustments use the same pass:
 *     - "color.gamma"          (Gamma)
 *     - "color.exposure"       (Exposure)
 *     - "color.black-white"    (Black & White)
 *   The original two remain faithful ports. Every descriptor exposes only its
 *   own controls while all other adjustments keep their identity values. This
 *   file carries metadata; effects/ColorAdjust.c contains the implementation.
 * ========================================================================== */

#include "ColorCommon.h"
#include "ColorEffects.h"

/* ==========================================================================
 *   Descriptor tables
 * ========================================================================== */

static const RuwaEffectCapabilities k_color_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE,   /* supports_document_tile   */
    RUWA_TRUE,   /* supports_viewport_screen */
    RUWA_FALSE,  /* expands_bounds           */
    RUWA_FALSE,  /* requires_neighbor_tiles  */
    RUWA_FALSE,  /* requires_backdrop        */
    RUWA_FALSE,  /* order_dependent          */
    RUWA_FALSE   /* reads_whole_layer        */
};

/* Brightness / Contrast: two unit parameters in [-1, 1], SLIDER editor —
 * matching the former unitParam() descriptors exactly. */
static const RuwaEffectParamDef k_brightness_contrast_params[] = {
    {
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "brightness",
        .label            = "Brightness",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 0.0,
        .min_value        = -1.0,
        .max_value        = 1.0,
        .step_value       = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "contrast",
        .label            = "Contrast",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 0.0,
        .min_value        = -1.0,
        .max_value        = 1.0,
        .step_value       = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

/* Hue / Saturation: hue in degrees [-180, 180] (step 1), saturation a unit
 * parameter in [-1, 1] (step 0.01) — matching the former hueParam() /
 * unitParam() descriptors exactly. */
static const RuwaEffectParamDef k_hue_saturation_params[] = {
    {
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "hue",
        .label            = "Hue",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 0.0,
        .min_value        = -180.0,
        .max_value        = 180.0,
        .step_value       = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "saturation",
        .label            = "Saturation",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 0.0,
        .min_value        = -1.0,
        .max_value        = 1.0,
        .step_value       = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_gamma_params[] = {
    {
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "gamma",
        .label            = "Gamma",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 1.0,
        .min_value        = 0.1,
        .max_value        = 4.0,
        .step_value       = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_exposure_params[] = {
    {
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "exposure",
        .label            = "Exposure",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 0.0,
        .min_value        = -10.0,
        .max_value        = 10.0,
        .step_value       = 0.1,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_black_white_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "blackWhiteAmount", .label = "Amount",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.0, .min_value = 0.0,
        .max_value = 1.0, .step_value = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "redWeight", .label = "Red",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.2126, .min_value = 0.0,
        .max_value = 2.0, .step_value = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "greenWeight", .label = "Green",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.7152, .min_value = 0.0,
        .max_value = 2.0, .step_value = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "blueWeight", .label = "Blue",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0722, .min_value = 0.0,
        .max_value = 2.0, .step_value = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectDescriptor k_effects[] = {
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "brightness.contrast", /* kept for document compat */
        .display_name           = "Brightness / Contrast",
        .category               = "Color Adjust",
        .version                = 1u,
        .capabilities           = &k_color_caps,
        .params                 = k_brightness_contrast_params,
        .param_count            = 2u,
        .user_data              = NULL,
        .pixel_expansion_radius = NULL, /* null == 0 (former tileExpansionRadius == 0) */
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = color_adjust_create_pass,
        .render_pass            = color_adjust_render_pass,
        .destroy_pass           = color_adjust_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "hue.saturation", /* kept for document compat */
        .display_name           = "Hue / Saturation",
        .category               = "Color Adjust",
        .version                = 1u,
        .capabilities           = &k_color_caps,
        .params                 = k_hue_saturation_params,
        .param_count            = 2u,
        .user_data              = NULL,
        .pixel_expansion_radius = NULL, /* null == 0 (former tileExpansionRadius == 0) */
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = color_adjust_create_pass,
        .render_pass            = color_adjust_render_pass,
        .destroy_pass           = color_adjust_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "color.gamma",
        .display_name           = "Gamma",
        .category               = "Color Adjust",
        .version                = 1u,
        .capabilities           = &k_color_caps,
        .params                 = k_gamma_params,
        .param_count            = 1u,
        .user_data              = NULL,
        .pixel_expansion_radius = NULL,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = color_adjust_create_pass,
        .render_pass            = color_adjust_render_pass,
        .destroy_pass           = color_adjust_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "color.exposure",
        .display_name           = "Exposure",
        .category               = "Color Adjust",
        .version                = 1u,
        .capabilities           = &k_color_caps,
        .params                 = k_exposure_params,
        .param_count            = 1u,
        .user_data              = NULL,
        .pixel_expansion_radius = NULL,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = color_adjust_create_pass,
        .render_pass            = color_adjust_render_pass,
        .destroy_pass           = color_adjust_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "color.black-white",
        .display_name           = "Black & White",
        .category               = "Color Adjust",
        .version                = 1u,
        .capabilities           = &k_color_caps,
        .params                 = k_black_white_params,
        .param_count            = 4u,
        .user_data              = NULL,
        .pixel_expansion_radius = NULL,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = color_adjust_create_pass,
        .render_pass            = color_adjust_render_pass,
        .destroy_pass           = color_adjust_destroy_pass,
    },
};

static const RuwaEffectPluginApi k_plugin = {
    .struct_size    = sizeof(RuwaEffectPluginApi),
    .abi_major      = RUWA_EFFECT_ABI_MAJOR,
    .abi_minor      = RUWA_EFFECT_ABI_MINOR,
    .plugin_id      = "com.ruwa.standard.color",
    .plugin_name    = "Ruwa Standard Color",
    .plugin_version = "1.0",
    .vendor         = "Ruwa",
    .effect_count   = 5u,
    .effects        = k_effects,
    .shutdown       = NULL,
};

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host)
{
    if (requested_abi_major != RUWA_EFFECT_ABI_MAJOR) {
        return NULL; /* incompatible ABI major — the host rejects us cleanly */
    }
    color_set_host_api(host); /* store for the plugin's lifetime; do not call it here yet */
    return &k_plugin;
}
