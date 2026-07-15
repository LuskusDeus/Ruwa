# Ruwa Effect SDK — ABI v1

A stable C ABI for building Ruwa layer-effect plugins. A plugin is a native DLL
that exports one symbol and is loaded, negotiated and driven by the host's
`EffectPluginManager`. Standard Ruwa effects ship as ordinary plugins through
this same SDK — there are no privileged host paths.

Header set (under `sdk/include/`, include path = `sdk/include`):

| Header | Contents |
|--------|----------|
| `ruwa/effect/ruwa_effect_abi.h` | version/export macros, fixed types, opaque handles, enums |
| `ruwa/effect/ruwa_effect_gpu.h` | `RuwaEffectPassInput`, `RuwaEffectGpuApi`, shader-source view |
| `ruwa/effect/ruwa_effect_plugin.h` | descriptor, capabilities, coverage/migration, plugin & host API tables, query entry point |
| `ruwa/effect/ruwa_effect_sdk.h` | umbrella — include just this |

Pure C. No Qt, no STL, no C++ classes, no exceptions across the boundary.

## The one entry point

```c
RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL
ruwa_effect_plugin_query(uint32_t requested_abi_major, const RuwaEffectHostApi* host);
```

- Return `NULL` if `requested_abi_major != RUWA_EFFECT_ABI_MAJOR`.
- Set `abi_major = RUWA_EFFECT_ABI_MAJOR`, `abi_minor = RUWA_EFFECT_ABI_MINOR`.
- The returned `RuwaEffectPluginApi`, its `effects[]`, every string and every
  nested array must have **static lifetime** — valid until `shutdown()`.
- Store `host`; it is valid for the plugin's whole lifetime. Do **not** call any
  host function from inside `query` itself (no GL context is current yet).

## ABI versioning & `struct_size`

- Every versioned struct leads with `struct_size` = its own `sizeof` at the
  compiler that produced it. Fill it in on the structs you populate.
- The reader (host or plugin) copies only `min(local_sizeof, struct_size)` bytes
  and treats absent trailing fields as zero. This makes both directions safe:
  an old plugin against a new host, and a new plugin against an old host.
- Arrays of versioned structures (`effects[]`, `params[]`) are packed using the
  producer's `struct_size` as the element stride. Every element in one array
  must declare the same `struct_size`; never index such an array using a newer
  consumer's `sizeof(T)`.
- A separately versioned structure is referenced by pointer, not embedded ahead
  of other fields in another ABI structure. In particular,
  `RuwaEffectDescriptor::capabilities` points to static-lifetime storage. This
  lets capabilities grow without shifting the descriptor's following fields.
- New fields and function pointers are appended to the **end** only. An
  already-shipped prefix never changes within ABI major 1, which stays binary
  compatible across all later major-1 Ruwa releases.
- A mismatched **major** is rejected outright by the host.

## Ownership

- **Strings / arrays in the descriptor** (`type_id`, `params`, `choices`, …) are
  owned by the plugin and must outlive the plugin; the host copies what it needs
  into its own internal descriptors — it never frees plugin memory.
- **Transient scratch textures** (`alloc_scratch_texture`) are host-owned, valid
  only for the current `render_pass` call. Never destroy them.
- **Persistent textures / samplers / pipelines** (`create_*`) are plugin-owned.
  You must `destroy_*` every one of them in `destroy_pass`, before the context
  and DLL go away. Resize a persistent texture by destroy + create.
- No allocation is transferred across the ABI without an explicit create/destroy
  pair on the same table.

## Lifetime

- One **pass instance per (effect `type_id` × GL context)**, created by
  `create_pass`. It is shared by every layer that uses the effect; do not assume
  per-layer state. Multi-context (several windows/tabs) means several instances,
  each with its own handles — a handle from one is invalid in another.
- The host keeps the DLL loaded while any effect instance or live GPU handle
  exists. **Hot reload / unload while running is not supported in v1.**
- Teardown order is guaranteed: `destroy_pass` (all instances, on the owning
  render thread, context current) → `shutdown()` → DLL unload. No callback fires
  after `shutdown()`.

## Threading

