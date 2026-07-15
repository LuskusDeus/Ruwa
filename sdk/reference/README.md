# Ruwa Reference Effect Plugin

A minimal, exhaustively commented example that authors one real effect —
**Reference Tint** — against the Ruwa Effect SDK (ABI v1) and nothing else. It
is the runnable companion to [`../README.md`](../README.md): read that for the
written contract, read [`src/reference_tint.c`](src/reference_tint.c) top to
bottom for the code.

## What it demonstrates

The complete shape of the simplest kind of GPU effect, in one file:

- the single exported entry point `ruwa_effect_plugin_query`;
- storing the host API table and using its logger + GPU command table;
- the GPU pass lifecycle — `create_pass` (compile a graphics pipeline),
  `render_pass` (bind uniforms + a texture, one fullscreen draw), `destroy_pass`
  (release it), one create/destroy pair per resource;
- reading typed parameters — a **Real** slider and a **Color** (with the
  premultiplied-alpha unpack/repack the whole chain uses);
- declaring capabilities, a versioned descriptor and the plugin table, each
  leading with `struct_size`.

The effect blends every covered pixel of the layer toward a chosen colour by an
`Amount`. It never expands bounds, reads neighbours or touches the backdrop —
so all its state callbacks (`pixel_expansion_radius`, `resolve_coverage`,
`migrate_state`) are simply `NULL`, and the host's default coverage policy is
exactly right.

## Not part of the app build

This directory is **not** wired into the repository build. Nothing in the root
`CMakeLists.txt` (or `plugins/CMakeLists.txt`) references it, so building Ruwa
never builds or ships it. It exists purely as reference material and as an
on-demand proof that a plugin compiles against `sdk/include` alone. The
production standard packages under [`../../plugins/standard/`](../../plugins/standard)
are what actually ship in the app.

## Build it standalone

It sees only `../include` and links no Qt and no Ruwa code:

```sh
cmake -S sdk/reference -B build-reference
cmake --build build-reference
```

The output is `reference_tint.dll` (`.so` on Linux, `.dylib` on macOS) — exactly
`<name>` with no `lib` prefix and no import library, the shape the loader scans
for.

## Try it in Ruwa

Copy the built library into one of the two folders `EffectPluginManager` scans
at startup:

- `effects/` **beside `Ruwa.exe`** — where the standard packages also land; or
- `effects/` under the per-user app-data location
  (`QStandardPaths::AppDataLocation`, e.g. `%AppData%/…/Ruwa/effects` on Windows).

Launch Ruwa and add a layer effect: **Reference Tint** appears in the picker
under the **Example** category. If the DLL is rejected, the reason is logged
with the `[effect-plugin]` prefix (e.g. an ABI-major mismatch or a failed
shader compile) — the host disables just that plugin and keeps running.

## Use it as a seed

Copy this directory, rename the file, the `plugin_id`, the effect `type_id`
(both must contain a dot and be unique), the display strings and the shader, and
you have your own plugin. Grow from there toward the standard packages, which
show samplers, compute passes, neighbourhood reads, custom coverage and schema
migrations.
