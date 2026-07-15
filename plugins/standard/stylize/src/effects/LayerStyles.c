// SPDX-License-Identifier: MPL-2.0

/* Alpha-derived layer styles: Outer Glow, Inner Glow and Drop Shadow. */

#include "../StylizeCommon.h"
#include "../StylizeEffects.h"

#include <math.h>
#include <stdlib.h>

enum {
    LAYER_STYLE_OUTER_GLOW = 0,
    LAYER_STYLE_INNER_GLOW = 1,
    LAYER_STYLE_DROP_SHADOW = 2
};

static const char* const k_style_blur_fs =
    "#version 450 core\n"
    "const int kMaxTapPairs = 64;\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uTexelStep;\n"
    "uniform vec2 uTaps[kMaxTapPairs];\n"
    "uniform int uTapCount;\n"
    "uniform float uCenterWeight;\n"
    "uniform float uSpread;\n"
    "uniform int uExtractAlpha;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec4 styleSample(vec2 uv) {\n"
    "    vec4 sampleColor = texture(uSourceTexture, uv);\n"
    "    if (uExtractAlpha == 0) return sampleColor;\n"
    "    float mask = smoothstep(0.0, max(0.001, 1.0 - uSpread), sampleColor.a);\n"
    "    return vec4(mask);\n"
    "}\n"
    "void main() {\n"
    "    vec4 sum = styleSample(fragTexCoord) * uCenterWeight;\n"
    "    for (int i = 0; i < uTapCount; ++i) {\n"
    "        vec2 tap = uTaps[i];\n"
    "        vec2 offset = uTexelStep * tap.x;\n"
    "        sum += (styleSample(fragTexCoord + offset) + styleSample(fragTexCoord - offset)) * tap.y;\n"
    "    }\n"
    "    outColor = sum;\n"
    "}\n";

static const char* const k_style_composite_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform sampler2D uBlurredMask;\n"
    "uniform vec2 uOffsetUv;\n"
    "uniform vec4 uColor;\n"
    "uniform float uOpacity;\n"
    "uniform int uMode;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    vec4 source = texture(uSourceTexture, fragTexCoord);\n"
    "    vec2 maskUv = uMode == 2 ? fragTexCoord - uOffsetUv : fragTexCoord;\n"
    "    float blurred = clamp(texture(uBlurredMask, maskUv).r, 0.0, 1.0);\n"
    "    float strength = clamp(uOpacity * uColor.a, 0.0, 1.0);\n"
    "    if (uMode == 1) {\n"
    "        float blend = clamp((1.0 - blurred) * strength, 0.0, 1.0);\n"
    "        vec3 straight = source.a > 0.0001 ? clamp(source.rgb / source.a, 0.0, 1.0) : vec3(0.0);\n"
    "        vec3 tinted = mix(straight, clamp(uColor.rgb, 0.0, 1.0), blend);\n"
    "        outColor = vec4(tinted * source.a, source.a);\n"
    "        return;\n"
    "    }\n"
    "    float styleAlpha = blurred * strength * (1.0 - source.a);\n"
    "    vec3 stylePremul = clamp(uColor.rgb, 0.0, 1.0) * styleAlpha;\n"
    "    outColor = vec4(source.rgb + stylePremul, source.a + styleAlpha);\n"
    "}\n";

typedef struct LayerStylePass {
    int mode;
    RuwaEffectPipeline blur;
    RuwaEffectPipeline composite;
    RuwaEffectSampler sampler;
} LayerStylePass;

void* stylize_layer_style_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    if (!user_data || !g_stylize_host || !g_stylize_host->gpu) {
        return NULL;
    }
    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;
    RuwaEffectPipeline blur = stylize_create_graphics(gpu, k_style_blur_fs,
        "Ruwa.Standard.Stylize/layer-style.blur", "ruwa.standard.stylize.layer-style.blur.fs");
    RuwaEffectPipeline composite = stylize_create_graphics(gpu, k_style_composite_fs,
        "Ruwa.Standard.Stylize/layer-style.composite",
        "ruwa.standard.stylize.layer-style.composite.fs");
    if (!blur || !composite) {
        if (blur) gl->destroy_graphics_pipeline(gpu, blur);
        if (composite) gl->destroy_graphics_pipeline(gpu, composite);
        return NULL;
    }
    LayerStylePass* pass = (LayerStylePass*)calloc(1, sizeof(LayerStylePass));
    if (!pass) {
        gl->destroy_graphics_pipeline(gpu, blur);
        gl->destroy_graphics_pipeline(gpu, composite);
        return NULL;
    }
    pass->mode = *(const int*)user_data;
    pass->blur = blur;
    pass->composite = composite;
    pass->sampler = stylize_create_linear_sampler_or_null(gpu);
    return pass;
}

