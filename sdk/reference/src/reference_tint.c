// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   R U W A   |   R E F E R E N C E   E F F E C T   P L U G I N   (ABI v1)
 * ==========================================================================
 *
 *   THE canonical "hello world" of the Ruwa Effect SDK. One tiny, real effect —
 *   "Reference Tint" — authored from scratch against ONLY <ruwa/effect/...>. No
 *   Qt, no STL, no src/ headers, no host internals. If this file compiles with
 *   just `sdk/include` on the include path and loads into Ruwa, the SDK is
 *   self-sufficient — that is the whole point of a reference plugin.
 *
 *   It is deliberately NOT part of the application build: nothing in the root
 *   CMake references it. Build it standalone (see sdk/reference/README.md) to
 *   prove the SDK stands on its own, or copy this directory as the seed of your
 *   own plugin.
 *
 *   What the effect does
 *   --------------------
 *   Blends every covered pixel of the layer toward a chosen colour by an
 *   "Amount". It changes colour only where the layer already has coverage — it
 *   never expands bounds, reads neighbours, or touches the backdrop — so it is
 *   the simplest possible shape of a GPU effect: one fragment pipeline, one
 *   fullscreen draw.
 *
 *   Read it top to bottom. The five sections mirror the SDK contract:
 *     1. Host handle           — the table the host hands us at query time.
 *     2. GPU pass lifecycle    — create_pass / render_pass / destroy_pass.
 *     3. Parameter & metadata  — what the picker and property panel show.
 *     4. Descriptor + plugin   — how those wire into the ABI tables.
 *     5. The one entry point   — ruwa_effect_plugin_query.
 *
 *   The full written contract lives in sdk/README.md; this file is the runnable
 *   companion to it.
 * ========================================================================== */

#include <ruwa/effect/ruwa_effect_sdk.h> /* the umbrella header — include just this */

#include <stdlib.h> /* malloc / free */
#include <string.h> /* strcmp        */

/* ==========================================================================
 *   1.  HOST HANDLE
 * ==========================================================================
 *
 *   The host passes its RuwaEffectHostApi table into ruwa_effect_plugin_query
 *   exactly once. It stays valid for the plugin's whole lifetime, so we stash it
 *   in one process-global pointer and reach the GPU table + logger through it.
 *
 *   Contract reminder: do NOT call any host function from inside `query` itself
 *   (no GL context is current yet). We only store the pointer there. */
static const RuwaEffectHostApi* g_host = NULL;

/* Tiny logging convenience so the body reads cleanly. Safe from any thread. */
static void ref_log(RuwaEffectLogLevel level, const char* message)
{
    if (g_host && g_host->log && message) {
        g_host->log(level, message);
    }
}

/* ==========================================================================
 *   2.  GPU PASS LIFECYCLE   (render thread only, GL context current)
 * ==========================================================================
 *
 *   The host creates ONE pass instance per (effect x GL context) and shares it
 *   across every layer that uses the effect. So the pass state below is about
 *   the *compiled program*, never about a particular layer.
 *
 *   The fragment shader. The host supplies the fullscreen vertex shader and
 *   links it in automatically; it hands us:
 *     - `in  vec2 fragTexCoord;`  texcoord in [0,1] across the output region
 *     - `out vec4 outColor;`      the premultiplied-alpha result we must write
 *   and binds this pass's chain input to texture unit 0. Everything Ruwa moves
 *   through the effect chain is PREMULTIPLIED alpha (rgb already * a), so we
 *   unpremultiply to operate on straight colour, then repremultiply on the way
 *   out. Handling the a == 0 case first avoids a divide-by-zero. */
static const char* const k_tint_fragment_shader =
    "#version 450 core\n"
    "\n"
    "uniform sampler2D uSourceTexture; /* this pass's chain input, unit 0 */\n"
    "uniform vec4      uTintColor;     /* sRGB rgba in [0,1] */\n"
    "uniform float     uAmount;        /* 0 = original, 1 = full tint */\n"
    "\n"
    "in  vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "\n"
    "void main() {\n"
    "    vec4 premul = texture(uSourceTexture, fragTexCoord);\n"
    "    if (premul.a <= 0.0) {      /* no coverage: stay fully transparent */\n"
    "        outColor = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 straight = premul.rgb / premul.a;              /* unpremultiply */\n"
    "    float t = clamp(uAmount * uTintColor.a, 0.0, 1.0);  /* colour alpha eases the tint */\n"
    "    vec3 tinted = mix(straight, uTintColor.rgb, t);\n"
    "    outColor = vec4(tinted * premul.a, premul.a);       /* repremultiply; alpha unchanged */\n"
    "}\n";

/* Per-pass state: just the one compiled graphics pipeline. A real effect might
 * also hold samplers or persistent textures — all created in create_pass and
 * destroyed in destroy_pass, one create/destroy pair per resource. */
typedef struct TintPass {
    RuwaEffectPipeline pipeline;
} TintPass;

