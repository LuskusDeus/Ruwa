// SPDX-License-Identifier: MPL-2.0

#include "TextureCommon.h"

#include <string.h>

const RuwaEffectHostApi* g_texture_host = NULL;

void texture_set_host_api(const RuwaEffectHostApi* host)
{
    g_texture_host = host;
}

float texture_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int texture_clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static const RuwaEffectParamValue* texture_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (keys[i] && strcmp(keys[i], key) == 0) {
            return &values[i];
        }
    }
    return NULL;
}

float texture_real_param(const RuwaEffectPassInput* in, const char* key, float fallback)
{
    const RuwaEffectParamValue* v
        = texture_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? (float)v->value.as_real : fallback;
}

int texture_int_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v
        = texture_find_param(in->param_count, in->param_keys, in->param_values, key);
    return v ? v->value.as_int : fallback;
}

int texture_bool_param(const RuwaEffectPassInput* in, const char* key, int fallback)
{
    const RuwaEffectParamValue* v
        = texture_find_param(in->param_count, in->param_keys, in->param_values, key);
    return (v && v->type == RUWA_EFFECT_PARAM_BOOL) ? (v->value.as_bool != RUWA_FALSE ? 1 : 0)
                                                    : fallback;
}

TextureDocFrame texture_compute_doc_frame(const RuwaEffectPassInput* input)
{
    TextureDocFrame f;
    const RuwaEffectRegionFrame* r = &input->region;
    f.originX = r->origin_x;
    f.originY = r->origin_y;
    if (r->use_affine != RUWA_FALSE) {
        f.basisXx = r->basis_xx;
        f.basisXy = r->basis_xy;
        f.basisYx = r->basis_yx;
        f.basisYy = r->basis_yy;
    } else {
        f.basisXx = (float)input->output_width * r->document_px_per_texel;
        f.basisXy = 0.0f;
        f.basisYx = 0.0f;
        f.basisYy = (float)input->output_height * r->document_px_per_texel;
    }
    return f;
}
