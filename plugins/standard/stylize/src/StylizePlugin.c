// SPDX-License-Identifier: MPL-2.0

/* Ruwa.Standard.Stylize package metadata and C ABI entry point. */

#include "StylizeCommon.h"
#include "StylizeEffects.h"

/* ==========================================================================
 *   Descriptor tables
 * ========================================================================== */

/* Glow / Stroke / Halftone expand bounds and gather neighbour tiles. */
static const RuwaEffectCapabilities k_expanding_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE, RUWA_TRUE, RUWA_TRUE, RUWA_TRUE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE
};

/* Gradient Overlay is a pure per-fragment recolour: no bounds expansion. */
static const RuwaEffectCapabilities k_overlay_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE, RUWA_TRUE, RUWA_FALSE, RUWA_FALSE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE
};

/* Inner glow samples outside the current tile, but never creates new alpha. */
static const RuwaEffectCapabilities k_inner_style_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE, RUWA_TRUE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE
};

enum {
    LAYER_STYLE_OUTER_GLOW = 0,
    LAYER_STYLE_INNER_GLOW = 1,
    LAYER_STYLE_DROP_SHADOW = 2
};

static int k_outer_glow_mode = LAYER_STYLE_OUTER_GLOW;
static int k_inner_glow_mode = LAYER_STYLE_INNER_GLOW;
static int k_drop_shadow_mode = LAYER_STYLE_DROP_SHADOW;

static const char* const k_glow_blend_choices[] = { "Natural", "Additive" };
static const char* const k_stroke_position_choices[] = { "Outside", "Center", "Inside" };

static const RuwaEffectParamDef k_glow_params[] = {
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 64.0, .min_value = 0.0,
      .max_value = (double)GLOW_MAX_RADIUS, .step_value = 1.0,
      .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "intensity", .label = "Intensity",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.5, .min_value = 0.0, .max_value = 16.0,
      .step_value = 0.05, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "threshold", .label = "Threshold",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "softness", .label = "Softness",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.35, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "falloff", .label = "Falloff",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.2, .min_value = 0.35, .max_value = 3.0,
      .step_value = 0.05, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color", .label = "Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 1.0f, 1.0f, 1.0f, 1.0f } },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "blend", .label = "Blend",
      .type = RUWA_EFFECT_PARAM_CHOICE, .choices = k_glow_blend_choices, .choice_count = 2,
      .default_choice = 0 },
};

static const RuwaEffectParamDef k_stroke_params[] = {
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "size", .label = "Size",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 4.0, .min_value = 0.0,
      .max_value = (double)STROKE_MAX_RADIUS, .step_value = 1.0,
      .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "offset", .label = "Offset",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 0.0, .min_value = -(double)STROKE_MAX_RADIUS,
      .max_value = (double)STROKE_MAX_RADIUS, .step_value = 1.0,
      .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "position", .label = "Position",
      .type = RUWA_EFFECT_PARAM_CHOICE, .choices = k_stroke_position_choices, .choice_count = 3,
      .default_choice = 0 },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color", .label = "Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 0.0f, 0.0f, 0.0f, 1.0f } },
};

static const RuwaEffectParamDef k_gradient_params[] = {
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "x0", .label = "Start Pos",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0, .max_value = 16384.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD,
      .position_pair_key = "pos0", .position_axis = RUWA_EFFECT_AXIS_X },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "y0", .label = "Start Y",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0, .max_value = 16384.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD,
      .position_pair_key = "pos0", .position_axis = RUWA_EFFECT_AXIS_Y },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "x1", .label = "End Pos",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 512.0, .min_value = 0.0, .max_value = 16384.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD,
      .position_pair_key = "pos1", .position_axis = RUWA_EFFECT_AXIS_X,
      .default_binding = RUWA_EFFECT_BIND_CANVAS_WIDTH },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "y1", .label = "End Y",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 512.0, .min_value = 0.0, .max_value = 16384.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD,
      .position_pair_key = "pos1", .position_axis = RUWA_EFFECT_AXIS_Y,
      .default_binding = RUWA_EFFECT_BIND_CANVAS_HEIGHT },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color0", .label = "Start Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 0.0f, 0.0f, 0.0f, 1.0f } },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color1", .label = "End Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 1.0f, 1.0f, 1.0f, 1.0f } },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "opacity", .label = "Opacity",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.0, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
};