/* create_pass — compile pipelines / create samplers for one GL context. Returns
 * a plugin-owned opaque pointer handed back to render_pass / destroy_pass.
 * Returning NULL disables this effect on this context WITHOUT disturbing others;
 * the host isolates the failure. */
static void* RUWA_EFFECT_CALL tint_create_pass(void* user_data, RuwaEffectGpuContext gpu)
{
    (void)user_data; /* unused: this effect keeps no per-descriptor state */

    if (!g_host || !g_host->gpu) {
        return NULL;
    }
    const RuwaEffectGpuApi* gl = g_host->gpu;

    /* Describe the fragment source. The host owns GLSL compilation, the shader
     * cache and the real GL program; we only hand over text. A stable cache_key
     * lets identical sources share one linked program across instances/frames. */
    RuwaEffectShaderSource fragment;
    fragment.struct_size   = sizeof(RuwaEffectShaderSource);
    fragment.stage         = RUWA_EFFECT_STAGE_FRAGMENT;
    fragment.source        = k_tint_fragment_shader;
    fragment.source_length = 0u; /* 0 => source is null-terminated */
    fragment.debug_name    = "Ruwa.Reference/tint";
    fragment.cache_key     = "com.ruwa.reference.tint.fs";

    RuwaEffectPipeline pipeline = gl->create_graphics_pipeline(gpu, &fragment);
    if (!pipeline) {
        ref_log(RUWA_EFFECT_LOG_ERROR, "Ruwa.Reference: tint pipeline failed to compile");
        return NULL;
    }

    TintPass* pass = (TintPass*)malloc(sizeof(TintPass));
    if (!pass) {
        gl->destroy_graphics_pipeline(gpu, pipeline); /* no leak on OOM */
        return NULL;
    }
    pass->pipeline = pipeline;
    return pass;
}

/* render_pass — one invocation over one output region. Return the texture that
 * holds the result: our target_texture on success, or source_texture unchanged
 * for a pass-through no-op. The host restores its own framebuffer/viewport/blend
 * afterward, so we are free to set whatever GL state we need. */
static RuwaEffectTexture RUWA_EFFECT_CALL tint_render_pass(
    void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input)
{
    TintPass* pass = (TintPass*)pass_instance;

    /* Defensive pass-through: if anything essential is missing, hand the input
     * straight back rather than failing the whole chain. */
    if (!pass || !pass->pipeline || !g_host || !g_host->gpu || !input || !input->source_texture
        || !input->target_texture) {
        return input ? input->source_texture : NULL;
    }
    const RuwaEffectGpuApi* gl = g_host->gpu;

    /* Resolve our parameters. `param_values[i]` is in the SAME ORDER the
     * descriptor declared them, and `param_keys[i]` is its stable key. You may
     * index by declaration order or match by key; matching by key (below) is
     * robust to a host that ever reorders or omits values. Colours arrive as
     * sRGB floats in [0,1]; choices would arrive as `value.as_choice` (an index
     * into RuwaEffectParamDef::choices); bools as `value.as_bool`; ints/reals as
     * `value.as_int` / `value.as_real`. */
    float amount = 0.5f;
    float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (uint32_t i = 0; i < input->param_count; ++i) {
        const char* key = input->param_keys[i];
        const RuwaEffectParamValue* v = &input->param_values[i];
        if (!key) {
            continue;
        }
        if (strcmp(key, "amount") == 0) {
            amount = (float)v->value.as_real;
        } else if (strcmp(key, "tintColor") == 0) {
            tint[0] = v->value.as_color[0];
            tint[1] = v->value.as_color[1];
            tint[2] = v->value.as_color[2];
            tint[3] = v->value.as_color[3];
        }
    }
    if (amount < 0.0f) {
        amount = 0.0f;
    } else if (amount > 1.0f) {
        amount = 1.0f;
    }

    /* Draw. begin_render_pass binds `target` as the colour attachment, sets the
     * viewport to the target's own size and disables blending. Uniforms operate
     * on the currently bound pipeline; an optimised-out name is silently
     * ignored. bind_texture(unit 0, ..., NULL) uses the default sampler. */
    gl->begin_render_pass(gpu, input->target_texture);
    gl->bind_graphics_pipeline(gpu, pass->pipeline);
    gl->set_uniform_int(gpu, "uSourceTexture", 0);
    gl->set_uniform_float4(gpu, "uTintColor", tint[0], tint[1], tint[2], tint[3]);
    gl->set_uniform_float(gpu, "uAmount", amount);
    gl->bind_texture(gpu, 0, input->source_texture, NULL);
    gl->draw_fullscreen(gpu);

    return input->target_texture;
}

/* destroy_pass — free every pipeline / texture / sampler this pass created,
 * before the GL context is destroyed and before the DLL unloads. Symmetric with
 * create_pass: one destroy per create. */
static void RUWA_EFFECT_CALL tint_destroy_pass(void* pass_instance, RuwaEffectGpuContext gpu)
{
    TintPass* pass = (TintPass*)pass_instance;
    if (!pass) {
        return;
    }
    if (pass->pipeline && g_host && g_host->gpu) {
        g_host->gpu->destroy_graphics_pipeline(gpu, pass->pipeline);
    }
    free(pass);
}

