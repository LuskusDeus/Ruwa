// SPDX-License-Identifier: MPL-2.0

/* Sharpen: alpha-safe 3x3 unsharp filter for Ruwa.Standard.Blur. */

#include "../BlurCommon.h"
#include "../BlurEffects.h"

#include <stdlib.h>

static const char* const k_sharpen_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uTexelSize;\n"
    "uniform float uAmount;\n"
    "uniform float uThreshold;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec3 straightAt(vec2 uv) {\n"
    "    vec4 sampleColor = texture(uSourceTexture, uv);\n"
    "    return sampleColor.a > 0.0001 ? clamp(sampleColor.rgb / sampleColor.a, 0.0, 1.0) : vec3(0.0);\n"
    "}\n"
    "void main() {\n"
    "    vec4 source = texture(uSourceTexture, fragTexCoord);\n"
    "    if (source.a <= 0.0001 || uAmount <= 0.0) { outColor = source; return; }\n"
    "    vec2 t = uTexelSize;\n"
    "    vec3 center = straightAt(fragTexCoord);\n"
    "    vec3 blurred = center * 4.0;\n"
    "    blurred += straightAt(fragTexCoord + vec2( t.x, 0.0)) * 2.0;\n"
    "    blurred += straightAt(fragTexCoord + vec2(-t.x, 0.0)) * 2.0;\n"
    "    blurred += straightAt(fragTexCoord + vec2(0.0,  t.y)) * 2.0;\n"
    "    blurred += straightAt(fragTexCoord + vec2(0.0, -t.y)) * 2.0;\n"
    "    blurred += straightAt(fragTexCoord + vec2( t.x,  t.y));\n"
    "    blurred += straightAt(fragTexCoord + vec2(-t.x,  t.y));\n"
    "    blurred += straightAt(fragTexCoord + vec2( t.x, -t.y));\n"
    "    blurred += straightAt(fragTexCoord + vec2(-t.x, -t.y));\n"
    "    blurred *= 0.0625;\n"
    "    vec3 detail = center - blurred;\n"
    "    vec3 absoluteDetail = abs(detail);\n"
    "    float edge = max(absoluteDetail.r, max(absoluteDetail.g, absoluteDetail.b));\n"
    "    float gate = uThreshold <= 0.0 ? 1.0 : smoothstep(uThreshold, uThreshold + 0.02, edge);\n"
    "    vec3 result = clamp(center + detail * (uAmount * gate), 0.0, 1.0);\n"
    "    outColor = vec4(result * source.a, source.a);\n"
    "}\n";

typedef struct SharpenPass {
    RuwaEffectPipeline pipeline;
    RuwaEffectSampler sampler;
} SharpenPass;

void* blur_sharpen_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_blur_host || !g_blur_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = blur_create_graphics(gpu, k_sharpen_fs,
        "Ruwa.Standard.Blur/sharpen.basic", "ruwa.standard.blur.sharpen.basic.fs");
    if (!pipeline) {
        return NULL;
    }
    SharpenPass* pass = (SharpenPass*)calloc(1, sizeof(SharpenPass));
    if (!pass) {
        g_blur_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    pass->sampler = blur_create_linear_sampler(gpu);
    return pass;
}

int32_t blur_sharpen_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    (void)state;
    return 1;
}

RuwaEffectTexture blur_sharpen_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    SharpenPass* pass = (SharpenPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_blur_host || !g_blur_host->gpu || !input
        || !input->source_texture || !input->target_texture || input->output_width == 0
        || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }
    const float amount = blur_clampf(blur_real_param(input, "amount", 1.0f), 0.0f, 5.0f);
    if (amount <= 0.0f) {
        return input->source_texture;
    }
    const float threshold = blur_clampf(blur_real_param(input, "threshold", 0.0f), 0.0f, 1.0f);
    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const RuwaEffectGpuApi* gl = g_blur_host->gpu;

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uTexelSize", scale / (float)input->output_width,
        scale / (float)input->output_height);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->set_uniform_float(gpu, "uThreshold", threshold);
    gl->bind_texture(gpu, 0, input->source_texture, pass->sampler);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
    return input->target_texture;
}

void blur_sharpen_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    SharpenPass* pass = (SharpenPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (g_blur_host && g_blur_host->gpu) {
        if (pass->sampler) {
            g_blur_host->gpu->destroy_sampler(gpu, pass->sampler);
        }
        if (pass->pipeline) {
            g_blur_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
        }
    }
    free(pass);
}
