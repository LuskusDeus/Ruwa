// SPDX-License-Identifier: MPL-2.0

/* Glow: isolated implementation module for Ruwa.Standard.Stylize. */

#include "../StylizeCommon.h"
#include "../StylizeEffects.h"

#include <math.h>
#include <stdlib.h>

/* ==========================================================================
 *   Glow
 * ========================================================================== */

static const char* const k_glow_extract_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform float uThreshold;\n"
    "uniform float uSoftness;\n"
    "uniform vec4 uTint;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec3 srgbToLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }\n"
    "float max3(vec3 v) { return max(v.r, max(v.g, v.b)); }\n"
    "void main() {\n"
    "    vec4 src = texture(uSourceTexture, fragTexCoord);\n"
    "    float alpha = clamp(src.a, 0.0, 1.0);\n"
    "    if (alpha <= 0.0 || uTint.a <= 0.0) { outColor = vec4(0.0); return; }\n"
    "    float maxRgb = max3(src.rgb);\n"
    "    bool looksPremultiplied = maxRgb <= alpha + 0.002;\n"
    "    vec3 straight = looksPremultiplied ? src.rgb / max(alpha, 1e-4) : src.rgb;\n"
    "    straight = clamp(straight, vec3(0.0), vec3(1.0));\n"
    "    vec3 linearColor = srgbToLinear(clamp(straight, vec3(0.0), vec3(1.0)));\n"
    "    vec3 linearTint = srgbToLinear(clamp(uTint.rgb, vec3(0.0), vec3(1.0)));\n"
    "    float luminance = dot(linearColor, vec3(0.2126, 0.7152, 0.0722));\n"
    "    float mask = 1.0;\n"
    "    if (uThreshold > 0.0) {\n"
    "        float knee = max(uSoftness, 0.001);\n"
    "        mask = smoothstep(uThreshold - knee, uThreshold + knee, luminance);\n"
    "    }\n"
    "    vec3 emitted = linearColor * linearTint * (alpha * mask * uTint.a);\n"
    "    outColor = vec4(emitted, max3(emitted));\n"
    "}\n";

static const char* const k_glow_downsample_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uSourceTexelSize;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    vec2 t = uSourceTexelSize;\n"
    "    vec4 sum = texture(uSourceTexture, fragTexCoord) * 4.0;\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( t.x,  0.0)) * 2.0;\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2(-t.x,  0.0)) * 2.0;\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( 0.0,  t.y)) * 2.0;\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( 0.0, -t.y)) * 2.0;\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( t.x,  t.y));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2(-t.x,  t.y));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( t.x, -t.y));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2(-t.x, -t.y));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( 2.0 * t.x,  0.0));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2(-2.0 * t.x,  0.0));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( 0.0,  2.0 * t.y));\n"
    "    sum += texture(uSourceTexture, fragTexCoord + vec2( 0.0, -2.0 * t.y));\n"
    "    outColor = sum / 24.0;\n"
    "}\n";

static const char* const k_glow_blur_fs =
    "#version 450 core\n"
    "const int kMaxTapPairs = 16;\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uTexelStep;\n"
    "uniform vec2 uTaps[kMaxTapPairs];\n"
    "uniform int uTapCount;\n"
    "uniform float uCenterWeight;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    vec4 sum = texture(uSourceTexture, fragTexCoord) * uCenterWeight;\n"
    "    for (int i = 0; i < uTapCount; ++i) {\n"
    "        vec2 tap = uTaps[i];\n"
    "        vec2 offset = uTexelStep * tap.x;\n"
    "        sum += (texture(uSourceTexture, fragTexCoord + offset)\n"
    "                + texture(uSourceTexture, fragTexCoord - offset)) * tap.y;\n"
    "    }\n"
    "    outColor = sum;\n"
    "}\n";

