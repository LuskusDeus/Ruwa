// SPDX-License-Identifier: MPL-2.0

/* Shared pass for the standard non-neighbouring color adjustments. */

#include "../ColorCommon.h"
#include "../ColorEffects.h"

#include <stdlib.h>
#include <string.h>

/* `fragTexCoord` is supplied by the host's shared vertex shader. Identity-valued
 * branches are skipped so additions here do not perturb existing effects. */
static const char* const k_color_adjust_fs =
    "#version 450 core\n"
    "\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform float uBrightness;\n"
    "uniform float uContrast;\n"
    "uniform float uHue;\n"
    "uniform float uSaturation;\n"
    "uniform float uGamma;\n"
    "uniform float uExposure;\n"
    "uniform float uBlackWhiteAmount;\n"
    "uniform vec3 uBlackWhiteWeights;\n"
    "\n"
    "in vec2 fragTexCoord;\n"
    "\n"
    "out vec4 outColor;\n"
    "\n"
    "vec3 srgbToLinear(vec3 color) {\n"
    "    bvec3 low = lessThanEqual(color, vec3(0.04045));\n"
    "    vec3 linearLow = color / 12.92;\n"
    "    vec3 linearHigh = pow((color + 0.055) / 1.055, vec3(2.4));\n"
    "    return mix(linearHigh, linearLow, low);\n"
    "}\n"
    "\n"
    "vec3 linearToSrgb(vec3 color) {\n"
    "    color = max(color, vec3(0.0));\n"
    "    bvec3 low = lessThanEqual(color, vec3(0.0031308));\n"
    "    vec3 srgbLow = color * 12.92;\n"
    "    vec3 srgbHigh = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;\n"
    "    return mix(srgbHigh, srgbLow, low);\n"
    "}\n"
    "\n"
    "vec3 rgbToHsl(vec3 color) {\n"
    "    float maxChannel = max(max(color.r, color.g), color.b);\n"
    "    float minChannel = min(min(color.r, color.g), color.b);\n"
    "    float chroma = maxChannel - minChannel;\n"
    "    float lightness = (maxChannel + minChannel) * 0.5;\n"
    "    float hue = 0.0;\n"
    "    float saturation = 0.0;\n"
    "\n"
    "    if (chroma > 1e-5) {\n"
    "        saturation = chroma / (1.0 - abs(2.0 * lightness - 1.0));\n"
    "        if (maxChannel == color.r) {\n"
    "            hue = mod((color.g - color.b) / chroma, 6.0);\n"
    "        } else if (maxChannel == color.g) {\n"
    "            hue = (color.b - color.r) / chroma + 2.0;\n"
    "        } else {\n"
    "            hue = (color.r - color.g) / chroma + 4.0;\n"
    "        }\n"
    "        hue /= 6.0;\n"
    "    }\n"
    "\n"
    "    return vec3(hue, saturation, lightness);\n"
    "}\n"
    "\n"
    "float hueToRgb(float p, float q, float t) {\n"
    "    t = fract(t);\n"
    "    if (t < 1.0 / 6.0) {\n"
    "        return p + (q - p) * 6.0 * t;\n"
    "    }\n"
    "    if (t < 1.0 / 2.0) {\n"
    "        return q;\n"
    "    }\n"
    "    if (t < 2.0 / 3.0) {\n"
    "        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;\n"
    "    }\n"
    "    return p;\n"
    "}\n"
    "\n"
    "vec3 hslToRgb(vec3 hsl) {\n"
    "    float hue = fract(hsl.x);\n"
    "    float saturation = clamp(hsl.y, 0.0, 1.0);\n"
    "    float lightness = clamp(hsl.z, 0.0, 1.0);\n"
    "\n"
    "    if (saturation <= 1e-5) {\n"
    "        return vec3(lightness);\n"
    "    }\n"
    "\n"
    "    float q = lightness < 0.5\n"
    "        ? lightness * (1.0 + saturation)\n"
    "        : lightness + saturation - lightness * saturation;\n"
    "    float p = 2.0 * lightness - q;\n"
    "\n"
    "    return vec3(\n"
    "        hueToRgb(p, q, hue + 1.0 / 3.0),\n"
    "        hueToRgb(p, q, hue),\n"
    "        hueToRgb(p, q, hue - 1.0 / 3.0));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 premul = texture(uSourceTexture, fragTexCoord);\n"
    "    if (premul.a <= 0.0) {\n"
    "        outColor = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec3 color = premul.rgb / premul.a;\n"
    "    color += vec3(uBrightness);\n"
    "\n"
    "    float contrastScale = max(0.0, 1.0 + uContrast);\n"
    "    color = (color - vec3(0.5)) * contrastScale + vec3(0.5);\n"
    "\n"
    "    if (abs(uGamma - 1.0) > 1e-5) {\n"
    "        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / max(uGamma, 0.001)));\n"
    "    }\n"
    "\n"
    "    if (abs(uExposure) > 1e-5) {\n"
    "        vec3 linearColor = srgbToLinear(clamp(color, 0.0, 1.0));\n"
    "        color = linearToSrgb(linearColor * exp2(uExposure));\n"
    "    }\n"
    "\n"
    "    vec3 hsl = rgbToHsl(clamp(color, 0.0, 1.0));\n"
    "    hsl.x = fract(hsl.x + uHue / 360.0);\n"
    "    hsl.y = clamp(hsl.y * max(0.0, 1.0 + uSaturation), 0.0, 1.0);\n"
    "    color = hslToRgb(hsl);\n"
    "\n"
    "    if (uBlackWhiteAmount > 1e-5) {\n"
    "        vec3 weights = max(uBlackWhiteWeights, vec3(0.0));\n"
    "        float weightSum = weights.r + weights.g + weights.b;\n"
    "        weights = weightSum > 1e-5 ? weights / weightSum : vec3(0.2126, 0.7152, 0.0722);\n"
    "        vec3 linearColor = srgbToLinear(clamp(color, 0.0, 1.0));\n"
    "        float grayLinear = dot(linearColor, weights);\n"
    "        float gray = linearToSrgb(vec3(grayLinear)).r;\n"
    "        color = mix(color, vec3(gray), clamp(uBlackWhiteAmount, 0.0, 1.0));\n"
    "    }\n"
    "\n"
    "    color = clamp(color, 0.0, 1.0);\n"
    "    outColor = vec4(color * premul.a, premul.a);\n"
    "}\n";

