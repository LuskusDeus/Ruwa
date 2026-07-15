// SPDX-License-Identifier: MPL-2.0

/* Box Blur: isolated implementation module for Ruwa.Standard.Blur. */

#include "../BlurCommon.h"
#include "../BlurEffects.h"

#include <stdlib.h>

/* ==========================================================================
 *   Box Blur
 * ========================================================================== */

static const char* const k_box_fs =
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
    "    vec4 sum = texture(uSourceTexture, fragTexCoord);\n"
    "    int pairs = uRadius / 2;\n"
    "    for (int k = 1; k <= pairs; ++k) {\n"
    "        vec2 offset = uTexelStep * (float(2 * k) - 0.5);\n"
    "        sum += 2.0 * (texture(uSourceTexture, fragTexCoord + offset)\n"
    "                      + texture(uSourceTexture, fragTexCoord - offset));\n"
    "    }\n"
    "    if ((uRadius & 1) == 1) {\n"
    "        vec2 offset = uTexelStep * float(uRadius);\n"
    "        sum += texture(uSourceTexture, fragTexCoord + offset)\n"
    "               + texture(uSourceTexture, fragTexCoord - offset);\n"
    "    }\n"
    "    outColor = sum / float(2 * uRadius + 1);\n"
    "}\n";

static const char* const k_box_comp =
    "#version 450 core\n"
    "layout(local_size_x = 256) in;\n"
    "layout(rgba32f, binding = 0) uniform writeonly image2D uPrefixImage;\n"
    "layout(rgba8, binding = 1) uniform writeonly image2D uTargetImage;\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform int uRadius;\n"
    "uniform int uVertical;\n"
    "uniform int uPhase;\n"
    "const int kGroup = 256;\n"
    "shared vec4 sScanA[kGroup];\n"
    "shared vec4 sScanB[kGroup];\n"
    "ivec2 texelAt(int line, int p) {\n"
    "    return uVertical == 1 ? ivec2(line, p) : ivec2(p, line);\n"
    "}\n"
    "vec4 loadByte(int line, int p) {\n"
    "    return round(texelFetch(uSourceTexture, texelAt(line, p), 0) * 255.0);\n"
    "}\n"
    "vec4 prefixAt(int line, int p) {\n"
    "    return texelFetch(uSourceTexture, texelAt(line, p), 0);\n"
    "}\n"
    "void scanLine(int lineLength) {\n"
    "    int line = int(gl_WorkGroupID.x);\n"
    "    int tid  = int(gl_LocalInvocationID.x);\n"
    "    int baseLen  = lineLength / kGroup;\n"
    "    int rem      = lineLength - baseLen * kGroup;\n"
    "    int segLen   = baseLen + (tid < rem ? 1 : 0);\n"
    "    int segStart = tid * baseLen + min(tid, rem);\n"
    "    int segEnd   = segStart + segLen;\n"
    "    vec4 localSum = vec4(0.0);\n"
    "    for (int p = segStart; p < segEnd; ++p) {\n"
    "        localSum += loadByte(line, p);\n"
    "    }\n"
    "    sScanA[tid] = localSum;\n"
    "    barrier();\n"
    "    bool srcIsA = true;\n"
    "    for (int offset = 1; offset < kGroup; offset <<= 1) {\n"
    "        if (srcIsA) {\n"
    "            sScanB[tid] = (tid >= offset) ? sScanA[tid] + sScanA[tid - offset] : sScanA[tid];\n"
    "        } else {\n"
    "            sScanA[tid] = (tid >= offset) ? sScanB[tid] + sScanB[tid - offset] : sScanB[tid];\n"
    "        }\n"
    "        barrier();\n"
    "        srcIsA = !srcIsA;\n"
    "    }\n"
    "    vec4 inclusivePartial = srcIsA ? sScanA[tid] : sScanB[tid];\n"
    "    vec4 exclusiveBase = inclusivePartial - localSum;\n"
    "    vec4 running = exclusiveBase;\n"
    "    for (int p = segStart; p < segEnd; ++p) {\n"
    "        running += loadByte(line, p);\n"
    "        imageStore(uPrefixImage, texelAt(line, p), running);\n"
    "    }\n"
    "}\n"
    "void boxDifference(int lineCount, int lineLength) {\n"
    "    int gid   = int(gl_GlobalInvocationID.x);\n"
    "    int total = lineCount * lineLength;\n"
    "    if (gid >= total) {\n"
    "        return;\n"
    "    }\n"
    "    int line = gid / lineLength;\n"
    "    int x    = gid - line * lineLength;\n"
    "    int last = lineLength - 1;\n"
    "    int R    = uRadius;\n"
    "    int lo = max(x - R, 0);\n"
    "    int hi = min(x + R, last);\n"
    "    vec4 interior = prefixAt(line, hi) - (lo > 0 ? prefixAt(line, lo - 1) : vec4(0.0));\n"
    "    vec4 edge0 = prefixAt(line, 0);\n"
    "    vec4 edgeN = (lineLength >= 2) ? (prefixAt(line, last) - prefixAt(line, last - 1)) : edge0;\n"
    "    int leftOverflow  = max(0, R - x);\n"
    "    int rightOverflow = max(0, (x + R) - last);\n"
    "    vec4 sum = interior\n"
    "             + float(leftOverflow)  * edge0\n"
    "             + float(rightOverflow) * edgeN;\n"
    "    float windowScale = 255.0 * float(2 * R + 1);\n"
    "    imageStore(uTargetImage, texelAt(line, x), sum / windowScale);\n"
    "}\n"
    "void main() {\n"
    "    ivec2 size = textureSize(uSourceTexture, 0);\n"
    "    int lineCount  = uVertical == 1 ? size.x : size.y;\n"
    "    int lineLength = uVertical == 1 ? size.y : size.x;\n"
    "    if (uPhase == 0) {\n"
    "        if (int(gl_WorkGroupID.x) >= lineCount) {\n"
    "            return;\n"
    "        }\n"
    "        scanLine(lineLength);\n"
    "    } else {\n"
    "        boxDifference(lineCount, lineLength);\n"
    "    }\n"
    "}\n";

