// SPDX-License-Identifier: MPL-2.0

/* Wave: directional sinusoidal displacement for Ruwa.Standard.Distort. */

#include "../DistortCommon.h"
#include "../DistortEffects.h"

#include <math.h>

static const char* const k_wave_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uInvBasis0;\n"
    "uniform vec2 uInvBasis1;\n"
    "uniform vec2 uWaveDirection;\n"
    "uniform vec2 uDisplacementDirection;\n"
    "uniform float uAmplitudePx;\n"
    "uniform float uWavelengthPx;\n"
    "uniform float uPhase;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "const float kTau = 6.28318530717958647692;\n"
    "void main() {\n"
    "    if (uAmplitudePx <= 0.0 || uWavelengthPx <= 0.0) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "    float coordinate = dot(docPos, uWaveDirection);\n"
    "    float displacement = sin(coordinate * kTau / uWavelengthPx + uPhase) * uAmplitudePx;\n"
    "    vec2 sampleDoc = docPos - uDisplacementDirection * displacement;\n"
    "    vec2 relative = sampleDoc - uRegionOrigin;\n"
    "    vec2 sampleUV = vec2(dot(relative, uInvBasis0), dot(relative, uInvBasis1));\n"
    "    outColor = texture(uSourceTexture, sampleUV);\n"
    "}\n";

void* distort_wave_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    return distort_create_pass(gpu, k_wave_fs, "Ruwa.Standard.Distort/distort.wave",
        "ruwa.standard.distort.wave.fs");
}

int32_t distort_wave_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state || !state->param_keys || !state->param_values) {
        return 0;
    }
    const RuwaEffectParamValue* amplitude
        = distort_find_param(state->param_count, state->param_keys, state->param_values, "amplitude");
    return distort_clampi(amplitude ? amplitude->value.as_int : 20, 0, WAVE_MAX_AMPLITUDE);
}

RuwaEffectTexture distort_wave_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    RuwaEffectPipeline pipeline = distort_begin(pass_instance, input);
    if (!pipeline) {
        return input ? input->source_texture : NULL;
    }

    const int amplitude
        = distort_clampi(distort_int_param(input, "amplitude", 20), 0, WAVE_MAX_AMPLITUDE);
    const int wavelength
        = distort_clampi(distort_int_param(input, "wavelength", 120), 1, WAVE_MAX_WAVELENGTH);
    if (amplitude <= 0) {
        return input->source_texture;
    }

    const float angle = distort_real_param(input, "angle", 0.0f) * DISTORT_PI / 180.0f;
    const float phase = distort_real_param(input, "phase", 0.0f) * DISTORT_PI / 180.0f;
    const float waveX = cosf(angle);
    const float waveY = sinf(angle);
    const DistortDocFrame frame = distort_compute_doc_frame(input);
    const RuwaEffectGpuApi* gl = g_distort_host->gpu;

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    distort_set_doc_frame_uniforms(gl, gpu, &frame);
    gl->set_uniform_float2(gpu, "uWaveDirection", waveX, waveY);
    gl->set_uniform_float2(gpu, "uDisplacementDirection", -waveY, waveX);
    gl->set_uniform_float(gpu, "uAmplitudePx", (float)amplitude);
    gl->set_uniform_float(gpu, "uWavelengthPx", (float)wavelength);
    gl->set_uniform_float(gpu, "uPhase", phase);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
    return input->target_texture;
}
