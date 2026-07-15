// SPDX-License-Identifier: MPL-2.0

/* Ripple: isolated implementation module for Ruwa.Standard.Distort. */

#include "../DistortCommon.h"
#include "../DistortEffects.h"

/* ==========================================================================
 *   Ripple — bounded radial sine displacement (ROI-scissored)
 * ========================================================================== */

static const char* const k_ripple_fs =
    "#version 450 core\n"
    "\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uInvBasis0;\n"
    "uniform vec2 uInvBasis1;\n"
    "uniform vec2 uCenter;\n"
    "uniform float uAmplitudePx;\n"
    "uniform float uWavelengthPx;\n"
    "uniform float uRadiusPx;\n"
    "uniform float uPhase;\n"
    "\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "\n"
    "const float kTau = 6.28318530717958647692;\n"
    "\n"
    "void main() {\n"
    "    if (uAmplitudePx <= 0.0 || uWavelengthPx <= 0.0 || uRadiusPx <= 0.0) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "    vec2 delta = docPos - uCenter;\n"
    "    float dist = length(delta);\n"
    "    if (dist <= 1e-4 || dist >= uRadiusPx) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec2 dir = delta / dist;\n"
    "    float falloff = 1.0 - smoothstep(0.0, uRadiusPx, dist);\n"
    "    float wave = sin(dist * kTau / uWavelengthPx + uPhase);\n"
    "    vec2 sampleDoc = docPos + dir * (wave * uAmplitudePx * falloff);\n"
    "\n"
    "    vec2 rel = sampleDoc - uRegionOrigin;\n"
    "    vec2 sampleUV = vec2(dot(rel, uInvBasis0), dot(rel, uInvBasis1));\n"
    "    outColor = texture(uSourceTexture, sampleUV);\n"
    "}\n";

void* distort_ripple_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    return distort_create_pass(gpu, k_ripple_fs, "Ruwa.Standard.Distort/distort.ripple",
        "ruwa.standard.distort.ripple.fs");
}

int32_t distort_ripple_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state || !state->param_keys || !state->param_values) {
        return 0;
    }
    const RuwaEffectParamValue* a
        = distort_find_param(state->param_count, state->param_keys, state->param_values, "amplitude");
    return distort_clampi(a ? a->value.as_int : 16, 0, RIPPLE_MAX_AMPLITUDE);
}

RuwaEffectTexture distort_ripple_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    RuwaEffectPipeline pipeline = distort_begin(pass_instance, input);
    if (!pipeline) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_distort_host->gpu;

    const float amplitude = (float)distort_clampi(distort_int_param(input, "amplitude", 16), 0, RIPPLE_MAX_AMPLITUDE);
    const float radius = (float)distort_clampi(distort_int_param(input, "radius", 512), 0, RIPPLE_MAX_RADIUS);
    const float wavelength
        = (float)distort_clampi(distort_int_param(input, "wavelength", 80), 1, RIPPLE_MAX_WAVELENGTH);
    if (amplitude <= 0.0f || radius <= 0.0f || wavelength <= 0.0f) {
        return input->source_texture;
    }
    const float centerX = distort_real_param(input, "centerX", 0.0f);
    const float centerY = distort_real_param(input, "centerY", 0.0f);
    const float phase = distort_real_param(input, "phase", 0.0f) * DISTORT_PI / 180.0f;

    const DistortDocFrame frame = distort_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    distort_set_doc_frame_uniforms(gl, gpu, &frame);
    gl->set_uniform_float2(gpu, "uCenter", centerX, centerY);
    gl->set_uniform_float(gpu, "uAmplitudePx", amplitude);
    gl->set_uniform_float(gpu, "uWavelengthPx", wavelength);
    gl->set_uniform_float(gpu, "uRadiusPx", radius);
    gl->set_uniform_float(gpu, "uPhase", phase);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);

    return input->target_texture;
}