static const RuwaEffectParamDef k_halftone_params[] = {
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "size", .label = "Size",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 14.0, .min_value = (double)HALFTONE_MIN_CELL,
      .max_value = (double)HALFTONE_MAX_CELL, .step_value = 1.0,
      .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "angle", .label = "Angle",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 45.0, .min_value = 0.0, .max_value = 180.0,
      .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.0, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "contrast", .label = "Contrast",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.5, .min_value = 0.0, .max_value = 1.0,
      .step_value = 0.01, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "monochrome", .label = "Monochrome",
      .type = RUWA_EFFECT_PARAM_BOOL, .default_value = 1.0 },
};

#define STYLE_RADIUS_PARAM                                                      \
    {                                                                           \
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius",            \
        .label = "Radius", .type = RUWA_EFFECT_PARAM_INT, .default_value = 16.0, \
        .min_value = 0.0, .max_value = (double)LAYER_STYLE_MAX_RADIUS,          \
        .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,       \
    }
#define STYLE_SPREAD_PARAM                                                      \
    {                                                                           \
        .struct_size = sizeof(RuwaEffectParamDef), .key = "spread",            \
        .label = "Spread", .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, \
        .min_value = 0.0, .max_value = 1.0, .step_value = 0.01,                 \
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,                          \
    }
#define STYLE_OPACITY_PARAM                                                     \
    {                                                                           \
        .struct_size = sizeof(RuwaEffectParamDef), .key = "opacity",           \
        .label = "Opacity", .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.75, \
        .min_value = 0.0, .max_value = 1.0, .step_value = 0.01,                 \
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,                          \
    }

static const RuwaEffectParamDef k_outer_glow_params[] = {
    STYLE_RADIUS_PARAM,
    STYLE_SPREAD_PARAM,
    STYLE_OPACITY_PARAM,
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color", .label = "Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 1.0f, 0.8f, 0.2f, 1.0f } },
};

static const RuwaEffectParamDef k_inner_glow_params[] = {
    STYLE_RADIUS_PARAM,
    STYLE_SPREAD_PARAM,
    STYLE_OPACITY_PARAM,
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color", .label = "Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 1.0f, 1.0f, 1.0f, 1.0f } },
};

static const RuwaEffectParamDef k_drop_shadow_params[] = {
    STYLE_RADIUS_PARAM,
    STYLE_SPREAD_PARAM,
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "distance", .label = "Distance",
      .type = RUWA_EFFECT_PARAM_INT, .default_value = 12.0, .min_value = 0.0,
      .max_value = (double)LAYER_STYLE_MAX_DISTANCE, .step_value = 1.0,
      .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "angle", .label = "Angle",
      .type = RUWA_EFFECT_PARAM_REAL, .default_value = 45.0, .min_value = 0.0,
      .max_value = 360.0, .step_value = 1.0,
      .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER },
    STYLE_OPACITY_PARAM,
    { .struct_size = sizeof(RuwaEffectParamDef), .key = "color", .label = "Color",
      .type = RUWA_EFFECT_PARAM_COLOR, .default_color = { 0.0f, 0.0f, 0.0f, 1.0f } },
};

