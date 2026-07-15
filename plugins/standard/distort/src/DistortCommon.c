// SPDX-License-Identifier: MPL-2.0

#include "DistortCommon.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const RuwaEffectHostApi* g_distort_host = NULL;

void distort_set_host_api(const RuwaEffectHostApi* host)
{
    g_distort_host = host;
}

/* Fragment-to-document affine transform reconstructed identically to the host's
 * computeEffectDocFrame implementation. */

float distort_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int distort_clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

const RuwaEffectParamValue* distort_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (keys[i] && strcmp(keys[i], key) == 0) {
            return &values[i];
        }
    }
    return NULL;
}

float distort_real_param(const RuwaEffectPassInput* in, const char* key, float fallback)
{
    const RuwaEffectParamValue* v = distort_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? (float)v->value.as_real : fallback;
}

int distort_int_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v = distort_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? v->value.as_int : fallback;
}

DistortDocFrame distort_compute_doc_frame(const RuwaEffectPassInput* input)
{
    DistortDocFrame f;
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

void distort_set_doc_frame_uniforms(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, const DistortDocFrame* f)
{
    gl->set_uniform_float2(gpu, "uRegionOrigin", f->originX, f->originY);
    gl->set_uniform_float2(gpu, "uBasisX", f->basisXx, f->basisXy);
    gl->set_uniform_float2(gpu, "uBasisY", f->basisYx, f->basisYy);
    gl->set_uniform_float2(gpu, "uInvBasis0", f->inv0x, f->inv0y);
    gl->set_uniform_float2(gpu, "uInvBasis1", f->inv1x, f->inv1y);
}

/* One pass type for all three effects: each compiles its own fragment shader. */
typedef struct DistortPass {
    RuwaEffectPipeline pipeline;
} DistortPass;

void* distort_create_pass(
    RuwaEffectGpuContext gpu, const char* source, const char* debug_name, const char* cache_key)
{
    if (!g_distort_host || !g_distort_host->gpu) {
        return NULL;
    }
    const RuwaEffectGpuApi* gl = g_distort_host->gpu;

    RuwaEffectShaderSource fragment;
    fragment.struct_size   = sizeof(RuwaEffectShaderSource);
    fragment.stage         = RUWA_EFFECT_STAGE_FRAGMENT;
    fragment.source        = source;
    fragment.source_length = 0u; /* null-terminated */
    fragment.debug_name    = debug_name;
    fragment.cache_key     = cache_key;

    RuwaEffectPipeline pipeline = gl->create_graphics_pipeline(gpu, &fragment);
    if (!pipeline) {
        if (g_distort_host->log) {
            g_distort_host->log(RUWA_EFFECT_LOG_ERROR, "Ruwa.Standard.Distort: pipeline failed to compile");
        }
        return NULL;
    }

    DistortPass* pass = (DistortPass*)malloc(sizeof(DistortPass));
    if (!pass) {
        gl->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

void distort_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    DistortPass* pass = (DistortPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_distort_host && g_distort_host->gpu) {
        g_distort_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}

/* Shared render guard: returns the bound pipeline, or NULL when the invocation
 * must pass through (missing inputs / invalid document mapping). */
RuwaEffectPipeline distort_begin(void* pass_instance, const RuwaEffectPassInput* input)
{
    DistortPass* pass = (DistortPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_distort_host || !g_distort_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0
        || input->region.valid == RUWA_FALSE) {
        return NULL;
    }
    return pass->pipeline;
}
