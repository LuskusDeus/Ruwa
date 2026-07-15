// SPDX-License-Identifier: MPL-2.0

/* Unsharp Mask: Gaussian detail enhancement for Ruwa.Standard.Blur. */

#include "../BlurCommon.h"
#include "../BlurEffects.h"

#include <stdlib.h>

static const char* const k_unsharp_composite_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform sampler2D uBlurredTexture;\n"
    "uniform float uAmount;\n"
    "uniform float uThreshold;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    vec4 source = texture(uSourceTexture, fragTexCoord);\n"
    "    if (source.a <= 0.0001 || uAmount <= 0.0) { outColor = source; return; }\n"
    "    vec4 blurred = texture(uBlurredTexture, fragTexCoord);\n"
    "    vec3 center = clamp(source.rgb / source.a, 0.0, 1.0);\n"
    "    vec3 lowPass = blurred.a > 0.0001\n"
    "        ? clamp(blurred.rgb / blurred.a, 0.0, 1.0) : center;\n"
    "    vec3 detail = center - lowPass;\n"
    "    vec3 absoluteDetail = abs(detail);\n"
    "    float edge = max(absoluteDetail.r, max(absoluteDetail.g, absoluteDetail.b));\n"
    "    float gate = uThreshold <= 0.0\n"
    "        ? 1.0 : smoothstep(uThreshold, uThreshold + (1.0 / 255.0), edge);\n"
    "    vec3 result = clamp(center + detail * (uAmount * gate), 0.0, 1.0);\n"
    "    outColor = vec4(result * source.a, source.a);\n"
    "}\n";

typedef struct UnsharpMaskPass {
    RuwaEffectPipeline blurPipeline;
    RuwaEffectPipeline compositePipeline;
    RuwaEffectSampler sampler;
} UnsharpMaskPass;

void* blur_unsharp_mask_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_blur_host || !g_blur_host->gpu) {
        return NULL;
    }

    const RuwaEffectGpuApi* gl = g_blur_host->gpu;
    RuwaEffectPipeline blurPipeline = blur_create_graphics(gpu,
        blur_gaussian_fragment_source(), "Ruwa.Standard.Blur/sharpen.unsharp-mask.blur",
        "ruwa.standard.blur.gaussian.fs");
    if (!blurPipeline) {
        return NULL;
    }

    RuwaEffectPipeline compositePipeline = blur_create_graphics(gpu, k_unsharp_composite_fs,
        "Ruwa.Standard.Blur/sharpen.unsharp-mask.composite",
        "ruwa.standard.blur.sharpen.unsharp-mask.composite.fs");
    if (!compositePipeline) {
        gl->destroy_graphics_pipeline(gpu, blurPipeline);
        return NULL;
    }

    RuwaEffectSampler sampler = blur_create_linear_sampler(gpu);
    if (!sampler) {
        gl->destroy_graphics_pipeline(gpu, compositePipeline);
        gl->destroy_graphics_pipeline(gpu, blurPipeline);
        return NULL;
    }

    UnsharpMaskPass* pass = (UnsharpMaskPass*)calloc(1, sizeof(UnsharpMaskPass));
    if (!pass) {
        gl->destroy_sampler(gpu, sampler);
        gl->destroy_graphics_pipeline(gpu, compositePipeline);
        gl->destroy_graphics_pipeline(gpu, blurPipeline);
        return NULL;
    }
    pass->blurPipeline = blurPipeline;
    pass->compositePipeline = compositePipeline;
    pass->sampler = sampler;
    return pass;
}

int32_t blur_unsharp_mask_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* radius = blur_find_param(
        state->param_count, state->param_keys, state->param_values, "radius");
    return blur_clampi(radius ? radius->value.as_int : 2, 0, GAUSSIAN_MAX_RADIUS);
}

RuwaEffectTexture blur_unsharp_mask_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    UnsharpMaskPass* pass = (UnsharpMaskPass*)pass_instance;
    if (!pass || !pass->blurPipeline || !pass->compositePipeline || !pass->sampler
        || !g_blur_host || !g_blur_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }

    const int radius = blur_clampi(
        blur_int_param(input, "radius", 2), 0, GAUSSIAN_MAX_RADIUS);
    const float amount = blur_clampf(
        blur_real_param(input, "amount", 1.0f), 0.0f, 5.0f);
    if (radius <= 0 || amount <= 0.0f) {
        return input->source_texture;
    }
    const float threshold = blur_clampf(
        blur_real_param(input, "threshold", 0.0f), 0.0f, 1.0f);

    const RuwaEffectGpuApi* gl = g_blur_host->gpu;
    RuwaEffectTexture horizontal = gl->alloc_scratch_texture(gpu, RUWA_EFFECT_FORMAT_RGBA16F);
    RuwaEffectTexture blurred = gl->alloc_scratch_texture(gpu, RUWA_EFFECT_FORMAT_RGBA16F);
    if (!horizontal || !blurred) {
        return input->source_texture;
    }

    blur_upload_gaussian_kernel(gl, gpu, pass->blurPipeline, radius);
    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    blur_draw_gaussian_axis(gl, gpu, pass->blurPipeline, pass->sampler,
        input->source_texture, horizontal, scale / (float)input->output_width,
        0.0f, 0, radius);
    blur_draw_gaussian_axis(gl, gpu, pass->blurPipeline, pass->sampler, horizontal,
        blurred, 0.0f, scale / (float)input->output_height, 0, 0);

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->compositePipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_int(gpu, "uBlurredTexture", 1);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->set_uniform_float(gpu, "uThreshold", threshold);
    gl->bind_texture(gpu, 0, input->source_texture, pass->sampler);
    gl->bind_texture(gpu, 1, blurred, pass->sampler);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
    return input->target_texture;
}

void blur_unsharp_mask_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    UnsharpMaskPass* pass = (UnsharpMaskPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (g_blur_host && g_blur_host->gpu) {
        const RuwaEffectGpuApi* gl = g_blur_host->gpu;
        if (pass->sampler) {
            gl->destroy_sampler(gpu, pass->sampler);
        }
        if (pass->compositePipeline) {
            gl->destroy_graphics_pipeline(gpu, pass->compositePipeline);
        }
        if (pass->blurPipeline) {
            gl->destroy_graphics_pipeline(gpu, pass->blurPipeline);
        }
    }
    free(pass);
}
