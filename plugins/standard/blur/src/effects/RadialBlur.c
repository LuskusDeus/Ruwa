// SPDX-License-Identifier: MPL-2.0

/* Radial Blur: isolated implementation module for Ruwa.Standard.Blur. */

#include "../BlurCommon.h"
#include "../BlurEffects.h"

#include <stdlib.h>

/* ==========================================================================
 *   Radial (Zoom) Blur
 * ========================================================================== */

static const char* const k_radial_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uInvBasis0;\n"
    "uniform vec2 uInvBasis1;\n"
    "uniform vec2 uCenter;\n"
    "uniform float uAmount;\n"
    "uniform float uMaxRadiusPx;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "const int kTaps = 8;\n"
    "void main() {\n"
    "    if (uMaxRadiusPx <= 0.0 || uAmount <= 0.0) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "    vec2 dir = docPos - uCenter;\n"
    "    float dist = length(dir);\n"
    "    if (dist < 1e-3) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "    vec2 dirNorm = dir / dist;\n"
    "    float displacement = min(dist * uAmount, uMaxRadiusPx);\n"
    "    vec4 sum = vec4(0.0);\n"
    "    for (int i = -kTaps; i <= kTaps; ++i) {\n"
    "        vec2 sampleDoc = docPos + dirNorm * (displacement * float(i) / float(kTaps));\n"
    "        vec2 rel = sampleDoc - uRegionOrigin;\n"
    "        vec2 sampleUV = vec2(dot(rel, uInvBasis0), dot(rel, uInvBasis1));\n"
    "        sum += texture(uSourceTexture, sampleUV);\n"
    "    }\n"
    "    outColor = sum / float(2 * kTaps + 1);\n"
    "}\n";

typedef struct RadialPass {
    RuwaEffectPipeline pipeline;
} RadialPass;

void* blur_radial_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_blur_host || !g_blur_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = blur_create_graphics(
        gpu, k_radial_fs, "Ruwa.Standard.Blur/blur.radial", "ruwa.standard.blur.radial.fs");
    if (!pipeline) {
        return NULL;
    }
    RadialPass* pass = (RadialPass*)malloc(sizeof(RadialPass));
    if (!pass) {
        g_blur_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

int32_t blur_radial_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* r
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    return blur_clampi(r ? r->value.as_int : 32, 0, RADIAL_MAX_RADIUS);
}

RuwaEffectTexture blur_radial_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    RadialPass* pass = (RadialPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_blur_host || !g_blur_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0
        || input->region.valid == RUWA_FALSE) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_blur_host->gpu;

    const float centerX = blur_real_param(input, "centerX", 0.0f);
    const float centerY = blur_real_param(input, "centerY", 0.0f);
    const float amount = blur_clampf(blur_real_param(input, "amount", 0.02f), 0.0f, 0.2f);
    const float maxRadius = (float)blur_clampi(blur_int_param(input, "radius", 32), 0, RADIAL_MAX_RADIUS);
    if (amount <= 0.0f || maxRadius <= 0.0f) {
        return input->source_texture;
    }

    const BlurDocFrame frame = blur_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    blur_set_doc_frame_uniforms(gl, gpu, &frame);
    gl->set_uniform_float2(gpu, "uCenter", centerX, centerY);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->set_uniform_float(gpu, "uMaxRadiusPx", maxRadius);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);

    return input->target_texture;
}

void blur_radial_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    RadialPass* pass = (RadialPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_blur_host && g_blur_host->gpu) {
        g_blur_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
