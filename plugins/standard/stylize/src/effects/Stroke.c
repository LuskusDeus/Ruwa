// SPDX-License-Identifier: MPL-2.0

/* Stroke: isolated implementation module for Ruwa.Standard.Stylize. */

#include "../StylizeCommon.h"
#include "../StylizeEffects.h"

#include <stdlib.h>

/* ==========================================================================
 *   Stroke (Outline) — shape-driven via layer_alpha_texture
 * ========================================================================== */

static const char* const k_stroke_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform sampler2D uShapeTexture;\n"
    "uniform vec2 uTexelStep;\n"
    "uniform int uRadius;\n"
    "uniform int uOffset;\n"
    "uniform int uPosition;\n"
    "uniform vec4 uColor;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "const int kMaxAngular = 24;\n"
    "const int kMaxRadius = 128;\n"
    "vec2 morphCoverage(vec2 uv, int radius) {\n"
    "    float dilate = texture(uShapeTexture, uv).a;\n"
    "    float erode = dilate;\n"
    "    radius = min(radius, kMaxRadius);\n"
    "    if (radius <= 0) { return vec2(dilate, erode); }\n"
    "    for (int r = 2; r <= radius; r += 2) {\n"
    "        float fr = float(r);\n"
    "        for (int a = 0; a < kMaxAngular; ++a) {\n"
    "            float ang = (6.2831853 * float(a)) / float(kMaxAngular);\n"
    "            vec2 off = vec2(cos(ang), sin(ang)) * fr;\n"
    "            float s = texture(uShapeTexture, uv + off * uTexelStep).a;\n"
    "            dilate = max(dilate, s);\n"
    "            erode = min(erode, s);\n"
    "        }\n"
    "    }\n"
    "    return vec2(dilate, erode);\n"
    "}\n"
    "float signedCoverage(vec2 uv, int r) {\n"
    "    return r >= 0 ? morphCoverage(uv, r).x : morphCoverage(uv, -r).y;\n"
    "}\n"
    "void main() {\n"
    "    vec4 srcPremul = texture(uSourceTexture, fragTexCoord);\n"
    "    int rNear;\n"
    "    int rFar;\n"
    "    if (uPosition == 2) {\n"
    "        rNear = -uRadius - uOffset;\n"
    "        rFar = -uOffset;\n"
    "    } else if (uPosition == 1) {\n"
    "        int halfRadius = max(uRadius / 2, 1);\n"
    "        rNear = uOffset - halfRadius;\n"
    "        rFar = uOffset + halfRadius;\n"
    "    } else {\n"
    "        rNear = uOffset;\n"
    "        rFar = uOffset + uRadius;\n"
    "    }\n"
    "    float strokeMask = clamp(signedCoverage(fragTexCoord, rFar)\n"
    "                            - signedCoverage(fragTexCoord, rNear), 0.0, 1.0);\n"
    "    float strokeA = clamp(strokeMask * uColor.a, 0.0, 1.0);\n"
    "    vec3 strokePremul = uColor.rgb * strokeA;\n"
    "    outColor.rgb = strokePremul + srcPremul.rgb * (1.0 - strokeA);\n"
    "    outColor.a = strokeA + srcPremul.a * (1.0 - strokeA);\n"
    "}\n";

typedef struct StrokePass {
    RuwaEffectPipeline pipeline;
} StrokePass;

void* stylize_stroke_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_stylize_host || !g_stylize_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = stylize_create_graphics(
        gpu, k_stroke_fs, "Ruwa.Standard.Stylize/stroke.outline", "ruwa.standard.stylize.stroke.fs");
    if (!pipeline) {
        return NULL;
    }
    StrokePass* pass = (StrokePass*)malloc(sizeof(StrokePass));
    if (!pass) {
        g_stylize_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

int32_t stylize_stroke_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* sv
        = stylize_find_param(state->param_count, state->param_keys, state->param_values, "size");
    const RuwaEffectParamValue* ov
        = stylize_find_param(state->param_count, state->param_keys, state->param_values, "offset");
    const int size = stylize_clampi(sv ? sv->value.as_int : 4, 0, STROKE_MAX_RADIUS);
    const int offset = stylize_clampi(ov ? ov->value.as_int : 0, -STROKE_MAX_RADIUS, STROKE_MAX_RADIUS);
    return size + (offset < 0 ? -offset : offset);
}

RuwaEffectTexture stylize_stroke_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    StrokePass* pass = (StrokePass*)pass_instance;
    if (!pass || !pass->pipeline || !g_stylize_host || !g_stylize_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;

    const int size = stylize_clampi(stylize_int_param(input, "size", 4), 0, STROKE_MAX_RADIUS);
    const int offset = stylize_clampi(stylize_int_param(input, "offset", 0), -STROKE_MAX_RADIUS, STROKE_MAX_RADIUS);
    if (size <= 0 && offset == 0) {
        return input->source_texture;
    }
    /* The shape is the layer's pre-effect coverage; fall back to the in-flight
     * source when no distinct shape texture is bound. */
    RuwaEffectTexture shape
        = input->layer_alpha_texture ? input->layer_alpha_texture : input->source_texture;
    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const float stepX = scale / (float)input->output_width;
    const float stepY = scale / (float)input->output_height;
    float color[4];
    stylize_color_param(input, "color", color, 0.0f, 0.0f, 0.0f, 1.0f);
    const int position = stylize_choice_param(input, "position", 0);

    gl->begin_render_pass(gpu, input->target_texture);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_int(gpu, "uShapeTexture", 1);
    gl->set_uniform_float2(gpu, "uTexelStep", stepX, stepY);
    gl->set_uniform_int(gpu, "uRadius", size);
    gl->set_uniform_int(gpu, "uOffset", offset);
    gl->set_uniform_int(gpu, "uPosition", position);
    gl->set_uniform_float4(gpu, "uColor", color[0], color[1], color[2], color[3]);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->bind_texture(gpu, 1, shape, NULL);
    gl->draw_fullscreen(gpu);

    return input->target_texture;
}

void stylize_stroke_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    StrokePass* pass = (StrokePass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_stylize_host && g_stylize_host->gpu) {
        g_stylize_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
