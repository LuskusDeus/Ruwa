// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/GLTargetLayerPreviewPass.h"

#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"
#include "shared/rendering/GLStateGuard.h"

namespace aether {

GLTargetLayerPreviewPass::GLTargetLayerPreviewPass(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLTargetLayerPreviewPass::~GLTargetLayerPreviewPass()
{
    shutdown();
}

Result<void> GLTargetLayerPreviewPass::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_program = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_program->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/target_layer_preview.frag.glsl");
    if (!result) {
        m_program.reset();
        return result;
    }

    m_gl->glGenFramebuffers(1, &m_fbo);
    m_gl->glGenVertexArrays(1, &m_emptyVao);
    if (!m_fbo || !m_emptyVao) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed,
            "Failed to create target preview pass objects" };
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLTargetLayerPreviewPass::shutdown()
{
    deleteTexture(m_gl, m_outputTexture);
    if (m_emptyVao) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    m_width = 0;
    m_height = 0;
    m_program.reset();
    m_initialized = false;
}

GLuint GLTargetLayerPreviewPass::render(GLuint targetLayerBaseTexture, GLuint lassoMaskTexture,
    const QRect& lassoMaskBounds, uint32_t viewportWidth, uint32_t viewportHeight,
    const Color& fillColor, bool preserveBaseAlpha, GLuint selectionMaskTexture)
{
    if (!m_initialized || !m_program || !m_program->isValid() || !targetLayerBaseTexture
        || !lassoMaskTexture || viewportWidth == 0 || viewportHeight == 0
        || !lassoMaskBounds.isValid() || lassoMaskBounds.isEmpty()) {
        return 0;
    }

    ensureRenderTarget(viewportWidth, viewportHeight);
    if (!m_outputTexture) {
        return 0;
    }

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
    m_program->setUniform("uBaseTexture", 0);
    m_program->setUniform("uMaskTexture", 1);
    m_program->setUniform("uSelectionMaskTexture", 2);
    m_program->setUniform("uUseSelectionMask", selectionMaskTexture ? 1 : 0);
    m_program->setUniform("uPreserveBaseAlpha", preserveBaseAlpha ? 1 : 0);
    m_program->setUniform(
        "uViewportSize", static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));
    m_program->setUniform("uMaskOrigin", lassoMaskBounds.x(), lassoMaskBounds.y());
    m_program->setUniform("uMaskSize", lassoMaskBounds.width(), lassoMaskBounds.height());
    m_program->setUniform("uFillColor", fillColor.r, fillColor.g, fillColor.b, fillColor.a);

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, targetLayerBaseTexture);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, lassoMaskTexture);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, selectionMaskTexture);

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

void GLTargetLayerPreviewPass::ensureRenderTarget(uint32_t width, uint32_t height)
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