typedef struct BoxPass {
    RuwaEffectPipeline gfx;
    RuwaEffectComputePipeline comp;
    RuwaEffectSampler linearSampler;
    RuwaEffectTexture prefix;
    uint32_t prefixW, prefixH;
} BoxPass;

void* blur_box_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data;
    if (!g_blur_host || !g_blur_host->gpu) {
        return NULL;
    }
    const RuwaEffectGpuApi* gl = g_blur_host->gpu;

    RuwaEffectPipeline gfx
        = blur_create_graphics(gpu, k_box_fs, "Ruwa.Standard.Blur/blur.box", "ruwa.standard.blur.box.fs");
    if (!gfx) {
        return NULL;
    }

    RuwaEffectShaderSource cs;
    cs.struct_size   = sizeof(RuwaEffectShaderSource);
    cs.stage         = RUWA_EFFECT_STAGE_COMPUTE;
    cs.source        = k_box_comp;
    cs.source_length = 0u;
    cs.debug_name    = "Ruwa.Standard.Blur/blur.box.compute";
    cs.cache_key     = "ruwa.standard.blur.box.comp";
    RuwaEffectComputePipeline comp = gl->create_compute_pipeline(gpu, &cs);
    if (!comp) {
        gl->destroy_graphics_pipeline(gpu, gfx);
        return NULL;
    }

    BoxPass* pass = (BoxPass*)malloc(sizeof(BoxPass));
    if (!pass) {
        gl->destroy_compute_pipeline(gpu, comp);
        gl->destroy_graphics_pipeline(gpu, gfx);
        return NULL;
    }
    pass->gfx = gfx;
    pass->comp = comp;
    pass->linearSampler = blur_create_linear_sampler(gpu);
    pass->prefix = NULL;
    pass->prefixW = 0;
    pass->prefixH = 0;
    return pass;
}

