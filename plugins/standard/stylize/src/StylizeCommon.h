// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_STYLIZE_COMMON_H
#define RUWA_STANDARD_STYLIZE_COMMON_H

#include <ruwa/effect/ruwa_effect_sdk.h>

#define STYLIZE_PI 3.14159265358979323846f

#define GLOW_MAX_RADIUS 512
#define GLOW_MAX_LEVELS 8
#define GLOW_MAX_BLUR_RADIUS 32
#define GLOW_MAX_TAP_PAIRS ((GLOW_MAX_BLUR_RADIUS + 1) / 2)
#define STROKE_MAX_RADIUS 64
#define HALFTONE_MIN_CELL 2
#define HALFTONE_MAX_CELL 256
#define STYLIZE_GAUSSIAN_MAX_RADIUS 128
#define STYLIZE_GAUSSIAN_MAX_TAP_PAIRS ((STYLIZE_GAUSSIAN_MAX_RADIUS + 1) / 2)
#define LAYER_STYLE_MAX_RADIUS STYLIZE_GAUSSIAN_MAX_RADIUS
#define LAYER_STYLE_MAX_DISTANCE 512

typedef struct StylizeDocFrame {
    float originX, originY;
    float basisXx, basisXy, basisYx, basisYy;
    float inv0x, inv0y, inv1x, inv1y;
} StylizeDocFrame;

/* The host table is supplied through the plugin query entry point and remains
 * valid for the lifetime of every effect implementation module. */
extern const RuwaEffectHostApi* g_stylize_host;

void stylize_set_host_api(const RuwaEffectHostApi* host);

float stylize_clampf(float value, float minimum, float maximum);
int stylize_clampi(int value, int minimum, int maximum);
const RuwaEffectParamValue* stylize_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key);
float stylize_real_param(const RuwaEffectPassInput* input, const char* key, float fallback);
int stylize_int_param(const RuwaEffectPassInput* input, const char* key, int fallback);
int stylize_choice_param(const RuwaEffectPassInput* input, const char* key, int fallback);
int stylize_bool_param(const RuwaEffectPassInput* input, const char* key, int fallback);
void stylize_color_param(const RuwaEffectPassInput* input, const char* key, float output[4],
    float default_red, float default_green, float default_blue, float default_alpha);

StylizeDocFrame stylize_compute_doc_frame(const RuwaEffectPassInput* input);
void stylize_set_doc_frame_forward(
    const RuwaEffectGpuApi* gpu_api, RuwaEffectGpuContext gpu, const StylizeDocFrame* frame);
void stylize_set_doc_frame_inverse(
    const RuwaEffectGpuApi* gpu_api, RuwaEffectGpuContext gpu, const StylizeDocFrame* frame);

RuwaEffectPipeline stylize_create_graphics(
    RuwaEffectGpuContext gpu, const char* source, const char* debug_name, const char* cache_key);
RuwaEffectSampler stylize_create_linear_sampler_or_null(RuwaEffectGpuContext gpu);
void stylize_upload_gaussian_kernel(const RuwaEffectGpuApi* gpu_api,
    RuwaEffectGpuContext gpu, RuwaEffectPipeline pipeline, int radius, int maximum_radius);

#endif /* RUWA_STANDARD_STYLIZE_COMMON_H */
