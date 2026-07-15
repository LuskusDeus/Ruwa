// SPDX-License-Identifier: MPL-2.0

#include "BlurCommon.h"

#include <math.h>
#include <string.h>

const RuwaEffectHostApi* g_blur_host = NULL;

static const char* const k_gaussian_fragment_source =
    "#version 450 core\n"
    "const int kMaxTapPairs = 128;\n"
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

void blur_set_host_api(const RuwaEffectHostApi* host)
{
    g_blur_host = host;
}

/* Fragment-to-document affine transform reconstructed identically to the host's
 * computeEffectDocFrame implementation. */

float blur_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int blur_clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

const RuwaEffectParamValue* blur_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (keys[i] && strcmp(keys[i], key) == 0) {
            return &values[i];
        }
    }
    return NULL;
}

float blur_real_param(const RuwaEffectPassInput* in, const char* key, float fallback)
{
    const RuwaEffectParamValue* v
        = blur_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? (float)v->value.as_real : fallback;
}

int blur_int_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v
        = blur_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? v->value.as_int : fallback;
}

BlurDocFrame blur_compute_doc_frame(const RuwaEffectPassInput* input)
{
    BlurDocFrame f;
    const RuwaEffectRegionFrame* r = &input->region;
    f.originX = r->origin_x;
    f.originY = r->origin_y;
    if (r->use_affine != RUWA_FALSE) {
        f.basisXx = r->basis_xx;
        f.basisXy = r->basis_xy;
        f.basisYx = r->basis_yx;
        f.basisYy = r->basis_yy;
    } else {
        const float sx = (float)input->output_width * r->document_px_per_texel;
        const float sy = (float)input->output_height * r->document_px_per_texel;
        f.basisXx = sx;
        f.basisXy = 0.0f;
        f.basisYx = 0.0f;
        f.basisYy = sy;
    }
    const float det = f.basisXx * f.basisYy - f.basisYx * f.basisXy;
    const float invDet = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;
    f.inv0x = f.basisYy * invDet;
    f.inv0y = -f.basisYx * invDet;
    f.inv1x = -f.basisXy * invDet;
    f.inv1y = f.basisXx * invDet;
    return f;
}

void blur_set_doc_frame_uniforms(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, const BlurDocFrame* f)
{
    gl->set_uniform_float2(gpu, "uRegionOrigin", f->originX, f->originY);
    gl->set_uniform_float2(gpu, "uBasisX", f->basisXx, f->basisXy);
    gl->set_uniform_float2(gpu, "uBasisY", f->basisYx, f->basisYy);
    gl->set_uniform_float2(gpu, "uInvBasis0", f->inv0x, f->inv0y);
    gl->set_uniform_float2(gpu, "uInvBasis1", f->inv1x, f->inv1y);
}

RuwaEffectPipeline blur_create_graphics(
    RuwaEffectGpuContext gpu, const char* source, const char* debug_name, const char* cache_key)
{
    RuwaEffectShaderSource fragment;
    fragment.struct_size   = sizeof(RuwaEffectShaderSource);
    fragment.stage         = RUWA_EFFECT_STAGE_FRAGMENT;
    fragment.source        = source;
    fragment.source_length = 0u;
    fragment.debug_name    = debug_name;
    fragment.cache_key     = cache_key;
    return g_blur_host->gpu->create_graphics_pipeline(gpu, &fragment);
}

/* The paired-tap / paired-midpoint blur samplers need a linear-filtered source;
 * external chain textures may arrive nearest-filtered, so override with our own. */
RuwaEffectSampler blur_create_linear_sampler(RuwaEffectGpuContext gpu)
{
    return g_blur_host->gpu->create_sampler(
        gpu, RUWA_EFFECT_FILTER_LINEAR, RUWA_EFFECT_FILTER_LINEAR, RUWA_EFFECT_WRAP_CLAMP_TO_EDGE);
}

const char* blur_gaussian_fragment_source(void)
{
    return k_gaussian_fragment_source;
}

void blur_upload_gaussian_kernel(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu,
    RuwaEffectPipeline pipeline, int radius)
{
    radius = blur_clampi(radius, 0, GAUSSIAN_MAX_RADIUS);

    double weights[GAUSSIAN_MAX_RADIUS + 1] = { 0.0 };
    const double sigma = (double)radius / 3.0 > 0.5 ? (double)radius / 3.0 : 0.5;
    const double twoSigma2 = 2.0 * sigma * sigma;
    double total = 1.0;
    for (int i = 1; i <= radius; ++i) {
        weights[i] = exp(-(double)i * i / twoSigma2);
        total += 2.0 * weights[i];
    }

    float taps[2 * GAUSSIAN_MAX_TAP_PAIRS] = { 0.0f };
    int tapCount = 0;
    int i = 1;
    for (; i + 1 <= radius; i += 2) {
        const double w0 = weights[i];
        const double w1 = weights[i + 1];
        const double pairWeight = w0 + w1;
        taps[2 * tapCount] = (float)((w0 * i + w1 * (i + 1)) / pairWeight);
        taps[2 * tapCount + 1] = (float)(pairWeight / total);
        ++tapCount;
    }
    if (i <= radius) {
        taps[2 * tapCount] = (float)i;
        taps[2 * tapCount + 1] = (float)(weights[i] / total);
        ++tapCount;
    }

    gl->bind_graphics_pipeline(gpu, pipeline);
    gl->set_uniform_int(gpu, "uTapCount", tapCount);
    gl->set_uniform_float(gpu, "uCenterWeight", (float)(1.0 / total));
    gl->set_uniform_vec2_array(gpu, "uTaps", taps, (uint32_t)tapCount);
}

void blur_draw_gaussian_axis(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu,
    RuwaEffectPipeline pipeline, RuwaEffectSampler sampler, RuwaEffectTexture source,
    RuwaEffectTexture target, float stepX, float stepY, int roiExpandX, int roiExpandY)
{
    gl->begin_render_pass(gpu, target);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, roiExpandX, roiExpandY);
    gl->bind_graphics_pipeline(gpu, pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uTexelStep", stepX, stepY);
    gl->bind_texture(gpu, 0, source, sampler);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
}
