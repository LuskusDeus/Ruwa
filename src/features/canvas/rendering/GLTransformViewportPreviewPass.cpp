// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/GLTransformViewportPreviewPass.h"

#include "features/transform/TransformState.h"
#include "shared/tiles/TileTypes.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"
#include "shared/rendering/GLStateGuard.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aether {

namespace {

std::array<float, 9> computeInverseAffineMatrix(const TransformState& state)
{
    const float sx = state.scale.x;
    const float sy = state.scale.y;
    const float cosR = std::cos(state.rotation);
    const float sinR = std::sin(state.rotation);
    const float px = state.pivot.x;
    const float py = state.pivot.y;
    const float tx = state.translation.x;
    const float ty = state.translation.y;

    const float invSx = (std::abs(sx) > 0.0001f) ? 1.0f / sx : 0.0f;
    const float invSy = (std::abs(sy) > 0.0001f) ? 1.0f / sy : 0.0f;

    const float a00 = cosR * invSx;
    const float a01 = sinR * invSx;
    const float a10 = -sinR * invSy;
    const float a11 = cosR * invSy;

    const float fwdTx = px + tx - px * sx * cosR + py * sy * sinR;
    const float fwdTy = py + ty - px * sx * sinR - py * sy * cosR;

    const float invTx = -(a00 * fwdTx + a01 * fwdTy);
    const float invTy = -(a10 * fwdTx + a11 * fwdTy);

    return { a00, a10, 0.0f, a01, a11, 0.0f, invTx, invTy, 1.0f };
}

} // namespace

GLTransformViewportPreviewPass::GLTransformViewportPreviewPass(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLTransformViewportPreviewPass::~GLTransformViewportPreviewPass()
{
    shutdown();
}

Result<void> GLTransformViewportPreviewPass::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_program = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_program->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/transform_viewport_preview.frag.glsl");
    if (!result) {
        m_program.reset();
        return result;
    }

    // Forward-rasterized deform pipeline (only used for deform mode).
    m_deformMeshProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto deformResult
        = m_deformMeshProgram->loadFromFiles(shaderDir + "/transform_deform_mesh.vert.glsl",
            shaderDir + "/transform_deform_mesh.frag.glsl");
    if (!deformResult) {
        m_program.reset();
        m_deformMeshProgram.reset();
        return deformResult;
    }

    // Base pass for the deform+selection-mask case. Uses the existing
    // fullscreen-quad vertex shader (composite.vert.glsl) and a small
    // base-only fragment shader.
    m_deformBaseProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto baseResult = m_deformBaseProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/transform_deform_base.frag.glsl");
    if (!baseResult) {
        m_program.reset();
        m_deformMeshProgram.reset();
        m_deformBaseProgram.reset();
        return baseResult;
    }

    m_gl->glGenFramebuffers(1, &m_fbo);
    m_gl->glGenVertexArrays(1, &m_emptyVao);
    if (!m_fbo || !m_emptyVao) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed,
            "Failed to create transform viewport preview objects" };
    }

    auto gridResult = initDeformMeshGrid();
    if (!gridResult) {
        shutdown();
        return gridResult;
    }

    m_gl->glGenSamplers(1, &m_deformMeshSampler);
    if (m_deformMeshSampler) {
        m_gl->glSamplerParameteri(
            m_deformMeshSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        m_gl->glSamplerParameteri(m_deformMeshSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glSamplerParameteri(m_deformMeshSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl->glSamplerParameteri(m_deformMeshSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_initialized = true;
    return Result<void>::ok();
}

Result<void> GLTransformViewportPreviewPass::initDeformMeshGrid()
{
    constexpr int N = TransformState::DEFORM_TESS_DENSITY;
    constexpr int verts = (N + 1) * (N + 1);
    static_assert(verts <= 0xFFFF, "DEFORM_TESS_DENSITY too high for uint16 indices");

    std::vector<float> uvData;
    uvData.reserve(static_cast<size_t>(verts) * 2);
    for (int j = 0; j <= N; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(N);
        for (int i = 0; i <= N; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(N);
            uvData.push_back(u);
            uvData.push_back(v);
        }
    }

    std::vector<uint16_t> indexData;
    indexData.reserve(static_cast<size_t>(N) * N * 6);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            const uint16_t a = static_cast<uint16_t>(j * (N + 1) + i);
            const uint16_t b = static_cast<uint16_t>(a + 1);
            const uint16_t c = static_cast<uint16_t>(a + (N + 1));
            const uint16_t d = static_cast<uint16_t>(c + 1);
            // Two triangles per quad (CCW): a-b-c, c-b-d
            indexData.push_back(a);
            indexData.push_back(b);
            indexData.push_back(c);
            indexData.push_back(c);
            indexData.push_back(b);
            indexData.push_back(d);
        }
    }
    m_deformMeshIndexCount = static_cast<GLsizei>(indexData.size());

    m_gl->glGenVertexArrays(1, &m_deformMeshVao);
    m_gl->glGenBuffers(1, &m_deformMeshVbo);
    m_gl->glGenBuffers(1, &m_deformMeshIbo);
    if (!m_deformMeshVao || !m_deformMeshVbo || !m_deformMeshIbo) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create deform mesh buffers" };
    }

    m_gl->glBindVertexArray(m_deformMeshVao);

    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_deformMeshVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(uvData.size() * sizeof(float)),
        uvData.data(), GL_STATIC_DRAW);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_deformMeshIbo);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indexData.size() * sizeof(uint16_t)), indexData.data(),
        GL_STATIC_DRAW);

    m_gl->glBindVertexArray(0);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return Result<void>::ok();
}

