// SPDX-License-Identifier: MPL-2.0

/* Noise / Grain: shared procedural texture pass for Ruwa.Standard.Texture. Both
 * effects run the one fragment shader with a `mode` uniform selecting Noise vs
 * Grain; the mode is carried per descriptor through `user_data`. */

#include "../TextureCommon.h"
#include "../TextureEffects.h"

#include <stdint.h>
#include <stdlib.h>

static const char* const k_texture_noise_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform int uMode;\n"
    "uniform float uAmount;\n"
    "uniform float uCellSizePx;\n"
    "uniform float uContrast;\n"
    "uniform float uSeed;\n"
    "uniform int uMonochrome;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "float hash12(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "vec3 hash32(vec2 p) {\n"
    "    return vec3(\n"
    "        hash12(p + vec2(17.0, 59.4)),\n"
    "        hash12(p + vec2(83.1, 11.7)),\n"
    "        hash12(p + vec2(41.6, 97.3)));\n"
    "}\n"
    "void main() {\n"
    "    vec4 premul = texture(uSourceTexture, fragTexCoord);\n"
    "    if (premul.a <= 0.0 || uAmount <= 0.0 || uCellSizePx <= 0.0) {\n"
    "        outColor = premul;\n"
    "        return;\n"
    "    }\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "    vec2 cell = floor(docPos / uCellSizePx) + vec2(uSeed * 37.0, uSeed * 13.0);\n"
    "    vec3 color = premul.rgb / premul.a;\n"
    "    if (uMode == 0) {\n"
    "        vec3 noise = uMonochrome == 1 ? vec3(hash12(cell)) : hash32(cell);\n"
    "        color += (noise - vec3(0.5)) * (2.0 * uAmount);\n"
    "    } else {\n"
    "        float raw = hash12(cell) - 0.5;\n"
    "        float grain = sign(raw) * pow(abs(raw) * 2.0, mix(1.6, 0.45, uContrast));\n"
    "        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "        float tonalWeight = mix(1.25, 0.75, smoothstep(0.15, 0.9, luminance));\n"
    "        color += vec3(grain * uAmount * tonalWeight);\n"
    "    }\n"
    "    color = clamp(color, 0.0, 1.0);\n"
    "    outColor = vec4(color * premul.a, premul.a);\n"
    "}\n";

typedef struct TexturePass {
    RuwaEffectPipeline pipeline;
    int mode; /* 0 = Noise, 1 = Grain */
} TexturePass;

void* texture_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    if (!g_texture_host || !g_texture_host->gpu) {
        return NULL;
    }
    RuwaEffectShaderSource fragment;
    fragment.struct_size   = sizeof(RuwaEffectShaderSource);
    fragment.stage         = RUWA_EFFECT_STAGE_FRAGMENT;
    fragment.source        = k_texture_noise_fs;
    fragment.source_length = 0u;
    fragment.debug_name    = "Ruwa.Standard.Texture/texture.noise";
    /* Both effects link the one cached program. */
    fragment.cache_key     = "ruwa.standard.texture.noise.fs";

    RuwaEffectPipeline pipeline = g_texture_host->gpu->create_graphics_pipeline(gpu, &fragment);
    if (!pipeline) {
        if (g_texture_host->log) {
            g_texture_host->log(
                RUWA_EFFECT_LOG_ERROR, "Ruwa.Standard.Texture: pipeline failed to compile");
        }
        return NULL;
    }
    TexturePass* pass = (TexturePass*)malloc(sizeof(TexturePass));
    if (!pass) {
        g_texture_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    pass->mode = (int)(intptr_t)user_data; /* descriptor-carried mode */
    return pass;
}

RuwaEffectTexture texture_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    TexturePass* pass = (TexturePass*)pass_instance;
    if (!pass || !pass->pipeline || !g_texture_host || !g_texture_host->gpu || !input
        || !input->source_texture || !input->target_texture || input->output_width == 0
        || input->output_height == 0 || input->region.valid == RUWA_FALSE) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_texture_host->gpu;
    const int mode = pass->mode;

    const float amount = texture_clampf(texture_real_param(input, "amount", 0.0f), 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return input->source_texture;
    }
    /* Noise sizes its grid via "scale", Grain via "size"; each effect only
     * declares its own key (the other falls back to its default). */
    const int scale = texture_int_param(input, "scale", 1) > 1 ? texture_int_param(input, "scale", 1)
                                                               : 1;
    const int size = texture_int_param(input, "size", 2) > 1 ? texture_int_param(input, "size", 2)
                                                             : 1;
    const float cellSize = (float)(mode == 0 ? scale : size);
    const float contrast = texture_clampf(texture_real_param(input, "contrast", 0.35f), 0.0f, 1.0f);
    const int seed = texture_clampi(texture_int_param(input, "seed", 0), 0, 10000);
    const int monochrome = (mode == 1) || texture_bool_param(input, "monochrome", 0) ? 1 : 0;

    const TextureDocFrame frame = texture_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uRegionOrigin", frame.originX, frame.originY);
    gl->set_uniform_float2(gpu, "uBasisX", frame.basisXx, frame.basisXy);
    gl->set_uniform_float2(gpu, "uBasisY", frame.basisYx, frame.basisYy);
    gl->set_uniform_int(gpu, "uMode", mode);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->set_uniform_float(gpu, "uCellSizePx", cellSize);
    gl->set_uniform_float(gpu, "uContrast", contrast);
    gl->set_uniform_float(gpu, "uSeed", (float)seed);
    gl->set_uniform_int(gpu, "uMonochrome", monochrome);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);

    return input->target_texture;
}

void texture_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    TexturePass* pass = (TexturePass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_texture_host && g_texture_host->gpu) {
        g_texture_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
