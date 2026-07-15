// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   R U W A   |   E F F E C T   S D K   —   A B I   v1   (GPU + pass input)
 * ==========================================================================
 *
 *   The per-invocation pass input (§2.1 of the plan) and the host GPU command
 *   API (§2.2 / §6). The GPU API is deliberately the EXACT set of operations
 *   the existing effects use — nothing speculative. Every call is expressible
 *   by an arbitrary third-party plugin; there are no private host paths.
 * ========================================================================== */

#ifndef RUWA_EFFECT_GPU_H
#define RUWA_EFFECT_GPU_H

#include "ruwa_effect_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Tile key ----------------------------------------------------------- */

/* POD mirror of aether::TileKey, the coverage currency. Passed by value across
 * the ABI (never a C++ container). */
typedef struct RuwaTileKey {
    int32_t x;
    int32_t y;
} RuwaTileKey;

/* --- Typed parameter value ----------------------------------------------
 * A resolved parameter as seen at render / coverage time. Colours arrive as
 * sRGB-stored floats in [0,1] (the same numbers QColor::redF()… yields from the
 * stored "#RRGGBBAA"); the host adapter owns the string <-> float conversion so
 * documents keep their existing format. Choices arrive as an index into the
 * declared `choices`; the adapter maps that to / from the stored choice string. */
typedef struct RuwaEffectParamValue {
    RuwaEffectParamType type;
    union {
        RuwaBool as_bool;
        int32_t  as_int;
        double   as_real;
        float    as_color[4]; /* r,g,b,a in [0,1] */
        int32_t  as_choice;   /* index into RuwaEffectParamDef::choices */
    } value;
} RuwaEffectParamValue;

/* --- Region frame (fragment -> document mapping) -------------------------
 * POD mirror of ruwa::core::effects::EffectRegionFrame. Lets a positional /
 * distortion effect be a function of absolute document position. When
 * `valid == RUWA_FALSE` a positional effect must no-op / pass through. When
 * `use_affine` is set the mapping is the full 2D affine (rotated/flipped/zoomed
 * viewport preview); otherwise it is origin + fragTexCoord * outputSize *
 * document_px_per_texel and the host fills the basis from the output size. */
typedef struct RuwaEffectRegionFrame {
    float   origin_x;
    float   origin_y;
    float   document_px_per_texel;
    RuwaBool valid;
    RuwaBool use_affine;
    float   basis_xx; /* d(documentPos)/d(fragTexCoord.x), x component */
    float   basis_xy; /* d(documentPos)/d(fragTexCoord.x), y component */
    float   basis_yx; /* d(documentPos)/d(fragTexCoord.y), x component */
    float   basis_yy; /* d(documentPos)/d(fragTexCoord.y), y component */
} RuwaEffectRegionFrame;

/* --- Per-invocation pass input (§2.1) ------------------------------------
 * The named inputs the host passes to every render_pass call. Versioned with
 * its own `struct_size`; NOT dissolved into "uniforms/bindings". Read only the
 * declared prefix. */
typedef struct RuwaEffectPassInput {
    uint32_t struct_size;

    /* Target region geometry. */
    uint32_t                   output_width;
    uint32_t                   output_height;
    RuwaEffectEvaluationSpace  evaluation_space;
    /* Multiplies pixel-radius sampling distances so a DOCUMENT-pixel radius has
     * the right magnitude in the current space (1.0 in document-tile space; the
     * camera zoom in viewport space). */
    float                      space_scale;
    RuwaEffectRegionFrame      region;
    /* True when source spans the WHOLE layer (its materialised bbox) — the
     * guarantee a distortion (reads_whole_layer) needs before sampling far from
     * a fragment. When false, a distortion pass must pass its input through. */
    RuwaBool                   whole_layer_source;

    /* Textures (any may be null). */
    RuwaEffectTexture source_texture;      /* this pass's chain input */
    RuwaEffectTexture target_texture;      /* default write target, output-sized */
    RuwaEffectTexture layer_alpha_texture; /* layer shape BEFORE the chain (stroke/glow/bevel) */
    RuwaEffectTexture backdrop_texture;    /* backing under the layer (requires_backdrop) */

    /* Output-space rect downstream actually reads; a pass MAY clip writes to it
     * via begin_roi_scissor (optimization only). roi_width == 0 means
     * "unknown / everything is needed". */
    int32_t  roi_x;
    int32_t  roi_y;
    uint32_t roi_width;
    uint32_t roi_height;

    /* Resolved parameters, in the SAME ORDER the descriptor declared them.
     * `param_keys[i]` is the stable key of `param_values[i]`. A plugin may index
     * by declaration order or match by key. */
    uint32_t                     param_count;
    const char* const*           param_keys;
    const RuwaEffectParamValue*  param_values;
    /* Schema version of the effect state being rendered (LayerEffectState::version). */
    uint32_t                     state_version;
} RuwaEffectPassInput;

