// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_BLUR_COMMON_H
#define RUWA_STANDARD_BLUR_COMMON_H

#include <ruwa/effect/ruwa_effect_sdk.h>

#define BLUR_PI 3.14159265358979323846f
#define BLUR_TILE_SIZE 256

#define GAUSSIAN_MAX_RADIUS 256
#define GAUSSIAN_MAX_TAP_PAIRS ((GAUSSIAN_MAX_RADIUS + 1) / 2)
#define BOX_MAX_RADIUS 256
#define BOX_MAX_ITERATIONS 3
#define BOX_SCAN_GROUP 256u
#define MOTION_MAX_DISTANCE 256
#define RADIAL_MAX_RADIUS 256

typedef struct BlurDocFrame {
    float originX, originY;
    float basisXx, basisXy, basisYx, basisYy;
    float inv0x, inv0y, inv1x, inv1y;
} BlurDocFrame;

/* The host table is process-lifetime state supplied once through the plugin
 * query entry point and shared by all effect implementation modules. */
extern const RuwaEffectHostApi* g_blur_host;

void blur_set_host_api(const RuwaEffectHostApi* host);

float blur_clampf(float value, float minimum, float maximum);
int blur_clampi(int value, int minimum, int maximum);
const RuwaEffectParamValue* blur_find_param(
    uint32_t count, const char* const* keys, const RuwaEffectParamValue* values, const char* key);
float blur_real_param(const RuwaEffectPassInput* input, const char* key, float fallback);
int blur_int_param(const RuwaEffectPassInput* input, const char* key, int fallback);

BlurDocFrame blur_compute_doc_frame(const RuwaEffectPassInput* input);
void blur_set_doc_frame_uniforms(
    const RuwaEffectGpuApi* gpu_api, RuwaEffectGpuContext gpu, const BlurDocFrame* frame);
RuwaEffectPipeline blur_create_graphics(
    RuwaEffectGpuContext gpu, const char* source, const char* debug_name, const char* cache_key);
RuwaEffectSampler blur_create_linear_sampler(RuwaEffectGpuContext gpu);
const char* blur_gaussian_fragment_source(void);
void blur_upload_gaussian_kernel(const RuwaEffectGpuApi* gpu_api, RuwaEffectGpuContext gpu,
    RuwaEffectPipeline pipeline, int radius);
void blur_draw_gaussian_axis(const RuwaEffectGpuApi* gpu_api, RuwaEffectGpuContext gpu,
    RuwaEffectPipeline pipeline, RuwaEffectSampler sampler, RuwaEffectTexture source,
    RuwaEffectTexture target, float step_x, float step_y, int roi_expand_x, int roi_expand_y);

#endif /* RUWA_STANDARD_BLUR_COMMON_H */
