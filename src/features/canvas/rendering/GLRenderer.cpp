// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   R E N D E R E R
// ==========================================================================

#include "features/canvas/rendering/GLRenderer.h"
#include "features/canvas/scene/CanvasDisplayTransforms.h"
#include "shared/rendering/GLShaderProgram.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/canvas/rendering/GLCompositor.h"
#include "features/brush/rendering/GLBrushRenderer.h"
#include "features/fill/GLFillRenderer.h"
#include "features/transform/GLTransformRenderer.h"
#include "features/canvas/rendering/GLViewportCompositor.h"
#include "features/canvas/rendering/GLLassoMaskRenderer.h"
#include "features/canvas/rendering/GLTargetLayerPreviewPass.h"
#include "features/canvas/rendering/GLTransformViewportPreviewPass.h"
#include "features/brush/engine/BrushEngine.h"
#include "shared/tiles/TileData.h"
namespace aether {

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLRenderer::GLRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLRenderer::~GLRenderer()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> GLRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    auto shaderResult = createShaders(shaderDir);
    if (!shaderResult) {
        return shaderResult;
    }

    auto vaoResult = createEmptyVAO();
    if (!vaoResult) {
        return vaoResult;
    }

    // Initialize tile renderer
    m_tileRenderer = std::make_unique<GLTileRenderer>(m_gl);
    auto tileResult = m_tileRenderer->initialize(shaderDir);
    if (!tileResult) {
        return tileResult;
    }

    // Initialize compositor
    m_compositor = std::make_unique<GLCompositor>(m_gl);
    auto compResult = m_compositor->initialize(shaderDir);
    if (!compResult) {
        return compResult;
    }

    // Initialize brush renderer
    m_brushRenderer = std::make_unique<GLBrushRenderer>(m_gl);
    auto brushResult = m_brushRenderer->initialize(shaderDir);
    if (!brushResult) {
        return brushResult;
    }

    // Initialize fill renderer
    m_fillRenderer = std::make_unique<GLFillRenderer>(m_gl);
    auto fillResult = m_fillRenderer->initialize(shaderDir);
    if (!fillResult) {
        return fillResult;
    }

    // Initialize brush execution backend and connect GPU backend
    m_brushExecutionBackend = std::make_unique<BrushExecutionBackend>();
    m_brushExecutionBackend->configureGpuBackend(m_brushRenderer.get(), m_tileRenderer.get());

    // Initialize transform renderer
    m_transformRenderer = std::make_unique<GLTransformRenderer>(m_gl);
    auto transformResult = m_transformRenderer->initialize();
    if (!transformResult) {
        return transformResult;
    }

    m_viewportCompositor = std::make_unique<GLViewportCompositor>(m_gl);
    auto viewportCompositeResult = m_viewportCompositor->initialize(shaderDir);
    if (!viewportCompositeResult) {
        return viewportCompositeResult;
    }

    m_lassoMaskRenderer = std::make_unique<GLLassoMaskRenderer>(m_gl);
    auto lassoMaskResult = m_lassoMaskRenderer->initialize(shaderDir);
    if (!lassoMaskResult) {
        return lassoMaskResult;
    }

    m_targetLayerPreviewPass = std::make_unique<GLTargetLayerPreviewPass>(m_gl);
    auto targetPreviewResult = m_targetLayerPreviewPass->initialize(shaderDir);
    if (!targetPreviewResult) {
        return targetPreviewResult;
    }

