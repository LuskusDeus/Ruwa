// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   R U W A   |   E F F E C T   S D K   —   A B I   v1   (plugin contract)
 * ==========================================================================
 *
 *   The descriptor, coverage / migration callbacks, the plugin API table and
 *   the host API table, plus the single exported query entry point. Include
 *   this (or the umbrella ruwa_effect_sdk.h) to author a plugin.
 * ========================================================================== */

#ifndef RUWA_EFFECT_PLUGIN_H
#define RUWA_EFFECT_PLUGIN_H

#include "ruwa_effect_abi.h"
#include "ruwa_effect_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Parameter definition (§7.1) -----------------------------------------
 * Mirrors ruwa::core::effects::EffectParamDefinition. Numeric fields are read
 * per `type`: Bool/Int/Real use default_value/min/max/step; Color uses
 * default_color; Choice uses choices/choice_count/default_choice. */
typedef struct RuwaEffectParamDef {
    uint32_t             struct_size;
    const char*          key;    /* stable; also the serialization key */
    const char*          label;
    RuwaEffectParamType  type;

    /* Bool (0/1), Int, Real. */
    double               default_value;
    double               min_value;
    double               max_value;
    double               step_value;
    RuwaEffectParamEditor preferred_editor; /* Int/Real; SLIDER default */

    /* Color: default as sRGB floats; the host serializes to "#RRGGBBAA". */
    float                default_color[4];

    /* Choice: display strings; the host serializes the chosen STRING (document
     * compatibility), and hands the plugin the index at render time. */
    const char* const*   choices;
    uint32_t             choice_count;
    int32_t              default_choice;

    /* Position pairing: a non-null, non-empty position_pair_key ties this Real
     * param to exactly one other Real sharing the key (one X, one Y) into a
     * single on-canvas position editor. The host serializes the two into the
     * SAME paired keys used today (never a new aggregate key). default_binding
     * ties the default to canvas size. */
    const char*              position_pair_key;
    RuwaEffectPositionAxis   position_axis;
    RuwaEffectDefaultBinding default_binding;
} RuwaEffectParamDef;

/* --- Capabilities --------------------------------------------------------
 * Mirrors ruwa::core::effects::EffectCapabilities. */
typedef struct RuwaEffectCapabilities {
    uint32_t struct_size;
    RuwaBool supports_document_tile;
    RuwaBool supports_viewport_screen;
    RuwaBool expands_bounds;
    RuwaBool requires_neighbor_tiles;
    RuwaBool requires_backdrop;
    RuwaBool order_dependent;
    RuwaBool reads_whole_layer;
} RuwaEffectCapabilities;

/* --- Effect state view (pure, non-GPU) -----------------------------------
 * The parameters + schema version handed to coverage / bounds callbacks. Same
 * layout convention as RuwaEffectPassInput's parameter arrays. */
typedef struct RuwaEffectStateView {
    uint32_t                    struct_size;
    uint32_t                    version;
    uint32_t                    param_count;
    const char* const*          param_keys;
    const RuwaEffectParamValue* param_values;
} RuwaEffectStateView;

/* --- Coverage (§7.3) -----------------------------------------------------
 * The host enumerates the input tiles through the opaque RuwaEffectCoverageInput
 * (count / at live in RuwaEffectHostApi); the plugin emits output tiles through
 * `emit`. No C++ container ever crosses the boundary, so whole-layer
 * displacement (thousands of tiles) streams without copying. */
typedef void (RUWA_EFFECT_CALL *RuwaEffectCoverageEmit)(void* emit_ctx, RuwaTileKey key);

/* --- Effect descriptor ---------------------------------------------------
 * One registered effect. `user_data` is passed back to every callback so a
 * plugin can share one C++ implementation across several descriptors. The GPU
 * pass callbacks run on the render thread only; the coverage / migration
 * callbacks are pure state functions and must NOT touch the GPU. */