/* --- Shader source view --------------------------------------------------
 * The host compiles GLSL, owns the shader cache and creates / destroys the real
 * GL program. The plugin only hands over source. Provide a stable `cache_key`
 * so identical sources share a linked program across pass instances / frames. */
typedef struct RuwaEffectShaderSource {
    uint32_t              struct_size;
    RuwaEffectShaderStage stage;
    const char*           source;        /* GLSL text */
    uint32_t              source_length; /* bytes; 0 => `source` is null-terminated */
    const char*           debug_name;    /* for logs; may be null */
    const char*           cache_key;     /* stable; may be null (no caching) */
} RuwaEffectShaderSource;

/* --- Host GPU command API (§2.2 / §6) ------------------------------------
 * A single table of function pointers, provided by the host, reached through
 * RuwaEffectHostApi::gpu. Every call takes the RuwaEffectGpuContext handed to a
 * pass callback as its first argument.
 *
 * Validity:
 *   [R] Resource lifecycle — legal in create_pass / render_pass / destroy_pass.
 *   [C] Commands & transient scratch — legal ONLY inside render_pass, between a
 *       begin_render_pass and the end of the call.
 * Calling a [C] function outside render_pass, or any function off the render
 * thread / without the GL context current, is undefined and rejected. */
typedef struct RuwaEffectGpuApi {
    uint32_t struct_size;

    /* [R] Persistent, plugin-owned textures. Created with NEAREST/CLAMP_TO_EDGE
     * defaults; use a sampler object for filtered reads. Resize by destroy +
     * create. Must be destroyed in destroy_pass (before the context dies). */
    RuwaEffectTexture (RUWA_EFFECT_CALL *create_texture)(
        RuwaEffectGpuContext gpu, uint32_t width, uint32_t height,
        RuwaEffectTextureFormat format);
    void (RUWA_EFFECT_CALL *destroy_texture)(
        RuwaEffectGpuContext gpu, RuwaEffectTexture texture);
    uint32_t (RUWA_EFFECT_CALL *texture_width)(
        RuwaEffectGpuContext gpu, RuwaEffectTexture texture);
    uint32_t (RUWA_EFFECT_CALL *texture_height)(
        RuwaEffectGpuContext gpu, RuwaEffectTexture texture);

    /* [R] Persistent, plugin-owned sampler objects. */
    RuwaEffectSampler (RUWA_EFFECT_CALL *create_sampler)(
        RuwaEffectGpuContext gpu, RuwaEffectSamplerFilter min_filter,
        RuwaEffectSamplerFilter mag_filter, RuwaEffectSamplerWrap wrap);
    void (RUWA_EFFECT_CALL *destroy_sampler)(
        RuwaEffectGpuContext gpu, RuwaEffectSampler sampler);

    /* [R] Pipelines. Graphics pipelines pair the host's shared fullscreen vertex
     * shader with the plugin fragment source; compute pipelines take one compute
     * source. The `fragment` / `compute` source's `stage` field must match. */
    RuwaEffectPipeline (RUWA_EFFECT_CALL *create_graphics_pipeline)(
        RuwaEffectGpuContext gpu, const RuwaEffectShaderSource* fragment);
    void (RUWA_EFFECT_CALL *destroy_graphics_pipeline)(
        RuwaEffectGpuContext gpu, RuwaEffectPipeline pipeline);
    RuwaEffectComputePipeline (RUWA_EFFECT_CALL *create_compute_pipeline)(
        RuwaEffectGpuContext gpu, const RuwaEffectShaderSource* compute);
    void (RUWA_EFFECT_CALL *destroy_compute_pipeline)(
        RuwaEffectGpuContext gpu, RuwaEffectComputePipeline pipeline);

    /* [C] Transient, host-owned scratch sized to the current region and valid
     * only for THIS render_pass call. The plugin must not destroy it. Formats
     * RGBA8 / RGBA16F (high precision); returns null on failure. */
    RuwaEffectTexture (RUWA_EFFECT_CALL *alloc_scratch_texture)(
        RuwaEffectGpuContext gpu, RuwaEffectTextureFormat format);

    /* [C] Graphics. begin_render_pass binds `target` as the colour attachment,
     * sets the viewport to the target texture's own size and disables blending.
     * draw_fullscreen issues the shared 6-vertex fullscreen triangle pair. */
    void (RUWA_EFFECT_CALL *begin_render_pass)(
        RuwaEffectGpuContext gpu, RuwaEffectTexture target);
    void (RUWA_EFFECT_CALL *bind_graphics_pipeline)(
        RuwaEffectGpuContext gpu, RuwaEffectPipeline pipeline);
    void (RUWA_EFFECT_CALL *bind_texture)(
        RuwaEffectGpuContext gpu, uint32_t unit, RuwaEffectTexture texture,
        RuwaEffectSampler sampler /* may be null for the default */);
    void (RUWA_EFFECT_CALL *draw_fullscreen)(RuwaEffectGpuContext gpu);

    /* [C] ROI scissor. Returns RUWA_TRUE if a scissor was enabled (a 1-texel
     * guard ring is always added); the caller must pass that back to
     * end_roi_scissor. No-op / RUWA_FALSE when the ROI is unknown or full. */
    RuwaBool (RUWA_EFFECT_CALL *begin_roi_scissor)(
        RuwaEffectGpuContext gpu, int32_t expand_x, int32_t expand_y);
    void (RUWA_EFFECT_CALL *end_roi_scissor)(
        RuwaEffectGpuContext gpu, RuwaBool scissor_active);

    /* [C] Compute / image load-store. */
    void (RUWA_EFFECT_CALL *bind_compute_pipeline)(
        RuwaEffectGpuContext gpu, RuwaEffectComputePipeline pipeline);
    void (RUWA_EFFECT_CALL *bind_image_texture)(
        RuwaEffectGpuContext gpu, uint32_t unit, RuwaEffectTexture texture,
        RuwaEffectImageAccess access, RuwaEffectTextureFormat format);
    void (RUWA_EFFECT_CALL *dispatch_compute)(
        RuwaEffectGpuContext gpu, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);
    void (RUWA_EFFECT_CALL *memory_barrier)(
        RuwaEffectGpuContext gpu, uint32_t barrier_bits /* RuwaEffectBarrierBits */);

    /* [C] Uniforms. Operate on the currently bound graphics or compute pipeline;
     * a missing / optimised-out uniform name is silently ignored. */
    void (RUWA_EFFECT_CALL *set_uniform_int)(
        RuwaEffectGpuContext gpu, const char* name, int32_t v);
    void (RUWA_EFFECT_CALL *set_uniform_int2)(
        RuwaEffectGpuContext gpu, const char* name, int32_t x, int32_t y);
    void (RUWA_EFFECT_CALL *set_uniform_float)(
        RuwaEffectGpuContext gpu, const char* name, float v);
    void (RUWA_EFFECT_CALL *set_uniform_float2)(
        RuwaEffectGpuContext gpu, const char* name, float x, float y);
    void (RUWA_EFFECT_CALL *set_uniform_float3)(
        RuwaEffectGpuContext gpu, const char* name, float x, float y, float z);
    void (RUWA_EFFECT_CALL *set_uniform_float4)(
        RuwaEffectGpuContext gpu, const char* name, float x, float y, float z, float w);
    /* `values` is tightly packed x0,y0,x1,y1,… for `count` vec2 elements. */
    void (RUWA_EFFECT_CALL *set_uniform_vec2_array)(
        RuwaEffectGpuContext gpu, const char* name, const float* values, uint32_t count);

    /* [C] Full-texture copy (internal crop / passthrough shape). src and dst
     * must share dimensions and be colour-renderable. */
    void (RUWA_EFFECT_CALL *blit_texture)(
        RuwaEffectGpuContext gpu, RuwaEffectTexture src, RuwaEffectTexture dst);
} RuwaEffectGpuApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RUWA_EFFECT_GPU_H */
