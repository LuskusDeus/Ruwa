// SPDX-License-Identifier: MPL-2.0

#include "StylizeCommon.h"

#include <math.h>
#include <string.h>

const RuwaEffectHostApi* g_stylize_host = NULL;

void stylize_set_host_api(const RuwaEffectHostApi* host)
{
    g_stylize_host = host;
}

/* Fragment-to-document affine transform reconstructed identically to the host's
 * computeEffectDocFrame implementation. */

float stylize_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int stylize_clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

const RuwaEffectParamValue* stylize_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (keys[i] && strcmp(keys[i], key) == 0) {
            return &values[i];
        }
    }
    return NULL;
}

float stylize_real_param(const RuwaEffectPassInput* in, const char* key, float fallback)
{
    const RuwaEffectParamValue* v
        = stylize_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? (float)v->value.as_real : fallback;
}

int stylize_int_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v
        = stylize_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? v->value.as_int : fallback;
}

int stylize_choice_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v
        = stylize_find_param(in->param_count, in->param_keys, in->param_values, key);
    return (v && v->type == RUWA_EFFECT_PARAM_CHOICE) ? v->value.as_choice : fallback;
}

int stylize_bool_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v
        = stylize_find_param(in->param_count, in->param_keys, in->param_values, key);
    return (v && v->type == RUWA_EFFECT_PARAM_BOOL) ? (v->value.as_bool != RUWA_FALSE ? 1 : 0)
                                                    : fallback;
}

void stylize_color_param(const RuwaEffectPassInput* in, const char* key, float out[4], float dr,
    float dg, float db, float da)
{
    const RuwaEffectParamValue* v
        = stylize_find_param(in->param_count, in->param_keys, in->param_values, key);
    if (v && v->type == RUWA_EFFECT_PARAM_COLOR) {
        out[0] = v->value.as_color[0];
        out[1] = v->value.as_color[1];
        out[2] = v->value.as_color[2];
        out[3] = v->value.as_color[3];
    } else {
        out[0] = dr;
        out[1] = dg;
        out[2] = db;
        out[3] = da;
    }
}

StylizeDocFrame stylize_compute_doc_frame(const RuwaEffectPassInput* input)
{
    StylizeDocFrame f;
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

void stylize_set_doc_frame_forward(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, const StylizeDocFrame* f)
{
    gl->set_uniform_float2(gpu, "uRegionOrigin", f->originX, f->originY);
    gl->set_uniform_float2(gpu, "uBasisX", f->basisXx, f->basisXy);
    gl->set_uniform_float2(gpu, "uBasisY", f->basisYx, f->basisYy);
}

void stylize_set_doc_frame_inverse(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, const StylizeDocFrame* f)
{
    gl->set_uniform_float2(gpu, "uInvBasis0", f->inv0x, f->inv0y);
    gl->set_uniform_float2(gpu, "uInvBasis1", f->inv1x, f->inv1y);
}

RuwaEffectPipeline stylize_create_graphics(
    RuwaEffectGpuContext gpu, const char* source, const char* debug_name, const char* cache_key)
{
    RuwaEffectShaderSource fragment;
    fragment.struct_size   = sizeof(RuwaEffectShaderSource);
    fragment.stage         = RUWA_EFFECT_STAGE_FRAGMENT;
    fragment.source        = source;
    fragment.source_length = 0u;
    fragment.debug_name    = debug_name;
    fragment.cache_key     = cache_key;
    return g_stylize_host->gpu->create_graphics_pipeline(gpu, &fragment);
}

RuwaEffectSampler stylize_create_linear_sampler_or_null(RuwaEffectGpuContext gpu)
{
    return g_stylize_host->gpu->create_sampler(
        gpu, RUWA_EFFECT_FILTER_LINEAR, RUWA_EFFECT_FILTER_LINEAR, RUWA_EFFECT_WRAP_CLAMP_TO_EDGE);
}

void stylize_upload_gaussian_kernel(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu,
    RuwaEffectPipeline pipeline, int radius, int maximumRadius)
{
    maximumRadius = stylize_clampi(maximumRadius, 0, STYLIZE_GAUSSIAN_MAX_RADIUS);
    radius = stylize_clampi(radius, 0, maximumRadius);

    double weights[STYLIZE_GAUSSIAN_MAX_RADIUS + 1] = { 0.0 };
    const double sigma = (double)radius / 3.0 > 0.5 ? (double)radius / 3.0 : 0.5;
    const double twoSigma2 = 2.0 * sigma * sigma;
    double total = 1.0;
    for (int i = 1; i <= radius; ++i) {
        weights[i] = exp(-(double)i * i / twoSigma2);
        total += 2.0 * weights[i];
    }

    float taps[2 * STYLIZE_GAUSSIAN_MAX_TAP_PAIRS] = { 0.0f };
    int tapCount = 0;
    int i = 1;
    for (; i + 1 <= radius; i += 2) {
        const double pairWeight = weights[i] + weights[i + 1];
        taps[2 * tapCount] = (float)((weights[i] * i + weights[i + 1] * (i + 1)) / pairWeight);
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
