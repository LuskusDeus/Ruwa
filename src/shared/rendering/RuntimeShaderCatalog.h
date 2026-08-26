// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   R U N T I M E   S H A D E R   C A T A L O G
// ============================================================================

#ifndef RUWA_SHARED_RENDERING_RUNTIMESHADERCATALOG_H
#define RUWA_SHARED_RENDERING_RUNTIMESHADERCATALOG_H

#include <array>
#include <string_view>

namespace aether {

enum class RuntimeShaderProgramType {
    Graphics,
    Compute,
};

struct RuntimeShaderProgramDefinition {
    std::string_view name;
    RuntimeShaderProgramType type;
    std::string_view vertexShader;
    std::string_view fragmentShader;
    std::string_view computeShader;
};

constexpr RuntimeShaderProgramDefinition graphicsShaderProgram(
    std::string_view name, std::string_view vertexShader, std::string_view fragmentShader)
{
    return { name, RuntimeShaderProgramType::Graphics, vertexShader, fragmentShader, {} };
}

constexpr RuntimeShaderProgramDefinition computeShaderProgram(
    std::string_view name, std::string_view computeShader)
{
    return { name, RuntimeShaderProgramType::Compute, {}, {}, computeShader };
}

// Authoritative list of file-backed programs shipped with Ruwa. The directory
// resolver validates every referenced file and the startup warmup compiles and
// links every definition using the same GLShaderProgram path as runtime code.
inline constexpr std::array kRuntimeShaderPrograms {
    graphicsShaderProgram("background", "background.vert.glsl", "background.frag.glsl"),
    graphicsShaderProgram("brush-stamp", "brush_stamp.vert.glsl", "brush_stamp.frag.glsl"),
    graphicsShaderProgram("canvas", "canvas.vert.glsl", "canvas.frag.glsl"),
    graphicsShaderProgram("composite", "composite.vert.glsl", "composite.frag.glsl"),
    graphicsShaderProgram(
        "backdrop-downsample", "composite.vert.glsl", "backdrop_downsample.frag.glsl"),
    graphicsShaderProgram(
        "backdrop-upsample", "composite.vert.glsl", "backdrop_upsample.frag.glsl"),
    graphicsShaderProgram(
        "backdrop-composite", "composite.vert.glsl", "backdrop_composite.frag.glsl"),
    graphicsShaderProgram("display-pyramid", "composite.vert.glsl", "pyramid_downsample.frag.glsl"),
    graphicsShaderProgram("lasso-mask", "lasso_mask.vert.glsl", "lasso_mask.frag.glsl"),
    graphicsShaderProgram(
        "target-layer-preview", "composite.vert.glsl", "target_layer_preview.frag.glsl"),
    graphicsShaderProgram("tile", "tile.vert.glsl", "tile.frag.glsl"),
    graphicsShaderProgram("tile-pyramid", "tile.vert.glsl", "tile_pyramid.frag.glsl"),
    graphicsShaderProgram("transform-viewport-preview", "composite.vert.glsl",
        "transform_viewport_preview.frag.glsl"),
    graphicsShaderProgram("transform-deform-mesh", "transform_deform_mesh.vert.glsl",
        "transform_deform_mesh.frag.glsl"),
    graphicsShaderProgram(
        "transform-deform-base", "composite.vert.glsl", "transform_deform_base.frag.glsl"),
    graphicsShaderProgram("viewport-blit", "composite.vert.glsl", "fill_blit.frag.glsl"),
    graphicsShaderProgram(
        "layer-effect-blit", "composite.vert.glsl", "layer_effect_blit.frag.glsl"),
    graphicsShaderProgram("fill-blit", "fill_blit.vert.glsl", "fill_blit.frag.glsl"),
    computeShaderProgram("fill-init", "fill_init.comp.glsl"),
    computeShaderProgram("fill-expand", "fill_expand.comp.glsl"),
    computeShaderProgram("fill-prepare", "fill_prepare.comp.glsl"),
};

} // namespace aether

#endif // RUWA_SHARED_RENDERING_RUNTIMESHADERCATALOG_H
