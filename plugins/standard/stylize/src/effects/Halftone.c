// SPDX-License-Identifier: MPL-2.0

/* Halftone: isolated implementation module for Ruwa.Standard.Stylize. */

#include "../StylizeCommon.h"
#include "../StylizeEffects.h"

#include <stdlib.h>

/* ==========================================================================
 *   Halftone — positional dot screen
 * ========================================================================== */

static const char* const k_halftone_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uInvBasis0;\n"
    "uniform vec2 uInvBasis1;\n"
    "uniform float uCellSizePx;\n"
    "uniform float uAngle;\n"
    "uniform float uAmount;\n"
    "uniform float uContrast;\n"
    "uniform int uMonochrome;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec2 rotate2(vec2 v, float angle) {\n"
    "    float s = sin(angle);\n"
    "    float c = cos(angle);\n"
    "    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);\n"
    "}\n"
    "void main() {\n"
    "    vec4 srcPremul = texture(uSourceTexture, fragTexCoord);\n"
    "    if (srcPremul.a <= 0.0 || uCellSizePx <= 0.0 || uAmount <= 0.0) {\n"
    "        outColor = srcPremul;\n"
    "        return;\n"
    "    }\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "    vec2 screenPos = rotate2(docPos, uAngle);\n"
    "    vec2 cell = floor(screenPos / uCellSizePx);\n"
    "    vec2 cellCenterScreen = (cell + vec2(0.5)) * uCellSizePx;\n"
    "    vec2 local = (screenPos - cellCenterScreen) / uCellSizePx;\n"
    "    vec2 cellCenterDoc = rotate2(cellCenterScreen, -uAngle);\n"
    "    vec2 centerRel = cellCenterDoc - uRegionOrigin;\n"
    "    vec2 centerUV = vec2(dot(centerRel, uInvBasis0), dot(centerRel, uInvBasis1));\n"
    "    vec4 centerPremul = texture(uSourceTexture, centerUV);\n"
    "    vec3 centerStraight = centerPremul.a > 1e-4 ? centerPremul.rgb / centerPremul.a : vec3(1.0);\n"
    "    float luminance = dot(centerStraight, vec3(0.2126, 0.7152, 0.0722));\n"
    "    float tone = 1.0 - luminance;\n"
    "    tone = clamp((tone - 0.5) * (1.0 + uContrast * 3.0) + 0.5, 0.0, 1.0);\n"
    "    float radius = sqrt(tone) * 0.48;\n"
    "    float edge = max(0.015, 1.0 / max(uCellSizePx, 1.0));\n"
    "    float dotMask = 1.0 - smoothstep(radius - edge, radius + edge, length(local));\n"
    "    vec3 srcStraight = srcPremul.rgb / srcPremul.a;\n"
    "    vec3 paper = vec3(1.0);\n"
    "    vec3 ink = uMonochrome == 1 ? vec3(0.0) : centerStraight;\n"
    "    vec3 halftone = mix(paper, ink, dotMask);\n"
    "    vec3 result = mix(srcStraight, halftone, uAmount);\n"
    "    result = clamp(result, 0.0, 1.0);\n"
    "    outColor = vec4(result * srcPremul.a, srcPremul.a);\n"
    "}\n";

typedef struct HalftonePass {
    RuwaEffectPipeline pipeline;
} HalftonePass;

void* stylize_halftone_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_stylize_host || !g_stylize_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = stylize_create_graphics(
        gpu, k_halftone_fs, "Ruwa.Standard.Stylize/stylize.halftone", "ruwa.standard.stylize.halftone.fs");
    if (!pipeline) {
        return NULL;
    }
    HalftonePass* pass = (HalftonePass*)malloc(sizeof(HalftonePass));
    if (!pass) {
        g_stylize_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

int32_t stylize_halftone_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* s
        = stylize_find_param(state->param_count, state->param_keys, state->param_values, "size");
    return stylize_clampi(s ? s->value.as_int : 14, HALFTONE_MIN_CELL, HALFTONE_MAX_CELL);
}

RuwaEffectTexture stylize_halftone_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    HalftonePass* pass = (HalftonePass*)pass_instance;
    if (!pass || !pass->pipeline || !g_stylize_host || !g_stylize_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0
        || input->region.valid == RUWA_FALSE) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;

    const float amount = stylize_clampf(stylize_real_param(input, "amount", 1.0f), 0.0f, 1.0f);
    const float cellSize = (float)stylize_clampi(stylize_int_param(input, "size", 14), HALFTONE_MIN_CELL, HALFTONE_MAX_CELL);
    if (amount <= 0.0f || cellSize <= 0.0f) {
        return input->source_texture;
    }
    const float angleRadians = stylize_real_param(input, "angle", 45.0f) * STYLIZE_PI / 180.0f;
    const float contrast = stylize_clampf(stylize_real_param(input, "contrast", 0.5f), 0.0f, 1.0f);
    const int monochrome = stylize_bool_param(input, "monochrome", 1);

    const StylizeDocFrame frame = stylize_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    stylize_set_doc_frame_forward(gl, gpu, &frame);
    stylize_set_doc_frame_inverse(gl, gpu, &frame);
    gl->set_uniform_float(gpu, "uCellSizePx", cellSize);
    gl->set_uniform_float(gpu, "uAngle", angleRadians);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->set_uniform_float(gpu, "uContrast", contrast);
    gl->set_uniform_int(gpu, "uMonochrome", monochrome);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);

    return input->target_texture;
}

void stylize_halftone_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    HalftonePass* pass = (HalftonePass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_stylize_host && g_stylize_host->gpu) {
        g_stylize_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