int32_t blur_box_pixel_expansion_radius(void* user_data, const RuwaEffectStateView* state)
{
    (void)user_data;
    if (!state) {
        return 0;
    }
    const RuwaEffectParamValue* rv
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "radius");
    const RuwaEffectParamValue* iv
        = blur_find_param(state->param_count, state->param_keys, state->param_values, "iterations");
    const int radius = blur_clampi(rv ? rv->value.as_int : 8, 0, BOX_MAX_RADIUS);
    const int iterations = blur_clampi(iv ? iv->value.as_int : 1, 1, BOX_MAX_ITERATIONS);
    return radius * iterations;
}

/* Persistent RGBA32F line-prefix texture, resized (destroy + create) to the
 * working region. RGBA32F because an inclusive line prefix reaches N*255. */
static RuwaEffectTexture box_ensure_prefix(
    const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, BoxPass* pass, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) {
        return NULL;
    }
    if (pass->prefix && pass->prefixW == w && pass->prefixH == h) {
        return pass->prefix;
    }
    if (pass->prefix) {
        gl->destroy_texture(gpu, pass->prefix);
        pass->prefix = NULL;
    }
    pass->prefix = gl->create_texture(gpu, w, h, RUWA_EFFECT_FORMAT_RGBA32F);
    pass->prefixW = w;
    pass->prefixH = h;
    return pass->prefix;
}

static void box_dispatch_scan(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, BoxPass* pass,
    const RuwaEffectPassInput* input, RuwaEffectTexture src, int vertical)
{
    gl->bind_compute_pipeline(gpu, pass->comp);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_int(gpu, "uVertical", vertical);
    gl->set_uniform_int(gpu, "uPhase", 0);
    gl->bind_texture(gpu, 0, src, NULL);
    gl->bind_image_texture(gpu, 0, pass->prefix, RUWA_EFFECT_IMAGE_WRITE_ONLY, RUWA_EFFECT_FORMAT_RGBA32F);
    const uint32_t lineCount = vertical ? input->output_width : input->output_height;
    gl->dispatch_compute(gpu, lineCount, 1, 1);
}

static void box_dispatch_difference(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu,
    BoxPass* pass, const RuwaEffectPassInput* input, RuwaEffectTexture target, int radius,
    int vertical)
{
    gl->bind_compute_pipeline(gpu, pass->comp);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_int(gpu, "uRadius", radius);
    gl->set_uniform_int(gpu, "uVertical", vertical);
    gl->set_uniform_int(gpu, "uPhase", 1);
    gl->bind_texture(gpu, 0, pass->prefix, NULL);
    gl->bind_image_texture(gpu, 1, target, RUWA_EFFECT_IMAGE_WRITE_ONLY, RUWA_EFFECT_FORMAT_RGBA8);
    const uint32_t total = input->output_width * input->output_height;
    const uint32_t groups = (total + BOX_SCAN_GROUP - 1u) / BOX_SCAN_GROUP;
    gl->dispatch_compute(gpu, groups, 1, 1);
}

static void box_draw_axis(const RuwaEffectGpuApi* gl, RuwaEffectGpuContext gpu, const BoxPass* pass,
    RuwaEffectTexture src, RuwaEffectTexture target, int radius, float stepX, float stepY,
    int roiExpandX, int roiExpandY)
{
    gl->begin_render_pass(gpu, target);
    const RuwaBool scissor = gl->begin_roi_scissor(gpu, roiExpandX, roiExpandY);
    gl->bind_graphics_pipeline(gpu, pass->gfx);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float2(gpu, "uTexelStep", stepX, stepY);
    gl->set_uniform_int(gpu, "uRadius", radius);
    gl->bind_texture(gpu, 0, src, pass->linearSampler);
    gl->draw_fullscreen(gpu);
    gl->end_roi_scissor(gpu, scissor);
}