typedef struct RuwaEffectDescriptor {
    uint32_t struct_size;

    const char* type_id;       /* stable; reverse-domain for new effects,
                                * existing ids (e.g. "stylize.glow") kept as-is */
    const char* display_name;
    const char* category;      /* picker folder; empty -> "Other" */
    uint32_t    version;       /* current schema version */

    /* Static-lifetime, separately versioned structure. It is a pointer rather
     * than an embedded value so appending capability fields never shifts the
     * following RuwaEffectDescriptor ABI prefix. Never null. */
    const RuwaEffectCapabilities* capabilities;
    /* Versioned array. Elements are packed at params[0].struct_size byte
     * intervals and must all declare the same size. */
    const RuwaEffectParamDef*  params;
    uint32_t                   param_count;

    void* user_data;

    /* Pixel-space radius sampled beyond a tile (blur/shadow reach). May be null
     * (treated as 0). Pure state function. */
    int32_t (RUWA_EFFECT_CALL *pixel_expansion_radius)(
        void* user_data, const RuwaEffectStateView* state);

    /* Custom coverage. Enumerate `input` via the host, emit output tiles via
     * `emit`/`emit_ctx`. Return RUWA_TRUE if handled; RUWA_FALSE (or a null
     * callback) falls back to pixel_expansion_radius. Pure state function. */
    RuwaBool (RUWA_EFFECT_CALL *resolve_coverage)(
        void* user_data, const RuwaEffectStateView* state,
        RuwaEffectCoverageInput input, RuwaEffectCoverageEmit emit_fn, void* emit_ctx);

    /* Migrate one schema step (from_version -> from_version+1). Applied
     * sequentially by the host until state reaches `version`. Read/write params
     * through the host's state_get_param / state_set_param. May be null. */
    void (RUWA_EFFECT_CALL *migrate_state)(
        void* user_data, uint32_t from_version, RuwaEffectMutableState state);

    /* --- GPU pass lifecycle (render thread; one instance per GL context) --- */

    /* Create the pass instance for a context: compile pipelines, create
     * samplers. Returns a plugin-owned opaque pointer passed to render/destroy.
     * Return null to signal failure (the host skips this effect on this context
     * without disturbing others). */
    void* (RUWA_EFFECT_CALL *create_pass)(void* user_data, RuwaEffectGpuContext gpu);

    /* Render one invocation. Return the texture holding the result — usually
     * `input->target_texture`, or `input->source_texture` unchanged for a no-op.
     * Persistent resources may be created lazily here via `gpu`. */
    RuwaEffectTexture (RUWA_EFFECT_CALL *render_pass)(
        void* pass_instance, RuwaEffectGpuContext gpu, const RuwaEffectPassInput* input);

    /* Destroy the pass instance: free every pipeline / texture / sampler it
     * created. Called on the owning render thread BEFORE the context is
     * destroyed and before the DLL unloads. */
    void (RUWA_EFFECT_CALL *destroy_pass)(void* pass_instance, RuwaEffectGpuContext gpu);
} RuwaEffectDescriptor;

/* --- Plugin API table (returned by the query entry point) ---------------- */
typedef struct RuwaEffectPluginApi {
    uint32_t struct_size;
    uint32_t abi_major; /* must be RUWA_EFFECT_ABI_MAJOR */
    uint32_t abi_minor; /* RUWA_EFFECT_ABI_MINOR the plugin was built against */

    const char* plugin_id;      /* reverse-domain, unique across loaded plugins */
    const char* plugin_name;    /* human-readable */
    const char* plugin_version; /* display string */
    const char* vendor;         /* publisher shown at install time */

    uint32_t                     effect_count;
    /* Versioned array. Elements are packed at effects[0].struct_size byte
     * intervals and must all declare the same size. */
    const RuwaEffectDescriptor*  effects;

    /* Optional. Called once before the DLL unloads, after every pass instance
     * has been destroyed. No SDK callback fires after this returns. May be null. */
    void (RUWA_EFFECT_CALL *shutdown)(void);
} RuwaEffectPluginApi;

/* --- Host API table (passed INTO the query entry point) ------------------ */
typedef struct RuwaEffectHostApi {
    uint32_t struct_size;
    uint32_t abi_major; /* the host's ABI major */
    uint32_t abi_minor; /* the host's ABI minor */

    const char* host_name;    /* e.g. "Ruwa" */
    const char* host_version; /* display string */

    /* Diagnostics. Safe to call from any thread. */
    void (RUWA_EFFECT_CALL *log)(RuwaEffectLogLevel level, const char* message);

    /* The GPU command table (see ruwa_effect_gpu.h). Never null. */
    const RuwaEffectGpuApi* gpu;

    /* Coverage input enumeration, for resolve_coverage. */
    uint32_t    (RUWA_EFFECT_CALL *coverage_input_count)(RuwaEffectCoverageInput input);
    RuwaTileKey (RUWA_EFFECT_CALL *coverage_input_at)(
        RuwaEffectCoverageInput input, uint32_t index);

    /* Mutable-state access, for migrate_state. A get on a missing key returns a
     * value whose `type` echoes the requested slot with zeroed contents. */
    RuwaEffectParamValue (RUWA_EFFECT_CALL *state_get_param)(
        RuwaEffectMutableState state, const char* key);
    void (RUWA_EFFECT_CALL *state_set_param)(
        RuwaEffectMutableState state, const char* key, RuwaEffectParamValue value);

    /* The shared fullscreen vertex shader source the host links into every
     * graphics pipeline. Plugins rarely need it (it is applied automatically);
     * exposed for reference / documentation. May be null. */
    const char* (RUWA_EFFECT_CALL *shared_vertex_shader_source)(void);
} RuwaEffectHostApi;

/* --- Entry point ---------------------------------------------------------
 * The ONE exported symbol every plugin DLL provides (name in
 * RUWA_EFFECT_QUERY_SYMBOL_NAME). Return null if `requested_abi_major` is not
 * supported. The returned table (and everything it points at) must stay valid
 * until the plugin's shutdown() is called. `host` stays valid for the plugin's
 * whole lifetime; store it. */
typedef const RuwaEffectPluginApi* (RUWA_EFFECT_CALL *RuwaEffectQueryFn)(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host);

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL ruwa_effect_plugin_query(
    uint32_t requested_abi_major, const RuwaEffectHostApi* host);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RUWA_EFFECT_PLUGIN_H */
