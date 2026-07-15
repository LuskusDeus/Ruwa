// SPDX-License-Identifier: MPL-2.0

/* Gaussian Blur: isolated implementation module for Ruwa.Standard.Blur. */

#include "../BlurCommon.h"
#include "../BlurEffects.h"

#include <stdlib.h>

/* ==========================================================================
 *   Gaussian Blur
 * ========================================================================== */

typedef struct GaussianPass {
    RuwaEffectPipeline pipeline;
    RuwaEffectSampler linearSampler;
} GaussianPass;

void* blur_gaussian_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_blur_host || !g_blur_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = blur_create_graphics(
        gpu, blur_gaussian_fragment_source(), "Ruwa.Standard.Blur/blur.gaussian",
        "ruwa.standard.blur.gaussian.fs");
    if (!pipeline) {
        return NULL;
    }
    GaussianPass* pass = (GaussianPass*)malloc(sizeof(GaussianPass));
    if (!pass) {
        g_blur_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    pass->linearSampler = blur_create_linear_sampler(gpu);
    return pass;
}

int32_t blur_gaussian_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* r
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    return blur_clampi(r ? r->value.as_int : 8, 0, GAUSSIAN_MAX_RADIUS);
}

RuwaEffectTexture blur_gaussian_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    GaussianPass* pass = (GaussianPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_blur_host || !g_blur_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_blur_host->gpu;

    const int radius = blur_clampi(blur_int_param(input, "radius", 8), 0, GAUSSIAN_MAX_RADIUS);
    if (radius <= 0) {
        return input->source_texture;
    }
    RuwaEffectTexture intermediate = gl->alloc_scratch_texture(gpu, RUWA_EFFECT_FORMAT_RGBA8);
    if (!intermediate) {
        return input->source_texture;
    }

    blur_upload_gaussian_kernel(gl, gpu, pass->pipeline, radius);

    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const float invW = scale / (float)input->output_width;
    const float invH = scale / (float)input->output_height;

    /* Horizontal: source -> intermediate, keeping +/- radius rows valid for the
     * vertical pass. Vertical: intermediate -> target. */
    blur_draw_gaussian_axis(gl, gpu, pass->pipeline, pass->linearSampler,
        input->source_texture, intermediate, invW, 0.0f, 0, radius);
    blur_draw_gaussian_axis(gl, gpu, pass->pipeline, pass->linearSampler, intermediate,
        input->target_texture, 0.0f, invH, 0, 0);
    return input->target_texture;
}

void blur_gaussian_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    GaussianPass* pass = (GaussianPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (g_blur_host && g_blur_host->gpu) {
        if (pass->linearSampler) {
            g_blur_host->gpu->destroy_sampler(gpu, pass->linearSampler);
        }
        if (pass->pipeline) {
            g_blur_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
        }
    }
    free(pass);
}