/* ==========================================================================
 *   3.  PARAMETERS & METADATA
 * ==========================================================================
 *
 *   Every versioned struct leads with `struct_size` = its own sizeof, so an old
 *   plugin and a new host (or the reverse) stay binary compatible: the reader
 *   copies only the declared prefix. Fill it in on everything you populate.
 *
 *   Parameter keys are STABLE serialization keys — never rely on their indices
 *   for persistence. */
static const RuwaEffectParamDef k_tint_params[] = {
    {
        /* A Real slider in [0,1]. The host serializes it as a plain number. */
        .struct_size      = sizeof(RuwaEffectParamDef),
        .key              = "amount",
        .label            = "Amount",
        .type             = RUWA_EFFECT_PARAM_REAL,
        .default_value    = 0.5,
        .min_value        = 0.0,
        .max_value        = 1.0,
        .step_value       = 0.01,
        .preferred_editor = RUWA_EFFECT_EDITOR_SLIDER,
    },
    {
        /* A Colour. Authored/rendered as sRGB floats; the host serializes it to
         * the document's existing "#RRGGBBAA" string form, so round-trip is
         * preserved. default_color is the initial swatch. */
        .struct_size   = sizeof(RuwaEffectParamDef),
        .key           = "tintColor",
        .label         = "Tint Color",
        .type          = RUWA_EFFECT_PARAM_COLOR,
        .default_color = { 1.0f, 0.32f, 0.32f, 1.0f }, /* a soft red */
    },
};

/* Capabilities is a SEPARATELY versioned struct referenced by pointer (never
 * embedded), so it can grow without shifting the descriptor's later fields.
 * This effect is a pure per-pixel function: it works in both evaluation spaces
 * and needs nothing beyond its own source. Everything else is FALSE. */
static const RuwaEffectCapabilities k_tint_caps = {
    .struct_size              = sizeof(RuwaEffectCapabilities),
    .supports_document_tile   = RUWA_TRUE,
    .supports_viewport_screen = RUWA_TRUE,
    .expands_bounds           = RUWA_FALSE,
    .requires_neighbor_tiles  = RUWA_FALSE,
    .requires_backdrop        = RUWA_FALSE,
    .order_dependent          = RUWA_FALSE,
    .reads_whole_layer        = RUWA_FALSE,
};

/* ==========================================================================
 *   4.  DESCRIPTOR + PLUGIN TABLE
 * ==========================================================================
 *
 *   One RuwaEffectDescriptor per registered effect. The pure state callbacks
 *   (pixel_expansion_radius, resolve_coverage, migrate_state) are all NULL here:
 *   with no bounds expansion the host's default "unchanged coverage" policy is
 *   exactly right, and at version 1 there is nothing to migrate. */
static const RuwaEffectDescriptor k_effects[] = {
    {
        .struct_size            = sizeof(RuwaEffectDescriptor),
        .type_id                = "reference.tint", /* stable; must contain a dot */
        .display_name           = "Reference Tint",
        .category               = "Example", /* picker folder; "" -> "Other" */
        .version                = 1u,        /* current schema version (>= 1)  */
        .capabilities           = &k_tint_caps,
        .params                 = k_tint_params,
        .param_count            = 2u,
        .user_data              = NULL,
        .pixel_expansion_radius = NULL, /* null == 0 px reach */
        .resolve_coverage       = NULL, /* fall back to the radius policy */
        .migrate_state          = NULL, /* nothing to migrate at v1 */
        .create_pass            = tint_create_pass,
        .render_pass            = tint_render_pass,
        .destroy_pass           = tint_destroy_pass,
    },
};

/* The plugin API table the query entry point returns. Everything it points at
 * (this table, k_effects, every string) has static lifetime, so it stays valid
 * until shutdown() — here NULL, since we hold no process-global resources. */
static const RuwaEffectPluginApi k_plugin = {
    .struct_size    = sizeof(RuwaEffectPluginApi),
    .abi_major      = RUWA_EFFECT_ABI_MAJOR,
    .abi_minor      = RUWA_EFFECT_ABI_MINOR,
    .plugin_id      = "com.ruwa.reference", /* reverse-domain, unique; must contain a dot */
    .plugin_name    = "Ruwa Reference Effect",
    .plugin_version = "1.0",
    .vendor         = "Ruwa",
    .effect_count   = 1u,
    .effects        = k_effects,
    .shutdown       = NULL,
};

/* ==========================================================================
 *   5.  THE ONE ENTRY POINT
 * ==========================================================================
 *
 *   The single exported symbol every plugin DLL provides (its name is in
 *   RUWA_EFFECT_QUERY_SYMBOL_NAME). Reject a mismatched ABI major by returning
 *   NULL; the host then unloads us cleanly. Store `host` for later — but do not
 *   call it here: no GL context is current yet. */
RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host)
{
    if (requested_abi_major != RUWA_EFFECT_ABI_MAJOR) {
        return NULL;
    }
    g_host = host;
    return &k_plugin;
}