void GLTransformViewportPreviewPass::shutdown()
{
    deleteTexture(m_gl, m_outputTexture);
    if (m_emptyVao) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }
    if (m_deformMeshVao) {
        m_gl->glDeleteVertexArrays(1, &m_deformMeshVao);
        m_deformMeshVao = 0;
    }
    if (m_deformMeshVbo) {
        m_gl->glDeleteBuffers(1, &m_deformMeshVbo);
        m_deformMeshVbo = 0;
    }
    if (m_deformMeshIbo) {
        m_gl->glDeleteBuffers(1, &m_deformMeshIbo);
        m_deformMeshIbo = 0;
    }
    if (m_deformMeshSampler) {
        m_gl->glDeleteSamplers(1, &m_deformMeshSampler);
        m_deformMeshSampler = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    m_deformMeshIndexCount = 0;
    m_lastMippedSourceTexture = 0;
    m_width = 0;
    m_height = 0;
    m_program.reset();
    m_deformMeshProgram.reset();
    m_deformBaseProgram.reset();
    m_initialized = false;
}

GLuint GLTransformViewportPreviewPass::render(GLuint sourceAtlasTexture, int32_t sourceAtlasMinTX,
    int32_t sourceAtlasMinTY, uint32_t sourceAtlasWidth, uint32_t sourceAtlasHeight,
    GLuint targetBaseTexture, GLuint selectionMaskAtlasTexture, int32_t selectionMaskAtlasMinTX,
    int32_t selectionMaskAtlasMinTY, uint32_t selectionMaskAtlasWidth,
    uint32_t selectionMaskAtlasHeight, const TransformState& state, uint32_t viewportWidth,
    uint32_t viewportHeight, const Vector2& cameraPosition, float cameraZoom, float cameraRotation,
    uint32_t canvasWidth, uint32_t canvasHeight, float canvasCornerRadius, bool flipH, bool flipV,
    bool preserveMaskedSource, const Color& sourceBackgroundColor, bool clipToCanvas)
{
    if (!m_initialized || !m_program || !m_program->isValid() || !sourceAtlasTexture
        || !targetBaseTexture || sourceAtlasWidth == 0 || sourceAtlasHeight == 0
        || viewportWidth == 0 || viewportHeight == 0
        || (state.hasDeformMesh()
            && state.deformMesh->vertices.size() > TransformState::BSPLINE_MAX_CONTROL_POINTS)) {
        return 0;
    }

    ensureRenderTarget(viewportWidth, viewportHeight);
    if (!m_outputTexture) {
        return 0;
    }

    // Path B (forward-rasterized) for deform. Handles both no-mask and
    // selection-mask cases; the latter does a two-pass render (base then
    // mesh with src-over blending) inside renderDeformMeshPass.
    if (state.hasDeformMesh()) {
        return renderDeformMeshPass(sourceAtlasTexture,
            /*sourceIsScreen=*/false, sourceAtlasMinTX, sourceAtlasMinTY, sourceAtlasWidth,
            sourceAtlasHeight, sourceAtlasWidth, sourceAtlasHeight, Vector2 { 0.0f, 0.0f },
            targetBaseTexture, viewportWidth, viewportHeight, Vector2 { 0.0f, 0.0f },
            selectionMaskAtlasTexture, selectionMaskAtlasMinTX, selectionMaskAtlasMinTY,
            selectionMaskAtlasWidth, selectionMaskAtlasHeight, preserveMaskedSource, state,
            viewportWidth, viewportHeight, cameraPosition, cameraZoom, cameraRotation, canvasWidth,
            canvasHeight, canvasCornerRadius, flipH, flipV, clipToCanvas);
    }

    const auto inverseTransform = computeInverseAffineMatrix(state);

    GLFboViewportBlendGuard guard(m_gl);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outputTexture, 0);
    m_gl->glViewport(
        0, 0, static_cast<GLsizei>(viewportWidth), static_cast<GLsizei>(viewportHeight));
    m_gl->glDisable(GL_BLEND);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_program->use();
    m_program->setUniform("uSourceIsScreenTexture", 0);
    m_program->setUniform("uSourceAtlasTexture", 0);
    m_program->setUniform("uTargetBaseTexture", 1);
    m_program->setUniform("uSelectionMaskAtlasTexture", 2);
    m_program->setUniform("uUseSelectionMask", selectionMaskAtlasTexture ? 1 : 0);
    m_program->setUniform("uPreserveMaskedSource", preserveMaskedSource ? 1 : 0);
    m_program->setUniform(
        "uViewportSize", static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    m_program->setUniform("uSourceTextureSize", static_cast<float>(viewportWidth),
        static_cast<float>(viewportHeight));
    m_program->setUniform("uSourceScreenOffset", 0.0f, 0.0f);
    m_program->setUniform("uTargetBaseTextureSize", static_cast<float>(viewportWidth),
        static_cast<float>(viewportHeight));
    m_program->setUniform("uTargetBaseScreenOffset", 0.0f, 0.0f);
    m_program->setUniform("uCameraPosition", cameraPosition.x, cameraPosition.y);
    m_program->setUniform("uCameraZoom", cameraZoom);
    m_program->setUniform("uCameraRotation", cameraRotation);
    m_program->setUniform(
        "uCanvasSize", static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    m_program->setUniform("uCanvasCornerRadius", canvasCornerRadius);
    m_program->setUniform("uClipToCanvas", clipToCanvas ? 1 : 0);
    m_program->setUniform("uFlipH", flipH ? 1 : 0);
    m_program->setUniform("uFlipV", flipV ? 1 : 0);
    m_program->setUniform(
        "uAtlasSize", static_cast<float>(sourceAtlasWidth), static_cast<float>(sourceAtlasHeight));
    m_program->setUniform("uAtlasMinTile", static_cast<float>(sourceAtlasMinTX),
        static_cast<float>(sourceAtlasMinTY));
    m_program->setUniform("uMaskAtlasSize", static_cast<float>(selectionMaskAtlasWidth),
        static_cast<float>(selectionMaskAtlasHeight));
    m_program->setUniform("uMaskAtlasMinTile", static_cast<float>(selectionMaskAtlasMinTX),
        static_cast<float>(selectionMaskAtlasMinTY));
    m_program->setUniform("uTileSize", static_cast<float>(TILE_SIZE));
    m_program->setUniform("uContentBounds", state.contentBounds.left(), state.contentBounds.top(),
        state.contentBounds.right(), state.contentBounds.bottom());
    m_program->setUniform("uSourceBackgroundColor", sourceBackgroundColor.r,
        sourceBackgroundColor.g, sourceBackgroundColor.b, sourceBackgroundColor.a);

    // Deform mode took the forward-rasterized branch above; only affine
    // and free-quad reach this point.
    const int transformMode = state.hasFreeQuad() ? 1 : 0;
    m_program->setUniform("uTransformMode", transformMode);

    if (state.hasFreeQuad()) {
        const auto& q = *state.freeCorners;
        m_program->setUniform("uQ0", q[0].x, q[0].y);
        m_program->setUniform("uQ1", q[1].x, q[1].y);
        m_program->setUniform("uQ2", q[2].x, q[2].y);
        m_program->setUniform("uQ3", q[3].x, q[3].y);
    }

    const GLint matLoc = m_gl->glGetUniformLocation(m_program->handle(), "uInverseTransform");
    m_gl->glUniformMatrix3fv(matLoc, 1, GL_FALSE, inverseTransform.data());

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sourceAtlasTexture);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, targetBaseTexture);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, selectionMaskAtlasTexture);

    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);

    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    return m_outputTexture;
}

