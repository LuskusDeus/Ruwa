// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_STYLIZE_EFFECTS_H
#define RUWA_STANDARD_STYLIZE_EFFECTS_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* Glow */
void* stylize_glow_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t stylize_glow_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture stylize_glow_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void stylize_glow_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Stroke */
void* stylize_stroke_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t stylize_stroke_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture stylize_stroke_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void stylize_stroke_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Gradient Overlay */
void* stylize_gradient_create_pass(void* user_data, RuwaEffectGpuContext gpu);
RuwaEffectTexture stylize_gradient_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void stylize_gradient_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Halftone */
void* stylize_halftone_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t stylize_halftone_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture stylize_halftone_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void stylize_halftone_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Outer Glow / Inner Glow / Drop Shadow share one alpha-blur implementation. */
void* stylize_layer_style_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t stylize_layer_style_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture stylize_layer_style_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void stylize_layer_style_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

#endif /* RUWA_STANDARD_STYLIZE_EFFECTS_H */