static const char* const k_glow_composite_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform sampler2D uGlow0;\n"
    "uniform sampler2D uGlow1;\n"
    "uniform sampler2D uGlow2;\n"
    "uniform sampler2D uGlow3;\n"
    "uniform sampler2D uGlow4;\n"
    "uniform sampler2D uGlow5;\n"
    "uniform sampler2D uGlow6;\n"
    "uniform sampler2D uGlow7;\n"
    "uniform int uLevelCount;\n"
    "uniform int uBlendMode;\n"
    "uniform float uIntensity;\n"
    "uniform float uWeight0;\n"
    "uniform float uWeight1;\n"
    "uniform float uWeight2;\n"
    "uniform float uWeight3;\n"
    "uniform float uWeight4;\n"
    "uniform float uWeight5;\n"
    "uniform float uWeight6;\n"
    "uniform float uWeight7;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec3 linearToSrgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }\n"
    "vec3 glowToDisplay(vec3 linearLight) {\n"
    "    vec3 mapped = vec3(1.0) - exp(-max(linearLight, vec3(0.0)));\n"
    "    return linearToSrgb(mapped);\n"
    "}\n"
    "float max3(vec3 v) { return max(v.r, max(v.g, v.b)); }\n"
    "vec3 screen(vec3 base, vec3 light) { return 1.0 - (1.0 - base) * (1.0 - light); }\n"
    "vec4 glowSample() {\n"
    "    vec4 g = texture(uGlow0, fragTexCoord) * uWeight0;\n"
    "    if (uLevelCount > 1) g += texture(uGlow1, fragTexCoord) * uWeight1;\n"
    "    if (uLevelCount > 2) g += texture(uGlow2, fragTexCoord) * uWeight2;\n"
    "    if (uLevelCount > 3) g += texture(uGlow3, fragTexCoord) * uWeight3;\n"
    "    if (uLevelCount > 4) g += texture(uGlow4, fragTexCoord) * uWeight4;\n"
    "    if (uLevelCount > 5) g += texture(uGlow5, fragTexCoord) * uWeight5;\n"
    "    if (uLevelCount > 6) g += texture(uGlow6, fragTexCoord) * uWeight6;\n"
    "    if (uLevelCount > 7) g += texture(uGlow7, fragTexCoord) * uWeight7;\n"
    "    return g * uIntensity;\n"
    "}\n"
    "void main() {\n"
    "    vec4 src = texture(uSourceTexture, fragTexCoord);\n"
    "    vec4 glowLinear = glowSample();\n"
    "    vec3 glowEnergy = max(glowLinear.rgb, vec3(0.0));\n"
    "    vec3 glowRgb = glowToDisplay(glowEnergy);\n"
    "    float glowA = clamp(1.0 - exp(-max3(glowEnergy) * 0.45), 0.0, 0.92);\n"
    "    vec3 glowPremul = glowRgb * glowA;\n"
    "    vec3 srcStraight = src.a > 0.0001 ? clamp(src.rgb / src.a, vec3(0.0), vec3(1.0)) : vec3(0.0);\n"
    "    if (uBlendMode == 1) {\n"
    "        vec3 litSource = min(srcStraight + glowRgb, vec3(1.0)) * src.a;\n"
    "        vec3 rgb = litSource + glowPremul * (1.0 - src.a);\n"
    "        float alpha = clamp(src.a + glowA * (1.0 - src.a), 0.0, 1.0);\n"
    "        outColor = vec4(min(rgb, vec3(alpha)), alpha);\n"
    "        return;\n"
    "    }\n"
    "    vec3 litSource = screen(srcStraight, glowRgb) * src.a;\n"
    "    vec3 rgb = litSource + glowPremul * (1.0 - src.a);\n"
    "    float alpha = clamp(src.a + glowA * (1.0 - src.a), 0.0, 1.0);\n"
    "    outColor = vec4(min(rgb, vec3(alpha)), alpha);\n"
    "}\n";

typedef struct GlowLevel {
    RuwaEffectTexture glow;
    RuwaEffectTexture scratch;
    uint32_t width, height;
} GlowLevel;

typedef struct GlowPass {
    RuwaEffectPipeline extract, downsample, blur, composite;
    RuwaEffectSampler sampler;
    GlowLevel levels[GLOW_MAX_LEVELS];
    uint32_t pyramidW, pyramidH;
} GlowPass;

static int glow_level_count_for_radius(int radius, uint32_t width, uint32_t height)
{
    if (radius <= 0 || width == 0 || height == 0) {
        return 0;
    }
    const uint32_t maxDim = width > height ? width : height;
    int count = 1;
    while (count < GLOW_MAX_LEVELS && (6 << (count - 1)) < radius && (maxDim >> count) > 1u) {
        ++count;
    }
    return count < 1 ? 1 : count;
}