int32_t stylize_layer_style_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state)
{
    if (!user_data || !state) {
        return 0;
    }
    const int mode = *(const int*)user_data;
    const RuwaEffectParamValue* radiusValue
        = stylize_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    const int radius = stylize_clampi(
        radiusValue ? radiusValue->value.as_int : 16, 0, LAYER_STYLE_MAX_RADIUS);
    if (mode != LAYER_STYLE_DROP_SHADOW) {
        return radius;
    }
    const RuwaEffectParamValue* distanceValue
        = stylize_find_param(state->param_count, state->param_keys, state->param_values, "distance");
    const int distance = stylize_clampi(
        distanceValue ? distanceValue->value.as_int : 12, 0, LAYER_STYLE_MAX_DISTANCE);
    return radius + distance;
}

static void style_draw_blur_axis(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu,
    const LayerStylePass* pass, RuwaEffectTexture source, RuwaEffectTexture target,
    float stepX, float stepY, int extractAlpha, float spread)
{
    gl->begin_render_pass(gpu, target);
    gl->bind_graphics_pipeline(gpu, pass->blur);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uTexelStep", stepX, stepY);
    gl->set_uniform_int(gpu, "uExtractAlpha", extractAlpha);
    gl->set_uniform_float(gpu, "uSpread", spread);
    gl->bind_texture(gpu, 0, source, pass->sampler);
    gl->draw_fullscreen(gpu);
}

RuwaEffectTexture stylize_layer_style_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    LayerStylePass* pass = (LayerStylePass*)pass_instance;
    if (!pass || !pass->blur || !pass->composite || !g_stylize_host || !g_stylize_host->gpu
        || !input || !input->source_texture || !input->target_texture || input->output_width == 0
        || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }

    const int radius = stylize_clampi(
        stylize_int_param(input, "radius", 16), 0, LAYER_STYLE_MAX_RADIUS);
    const float opacity = stylize_clampf(stylize_real_param(input, "opacity", 0.75f), 0.0f, 1.0f);
    float color[4];
    stylize_color_param(input, "color", color, pass->mode == LAYER_STYLE_DROP_SHADOW ? 0.0f : 1.0f,
        pass->mode == LAYER_STYLE_DROP_SHADOW ? 0.0f : 1.0f,
        pass->mode == LAYER_STYLE_DROP_SHADOW ? 0.0f : 1.0f, 1.0f);
    if (opacity <= 0.0f || color[3] <= 0.0f) {
        return input->source_texture;
    }

    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;
    RuwaEffectTexture horizontal = gl->alloc_scratch_texture(gpu, RUWA_EFFECT_FORMAT_RGBA8);
    RuwaEffectTexture blurred = gl->alloc_scratch_texture(gpu, RUWA_EFFECT_FORMAT_RGBA8);
    if (!horizontal || !blurred) {
        return input->source_texture;
    }

    stylize_upload_gaussian_kernel(
        gl, gpu, pass->blur, radius, LAYER_STYLE_MAX_RADIUS);
    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const float spread = stylize_clampf(stylize_real_param(input, "spread", 0.0f), 0.0f, 1.0f);
    style_draw_blur_axis(gl, gpu, pass, input->source_texture, horizontal,
        scale / (float)input->output_width, 0.0f, 1, spread);
    style_draw_blur_axis(gl, gpu, pass, horizontal, blurred,
        0.0f, scale / (float)input->output_height, 0, spread);

    float offsetX = 0.0f;
    float offsetY = 0.0f;
    if (pass->mode == LAYER_STYLE_DROP_SHADOW) {
        const int distance = stylize_clampi(
            stylize_int_param(input, "distance", 12), 0, LAYER_STYLE_MAX_DISTANCE);
        const float angle = stylize_real_param(input, "angle", 45.0f) * STYLIZE_PI / 180.0f;
        offsetX = cosf(angle) * (float)distance * scale / (float)input->output_width;
        offsetY = sinf(angle) * (float)distance * scale / (float)input->output_height;
    }

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->composite);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_int(gpu, "uBlurredMask", 1);
    gl->set_uniform_float2(gpu, "uOffsetUv", offsetX, offsetY);
    gl->set_uniform_float4(gpu, "uColor", color[0], color[1], color[2], color[3]);
    gl->set_uniform_float(gpu, "uOpacity", opacity);
    gl->set_uniform_int(gpu, "uMode", pass->mode);
    gl->bind_texture(gpu, 0, input->source_texture, pass->sampler);
    gl->bind_texture(gpu, 1, blurred, pass->sampler);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
    return input->target_texture;
}

void stylize_layer_style_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    LayerStylePass* pass = (LayerStylePass*)pass_instance;
    if (!pass) {
        return;
    }
    if (g_stylize_host && g_stylize_host->gpu) {
        const RuwaEffectGpuApi* gl = g_stylize_host->gpu;
        if (pass->sampler) gl->destroy_sampler(gpu, pass->sampler);
        if (pass->blur) gl->destroy_graphics_pipeline(gpu, pass->blur);
        if (pass->composite) gl->destroy_graphics_pipeline(gpu, pass->composite);
    }
    free(pass);
}
