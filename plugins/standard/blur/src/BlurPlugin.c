// SPDX-License-Identifier: MPL-2.0

/* Ruwa.Standard.Blur package metadata and C ABI entry point. */

#include "BlurCommon.h"
#include "BlurEffects.h"

/* ==========================================================================
 *   Descriptor tables
 * ========================================================================== */

/* All four blur effects expand bounds, gather neighbour tiles and are order
 * dependent; none reads the whole layer or needs the backdrop. */
static const RuwaEffectCapabilities k_blur_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE,   /* supports_document_tile   */
    RUWA_TRUE,   /* supports_viewport_screen */
    RUWA_TRUE,   /* expands_bounds           */
    RUWA_TRUE,   /* requires_neighbor_tiles  */
    RUWA_FALSE,  /* requires_backdrop        */
    RUWA_TRUE,   /* order_dependent          */
    RUWA_FALSE   /* reads_whole_layer        */
};

/* Sharpen effects read a neighbourhood but keep the source alpha bounds. */
static const RuwaEffectCapabilities k_sharpen_caps = {
    sizeof(RuwaEffectCapabilities),
    RUWA_TRUE, RUWA_TRUE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE, RUWA_TRUE, RUWA_FALSE
};

static const RuwaEffectParamDef k_gaussian_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 8.0, .min_value = 0.0,
        .max_value = (double)GAUSSIAN_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_box_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 8.0, .min_value = 0.0,
        .max_value = (double)BOX_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "iterations", .label = "Iterations",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 1.0, .min_value = 1.0,
        .max_value = (double)BOX_MAX_ITERATIONS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_motion_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "angle", .label = "Angle",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 360.0, .step_value = 1.0, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "distance", .label = "Distance",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 20.0, .min_value = 0.0,
        .max_value = (double)MOTION_MAX_DISTANCE, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

/* Radial's center is a position pair with NO canvas default binding (matching
 * the former pointParam), so a fresh instance pivots on the document origin. */
static const RuwaEffectParamDef k_radial_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "centerX", .label = "Center",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 16384.0, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD, .position_pair_key = "center",
        .position_axis = RUWA_EFFECT_AXIS_X,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "centerY", .label = "Center Y",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 16384.0, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_NUMBER_FIELD, .position_pair_key = "center",
        .position_axis = RUWA_EFFECT_AXIS_Y,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.02, .min_value = 0.0,
        .max_value = 0.2, .step_value = 0.001, .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Max Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 32.0, .min_value = 0.0,
        .max_value = (double)RADIAL_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_sharpen_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.0, .min_value = 0.0,
        .max_value = 5.0, .step_value = 0.05,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "threshold", .label = "Threshold",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 1.0, .step_value = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectParamDef k_unsharp_mask_params[] = {
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "amount", .label = "Amount",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 1.0, .min_value = 0.0,
        .max_value = 5.0, .step_value = 0.05,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "radius", .label = "Radius",
        .type = RUWA_EFFECT_PARAM_INT, .default_value = 2.0, .min_value = 0.0,
        .max_value = (double)GAUSSIAN_MAX_RADIUS, .step_value = 1.0,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        .struct_size = sizeof(RuwaEffectParamDef), .key = "threshold", .label = "Threshold",
        .type = RUWA_EFFECT_PARAM_REAL, .default_value = 0.0, .min_value = 0.0,
        .max_value = 1.0, .step_value = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
};

static const RuwaEffectDescriptor k_effects[] = {
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "blur.gaussian",
        .display_name           = "Gaussian Blur",
        .category               = "Blur",
        .version                = 1u,
        .capabilities           = &k_blur_caps,
        .params                 = k_gaussian_params,
        .param_count            = 1u,
        .user_data              = NULL,
        .pixel_expansion_radius = blur_gaussian_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = blur_gaussian_create_pass,
        .render_pass            = blur_gaussian_render_pass,
        .destroy_pass           = blur_gaussian_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "blur.box",
        .display_name           = "Box Blur",
        .category               = "Blur",
        .version                = 1u,
        .capabilities           = &k_blur_caps,
        .params                 = k_box_params,
        .param_count            = 2u,
        .user_data              = NULL,
        .pixel_expansion_radius = blur_box_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = blur_box_create_pass,
        .render_pass            = blur_box_render_pass,
        .destroy_pass           = blur_box_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "blur.motion",
        .display_name           = "Motion Blur",
        .category               = "Blur",
        .version                = 1u,
        .capabilities           = &k_blur_caps,
        .params                 = k_motion_params,
        .param_count            = 2u,
        .user_data              = NULL,
        .pixel_expansion_radius = blur_motion_pixel_expansion_radius,
        .resolve_coverage       = blur_motion_resolve_coverage,
        .migrate_state          = NULL,
        .create_pass            = blur_motion_create_pass,
        .render_pass            = blur_motion_render_pass,
        .destroy_pass           = blur_motion_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "blur.radial",
        .display_name           = "Radial Blur",
        .category               = "Blur",
        .version                = 1u,
        .capabilities           = &k_blur_caps,
        .params                 = k_radial_params,
        .param_count            = 4u,
        .user_data              = NULL,
        .pixel_expansion_radius = blur_radial_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = blur_radial_create_pass,
        .render_pass            = blur_radial_render_pass,
        .destroy_pass           = blur_radial_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "sharpen.basic",
        .display_name           = "Sharpen",
        .category               = "Sharpen",
        .version                = 1u,
        .capabilities           = &k_sharpen_caps,
        .params                 = k_sharpen_params,
        .param_count            = 2u,
        .user_data              = NULL,
        .pixel_expansion_radius = blur_sharpen_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = blur_sharpen_create_pass,
        .render_pass            = blur_sharpen_render_pass,
        .destroy_pass           = blur_sharpen_destroy_pass,
    },
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "sharpen.unsharp-mask",
        .display_name           = "Unsharp Mask",
        .category               = "Sharpen",
        .version                = 1u,
        .capabilities           = &k_sharpen_caps,
        .params                 = k_unsharp_mask_params,
        .param_count            = 3u,
        .user_data              = NULL,
        .pixel_expansion_radius = blur_unsharp_mask_pixel_expansion_radius,
        .resolve_coverage       = NULL,
        .migrate_state          = NULL,
        .create_pass            = blur_unsharp_mask_create_pass,
        .render_pass            = blur_unsharp_mask_render_pass,
        .destroy_pass           = blur_unsharp_mask_destroy_pass,
    },
};

static const RuwaEffectPluginApi k_plugin = {
    .struct_size    = sizeof(RuwaEffectPluginApi),
    .abi_major      = RUWA_EFFECT_ABI_MAJOR,
    .abi_minor      = RUWA_EFFECT_ABI_MINOR,
    .plugin_id      = "com.ruwa.standard.blur",
    .plugin_name    = "Ruwa Standard Blur",
    .plugin_version = "1.0",
    .vendor         = "Ruwa",
    .effect_count   = 6u,
    .effects        = k_effects,
    .shutdown       = NULL,
};

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host)
{
    if (requested_abi_major != RUWA_EFFECT_ABI_MAJOR) {
        return NULL; /* incompatible ABI major — the host rejects us cleanly */
    }
    blur_set_host_api(host); /* store for the plugin's lifetime; do not call it here yet */
    return &k_plugin;
}