static int glow_blur_radius_for_level(int radius, int level)
{
    const int scale = 1 << level;
    int radiusAtLevel = (int)ceilf((float)radius / (float)scale);
    if (radiusAtLevel < 1) {
        radiusAtLevel = 1;
    }
    const int baseRadius = 4 + stylize_clampi(radius / 96, 0, 4);
    const int hi = GLOW_MAX_BLUR_RADIUS < radiusAtLevel ? GLOW_MAX_BLUR_RADIUS : radiusAtLevel;
    return stylize_clampi(baseRadius, 1, hi);
}

static void glow_destroy_pyramid(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, GlowPass* pass)
{
    for (int i = 0; i < GLOW_MAX_LEVELS; ++i) {
        if (pass->levels[i].glow) {
            gl->destroy_texture(gpu, pass->levels[i].glow);
            pass->levels[i].glow = NULL;
        }
        if (pass->levels[i].scratch) {
            gl->destroy_texture(gpu, pass->levels[i].scratch);
            pass->levels[i].scratch = NULL;
        }
        pass->levels[i].width = 0;
        pass->levels[i].height = 0;
    }
    pass->pyramidW = 0;
    pass->pyramidH = 0;
}

static int glow_ensure_pyramid(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, GlowPass* pass, uint32_t width, uint32_t height)
{
    if (pass->pyramidW == width && pass->pyramidH == height && pass->levels[0].glow
        && pass->levels[0].scratch) {
        return 1;
    }
    glow_destroy_pyramid(gl, gpu, pass);

    uint32_t levelWidth = width;
    uint32_t levelHeight = height;
    for (int i = 0; i < GLOW_MAX_LEVELS; ++i) {
        pass->levels[i].width = levelWidth > 1u ? levelWidth : 1u;
        pass->levels[i].height = levelHeight > 1u ? levelHeight : 1u;
        pass->levels[i].glow = gl->create_texture(
            gpu, pass->levels[i].width, pass->levels[i].height, RUWA_EFFECT_FORMAT_RGBA16F);
        pass->levels[i].scratch = gl->create_texture(
            gpu, pass->levels[i].width, pass->levels[i].height, RUWA_EFFECT_FORMAT_RGBA16F);
        if (!pass->levels[i].glow || !pass->levels[i].scratch) {
            glow_destroy_pyramid(gl, gpu, pass);
            return 0;
        }
        levelWidth = (levelWidth / 2u) > 1u ? (levelWidth / 2u) : 1u;
        levelHeight = (levelHeight / 2u) > 1u ? (levelHeight / 2u) : 1u;
    }
    pass->pyramidW = width;
    pass->pyramidH = height;
    return 1;
}

void* stylize_glow_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_stylize_host || !g_stylize_host->gpu) {
        return NULL;
    }
    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;
    RuwaEffectPipeline extract = stylize_create_graphics(
        gpu, k_glow_extract_fs, "Ruwa.Standard.Stylize/glow.extract", "ruwa.standard.stylize.glow.extract.fs");
    RuwaEffectPipeline downsample = stylize_create_graphics(gpu, k_glow_downsample_fs,
        "Ruwa.Standard.Stylize/glow.downsample", "ruwa.standard.stylize.glow.downsample.fs");
    RuwaEffectPipeline blur = stylize_create_graphics(
        gpu, k_glow_blur_fs, "Ruwa.Standard.Stylize/glow.blur", "ruwa.standard.stylize.glow.blur.fs");
    RuwaEffectPipeline composite = stylize_create_graphics(gpu, k_glow_composite_fs,
        "Ruwa.Standard.Stylize/glow.composite", "ruwa.standard.stylize.glow.composite.fs");
    if (!extract || !downsample || !blur || !composite) {
        if (extract) {
            gl->destroy_graphics_pipeline(gpu, extract);
        }
        if (downsample) {
            gl->destroy_graphics_pipeline(gpu, downsample);
        }
        if (blur) {
            gl->destroy_graphics_pipeline(gpu, blur);
        }
        if (composite) {
            gl->destroy_graphics_pipeline(gpu, composite);
        }
        return NULL;
    }
    GlowPass* pass = (GlowPass*)calloc(1, sizeof(GlowPass));
    if (!pass) {
        gl->destroy_graphics_pipeline(gpu, extract);
        gl->destroy_graphics_pipeline(gpu, downsample);
        gl->destroy_graphics_pipeline(gpu, blur);
        gl->destroy_graphics_pipeline(gpu, composite);
        return NULL;
    }
    pass->extract = extract;
    pass->downsample = downsample;
    pass->blur = blur;
    pass->composite = composite;
    pass->sampler = stylize_create_linear_sampler_or_null(gpu);
    return pass;
}

