// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/GLLassoMaskRenderer.h"

#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"

namespace aether {

GLLassoMaskRenderer::GLLassoMaskRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLLassoMaskRenderer::~GLLassoMaskRenderer()
{
    shutdown();
}

Result<void> GLLassoMaskRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_program = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_program->loadComputeFromFile(shaderDir + "/lasso_mask.comp.glsl");
    if (!result) {
        m_program.reset();
        return result;
    }

    m_gl->glGenBuffers(1, &m_pointSsbo);
    if (!m_pointSsbo) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed, "Failed to create lasso mask SSBO" };
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLLassoMaskRenderer::shutdown()
{
    deleteTexture(m_gl, m_maskTexture);
    if (m_pointSsbo) {
        m_gl->glDeleteBuffers(1, &m_pointSsbo);
        m_pointSsbo = 0;
    }
    m_maskSize = {};
    m_program.reset();
    m_initialized = false;
}

GLLassoMaskRenderer::MaskRenderResult GLLassoMaskRenderer::renderMask(
    const std::vector<Vector2>& localScreenPolygon, const QRect& bounds)
{
    MaskRenderResult result;
    if (!m_initialized || !m_program || !m_program->isValid() || localScreenPolygon.size() < 3
        || !bounds.isValid() || bounds.isEmpty()) {
        return result;
    }

    ensureMaskTexture(bounds.size());
    if (!m_maskTexture) {
        return result;
    }

    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_pointSsbo);
    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(localScreenPolygon.size() * sizeof(Vector2)),
        localScreenPolygon.data(), GL_STREAM_DRAW);
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_pointSsbo);

    const GLuint clearValue = 0;
    m_gl->glClearTexImage(m_maskTexture, 0, GL_RED, GL_UNSIGNED_BYTE, &clearValue);
    m_gl->glBindImageTexture(0, m_maskTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);

    m_program->use();
    m_program->setUniform("uMaskSize", bounds.width(), bounds.height());
    m_program->setUniform("uPointCount", static_cast<int>(localScreenPolygon.size()));

    const GLuint dispatchX = static_cast<GLuint>((bounds.width() + 15) / 16);
    const GLuint dispatchY = static_cast<GLuint>((bounds.height() + 15) / 16);
    m_gl->glDispatchCompute(dispatchX, dispatchY, 1);
    m_gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    m_gl->glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    result.texture = m_maskTexture;
    result.bounds = bounds;
    return result;
}

void GLLassoMaskRenderer::ensureMaskTexture(const QSize& size)
{
    if (!size.isValid() || size.isEmpty()) {
        return;
    }
    if (m_maskTexture && m_maskSize == size) {
        return;
    }

    deleteTexture(m_gl, m_maskTexture);
    m_maskTexture = createTexture2D(m_gl, size.width(), size.height(),
        { GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_R8, GL_RED,
            GL_UNSIGNED_BYTE });
    if (!m_maskTexture) {
        m_maskSize = {};
        return;
    }
    m_maskSize = size;
}

} // namespace aether