/* All descriptors share one pass type and one cached GPU program. */
typedef struct ColorAdjustPass {
    RuwaEffectPipeline pipeline;
} ColorAdjustPass;

void* color_adjust_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_color_host || !g_color_host->gpu) {
        return NULL;
    }
    const RuwaEffectGpuApi* gl = g_color_host->gpu;

    RuwaEffectShaderSource fragment;
    fragment.struct_size   = sizeof(RuwaEffectShaderSource);
    fragment.stage         = RUWA_EFFECT_STAGE_FRAGMENT;
    fragment.source        = k_color_adjust_fs;
    fragment.source_length = 0u; /* null-terminated */
    fragment.debug_name    = "Ruwa.Standard.Color/color_adjust";
    /* Shared cache key: both effects link the one program the host caches. */
    fragment.cache_key     = "ruwa.standard.color.color_adjust.fs";

    RuwaEffectPipeline pipeline = gl->create_graphics_pipeline(gpu, &fragment);
    if (!pipeline) {
        if (g_color_host->log) {
            g_color_host->log(RUWA_EFFECT_LOG_ERROR,
                "Ruwa.Standard.Color: color adjust pipeline failed to compile");
        }
        return NULL;
    }

    ColorAdjustPass* pass = (ColorAdjustPass*)malloc(sizeof(ColorAdjustPass));
    if (!pass) {
        gl->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

RuwaEffectTexture color_adjust_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    ColorAdjustPass* pass = (ColorAdjustPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_color_host || !g_color_host->gpu || !input
        || !input->source_texture || !input->target_texture) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_color_host->gpu;

    /* Read the parameters this descriptor exposes; every other adjustment
     * remains at its identity value. */
    float brightness = 0.0f;
    float contrast = 0.0f;
    float hue = 0.0f;
    float saturation = 0.0f;
    float gamma = 1.0f;
    float exposure = 0.0f;
    float blackWhiteAmount = 0.0f;
    float blackWhiteRed = 0.2126f;
    float blackWhiteGreen = 0.7152f;
    float blackWhiteBlue = 0.0722f;
    for (uint32_t i = 0; i < input->param_count; ++i) {
        const char* key = input->param_keys[i];
        const RuwaEffectParamValue* v = &input->param_values[i];
        if (!key) {
            continue;
        }
        if (strcmp(key, "brightness") == 0) {
            brightness = (float)v->value.as_real;
        } else if (strcmp(key, "contrast") == 0) {
            contrast = (float)v->value.as_real;
        } else if (strcmp(key, "hue") == 0) {
            hue = (float)v->value.as_real;
        } else if (strcmp(key, "saturation") == 0) {
            saturation = (float)v->value.as_real;
        } else if (strcmp(key, "gamma") == 0) {
            gamma = (float)v->value.as_real;
        } else if (strcmp(key, "exposure") == 0) {
            exposure = (float)v->value.as_real;
        } else if (strcmp(key, "blackWhiteAmount") == 0) {
            blackWhiteAmount = (float)v->value.as_real;
        } else if (strcmp(key, "redWeight") == 0) {
            blackWhiteRed = (float)v->value.as_real;
        } else if (strcmp(key, "greenWeight") == 0) {
            blackWhiteGreen = (float)v->value.as_real;
        } else if (strcmp(key, "blueWeight") == 0) {
            blackWhiteBlue = (float)v->value.as_real;
        }
    }
    brightness = color_clampf(brightness, -1.0f, 1.0f);
    contrast = color_clampf(contrast, -1.0f, 1.0f);
    hue = color_clampf(hue, -180.0f, 180.0f);
    saturation = color_clampf(saturation, -1.0f, 1.0f);
    gamma = color_clampf(gamma, 0.1f, 4.0f);
    exposure = color_clampf(exposure, -10.0f, 10.0f);
    blackWhiteAmount = color_clampf(blackWhiteAmount, 0.0f, 1.0f);
    blackWhiteRed = color_clampf(blackWhiteRed, 0.0f, 2.0f);
    blackWhiteGreen = color_clampf(blackWhiteGreen, 0.0f, 2.0f);
    blackWhiteBlue = color_clampf(blackWhiteBlue, 0.0f, 2.0f);

    gl->begin_render_pass(gpu, input->target_texture);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float(gpu, "uBrightness", brightness);
    gl->set_uniform_float(gpu, "uContrast", contrast);
    gl->set_uniform_float(gpu, "uHue", hue);
    gl->set_uniform_float(gpu, "uSaturation", saturation);
    gl->set_uniform_float(gpu, "uGamma", gamma);
    gl->set_uniform_float(gpu, "uExposure", exposure);
    gl->set_uniform_float(gpu, "uBlackWhiteAmount", blackWhiteAmount);
    gl->set_uniform_float3(
        gpu, "uBlackWhiteWeights", blackWhiteRed, blackWhiteGreen, blackWhiteBlue);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);

    return input->target_texture;
}

void color_adjust_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    ColorAdjustPass* pass = (ColorAdjustPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_color_host && g_color_host->gpu) {
        g_color_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
