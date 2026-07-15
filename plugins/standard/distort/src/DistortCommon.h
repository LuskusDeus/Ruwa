// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_DISTORT_COMMON_H
#define RUWA_STANDARD_DISTORT_COMMON_H

#include <ruwa/effect/ruwa_effect_sdk.h>

#define DISTORT_PI 3.14159265358979323846f

#define TWIRL_MAX_RADIUS 2048
#define PINCH_MAX_RADIUS 2048
#define RIPPLE_MAX_AMPLITUDE 256
#define RIPPLE_MAX_RADIUS 4096
#define RIPPLE_MAX_WAVELENGTH 1024
#define WAVE_MAX_AMPLITUDE 256
#define WAVE_MAX_WAVELENGTH 2048

typedef struct DistortDocFrame {
    float originX, originY;
    float basisXx, basisXy, basisYx, basisYy;
    float inv0x, inv0y, inv1x, inv1y;
} DistortDocFrame;

/* The host table is supplied through the plugin query entry point and remains
 * valid for the lifetime of every effect implementation module. */
extern const RuwaEffectHostApi* g_distort_host;

void distort_set_host_api(const RuwaEffectHostApi* host);

float distort_clampf(float value, float minimum, float maximum);
int distort_clampi(int value, int minimum, int maximum);
const RuwaEffectParamValue* distort_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key);
float distort_real_param(const RuwaEffectPassInput* input, const char* key, float fallback);
int distort_int_param(const RuwaEffectPassInput* input, const char* key, int fallback);

DistortDocFrame distort_compute_doc_frame(const RuwaEffectPassInput* input);
void distort_set_doc_frame_uniforms(
    const RuwaEffectGpuApi* gpu_api, RuwaEffectGpuContext gpu, const DistortDocFrame* frame);

/* Shared pipeline lifecycle used by all distortion effects. */
void* distort_create_pass(
    RuwaEffectGpuContext gpu, const char* source, const char* debug_name, const char* cache_key);
void distort_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);
RuwaEffectPipeline distort_begin(
    void* pass_instance, const RuwaEffectPassInput* input);

#endif /* RUWA_STANDARD_DISTORT_COMMON_H */
