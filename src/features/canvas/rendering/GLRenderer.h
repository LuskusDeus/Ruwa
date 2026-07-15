// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   R E N D E R E R
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_GLRENDERER_H
#define AETHER_ENGINE_OPENGL_GLRENDERER_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "features/canvas/scene/Viewport.h"
#include "features/canvas/scene/Canvas.h"
#include "shared/tiles/TileGrid.h"
#include "features/canvas/rendering/GLCompositor.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <array>

namespace aether {

class GLShaderProgram;
class GLTileRenderer;
class GLBrushRenderer;
class GLFillRenderer;
class GLTransformRenderer;
class GLViewportCompositor;
class GLLassoMaskRenderer;
class GLTargetLayerPreviewPass;
class GLTransformViewportPreviewPass;
class BrushExecutionBackend;

class GLRenderer {
public:
    explicit GLRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLRenderer();

    GLRenderer(const GLRenderer&) = delete;
    GLRenderer& operator=(const GLRenderer&) = delete;

    // Lifecycle
    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    // Frame operations
    void beginFrame(uint32_t width, uint32_t height);
    void endFrame();

    // Draw calls
    void drawBackground(const Color& color);
    void drawViewportChecker(
        const Color& checkerColor1, const Color& checkerColor2, float checkerSize = 8.0f);

    // Tile-based canvas rendering (draws from composition cache)
    /// Upload all dirty tiles to GPU.
    void uploadDirtyTiles(TileGrid& grid);
    /// Render tile textures (alpha blended over whatever is underneath).
    /// Canvas dimensions define the mirror frame; clipToCanvas independently
    /// controls whether tiles outside that frame are discarded.
    void drawTiles(const TileGrid& grid, const Viewport& viewport, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0, float cornerRadiusCanvasPx = 0.0f,
        bool canvasContentFlipH = false, bool canvasContentFlipV = false,
        bool compositeRoundedEdgesOverViewportBackground = false,
        const Color& viewportBackgroundColor = Color::transparent(), bool clipToCanvas = true);

    // Compositor: composite dirty tiles from layer stack into cache
    /// Composite all dirty tiles into the cache.
    void compositeAllDirty(const std::vector<CompositeLayerInfo>& layers, CompositionCache& cache,
        const Color& backdropColor = Color::transparent());
    void compositeDirtyKeys(const std::vector<CompositeLayerInfo>& layers, CompositionCache& cache,
        const std::vector<TileKey>& keys, const Color& backdropColor = Color::transparent());

    // Legacy: single-quad canvas (kept for fallback — checkerboard)
    void drawCanvas(const Canvas& canvas, const Viewport& viewport, const Color& checkerColor1,
        const Color& checkerColor2, float checkerSize = 8.0f, float cornerRadiusCanvasPx = 0.0f,
        bool canvasContentFlipH = false, bool canvasContentFlipV = false);

    GLTileRenderer* tileRenderer() { return m_tileRenderer.get(); }
    GLFillRenderer* fillRenderer() { return m_fillRenderer.get(); }
    GLCompositor* compositor() { return m_compositor.get(); }
    GLBrushRenderer* brushRenderer() { return m_brushRenderer.get(); }
    BrushExecutionBackend* brushExecutionBackend() { return m_brushExecutionBackend.get(); }
    GLTransformRenderer* transformRenderer() { return m_transformRenderer.get(); }
    GLViewportCompositor* viewportCompositor() { return m_viewportCompositor.get(); }
    GLLassoMaskRenderer* lassoMaskRenderer() { return m_lassoMaskRenderer.get(); }
    GLTargetLayerPreviewPass* targetLayerPreviewPass() { return m_targetLayerPreviewPass.get(); }
    GLTransformViewportPreviewPass* transformViewportPreviewPass()
    {
        return m_transformViewportPreviewPass.get();
    }

    bool isInitialized() const { return m_initialized; }

private:
    Result<void> createShaders(const QString& shaderDir);
    Result<void> createEmptyVAO();
    std::array<float, 16> createCanvasModelMatrix(const Canvas& canvas) const;

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    // Shaders
    std::unique_ptr<GLShaderProgram> m_backgroundProgram;
    std::unique_ptr<GLShaderProgram> m_canvasProgram;

    // Tile renderer (handles GPU texture upload + tile quad drawing)
    std::unique_ptr<GLTileRenderer> m_tileRenderer;

    // Compositor (FBO-based layer blending)
    std::unique_ptr<GLCompositor> m_compositor;

    // Brush renderer (GPU brush stamps into tile textures)
    std::unique_ptr<GLBrushRenderer> m_brushRenderer;

    // Fill renderer (GPU flood fill)
    std::unique_ptr<GLFillRenderer> m_fillRenderer;

    // Brush execution backend (CPU/GPU routing for raster brush ops)
    std::unique_ptr<BrushExecutionBackend> m_brushExecutionBackend;

    // Transform renderer (GPU-based layer transform with atlas)
    std::unique_ptr<GLTransformRenderer> m_transformRenderer;
    std::unique_ptr<GLViewportCompositor> m_viewportCompositor;
    std::unique_ptr<GLLassoMaskRenderer> m_lassoMaskRenderer;
    std::unique_ptr<GLTargetLayerPreviewPass> m_targetLayerPreviewPass;
    std::unique_ptr<GLTransformViewportPreviewPass> m_transformViewportPreviewPass;

    // Geometry
    GLuint m_emptyVAO = 0;

    // Frame state
    uint32_t m_viewportWidth = 0;
    uint32_t m_viewportHeight = 0;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLRENDERER_H
