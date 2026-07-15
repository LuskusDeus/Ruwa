// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_STANDARD_COLOR_COMMON_H
#define RUWA_STANDARD_COLOR_COMMON_H

#include <ruwa/effect/ruwa_effect_sdk.h>

/* The host table is process-lifetime state supplied once through the plugin
 * query entry point and shared by all effect implementation modules. */
extern const RuwaEffectHostApi* g_color_host;

void color_set_host_api(const RuwaEffectHostApi* host);

float color_clampf(float value, float minimum, float maximum);

#endif /* RUWA_STANDARD_COLOR_COMMON_H */