RuwaEffectTexture blur_box_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    BoxPass* pass = (BoxPass*)pass_instance;
    if (!pass || !pass->gfx || !g_blur_host || !g_blur_host->gpu || !input || !input->source_texture
        || !input->target_texture || input->output_width == 0 || input->output_height == 0) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_blur_host->gpu;

    const int radius = blur_clampi(blur_int_param(input, "radius", 8), 0, BOX_MAX_RADIUS);
    if (radius <= 0) {
        return input->source_texture;
    }
    RuwaEffectTexture intermediate = gl->alloc_scratch_texture(gpu, RUWA_EFFECT_FORMAT_RGBA8);
    if (!intermediate) {
        return input->source_texture;
    }

    const int iterations = blur_clampi(blur_int_param(input, "iterations", 1), 1, BOX_MAX_ITERATIONS);
    const int passes = 2 * iterations;
    const RuwaEffectTexture pingPong[2] = { intermediate, input->target_texture };

    /* Document-tile space: exact O(1) prefix-sum compute path. */
    if (input->evaluation_space == RUWA_EFFECT_SPACE_DOCUMENT_TILE && pass->comp
        && box_ensure_prefix(gl, gpu, pass, input->output_width, input->output_height)) {
        RuwaEffectTexture readTexture = input->source_texture;
        for (int p = 0; p < passes; ++p) {
            const RuwaEffectTexture writeTexture = pingPong[p % 2];
            box_dispatch_scan(gl, gpu, pass, input, readTexture, p % 2);
            gl->memory_barrier(gpu,
                RUWA_EFFECT_BARRIER_TEXTURE_FETCH | RUWA_EFFECT_BARRIER_SHADER_IMAGE_ACCESS);
            box_dispatch_difference(gl, gpu, pass, input, writeTexture, radius, p % 2);
            if (p + 1 < passes) {
                gl->memory_barrier(gpu,
                    RUWA_EFFECT_BARRIER_TEXTURE_FETCH | RUWA_EFFECT_BARRIER_SHADER_IMAGE_ACCESS);
            }
            readTexture = writeTexture;
        }
        gl->memory_barrier(gpu,
            RUWA_EFFECT_BARRIER_TEXTURE_FETCH | RUWA_EFFECT_BARRIER_FRAMEBUFFER
                | RUWA_EFFECT_BARRIER_SHADER_IMAGE_ACCESS);
        return input->target_texture;
    }

    /* Viewport space (or no prefix texture): paired-tap fragment fallback. */
    const float scale = input->space_scale > 0.0f ? input->space_scale : 1.0f;
    const float invW = scale / (float)input->output_width;
    const float invH = scale / (float)input->output_height;

    RuwaEffectTexture readTexture = input->source_texture;
    for (int p = 0; p < passes; ++p) {
        const int vertical = (p % 2) == 1;
        const int remaining = passes - 1 - p;
        const int reachOwnAxis = radius * (remaining / 2);
        const int reachOtherAxis = radius * ((remaining + 1) / 2);
        box_draw_axis(gl, gpu, pass, readTexture, pingPong[p % 2], radius,
            vertical ? 0.0f : invW, vertical ? invH : 0.0f,
            vertical ? reachOtherAxis : reachOwnAxis, vertical ? reachOwnAxis : reachOtherAxis);
        readTexture = pingPong[p % 2];
    }
    return input->target_texture;
}

void blur_box_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    BoxPass* pass = (BoxPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (g_blur_host && g_blur_host->gpu) {
        const RuwaEffectGpuApi* gl = g_blur_host->gpu;
        if (pass->prefix) {
            gl->destroy_texture(gpu, pass->prefix);
        }
        if (pass->linearSampler) {
            gl->destroy_sampler(gpu, pass->linearSampler);
        }
        if (pass->comp) {
            gl->destroy_compute_pipeline(gpu, pass->comp);
        }
        if (pass->gfx) {
            gl->destroy_graphics_pipeline(gpu, pass->gfx);
        }
    }
    free(pass);
}