int32_t stylize_glow_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* r
        = stylize_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    return stylize_clampi(r ? r->value.as_int : 64, 0, GLOW_MAX_RADIUS);
}

static void glow_draw_extract(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, GlowPass* pass,
    RuwaEffectTexture source, float threshold, float softness, const float tint[4])
{
    gl->begin_render_pass(gpu, pass->levels[0].glow);
    gl->bind_graphics_pipeline(gpu, pass->extract);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float(gpu, "uThreshold", threshold);
    gl->set_uniform_float(gpu, "uSoftness", softness);
    gl->set_uniform_float4(gpu, "uTint", tint[0], tint[1], tint[2], tint[3]);
    gl->bind_texture(gpu, 0, source, pass->sampler);
    gl->draw_fullscreen(gpu);
}

static void glow_draw_downsample(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, GlowPass* pass, int level)
{
    const GlowLevel* src = &pass->levels[level - 1];
    gl->begin_render_pass(gpu, pass->levels[level].glow);
    gl->bind_graphics_pipeline(gpu, pass->downsample);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(
        gpu, "uSourceTexelSize", 1.0f / (float)src->width, 1.0f / (float)src->height);
    gl->bind_texture(gpu, 0, src->glow, pass->sampler);
    gl->draw_fullscreen(gpu);
}

static void glow_draw_blur_axis(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, GlowPass* pass,
    int level, RuwaEffectTexture src, RuwaEffectTexture target, int radius, int horizontal)
{
    const GlowLevel* lv = &pass->levels[level];
    gl->begin_render_pass(gpu, target);
    stylize_upload_gaussian_kernel(
        gl, gpu, pass->blur, radius, GLOW_MAX_BLUR_RADIUS);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uTexelStep", horizontal ? 1.0f / (float)lv->width : 0.0f,
        horizontal ? 0.0f : 1.0f / (float)lv->height);
    gl->bind_texture(gpu, 0, src, pass->sampler);
    gl->draw_fullscreen(gpu);
}

static void glow_draw_composite(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, GlowPass* pass,
    RuwaEffectTexture source, RuwaEffectTexture target, int levelCount, const float weights[8],
    float intensity, int blendMode)
{
    gl->begin_render_pass(gpu, target);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->composite);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_int(gpu, "uGlow0", 1);
    gl->set_uniform_int(gpu, "uGlow1", 2);
    gl->set_uniform_int(gpu, "uGlow2", 3);
    gl->set_uniform_int(gpu, "uGlow3", 4);
    gl->set_uniform_int(gpu, "uGlow4", 5);
    gl->set_uniform_int(gpu, "uGlow5", 6);
    gl->set_uniform_int(gpu, "uGlow6", 7);
    gl->set_uniform_int(gpu, "uGlow7", 8);
    gl->set_uniform_int(gpu, "uLevelCount", levelCount);
    gl->set_uniform_float(gpu, "uIntensity", intensity);
    gl->set_uniform_int(gpu, "uBlendMode", blendMode);
    gl->set_uniform_float(gpu, "uWeight0", weights[0]);
    gl->set_uniform_float(gpu, "uWeight1", weights[1]);
    gl->set_uniform_float(gpu, "uWeight2", weights[2]);
    gl->set_uniform_float(gpu, "uWeight3", weights[3]);
    gl->set_uniform_float(gpu, "uWeight4", weights[4]);
    gl->set_uniform_float(gpu, "uWeight5", weights[5]);
    gl->set_uniform_float(gpu, "uWeight6", weights[6]);
    gl->set_uniform_float(gpu, "uWeight7", weights[7]);
    gl->bind_texture(gpu, 0, source, pass->sampler);
    for (int i = 0; i < GLOW_MAX_LEVELS; ++i) {
        gl->bind_texture(gpu, (uint32_t)(1 + i), pass->levels[i].glow, pass->sampler);
    }
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
}