static const RuwaEffectDescriptor k_effects[] = {
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "stylize.glow", .display_name = "Glow",
      .category = "Stylize", .version = 1u, .capabilities = &k_expanding_caps,
      .params = k_glow_params, .param_count = 7u, .user_data = NULL,
      .pixel_expansion_radius = stylize_glow_pixel_expansion_radius, .resolve_coverage = NULL,
      .migrate_state = NULL, .create_pass = stylize_glow_create_pass, .render_pass = stylize_glow_render_pass,
      .destroy_pass = stylize_glow_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "stroke.outline",
      .display_name = "Stroke", .category = "Stylize", .version = 1u,
      .capabilities = &k_expanding_caps, .params = k_stroke_params, .param_count = 4u,
      .user_data = NULL, .pixel_expansion_radius = stylize_stroke_pixel_expansion_radius,
      .resolve_coverage = NULL, .migrate_state = NULL, .create_pass = stylize_stroke_create_pass,
      .render_pass = stylize_stroke_render_pass, .destroy_pass = stylize_stroke_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "gradient.overlay",
      .display_name = "Gradient Overlay", .category = "Stylize", .version = 1u,
      .capabilities = &k_overlay_caps, .params = k_gradient_params, .param_count = 7u,
      .user_data = NULL, .pixel_expansion_radius = NULL, .resolve_coverage = NULL,
      .migrate_state = NULL, .create_pass = stylize_gradient_create_pass,
      .render_pass = stylize_gradient_render_pass, .destroy_pass = stylize_gradient_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "stylize.halftone",
      .display_name = "Halftone", .category = "Stylize", .version = 1u,
      .capabilities = &k_expanding_caps, .params = k_halftone_params, .param_count = 5u,
      .user_data = NULL, .pixel_expansion_radius = stylize_halftone_pixel_expansion_radius,
      .resolve_coverage = NULL, .migrate_state = NULL, .create_pass = stylize_halftone_create_pass,
      .render_pass = stylize_halftone_render_pass, .destroy_pass = stylize_halftone_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "stylize.outer-glow",
      .display_name = "Outer Glow", .category = "Stylize", .version = 1u,
      .capabilities = &k_expanding_caps, .params = k_outer_glow_params, .param_count = 4u,
      .user_data = &k_outer_glow_mode,
      .pixel_expansion_radius = stylize_layer_style_pixel_expansion_radius,
      .resolve_coverage = NULL, .migrate_state = NULL,
      .create_pass = stylize_layer_style_create_pass,
      .render_pass = stylize_layer_style_render_pass,
      .destroy_pass = stylize_layer_style_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "stylize.inner-glow",
      .display_name = "Inner Glow", .category = "Stylize", .version = 1u,
      .capabilities = &k_inner_style_caps, .params = k_inner_glow_params, .param_count = 4u,
      .user_data = &k_inner_glow_mode,
      .pixel_expansion_radius = stylize_layer_style_pixel_expansion_radius,
      .resolve_coverage = NULL, .migrate_state = NULL,
      .create_pass = stylize_layer_style_create_pass,
      .render_pass = stylize_layer_style_render_pass,
      .destroy_pass = stylize_layer_style_destroy_pass },
    { .struct_size = sizeof(RuwaEffectDescriptor), .type_id = "stylize.drop-shadow",
      .display_name = "Drop Shadow", .category = "Stylize", .version = 1u,
      .capabilities = &k_expanding_caps, .params = k_drop_shadow_params, .param_count = 6u,
      .user_data = &k_drop_shadow_mode,
      .pixel_expansion_radius = stylize_layer_style_pixel_expansion_radius,
      .resolve_coverage = NULL, .migrate_state = NULL,
      .create_pass = stylize_layer_style_create_pass,
      .render_pass = stylize_layer_style_render_pass,
      .destroy_pass = stylize_layer_style_destroy_pass },
};

static const RuwaEffectPluginApi k_plugin = {
    .struct_size    = sizeof(RuwaEffectPluginApi),
    .abi_major      = RUWA_EFFECT_ABI_MAJOR,
    .abi_minor      = RUWA_EFFECT_ABI_MINOR,
    .plugin_id      = "com.ruwa.standard.stylize",
    .plugin_name    = "Ruwa Standard Stylize",
    .plugin_version = "1.0",
    .vendor         = "Ruwa",
    .effect_count   = 7u,
    .effects        = k_effects,
    .shutdown       = NULL,
};

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host)
{
    if (requested_abi_major != RUWA_EFFECT_ABI_MAJOR) {
        return NULL; /* incompatible ABI major — the host rejects us cleanly */
    }
    stylize_set_host_api(host); /* store for the plugin's lifetime; do not call it here yet */
    return &k_plugin;
}
