// SPDX-License-Identifier: MPL-2.0

/* Twirl: isolated implementation module for Ruwa.Standard.Distort. */

#include "../DistortCommon.h"
#include "../DistortEffects.h"

/* ==========================================================================
 *   Twirl — whole-layer rotation warp
 * ========================================================================== */

static const char* const k_twirl_fs =
    "#version 450 core\n"
    "\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uRegionOrigin;\n"
    "uniform vec2 uBasisX;\n"
    "uniform vec2 uBasisY;\n"
    "uniform vec2 uInvBasis0;\n"
    "uniform vec2 uInvBasis1;\n"
    "uniform vec2 uCenter;\n"
    "uniform float uAngle;    // peak rotation at the centre, radians\n"
    "uniform float uRadiusPx; // falloff radius, document pixels\n"
    "\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "\n"
    "void main() {\n"
    "    if (uRadiusPx <= 0.0 || uAngle == 0.0) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec2 docPos = uRegionOrigin + fragTexCoord.x * uBasisX + fragTexCoord.y * uBasisY;\n"
    "\n"
    "    vec2 d = docPos - uCenter;\n"
    "    float dist = length(d);\n"
    "    if (dist >= uRadiusPx) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    float t = 1.0 - dist / uRadiusPx;\n"
    "    float angle = uAngle * t * t;\n"
    "    float s = sin(angle);\n"
    "    float c = cos(angle);\n"
    "    vec2 rotated = vec2(c * d.x - s * d.y, s * d.x + c * d.y);\n"
    "\n"
    "    vec2 rel = (uCenter + rotated) - uRegionOrigin;\n"
    "    vec2 sampleUV = vec2(dot(rel, uInvBasis0), dot(rel, uInvBasis1));\n"
    "    outColor = texture(uSourceTexture, sampleUV);\n"
    "}\n";

static int twirl_radius(const RuwaEffectPassInput* in)
{
    return distort_clampi(distort_int_param(in, "radius", 200), 0, TWIRL_MAX_RADIUS);
}

void* distort_twirl_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    return distort_create_pass(gpu, k_twirl_fs, "Ruwa.Standard.Distort/distort.twirl",
        "ruwa.standard.distort.twirl.fs");
}

int32_t distort_twirl_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state || !state->param_keys || !state->param_values) {
        return 0;
    }
    const RuwaEffectParamValue* r
        = distort_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    return distort_clampi(r ? r->value.as_int : 200, 0, TWIRL_MAX_RADIUS);
}

RuwaEffectTexture distort_twirl_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    RuwaEffectPipeline pipeline = distort_begin(pass_instance, input);
    if (!pipeline) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_distort_host->gpu;

    const float centerX = distort_real_param(input, "centerX", 0.0f);
    const float centerY = distort_real_param(input, "centerY", 0.0f);
    const float angleRadians = distort_real_param(input, "angle", 90.0f) * DISTORT_PI / 180.0f;
    const float radius = (float)twirl_radius(input);
    if (radius <= 0.0f || angleRadians == 0.0f) {
        return input->source_texture;
    }

    const DistortDocFrame frame = distort_compute_doc_frame(input);

    gl->begin_render_pass(gpu, input->target_texture);
    gl->bind_graphics_pipeline(gpu, pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    distort_set_doc_frame_uniforms(gl, gpu, &frame);
    gl->set_uniform_float2(gpu, "uCenter", centerX, centerY);
    gl->set_uniform_float(gpu, "uAngle", angleRadians);
    gl->set_uniform_float(gpu, "uRadiusPx", radius);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);

    return input->target_texture;
}
