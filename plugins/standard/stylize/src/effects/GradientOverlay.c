// SPDX-License-Identifier: MPL-2.0

/* Gradient Overlay: isolated implementation module for Ruwa.Standard.Stylize. */

#include "../StylizeCommon.h"
#include "../StylizeEffects.h"

#include <stdlib.h>

/* ==========================================================================
 *   Gradient Overlay — positional
 * ========================================================================== */

static const char* const k_gradient_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uP0;\n"
    "uniform vec2 uP1;\n"
    "uniform vec4 uColor0;\n"
    "uniform vec4 uColor1;\n"
    "uniform float uOpacity;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    vec4 srcPremul = texture(uSourceTexture, fragTexCoord);\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "    vec2 axis = uP1 - uP0;\n"
    "    float len2 = max(dot(axis, axis), 1e-6);\n"
    "    float t = clamp(dot(docPos - uP0, axis) / len2, 0.0, 1.0);\n"
    "    vec4 grad = mix(uColor0, uColor1, t);\n"
    "    float overlayFactor = clamp(grad.a * uOpacity, 0.0, 1.0);\n"
    "    float srcA = srcPremul.a;\n"
    "    vec3 srcStraight = srcA > 1e-4 ? srcPremul.rgb / srcA : vec3(0.0);\n"
    "    vec3 resultStraight = mix(srcStraight, grad.rgb, overlayFactor);\n"
    "    outColor.rgb = resultStraight * srcA;\n"
    "    outColor.a = srcA;\n"
    "}\n";

typedef struct GradientPass {
    RuwaEffectPipeline pipeline;
} GradientPass;

void* stylize_gradient_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_stylize_host || !g_stylize_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = stylize_create_graphics(gpu, k_gradient_fs,
        "Ruwa.Standard.Stylize/gradient.overlay", "ruwa.standard.stylize.gradient.fs");
    if (!pipeline) {
        return NULL;
    }
    GradientPass* pass = (GradientPass*)malloc(sizeof(GradientPass));
    if (!pass) {
        g_stylize_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

RuwaEffectTexture stylize_gradient_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    GradientPass* pass = (GradientPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_stylize_host || !g_stylize_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0
        || input->region.valid == RUWA_FALSE) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;

    const float x0 = stylize_real_param(input, "x0", 0.0f);
    const float y0 = stylize_real_param(input, "y0", 0.0f);
    const float x1 = stylize_real_param(input, "x1", 512.0f);
    const float y1 = stylize_real_param(input, "y1", 512.0f);
    float c0[4];
    float c1[4];
    stylize_color_param(input, "color0", c0, 0.0f, 0.0f, 0.0f, 1.0f);
    stylize_color_param(input, "color1", c1, 1.0f, 1.0f, 1.0f, 1.0f);
    const float opacity = stylize_clampf(stylize_real_param(input, "opacity", 1.0f), 0.0f, 1.0f);

    const StylizeDocFrame frame = stylize_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    stylize_set_doc_frame_forward(gl, gpu, &frame);
    gl->set_uniform_float2(gpu, "uP0", x0, y0);
    gl->set_uniform_float2(gpu, "uP1", x1, y1);
    gl->set_uniform_float4(gpu, "uColor0", c0[0], c0[1], c0[2], c0[3]);
    gl->set_uniform_float4(gpu, "uColor1", c1[0], c1[1], c1[2], c1[3]);
    gl->set_uniform_float(gpu, "uOpacity", opacity);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);

    return input->target_texture;
}

void stylize_gradient_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    GradientPass* pass = (GradientPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_stylize_host && g_stylize_host->gpu) {
        g_stylize_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
