// SPDX-License-Identifier: MPL-2.0

/* Motion Blur: isolated implementation module for Ruwa.Standard.Blur. */

#include "../BlurCommon.h"
#include "../BlurEffects.h"

#include <math.h>
#include <stdlib.h>

/* ==========================================================================
 *   Motion Blur (+ directional line coverage)
 * ========================================================================== */

static const char* const k_directional_fs =
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uTexelStep;\n"
    "uniform int uRadius;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    if (uRadius <= 0) {\n"
    "        outColor = texture(uSourceTexture, fragTexCoord);\n"
    "        return;\n"
    "    }\n"
    "    vec4 sum = vec4(0.0);\n"
    "    for (int i = -uRadius; i <= uRadius; ++i) {\n"
    "        sum += texture(uSourceTexture, fragTexCoord + uTexelStep * float(i));\n"
    "    }\n"
    "    outColor = sum / float(2 * uRadius + 1);\n"
    "}\n";

typedef struct MotionPass {
    RuwaEffectPipeline pipeline;
} MotionPass;

void* blur_motion_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_blur_host || !g_blur_host->gpu) {
        return NULL;
    }
    RuwaEffectPipeline pipeline = blur_create_graphics(
        gpu, k_directional_fs, "Ruwa.Standard.Blur/blur.motion", "ruwa.standard.blur.motion.fs");
    if (!pipeline) {
        return NULL;
    }
    MotionPass* pass = (MotionPass*)malloc(sizeof(MotionPass));
    if (!pass) {
        g_blur_host->gpu->destroy_graphics_pipeline(gpu, pipeline);
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

static int motion_reach_from_distance(int distance)
{
    return (blur_clampi(distance, 0, MOTION_MAX_DISTANCE) + 1) / 2;
}

int32_t blur_motion_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* d
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "distance");
    return motion_reach_from_distance(d ? d->value.as_int : 20);
}

/* --- directional line coverage (ports EffectCoverageResolver::lineCoverageOffsets) --- */

typedef struct TileOffset {
    int dx, dy;
} TileOffset;

