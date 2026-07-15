// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_TEXTURE_EFFECTS_H
#define RUWA_STANDARD_TEXTURE_EFFECTS_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* Shared Noise / Grain pass. Both "texture.noise" and "texture.grain" run the
 * one procedural fragment shader with a `mode` uniform selecting Noise vs Grain;
 * the mode is carried per descriptor through `user_data` (0 = Noise, 1 = Grain). */
void* texture_create_pass(void* user_data, RuwaEffectGpuContext gpu);
RuwaEffectTexture texture_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);
void texture_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu);

#endif /* RUWA_STANDARD_TEXTURE_EFFECTS_H */
