// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_COLOR_EFFECTS_H
#define RUWA_STANDARD_COLOR_EFFECTS_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* Shared Color Adjust pass. Every standard color adjustment compiles the same
 * shader and drives only the parameters exposed by its descriptor; all other
 * adjustments remain at their identity values. */
void* color_adjust_create_pass(void* user_data, RuwaEffectGpuContext gpu);
RuwaEffectTexture color_adjust_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void color_adjust_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

#endif /* RUWA_STANDARD_COLOR_EFFECTS_H */