static int floor_div(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

static int value_in_range(float v, float lo, float hi)
{
    return v >= lo && v <= hi;
}

static int clip_edge(float p, float q, float* t0, float* t1)
{
    if (p == 0.0f) {
        return q >= 0.0f;
    }
    const float r = q / p;
    if (p < 0.0f) {
        if (r > *t1) {
            return 0;
        }
        if (r > *t0) {
            *t0 = r;
        }
    } else {
        if (r < *t0) {
            return 0;
        }
        if (r < *t1) {
            *t1 = r;
        }
    }
    return 1;
}

static int segment_intersects_rect(float x0, float y0, float x1, float y1, float minX, float minY,
    float maxX, float maxY)
{
    if ((value_in_range(x0, minX, maxX) && value_in_range(y0, minY, maxY))
        || (value_in_range(x1, minX, maxX) && value_in_range(y1, minY, maxY))) {
        return 1;
    }
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float t0 = 0.0f;
    float t1 = 1.0f;
    return clip_edge(-dx, x0 - minX, &t0, &t1) && clip_edge(dx, maxX - x0, &t0, &t1)
        && clip_edge(-dy, y0 - minY, &t0, &t1) && clip_edge(dy, maxY - y0, &t0, &t1);
}

static int line_coverage_offsets(
    float angleDegrees, int negReach, int posReach, TileOffset* out, int maxOut)
{
    negReach = negReach < 0 ? 0 : negReach;
    posReach = posReach < 0 ? 0 : posReach;
    if (negReach == 0 && posReach == 0) {
        return 0;
    }
    const float a = angleDegrees * BLUR_PI / 180.0f;
    const float dirX = cosf(a);
    const float dirY = sinf(a);
    const float x0 = -dirX * (float)negReach;
    const float y0 = -dirY * (float)negReach;
    const float x1 = dirX * (float)posReach;
    const float y1 = dirY * (float)posReach;
    const float minSX = x0 < x1 ? x0 : x1;
    const float maxSX = x0 > x1 ? x0 : x1;
    const float minSY = y0 < y1 ? y0 : y1;
    const float maxSY = y0 > y1 ? y0 : y1;

    const int ts = BLUR_TILE_SIZE;
    const int minDx = floor_div((int)floorf(-maxSX), ts) - 1;
    const int maxDx = floor_div((int)ceilf((float)ts - minSX), ts) + 1;
    const int minDy = floor_div((int)floorf(-maxSY), ts) - 1;
    const int maxDy = floor_div((int)ceilf((float)ts - minSY), ts) + 1;

    int n = 0;
    for (int dy = minDy; dy <= maxDy; ++dy) {
        for (int dx = minDx; dx <= maxDx; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const float minX = -(float)(dx + 1) * ts + 1.0f;
            const float maxX = (float)(1 - dx) * ts - 1.0f;
            const float minY = -(float)(dy + 1) * ts + 1.0f;
            const float maxY = (float)(1 - dy) * ts - 1.0f;
            if (segment_intersects_rect(x0, y0, x1, y1, minX, minY, maxX, maxY)) {
                if (n < maxOut) {
                    out[n].dx = dx;
                    out[n].dy = dy;
                    ++n;
                }
            }
        }
    }
    return n;
}

/* Reproduces coverageTileOffsets: the line footprint unioned onto every input
 * tile. resolve_coverage returns the FULL next coverage, so emit the input tiles
 * themselves plus every offset copy. */
RuwaBool blur_motion_resolve_coverage(void* user_data, const RuwaEffectStateView* state,
    RuwaEffectCoverageInput input, RuwaEffectCoverageEmit emit_fn, void* emit_ctx)
{
    (void)user_data;
    if (!state || !g_blur_host || !emit_fn) {
        return RUWA_FALSE;
    }
    const RuwaEffectParamValue* dv
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "distance");
    const RuwaEffectParamValue* av
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "angle");
    const int reach = motion_reach_from_distance(dv ? dv->value.as_int : 20);
    const float angle = av ? (float)av->value.as_real : 0.0f;

    TileOffset offsets[64];
    const int count = line_coverage_offsets(angle, reach, reach, offsets, 64);

    const uint32_t inCount = g_blur_host->coverage_input_count(input);
    for (uint32_t i = 0; i < inCount; ++i) {
        const RuwaTileKey k = g_blur_host->coverage_input_at(input, i);
        emit_fn(emit_ctx, k); /* keep the source tile (union) */
        for (int j = 0; j < count; ++j) {
            RuwaTileKey ok;
            ok.x = k.x + offsets[j].dx;
            ok.y = k.y + offsets[j].dy;
            emit_fn(emit_ctx, ok);
        }
    }
    return RUWA_TRUE;
}

RuwaEffectTexture blur_motion_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    MotionPass* pass = (MotionPass*)pass_instance;
    if (!pass || !pass->pipeline || !g_blur_host || !g_blur_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_blur_host->gpu;

    const int radius = motion_reach_from_distance(blur_int_param(input, "distance", 20));
    if (radius <= 0) {
        return input->source_texture;
    }
    const float angleRadians = blur_real_param(input, "angle", 0.0f) * BLUR_PI / 180.0f;
    const float dirX = cosf(angleRadians);
    const float dirY = sinf(angleRadians);
    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const float stepX = dirX * (scale / (float)input->output_width);
    const float stepY = dirY * (scale / (float)input->output_height);

    gl->begin_render_pass(gpu, input->target_texture);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, 0, 0);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uTexelStep", stepX, stepY);
    gl->set_uniform_int(gpu, "uRadius", radius);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);

    return input->target_texture;
}

void blur_motion_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    MotionPass* pass = (MotionPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_blur_host && g_blur_host->gpu) {
        g_blur_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}
