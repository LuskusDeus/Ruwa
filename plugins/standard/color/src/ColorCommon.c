// SPDX-License-Identifier: MPL-2.0

#include "ColorCommon.h"

const RuwaEffectHostApi* g_color_host = NULL;

void color_set_host_api(const RuwaEffectHostApi* host)
{
    g_color_host = host;
}

float color_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}
