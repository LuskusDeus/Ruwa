// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_TEXTURE_COMMON_H
#define RUWA_STANDARD_TEXTURE_COMMON_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* Forward fragment<->document frame (origin + basis); the texture shader is a
 * pure function of absolute document position, so it needs no inverse. */
typedef struct TextureDocFrame {
    float originX, originY;
    float basisXx, basisXy, basisYx, basisYy;
} TextureDocFrame;

/* The host table is process-lifetime state supplied once through the plugin
 * query entry point and shared by all effect implementation modules. */
extern const RuwaEffectHostApi* g_texture_host;

void texture_set_host_api(const RuwaEffectHostApi* host);

float texture_clampf(float value, float minimum, float maximum);
int texture_clampi(int value, int minimum, int maximum);
float texture_real_param(const RuwaEffectPassInput* input, const char* key, float fallback);
int texture_int_param(const RuwaEffectPassInput* input, const char* key, int fallback);
int texture_bool_param(const RuwaEffectPassInput* input, const char* key, int fallback);

TextureDocFrame texture_compute_doc_frame(const RuwaEffectPassInput* input);

#endif /* RUWA_STANDARD_TEXTURE_COMMON_H */
