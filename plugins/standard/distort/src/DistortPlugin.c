// SPDX-License-Identifier: MPL-2.0

/* Ruwa.Standard.Distort package metadata and C ABI entry point. */

#include "DistortCommon.h"
#include "DistortEffects.h"

/* ==========================================================================
 *   Descriptor tables
 * ========================================================================== */

/* Twirl reaches arbitrarily far, so it routes to whole-layer materialisation. */
static const RuwaEffectCapabilities k_twirl_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE,   /* supports_document_tile   */
    RUWA_TRUE,   /* supports_viewport_screen */
    RUWA_TRUE,   /* expands_bounds           */
    RUWA_TRUE,   /* requires_neighbor_tiles  */
    RUWA_FALSE,  /* requires_backdrop        */
    RUWA_TRUE,   /* order_dependent          */
    RUWA_TRUE    /* reads_whole_layer        */
};

/* Pinch / Ripple are bounded — neighbour padding is enough, no whole layer. */
static const RuwaEffectCapabilities k_bounded_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE,   /* supports_document_tile   */
    RUWA_TRUE,   /* supports_viewport_screen */
    RUWA_TRUE,   /* expands_bounds           */
    RUWA_TRUE,   /* requires_neighbor_tiles  */
    RUWA_FALSE,  /* requires_backdrop        */
    RUWA_TRUE,   /* order_dependent          */
    RUWA_FALSE   /* reads_whole_layer        */
};

/* A center X/Y position pair (NumberField), defaulted to the canvas centre via
 * default_binding, serialized into the same paired "centerX"/"centerY" keys. */
#define CENTER_X_PARAM                                                      \
    {                                                                       \
        .struct_size = sizeof(RuwaEffectParamDef), .key = "centerX",        \
        .label = "Center", .type = RUWA_EFFECT_PARAM_REAL,                  \
        .default_value = 0.0, .min_value = 0.0, .max_value = 16384.0,       \
        .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD, \
        .position_pair_key = "center", .position_axis = RUWA_EFFECT_AXIS_X, \
        .default_binding = RUWA_EFFECT_BIND_CANVAS_HALF_WIDTH,              \
    }
#define CENTER_Y_PARAM                                                      \
    {                                                                       \
        .struct_size = sizeof(RuwaEffectParamDef), .key = "centerY",        \
        .label = "Center Y", .type = RUWA_EFFECT_PARAM_REAL,               \
        .default_value = 0.0, .min_value = 0.0, .max_value = 16384.0,       \
        .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD, \
        .position_pair_key = "center", .position_axis = RUWA_EFFECT_AXIS_Y, \
        .default_binding = RUWA_EFFECT_BIND_CANVAS_HALF_HEIGHT,             \
    }

static const RuwaEffectParamDef k_twirl_params[] = {
    CENTER_X_PARAM,
    CENTER_Y_PARAM,
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "angle", .label = "Angle",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 90.0, .min_value = -720.0,
        .max_value = 720.0, .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 200.0, .min_value = 0.0,
        .max_value = (double)TWIRL_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_pinch_params[] = {
    CENTER_X_PARAM,
    CENTER_Y_PARAM,
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.5, .min_value = -1.0,
        .max_value = 1.0, .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 240.0, .min_value = 0.0,
        .max_value = (double)PINCH_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_ripple_params[] = {
    CENTER_X_PARAM,
    CENTER_Y_PARAM,
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "amplitude", .label = "Amplitude",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 16.0, .min_value = 0.0,
        .max_value = (double)RIPPLE_MAX_AMPLITUDE, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "wavelength", .label = "Wavelength",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 80.0, .min_value = 1.0,
        .max_value = (double)RIPPLE_MAX_WAVELENGTH, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 512.0, .min_value = 0.0,
        .max_value = (double)RIPPLE_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "phase", .label = "Phase",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 360.0, .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_wave_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "amplitude", .label = "Amplitude",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 20.0, .min_value = 0.0,
        .max_value = (double)WAVE_MAX_AMPLITUDE, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "wavelength", .label = "Wavelength",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 120.0, .min_value = 1.0,
        .max_value = (double)WAVE_MAX_WAVELENGTH, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "angle", .label = "Angle",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 360.0, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "phase", .label = "Phase",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 360.0, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectDescriptor k_effects[] = {
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "distort.twirl",
        .display_name           = "Twirl",
        .category               = "Distort",
        .version                = 1u,
        .capabilities           = &k_twirl_caps,
        .params                 = k_twirl_params,
        .param_count            = 4u,
        .user_data              = NULL,
        .pixel_expansion_radius = distort_twirl_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = distort_twirl_create_pass,
        .render_pass            = distort_twirl_render_pass,
        .destroy_pass           = distort_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "distort.pinch",
        .display_name           = "Pinch",
        .category               = "Distort",
        .version                = 1u,
        .capabilities           = &k_bounded_caps,
        .params                 = k_pinch_params,
        .param_count            = 4u,
        .user_data              = NULL,
        .pixel_expansion_radius = distort_pinch_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = distort_pinch_create_pass,
        .render_pass            = distort_pinch_render_pass,
        .destroy_pass           = distort_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "distort.ripple",
        .display_name           = "Ripple",
        .category               = "Distort",
        .version                = 1u,
        .capabilities           = &k_bounded_caps,
        .params                 = k_ripple_params,
        .param_count            = 5u,
        .user_data              = NULL,
        .pixel_expansion_radius = distort_ripple_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = distort_ripple_create_pass,
        .render_pass            = distort_ripple_render_pass,
        .destroy_pass           = distort_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "distort.wave",
        .display_name           = "Wave",
        .category               = "Distort",
        .version                = 1u,
        .capabilities           = &k_bounded_caps,
        .params                 = k_wave_params,
        .param_count            = 4u,
        .user_data              = NULL,
        .pixel_expansion_radius = distort_wave_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = distort_wave_create_pass,
        .render_pass            = distort_wave_render_pass,
        .destroy_pass           = distort_destroy_pass,
    },
};

static const RuwaEffectPluginApi k_plugin = {
    .struct_size    = sizeof(RuwaEffectPluginApi),
    .abi_major      = RUWA_EFFECT_ABI_MAJOR,
    .abi_minor      = RUWA_EFFECT_ABI_MINOR,
    .plugin_id      = "com.ruwa.standard.distort",
    .plugin_name    = "Ruwa Standard Distort",
    .plugin_version = "1.0",
    .vendor         = "Ruwa",
    .effect_count   = 4u,
    .effects        = k_effects,
    .shutdown       = NULL,
};

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host)
{
    if (requested_abi_major != RUWA_EFFECT_ABI_MAJOR) {
        return NULL; /* incompatible ABI major — the host rejects us cleanly */
    }
    distort_set_host_api(host); /* store for the plugin's lifetime; do not call it here yet */
    return &k_plugin;
}