GLuint GLTransformViewportPreviewPass::renderFromScreenSource(GLuint sourceScreenTexture,
    GLuint targetBaseTexture, GLuint selectionMaskAtlasTexture, int32_t selectionMaskAtlasMinTX,
    int32_t selectionMaskAtlasMinTY, uint32_t selectionMaskAtlasWidth,
    uint32_t selectionMaskAtlasHeight, const TransformState& state, uint32_t viewportWidth,
    uint32_t viewportHeight, const Vector2& cameraPosition, float cameraZoom, float cameraRotation,
    uint32_t canvasWidth, uint32_t canvasHeight, float canvasCornerRadius, bool flipH, bool flipV,
    bool preserveMaskedSource, uint32_t sourceViewportWidth, uint32_t sourceViewportHeight,
    Vector2 sourceScreenOffset, uint32_t targetBaseViewportWidth, uint32_t targetBaseViewportHeight,
    Vector2 targetBaseScreenOffset, bool clipToCanvas)
{
    if (!m_initialized || !m_program || !m_program->isValid() || !sourceScreenTexture
        || !targetBaseTexture || viewportWidth == 0 || viewportHeight == 0
        || (state.hasDeformMesh()
            && state.deformMesh->vertices.size() > TransformState::BSPLINE_MAX_CONTROL_POINTS)) {
        return 0;
    }

    ensureRenderTarget(viewportWidth, viewportHeight);
    if (!m_outputTexture) {
        return 0;
    }

    // Path B (forward-rasterized) for deform. Handles both no-mask and
    // selection-mask cases via the two-pass logic inside renderDeformMeshPass.
    if (state.hasDeformMesh()) {
        const uint32_t effectiveSourceWidth
            = sourceViewportWidth > 0 ? sourceViewportWidth : viewportWidth;
        const uint32_t effectiveSourceHeight
            = sourceViewportHeight > 0 ? sourceViewportHeight : viewportHeight;
        const uint32_t effectiveTargetBaseWidth
            = targetBaseViewportWidth > 0 ? targetBaseViewportWidth : viewportWidth;
        const uint32_t effectiveTargetBaseHeight
            = targetBaseViewportHeight > 0 ? targetBaseViewportHeight : viewportHeight;
        return renderDeformMeshPass(sourceScreenTexture,
            /*sourceIsScreen=*/true,
            /*sourceAtlasMinTX=*/0,
            /*sourceAtlasMinTY=*/0,
            /*sourceAtlasWidth=*/viewportWidth,
            /*sourceAtlasHeight=*/viewportHeight, effectiveSourceWidth, effectiveSourceHeight,
            sourceScreenOffset, targetBaseTexture, effectiveTargetBaseWidth,
            effectiveTargetBaseHeight, targetBaseScreenOffset, selectionMaskAtlasTexture,
            selectionMaskAtlasMinTX, selectionMaskAtlasMinTY, selectionMaskAtlasWidth,
            selectionMaskAtlasHeight, preserveMaskedSource, state, viewportWidth, viewportHeight,
            cameraPosition, cameraZoom, cameraRotation, canvasWidth, canvasHeight,
            canvasCornerRadius, flipH, flipV, clipToCanvas);
    }

    const auto inverseTransform = computeInverseAffineMatrix(state);

    GLFboViewportBlendGuard guard(m_gl);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outputTexture, 0);
    m_gl->glViewport(
        0, 0, static_cast<GLsizei>(viewportWidth), static_cast<GLsizei>(viewportHeight));
    m_gl->glDisable(GL_BLEND);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_program->use();
    m_program->setUniform("uSourceIsScreenTexture", 1);
    m_program->setUniform("uSourceAtlasTexture", 0);
    m_program->setUniform("uTargetBaseTexture", 1);
    m_program->setUniform("uSelectionMaskAtlasTexture", 2);
    m_program->setUniform("uUseSelectionMask", selectionMaskAtlasTexture ? 1 : 0);
    m_program->setUniform("uPreserveMaskedSource", preserveMaskedSource ? 1 : 0);
    m_program->setUniform(
        "uViewportSize", static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    const uint32_t effectiveSourceWidth
        = sourceViewportWidth > 0 ? sourceViewportWidth : viewportWidth;
    const uint32_t effectiveSourceHeight
        = sourceViewportHeight > 0 ? sourceViewportHeight : viewportHeight;
    m_program->setUniform("uSourceTextureSize", static_cast<float>(effectiveSourceWidth),
        static_cast<float>(effectiveSourceHeight));
    m_program->setUniform("uSourceScreenOffset", sourceScreenOffset.x, sourceScreenOffset.y);
    const uint32_t effectiveTargetBaseWidth
        = targetBaseViewportWidth > 0 ? targetBaseViewportWidth : viewportWidth;
    const uint32_t effectiveTargetBaseHeight
        = targetBaseViewportHeight > 0 ? targetBaseViewportHeight : viewportHeight;
    m_program->setUniform("uTargetBaseTextureSize", static_cast<float>(effectiveTargetBaseWidth),
        static_cast<float>(effectiveTargetBaseHeight));
    m_program->setUniform(
        "uTargetBaseScreenOffset", targetBaseScreenOffset.x, targetBaseScreenOffset.y);
    m_program->setUniform("uCameraPosition", cameraPosition.x, cameraPosition.y);
    m_program->setUniform("uCameraZoom", cameraZoom);
    m_program->setUniform("uCameraRotation", cameraRotation);
    m_program->setUniform(
        "uCanvasSize", static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    m_program->setUniform("uCanvasCornerRadius", canvasCornerRadius);
    m_program->setUniform("uClipToCanvas", clipToCanvas ? 1 : 0);
    m_program->setUniform("uFlipH", flipH ? 1 : 0);
    m_program->setUniform("uFlipV", flipV ? 1 : 0);
    m_program->setUniform(
        "uAtlasSize", static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    m_program->setUniform("uAtlasMinTile", 0.0f, 0.0f);
    m_program->setUniform("uMaskAtlasSize", static_cast<float>(selectionMaskAtlasWidth),
        static_cast<float>(selectionMaskAtlasHeight));
    m_program->setUniform("uMaskAtlasMinTile", static_cast<float>(selectionMaskAtlasMinTX),
        static_cast<float>(selectionMaskAtlasMinTY));
    m_program->setUniform("uTileSize", static_cast<float>(TILE_SIZE));
    m_program->setUniform("uContentBounds", state.contentBounds.left(), state.contentBounds.top(),
        state.contentBounds.right(), state.contentBounds.bottom());
    // Screen-source transforms are content sources, never a mask grid; reset the
    // shared program's background uniform to transparent so a prior render() call
    // with a non-transparent mask background can't leak into this draw.
    m_program->setUniform("uSourceBackgroundColor", 0.0f, 0.0f, 0.0f, 0.0f);

    // Deform mode took the forward-rasterized branch above; only affine
    // and free-quad reach this point.
    const int transformMode = state.hasFreeQuad() ? 1 : 0;
    m_program->setUniform("uTransformMode", transformMode);

    if (state.hasFreeQuad()) {
        const auto& q = *state.freeCorners;
        m_program->setUniform("uQ0", q[0].x, q[0].y);
        m_program->setUniform("uQ1", q[1].x, q[1].y);
        m_program->setUniform("uQ2", q[2].x, q[2].y);
        m_program->setUniform("uQ3", q[3].x, q[3].y);
    }

    const GLint matLoc = m_gl->glGetUniformLocation(m_program->handle(), "uInverseTransform");
    m_gl->glUniformMatrix3fv(matLoc, 1, GL_FALSE, inverseTransform.data());

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sourceScreenTexture);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, targetBaseTexture);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, selectionMaskAtlasTexture);

    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);

    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    return m_outputTexture;
}