- **GPU callbacks** — `create_pass`, `render_pass`, `destroy_pass`, and every
  `RuwaEffectGpuApi` call — run **only on the render thread with the GL context
  current**. `[C]`-tagged GPU calls are further restricted to inside
  `render_pass`. This is enforced by the ABI shape: you can only reach the GPU
  table while holding the `RuwaEffectGpuContext` a pass callback was handed.
- **State callbacks** — `pixel_expansion_radius`, `resolve_coverage`,
  `migrate_state` — are pure functions of state. They may run off the render
  thread and must **not** touch the GPU table.

## Error handling

- No exceptions cross the ABI. Report failure by return value: `create_pass`
  returns `NULL`; `render_pass` returns `input->source_texture` (pass-through) if
  it cannot run; a pipeline/texture create returning `NULL` must be handled.
- The host isolates failures: a broken export, a shader that will not compile, or
  a throwing/misbehaving callback disables only that plugin/effect — other
  plugins and the renderer keep working.
- Use `host->log(level, msg)` for diagnostics.

## Parameters & documents

- Param types: **Bool, Int, Real, Color, Choice** (`RuwaEffectParamType`).
- **Color** is authored/rendered as sRGB floats but serialized by the host to
  the existing `"#RRGGBBAA"` string — document round-trip is preserved.
- **Choice** is authored as display strings; the host serializes the chosen
  string and hands your `render_pass` the **index** into `choices`.
- **Position** is a *pair* of Real params linked by `position_pair_key`
  (one `RUWA_EFFECT_AXIS_X`, one `_Y`), optionally bound to canvas size via
  `default_binding`. The host serializes them into the **same paired keys** used
  today (never a new aggregate key) and renders the on-canvas position editor.
- Param keys are stable strings. Never rely on parameter *indices* in
  serialization; they are valid only within a single call.

## Coverage

- Simple cases: leave `resolve_coverage` null and return a
  `pixel_expansion_radius`; the host's standard policies (unchanged / uniform
  radius / directional / whole-layer displacement) cover most effects.
- Custom cases: implement `resolve_coverage`. Enumerate input tiles with
  `host->coverage_input_count` / `coverage_input_at`, emit output tiles with the
  provided `emit(emit_ctx, key)`. No container crosses the ABI, so whole-layer
  displacement streams thousands of tiles without copying.

## Minimal plugin skeleton

```c
#include <ruwa/effect/ruwa_effect_sdk.h>

static const RuwaEffectHostApi* g_host;

static void* create_pass(void* ud, RuwaEffectGpuContext gpu) { /* compile pipelines */ }
static RuwaEffectTexture render_pass(void* p, RuwaEffectGpuContext gpu,
                                     const RuwaEffectPassInput* in) { /* draw */ }
static void destroy_pass(void* p, RuwaEffectGpuContext gpu) { /* free resources */ }

static const RuwaEffectParamDef k_params[] = { /* struct_size + fields */ };
static const RuwaEffectDescriptor k_effects[] = { /* struct_size + callbacks */ };
static const RuwaEffectPluginApi k_plugin = {
    sizeof(RuwaEffectPluginApi), RUWA_EFFECT_ABI_MAJOR, RUWA_EFFECT_ABI_MINOR,
    "com.example.myfx", "My FX", "1.0", "Example",
    1, k_effects, /* shutdown */ NULL
};

RUWA_EFFECT_EXPORT const RuwaEffectPluginApi* RUWA_EFFECT_CALL
ruwa_effect_plugin_query(uint32_t abi, const RuwaEffectHostApi* host) {
    if (abi != RUWA_EFFECT_ABI_MAJOR) return NULL;
    g_host = host;
    return &k_plugin;
}
```

A reference plugin — a small, buildable example that compiles against **only**
this include directory — lives under [`sdk/reference/`](reference/). It authors
one real effect (**Reference Tint**) in a single, heavily commented file and is
intentionally kept out of the application build; build it standalone to prove
the SDK stands on its own. The production standard packages under
[`plugins/standard/`](../plugins/standard) are ordinary plugins built through
this same SDK and go further (samplers, compute, neighbourhood reads, custom
coverage, migrations).
