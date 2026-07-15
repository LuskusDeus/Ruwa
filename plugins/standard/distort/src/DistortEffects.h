// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_DISTORT_EFFECTS_H
#define RUWA_STANDARD_DISTORT_EFFECTS_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* Twirl */
void* distort_twirl_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t distort_twirl_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture distort_twirl_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);

/* Pinch */
void* distort_pinch_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t distort_pinch_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture distort_pinch_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);

/* Ripple */
void* distort_ripple_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t distort_ripple_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture distort_ripple_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);

/* Wave */
void* distort_wave_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t distort_wave_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture distort_wave_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);

#endif /* RUWA_STANDARD_DISTORT_EFFECTS_H */