GLuint GLTransformViewportPreviewPass::renderDeformMeshPass(GLuint sourceTexture,
    bool sourceIsScreen, int32_t sourceAtlasMinTX, int32_t sourceAtlasMinTY,
    uint32_t sourceAtlasWidth, uint32_t sourceAtlasHeight, uint32_t sourceTextureWidth,
    uint32_t sourceTextureHeight, Vector2 sourceScreenOffset, GLuint targetBaseTexture,
    uint32_t targetBaseTextureWidth, uint32_t targetBaseTextureHeight,
    Vector2 targetBaseScreenOffset, GLuint selectionMaskAtlasTexture,
    int32_t selectionMaskAtlasMinTX, int32_t selectionMaskAtlasMinTY,
    uint32_t selectionMaskAtlasWidth, uint32_t selectionMaskAtlasHeight, bool preserveMaskedSource,
    const TransformState& state, uint32_t viewportWidth, uint32_t viewportHeight,
    const Vector2& cameraPosition, float cameraZoom, float cameraRotation, uint32_t canvasWidth,
    uint32_t canvasHeight, float canvasCornerRadius, bool flipH, bool flipV, bool clipToCanvas)
{
    if (!m_deformMeshProgram || !m_deformMeshProgram->isValid() || !state.hasDeformMesh()) {
        return 0;
    }
    const bool useMask = (selectionMaskAtlasTexture != 0);
    if (useMask
        && (!m_deformBaseProgram || !m_deformBaseProgram->isValid() || targetBaseTexture == 0)) {
        return 0;
    }

    GLFboViewportBlendGuard guard(m_gl);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outputTexture, 0);
    m_gl->glViewport(
        0, 0, static_cast<GLsizei>(viewportWidth), static_cast<GLsizei>(viewportHeight));
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_CULL_FACE);

    // ===== Pass A (mask path only): base content with mask carved =====
    if (useMask) {
        m_gl->glDisable(GL_BLEND);
        m_deformBaseProgram->use();
        m_deformBaseProgram->setUniform("uTargetBaseTexture", 1);
        m_deformBaseProgram->setUniform("uSelectionMaskAtlasTexture", 2);
        m_deformBaseProgram->setUniform("uPreserveMaskedSource", preserveMaskedSource ? 1 : 0);
        m_deformBaseProgram->setUniform(
            "uViewportSize", static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
        m_deformBaseProgram->setUniform("uTargetBaseTextureSize",
            static_cast<float>(targetBaseTextureWidth),
            static_cast<float>(targetBaseTextureHeight));
        m_deformBaseProgram->setUniform(
            "uTargetBaseScreenOffset", targetBaseScreenOffset.x, targetBaseScreenOffset.y);
        m_deformBaseProgram->setUniform("uMaskAtlasSize",
            static_cast<float>(selectionMaskAtlasWidth),
            static_cast<float>(selectionMaskAtlasHeight));
        m_deformBaseProgram->setUniform("uMaskAtlasMinTile",
            static_cast<float>(selectionMaskAtlasMinTX),
            static_cast<float>(selectionMaskAtlasMinTY));
        m_deformBaseProgram->setUniform("uTileSize", static_cast<float>(TILE_SIZE));
        m_deformBaseProgram->setUniform("uCameraPosition", cameraPosition.x, cameraPosition.y);
        m_deformBaseProgram->setUniform("uCameraZoom", cameraZoom);
        m_deformBaseProgram->setUniform("uCameraRotation", cameraRotation);
        m_deformBaseProgram->setUniform(
            "uCanvasSize", static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
        m_deformBaseProgram->setUniform("uCanvasCornerRadius", canvasCornerRadius);
        m_deformBaseProgram->setUniform("uClipToCanvas", clipToCanvas ? 1 : 0);
        m_deformBaseProgram->setUniform("uFlipH", flipH ? 1 : 0);
        m_deformBaseProgram->setUniform("uFlipV", flipV ? 1 : 0);

        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, targetBaseTexture);
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, selectionMaskAtlasTexture);

        m_gl->glBindVertexArray(m_emptyVao);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        m_gl->glBindVertexArray(0);
    }

    // ===== Pass B: deformed mesh with src-over blending =====
    // Premultiplied src-over: folded triangles composite naturally in
    // rasterization order; in the mask path this layers the transformed
    // source over the base content drawn in Pass A.
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_deformMeshProgram->use();
    m_deformMeshProgram->setUniform("uSourceAtlasTexture", 0);
    m_deformMeshProgram->setUniform("uSelectionMaskAtlasTexture", 2);
    m_deformMeshProgram->setUniform("uSourceIsScreenTexture", sourceIsScreen ? 1 : 0);
    m_deformMeshProgram->setUniform("uUseSelectionMask", useMask ? 1 : 0);
    m_deformMeshProgram->setUniform(
        "uViewportSize", static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    m_deformMeshProgram->setUniform("uSourceTextureSize", static_cast<float>(sourceTextureWidth),
        static_cast<float>(sourceTextureHeight));
    m_deformMeshProgram->setUniform(
        "uSourceScreenOffset", sourceScreenOffset.x, sourceScreenOffset.y);
    m_deformMeshProgram->setUniform("uCameraPosition", cameraPosition.x, cameraPosition.y);
    m_deformMeshProgram->setUniform("uCameraZoom", cameraZoom);
    m_deformMeshProgram->setUniform("uCameraRotation", cameraRotation);
    m_deformMeshProgram->setUniform(
        "uCanvasSize", static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    m_deformMeshProgram->setUniform("uCanvasCornerRadius", canvasCornerRadius);
    m_deformMeshProgram->setUniform("uClipToCanvas", clipToCanvas ? 1 : 0);
    m_deformMeshProgram->setUniform("uFlipH", flipH ? 1 : 0);
    m_deformMeshProgram->setUniform("uFlipV", flipV ? 1 : 0);
    m_deformMeshProgram->setUniform(
        "uAtlasSize", static_cast<float>(sourceAtlasWidth), static_cast<float>(sourceAtlasHeight));
    m_deformMeshProgram->setUniform("uAtlasMinTile", static_cast<float>(sourceAtlasMinTX),
        static_cast<float>(sourceAtlasMinTY));
    m_deformMeshProgram->setUniform("uMaskAtlasSize", static_cast<float>(selectionMaskAtlasWidth),
        static_cast<float>(selectionMaskAtlasHeight));
    m_deformMeshProgram->setUniform("uMaskAtlasMinTile",
        static_cast<float>(selectionMaskAtlasMinTX), static_cast<float>(selectionMaskAtlasMinTY));
    m_deformMeshProgram->setUniform("uTileSize", static_cast<float>(TILE_SIZE));
    m_deformMeshProgram->setUniform("uContentBounds", state.contentBounds.left(),
        state.contentBounds.top(), state.contentBounds.right(), state.contentBounds.bottom());

    const auto& mesh = *state.deformMesh;
    const int count = std::min<int>(
        static_cast<int>(mesh.vertices.size()), TransformState::BSPLINE_MAX_CONTROL_POINTS);

    m_deformMeshProgram->setUniform("uLatticeRows", mesh.rows);
    m_deformMeshProgram->setUniform("uLatticeCols", mesh.cols);

    std::array<float, TransformState::BSPLINE_MAX_CONTROL_POINTS * 2> cpData {};
    for (int i = 0; i < count; ++i) {
        cpData[static_cast<size_t>(i) * 2] = mesh.vertices[static_cast<size_t>(i)].target.x;
        cpData[static_cast<size_t>(i) * 2 + 1] = mesh.vertices[static_cast<size_t>(i)].target.y;
    }
    const GLint controlPointsLoc
        = m_gl->glGetUniformLocation(m_deformMeshProgram->handle(), "uControlPoints[0]");
    if (controlPointsLoc >= 0 && count > 0) {
        m_gl->glUniform2fv(controlPointsLoc, count, cpData.data());
    }

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sourceTexture);
    // Generate mip levels for the source once per source-texture identity.
    // During an interactive drag the source content does not change (only
    // lattice control points do), so regenerating the mip pyramid every
    // frame is wasted GPU work and can introduce driver sync stalls.
    if (sourceTexture != m_lastMippedSourceTexture) {
        m_gl->glGenerateMipmap(GL_TEXTURE_2D);
        m_lastMippedSourceTexture = sourceTexture;
    }
    if (m_deformMeshSampler) {
        m_gl->glBindSampler(0, m_deformMeshSampler);
    }
    if (useMask) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, selectionMaskAtlasTexture);
    }

    m_gl->glBindVertexArray(m_deformMeshVao);
    m_gl->glDrawElements(GL_TRIANGLES, m_deformMeshIndexCount, GL_UNSIGNED_SHORT, nullptr);
    m_gl->glBindVertexArray(0);

    if (m_deformMeshSampler) {
        m_gl->glBindSampler(0, 0);
    }
    if (useMask) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    return m_outputTexture;
}

void GLTransformViewportPreviewPass::ensureRenderTarget(uint32_t width, uint32_t height)
{
    if (m_outputTexture && m_width == width && m_height == height) {
        return;
    }

    deleteTexture(m_gl, m_outputTexture);
    m_outputTexture = createTexture2D(m_gl, width, height, { GL_LINEAR, GL_LINEAR });
    m_width = m_outputTexture ? width : 0;
    m_height = m_outputTexture ? height : 0;
}

} // namespace aether
