// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   R U W A   |   E F F E C T   S D K   —   A B I   v1   (foundation)
 * ==========================================================================
 *
 *   Public, stable C ABI for Ruwa layer-effect plugins. This header carries
 *   ONLY the foundational pieces every other SDK header builds on: export /
 *   version macros, fixed-width integer types, opaque resource handles and the
 *   plain enums shared across the descriptor, GPU and document contracts.
 *
 *   Rules (the full contract lives in sdk/README.md):
 *     - Pure C. No Qt, no STL, no C++ classes, no exceptions across the
 *       boundary. No ownership of memory transferred without an explicit
 *       allocator / deallocator pair in the ABI.
 *     - Every versioned struct leads with `struct_size` (its own sizeof at the
 *       compiler that built it). Readers copy only the declared prefix, so an
 *       old plugin talking to a new host — or the reverse — is always safe.
 *     - New fields / functions are appended to the END of a struct only; the
 *       layout of an already-shipped prefix never changes within ABI major 1.
 *
 *   ABI major 1 stays binary-compatible across all later Ruwa releases with
 *   major 1. A mismatched major is rejected by the host.
 * ========================================================================== */

#ifndef RUWA_EFFECT_ABI_H
#define RUWA_EFFECT_ABI_H

#include <stddef.h>
#include <stdint.h>

/* --- ABI version -------------------------------------------------------- */

#define RUWA_EFFECT_ABI_MAJOR 1u
#define RUWA_EFFECT_ABI_MINOR 0u

/* The one exported entry point every plugin DLL must define. */
#define RUWA_EFFECT_QUERY_SYMBOL_NAME "ruwa_effect_plugin_query"

/* --- Export / calling convention ---------------------------------------- */

#if defined(_WIN32) || defined(__CYGWIN__)
#  define RUWA_EFFECT_EXPORT __declspec(dllexport)
#  define RUWA_EFFECT_IMPORT __declspec(dllimport)
#else
#  if defined(__GNUC__) && __GNUC__ >= 4
#    define RUWA_EFFECT_EXPORT __attribute__((visibility("default")))
#  else
#    define RUWA_EFFECT_EXPORT
#  endif
#  define RUWA_EFFECT_IMPORT
#endif

/* x64 has a single calling convention; the macro documents intent and leaves
 * room for a future 32-bit target without touching every signature. */
#if defined(_WIN32) && !defined(_WIN64)
#  define RUWA_EFFECT_CALL __cdecl
#else
#  define RUWA_EFFECT_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Scalars ------------------------------------------------------------ */

/* ABI-stable boolean: exactly 0 (false) or 1 (true). Never a C++ `bool`, whose
 * width/representation is not fixed across compilers. */
typedef int32_t RuwaBool;

#define RUWA_FALSE 0
#define RUWA_TRUE  1

/* --- Opaque handles ------------------------------------------------------
 * Never dereferenced by a plugin. A null handle is always the invalid value.
 * A handle is only valid for the (pass instance x GL context) that produced it;
 * using one from another pass or context is undefined (the host rejects it). */

typedef struct RuwaEffectTexture_s*         RuwaEffectTexture;
typedef struct RuwaEffectSampler_s*         RuwaEffectSampler;
typedef struct RuwaEffectPipeline_s*        RuwaEffectPipeline;        /* shared VS + plugin FS */
typedef struct RuwaEffectComputePipeline_s* RuwaEffectComputePipeline;

/* The single GPU handle handed to a plugin's pass callbacks. Backs both
 * resource lifecycle and command recording; see RuwaEffectGpuApi for which
 * calls are legal when. Valid ONLY inside create_pass / render_pass /
 * destroy_pass, on the render thread, with the GL context current. */
typedef struct RuwaEffectGpuContext_s*      RuwaEffectGpuContext;

/* Host-owned iterator over the input tiles of a coverage query. */
typedef struct RuwaEffectCoverageInput_s*   RuwaEffectCoverageInput;

/* Host-owned mutable effect state handed to a migration callback. */
typedef struct RuwaEffectMutableState_s*    RuwaEffectMutableState;

/* --- Enums (shared) ------------------------------------------------------
 * All enums are fixed-width int32 across the ABI. Unknown values must be
 * treated defensively by both sides. */

