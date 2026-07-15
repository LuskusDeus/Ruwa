// SPDX-License-Identifier: MPL-2.0

/* Pinch: isolated implementation module for Ruwa.Standard.Distort. */

#include "../DistortCommon.h"
#include "../DistortEffects.h"

/* ==========================================================================
 *   Pinch — bounded radial pull / bulge (ROI-scissored)
 * ========================================================================== */

static const char* const k_pinch_fs =
    "#version 450 core\n"
    "\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uInvBasis0;\n"
    "uniform vec2 uInvBasis1;\n"
    "uniform vec2 uCenter;\n"
    "uniform float uAmount;\n"
    "uniform float uRadiusPx;\n"
    "\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "\n"
    "void main() {\n"
    "    if (uRadiusPx <= 0.0 || uAmount == 0.0) {\n"
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
    "    float t = dist / uRadiusPx;\n"
    "    float falloff = (1.0 - t) * (1.0 - t);\n"
    "    vec2 sampleDoc = uCenter + delta * (1.0 + uAmount * falloff);\n"
    "\n"
    "    vec2 rel = sampleDoc - uRegionOrigin;\n"
    "    vec2 sampleUV = vec2(dot(rel, uInvBasis0), dot(rel, uInvBasis1));\n"
    "    outColor = texture(uSourceTexture, sampleUV);\n"
    "}\n";

void* distort_pinch_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    return distort_create_pass(gpu, k_pinch_fs, "Ruwa.Standard.Distort/distort.pinch",
        "ruwa.standard.distort.pinch.fs");
}

/* Max sample offset is radius * |amount| * 4/27 (the extremum of t*(1-t)^2), so
 * neighbour padding bounds both rendering and coverage. Matches pinchMaxDisplacement. */
int32_t distort_pinch_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state || !state->param_keys || !state->param_values) {
        return 0;
    }
    const RuwaEffectParamValue* rv
        = distort_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    const RuwaEffectParamValue* av
        = distort_find_param(state->param_count, state->param_keys, state->param_values, "amount");
    const int radius = distort_clampi(rv ? rv->value.as_int : 240, 0, PINCH_MAX_RADIUS);
    const float amount = distort_clampf(av ? (float)av->value.as_real : 0.5f, -1.0f, 1.0f);
    return (int32_t)ceilf((float)radius * fabsf(amount) * (4.0f / 27.0f));
}

RuwaEffectTexture distort_pinch_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    RuwaEffectPipeline pipeline = distort_begin(pass_instance, input);
    if (!pipeline) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_distort_host->gpu;

    const float radius = (float)distort_clampi(distort_int_param(input, "radius", 240), 0, PINCH_MAX_RADIUS);
    const float amount = distort_clampf(distort_real_param(input, "amount", 0.5f), -1.0f, 1.0f);
    if (radius <= 0.0f || amount == 0.0f) {
        return input->source_texture;
    }
    const float centerX = distort_real_param(input, "centerX", 0.0f);
    const float centerY = distort_real_param(input, "centerY", 0.0f);

    const DistortDocFrame frame = distort_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    distort_set_doc_frame_uniforms(gl, gpu, &frame);
    gl->set_uniform_float2(gpu, "uCenter", centerX, centerY);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->set_uniform_float(gpu, "uRadiusPx", radius);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);

    return input->target_texture;
}