    m_transformViewportPreviewPass = std::make_unique<GLTransformViewportPreviewPass>(m_gl);
    auto transformViewportPreviewResult = m_transformViewportPreviewPass->initialize(shaderDir);
    if (!transformViewportPreviewResult) {
        return transformViewportPreviewResult;
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLRenderer::shutdown()
{
    if (!m_initialized)
        return;

    m_compositor.reset();
    if (m_brushExecutionBackend) {
        m_brushExecutionBackend->clearGpuBackend();
    }
    m_brushExecutionBackend.reset();
    m_brushRenderer.reset();
    m_fillRenderer.reset();
    m_transformRenderer.reset();
    m_transformViewportPreviewPass.reset();
    m_targetLayerPreviewPass.reset();
    m_lassoMaskRenderer.reset();
    m_viewportCompositor.reset();
    m_tileRenderer.reset();
    m_backgroundProgram.reset();
    m_canvasProgram.reset();

    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }

    m_initialized = false;
}

// ==========================================================================
//   F R A M E   O P E R A T I O N S
// ==========================================================================

void GLRenderer::beginFrame(uint32_t width, uint32_t height)
{
    m_viewportWidth = width;
    m_viewportHeight = height;

    // Flush any GPU textures orphaned by TileData destructors on other threads
    // or during grid clear/remove operations between frames.
    auto orphaned = OrphanedTextureCollector::instance().takeAll();
    if (!orphaned.empty()) {
        m_gl->glDeleteTextures(static_cast<GLsizei>(orphaned.size()), orphaned.data());
    }

    m_gl->glViewport(0, 0, width, height);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::endFrame()
{
    // Qt handles buffer swap
}

// ==========================================================================
//   D R A W   C A L L S
// ==========================================================================

void GLRenderer::drawBackground(const Color& color)
{
    if (!m_backgroundProgram || !m_backgroundProgram->isValid())
        return;

    m_backgroundProgram->use();
    m_backgroundProgram->setUniform("uColor", color.r, color.g, color.b, color.a);

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    m_gl->glBindVertexArray(0);
}

void GLRenderer::drawViewportChecker(
    const Color& checkerColor1, const Color& checkerColor2, float checkerSize)
{
    if (!m_canvasProgram || !m_canvasProgram->isValid())
        return;
    if (m_viewportWidth == 0 || m_viewportHeight == 0)
        return;

    static constexpr std::array<float, 16> kFullscreenMvp = { 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f };

    m_canvasProgram->use();
    m_canvasProgram->setUniform("uMVP", kFullscreenMvp);
    m_canvasProgram->setUniform(
        "uCanvasSize", static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight));
    m_canvasProgram->setUniform("uCornerRadius", 0.0f);
    m_canvasProgram->setUniform("uCheckerSize", checkerSize);
    m_canvasProgram->setUniform(
        "uCheckerColor1", checkerColor1.r, checkerColor1.g, checkerColor1.b, checkerColor1.a);
    m_canvasProgram->setUniform(
        "uCheckerColor2", checkerColor2.r, checkerColor2.g, checkerColor2.b, checkerColor2.a);

    m_gl->glDisable(GL_BLEND);
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

// ---- Tile-based rendering ----

void GLRenderer::uploadDirtyTiles(TileGrid& grid)
{
    if (m_tileRenderer) {
        m_tileRenderer->uploadDirtyTiles(grid);
    }
}

void GLRenderer::drawTiles(const TileGrid& grid, const Viewport& viewport, uint32_t canvasWidth,
    uint32_t canvasHeight, float cornerRadiusCanvasPx, bool canvasContentFlipH,
    bool canvasContentFlipV, bool compositeRoundedEdgesOverViewportBackground,
    const Color& viewportBackgroundColor, bool clipToCanvas)
{
    if (m_tileRenderer) {
        m_tileRenderer->render(grid, viewport, canvasWidth, canvasHeight, cornerRadiusCanvasPx,
            canvasContentFlipH, canvasContentFlipV, compositeRoundedEdgesOverViewportBackground,
            viewportBackgroundColor, clipToCanvas);
    }
}

// ---- Compositor ----

void GLRenderer::compositeAllDirty(const std::vector<CompositeLayerInfo>& layers,
    CompositionCache& cache, const Color& backdropColor)
{
    if (m_compositor && m_tileRenderer) {
        m_compositor->compositeAllDirty(layers, cache, m_tileRenderer.get(), backdropColor);
    }
}

void GLRenderer::compositeDirtyKeys(const std::vector<CompositeLayerInfo>& layers,
    CompositionCache& cache, const std::vector<TileKey>& keys, const Color& backdropColor)
{
    if (m_compositor && m_tileRenderer) {
        m_compositor->compositeDirtyKeys(layers, cache, m_tileRenderer.get(), keys, backdropColor);
    }
}

// ---- Legacy single-quad canvas (kept for compatibility) ----

void GLRenderer::drawCanvas(const Canvas& canvas, const Viewport& viewport,
    const Color& checkerColor1, const Color& checkerColor2, float checkerSize,
    float cornerRadiusCanvasPx, bool canvasContentFlipH, bool canvasContentFlipV)
{
    if (!m_canvasProgram || !m_canvasProgram->isValid())
        return;

    m_canvasProgram->use();

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::array<float, 16> modelMatrix = createCanvasModelMatrix(canvas);
    const float cw = static_cast<float>(canvas.width());
    const float ch = static_cast<float>(canvas.height());
    if (canvasContentFlipH || canvasContentFlipV) {
        const auto mirrorM
            = canvasContentMirrorMatrix4(cw, ch, canvasContentFlipH, canvasContentFlipV);
        modelMatrix = multiplyMat4ColMajor(mirrorM, modelMatrix);
    }
    auto vpMatrix = viewport.viewProjectionMatrix();

    std::array<float, 16> mvp {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += vpMatrix[k * 4 + row] * modelMatrix[col * 4 + k];
            }
            mvp[col * 4 + row] = sum;
        }
    }

    m_canvasProgram->setUniform("uMVP", mvp);
    m_canvasProgram->setUniform(
        "uCanvasSize", static_cast<float>(canvas.width()), static_cast<float>(canvas.height()));
    m_canvasProgram->setUniform("uCornerRadius", cornerRadiusCanvasPx);
    m_canvasProgram->setUniform("uCheckerSize", checkerSize);
    m_canvasProgram->setUniform(
        "uCheckerColor1", checkerColor1.r, checkerColor1.g, checkerColor1.b, checkerColor1.a);
    m_canvasProgram->setUniform(
        "uCheckerColor2", checkerColor2.r, checkerColor2.g, checkerColor2.b, checkerColor2.a);

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);

    m_gl->glDisable(GL_BLEND);
}

// ==========================================================================
//   C R E A T I O N   H E L P E R S
// ==========================================================================

Result<void> GLRenderer::createShaders(const QString& shaderDir)
{
    // Background shader
    m_backgroundProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto bgResult = m_backgroundProgram->loadFromFiles(
        shaderDir + "/background.vert.glsl", shaderDir + "/background.frag.glsl");

    if (!bgResult) {
        return bgResult;
    }

    // Canvas shader (used for checkerboard)
    m_canvasProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto canvasResult = m_canvasProgram->loadFromFiles(
        shaderDir + "/canvas.vert.glsl", shaderDir + "/canvas.frag.glsl");

    if (!canvasResult) {
        return canvasResult;
    }

    return Result<void>::ok();
}

Result<void> GLRenderer::createEmptyVAO()
{
    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create VAO" };
    }
    return Result<void>::ok();
}

std::array<float, 16> GLRenderer::createCanvasModelMatrix(const Canvas& canvas) const
{
    float w = static_cast<float>(canvas.width());
    float h = static_cast<float>(canvas.height());

    return { w, 0.0f, 0.0f, 0.0f, 0.0f, h, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f };
}

} // namespace aether