typedef int32_t RuwaEffectLogLevel;
enum {
    RUWA_EFFECT_LOG_DEBUG   = 0,
    RUWA_EFFECT_LOG_INFO    = 1,
    RUWA_EFFECT_LOG_WARNING = 2,
    RUWA_EFFECT_LOG_ERROR   = 3
};

/* Mirrors ruwa::core::effects::EffectParamType. */
typedef int32_t RuwaEffectParamType;
enum {
    RUWA_EFFECT_PARAM_BOOL   = 0,
    RUWA_EFFECT_PARAM_INT    = 1,
    RUWA_EFFECT_PARAM_REAL   = 2,
    RUWA_EFFECT_PARAM_COLOR  = 3,
    RUWA_EFFECT_PARAM_CHOICE = 4
};

/* Mirrors EffectParamEditorHint. Int/Real only; ignored otherwise. Default is
 * SLIDER so every value authored before this hint existed reads unchanged. */
typedef int32_t RuwaEffectParamEditor;
enum {
    RUWA_EFFECT_EDITOR_SLIDER       = 0,
    RUWA_EFFECT_EDITOR_NUMBER_FIELD = 1
};

/* Mirrors EffectParamPositionAxis. */
typedef int32_t RuwaEffectPositionAxis;
enum {
    RUWA_EFFECT_AXIS_X = 0,
    RUWA_EFFECT_AXIS_Y = 1
};

/* Mirrors EffectParamDefaultBinding: ties a positional default to canvas size
 * so a "corner to corner" effect starts out spanning the document. */
typedef int32_t RuwaEffectDefaultBinding;
enum {
    RUWA_EFFECT_BIND_NONE              = 0,
    RUWA_EFFECT_BIND_CANVAS_WIDTH      = 1,
    RUWA_EFFECT_BIND_CANVAS_HEIGHT     = 2,
    RUWA_EFFECT_BIND_CANVAS_HALF_WIDTH = 3,
    RUWA_EFFECT_BIND_CANVAS_HALF_HEIGHT= 4
};

/* Mirrors EffectEvaluationSpace. */
typedef int32_t RuwaEffectEvaluationSpace;
enum {
    RUWA_EFFECT_SPACE_DOCUMENT_TILE   = 0,
    RUWA_EFFECT_SPACE_VIEWPORT_SCREEN = 1
};

/* GPU texture formats the ABI must support. RGBA32F is mandatory: the Box Blur
 * prefix-sum needs 32-bit precision (f16 is insufficient). */
typedef int32_t RuwaEffectTextureFormat;
enum {
    RUWA_EFFECT_FORMAT_RGBA8   = 0,
    RUWA_EFFECT_FORMAT_RGBA16F = 1,
    RUWA_EFFECT_FORMAT_RGBA32F = 2
};

/* Access mode for an image load/store binding (glBindImageTexture). */
typedef int32_t RuwaEffectImageAccess;
enum {
    RUWA_EFFECT_IMAGE_READ_ONLY  = 0,
    RUWA_EFFECT_IMAGE_WRITE_ONLY = 1,
    RUWA_EFFECT_IMAGE_READ_WRITE = 2
};

/* Granular memory-barrier flags (OR together). Map 1:1 onto the GL barrier
 * bits Box Blur fences between its compute phases. */
typedef int32_t RuwaEffectBarrierBits;
enum {
    RUWA_EFFECT_BARRIER_TEXTURE_FETCH       = 1 << 0,
    RUWA_EFFECT_BARRIER_FRAMEBUFFER         = 1 << 1,
    RUWA_EFFECT_BARRIER_SHADER_IMAGE_ACCESS = 1 << 2
};

/* Shader stage of a source blob. The fullscreen VERTEX stage is supplied by the
 * host and is not a plugin-authored stage. */
typedef int32_t RuwaEffectShaderStage;
enum {
    RUWA_EFFECT_STAGE_FRAGMENT = 0,
    RUWA_EFFECT_STAGE_COMPUTE  = 1
};

typedef int32_t RuwaEffectSamplerFilter;
enum {
    RUWA_EFFECT_FILTER_NEAREST = 0,
    RUWA_EFFECT_FILTER_LINEAR  = 1
};

typedef int32_t RuwaEffectSamplerWrap;
enum {
    RUWA_EFFECT_WRAP_CLAMP_TO_EDGE = 0,
    RUWA_EFFECT_WRAP_REPEAT        = 1,
    RUWA_EFFECT_WRAP_MIRRORED      = 2
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RUWA_EFFECT_ABI_H */
