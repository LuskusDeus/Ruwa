// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_BLUR_EFFECTS_H
#define RUWA_STANDARD_BLUR_EFFECTS_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* Gaussian Blur */
void* blur_gaussian_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t blur_gaussian_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture blur_gaussian_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void blur_gaussian_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Box Blur */
void* blur_box_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t blur_box_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture blur_box_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void blur_box_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Motion Blur */
void* blur_motion_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t blur_motion_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state);
RuwaBool blur_motion_resolve_coverage(void* user_data, const RuwaEffectStateView* state,
    RuwaEffectCoverageInput input, RuwaEffectCoverageEmit emit_fn, void* emit_ctx);
RuwaEffectTexture blur_motion_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void blur_motion_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Radial Blur */
void* blur_radial_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t blur_radial_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture blur_radial_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void blur_radial_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Sharpen */
void* blur_sharpen_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t blur_sharpen_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture blur_sharpen_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void blur_sharpen_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

/* Unsharp Mask */
void* blur_unsharp_mask_create_pass(void* user_data, RuwaEffectGpuContext gpu);
int32_t blur_unsharp_mask_pixel_expansion_radius(
    void* user_data, const RuwaEffectStateView* state);
RuwaEffectTexture blur_unsharp_mask_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void blur_unsharp_mask_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

#endif /* RUWA_STANDARD_BLUR_EFFECTS_H */