RuwaEffectTexture stylize_glow_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    GlowPass* pass = (GlowPass*)pass_instance;
    if (!pass || !pass->extract || !pass->downsample || !pass->blur || !pass->composite || !g_stylize_host
        || !g_stylize_host->gpu || !input || !input->source_texture || !input->target_texture
        || input->output_width == 0 || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_stylize_host->gpu;

    const int documentRadius = stylize_clampi(stylize_int_param(input, "radius", 64), 0, GLOW_MAX_RADIUS);
    const float intensity = stylize_clampf(stylize_real_param(input, "intensity", 1.5f), 0.0f, 16.0f);
    float tint[4];
    stylize_color_param(input, "color", tint, 1.0f, 1.0f, 1.0f, 1.0f);
    if (documentRadius <= 0 || intensity <= 0.0f || tint[3] <= 0.0f) {
        return input->source_texture;
    }

    const float spaceScale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const int screenRadius
        = stylize_clampi((int)ceilf((float)documentRadius * spaceScale), 1, GLOW_MAX_RADIUS);
    const int levelCount
        = glow_level_count_for_radius(screenRadius, input->output_width, input->output_height);
    if (levelCount <= 0
        || !glow_ensure_pyramid(gl, gpu, pass, input->output_width, input->output_height)) {
        return input->source_texture;
    }

    const float threshold = stylize_clampf(stylize_real_param(input, "threshold", 0.0f), 0.0f, 1.0f);
    const float softness = stylize_clampf(stylize_real_param(input, "softness", 0.35f), 0.0f, 1.0f);
    const float falloff = stylize_clampf(stylize_real_param(input, "falloff", 1.2f), 0.35f, 3.0f);
    const int blendMode = stylize_choice_param(input, "blend", 0);

    glow_draw_extract(gl, gpu, pass, input->source_texture, threshold, softness, tint);
    for (int level = 1; level < levelCount; ++level) {
        glow_draw_downsample(gl, gpu, pass, level);
    }

    float weights[GLOW_MAX_LEVELS] = { 0.0f };
    float totalWeight = 0.0f;
    for (int level = 0; level < levelCount; ++level) {
        const int blurRadius = glow_blur_radius_for_level(screenRadius, level);
        glow_draw_blur_axis(
            gl, gpu, pass, level, pass->levels[level].glow, pass->levels[level].scratch, blurRadius, 1);
        glow_draw_blur_axis(
            gl, gpu, pass, level, pass->levels[level].scratch, pass->levels[level].glow, blurRadius, 0);
        const float effectiveRadius = (float)(blurRadius * (1 << level));
        const float t = effectiveRadius / (float)(screenRadius < 1 ? 1 : screenRadius);
        const float w = 1.0f / powf(1.0f + t * t, falloff);
        weights[level] = w;
        totalWeight += w;
    }
    if (totalWeight > 0.0f) {
        for (int i = 0; i < levelCount; ++i) {
            weights[i] /= totalWeight;
        }
    }

    glow_draw_composite(gl, gpu, pass, input->source_texture, input->target_texture, levelCount,
        weights, intensity, blendMode);
    return input->target_texture;
}

void stylize_glow_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    GlowPass* pass = (GlowPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (g_stylize_host && g_stylize_host->gpu) {
        const RuwaEffectGpuApi* gl = g_stylize_host->gpu;
        glow_destroy_pyramid(gl, gpu, pass);
        if (pass->sampler) {
            gl->destroy_sampler(gpu, pass->sampler);
        }
        if (pass->extract) {
            gl->destroy_graphics_pipeline(gpu, pass->extract);
        }
        if (pass->downsample) {
            gl->destroy_graphics_pipeline(gpu, pass->downsample);
        }
        if (pass->blur) {
            gl->destroy_graphics_pipeline(gpu, pass->blur);
        }
        if (pass->composite) {
            gl->destroy_graphics_pipeline(gpu, pass->composite);
        }
    }
    free(pass);
}
