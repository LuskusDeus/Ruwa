// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S C E N E   F B O   M A N A G E R
// ==========================================================================

#include "SceneFboManager.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLVersionFunctionsFactory>

namespace aether {

SceneFboManager::SceneFboManager() = default;

SceneFboManager::~SceneFboManager()
{
    // Destructor may run without current context. Caller must call releaseSceneFbo(gl) before
    // destroy.
    if (m_sceneFbo || m_sceneTexture) {
        if (QOpenGLContext::currentContext()) {
            QOpenGLFunctions_4_5_Core* gl
                = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(
                    QOpenGLContext::currentContext());
            if (gl) {
                releaseSceneFbo(gl);
            }
        }
    }
}

void SceneFboManager::ensureSceneFbo(QOpenGLFunctions_4_5_Core* gl, int w, int h)
{
    if (!gl || w <= 0 || h <= 0)
        return;
    QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if (m_context != currentContext) {
        m_sceneFbo = 0;
        m_sceneTexture = 0;
        m_sceneFboWidth = 0;
        m_sceneFboHeight = 0;
        m_context = currentContext;
    }
    if (m_sceneFbo && m_sceneFboWidth == w && m_sceneFboHeight == h)
        return;

    releaseSceneFbo(gl);
    m_sceneFboWidth = w;
    m_sceneFboHeight = h;

    gl->glGenTextures(1, &m_sceneTexture);
    gl->glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    gl->glGenFramebuffers(1, &m_sceneFbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTexture, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        releaseSceneFbo(gl);
        return;
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneFboManager::releaseSceneFbo(QOpenGLFunctions_4_5_Core* gl)
{
    if (!gl)
        return;
    if (m_sceneFbo) {
        if (QOpenGLContext::currentContext()) {
            GLint bound = 0;
            gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
            if (static_cast<GLuint>(bound) == m_sceneFbo)
                gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        gl->glDeleteFramebuffers(1, &m_sceneFbo);
        m_sceneFbo = 0;
    }
    if (m_sceneTexture) {
        gl->glDeleteTextures(1, &m_sceneTexture);
        m_sceneTexture = 0;
    }
    m_sceneFboWidth = 0;
    m_sceneFboHeight = 0;
    m_context = QOpenGLContext::currentContext();
}

void SceneFboManager::blitToDefaultFbo(
    QOpenGLFunctions_4_5_Core* gl, GLint defaultFbo, int w, int h) const
{
    if (!gl || !m_sceneFbo || w <= 0 || h <= 0)
        return;

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
    gl->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
}

void SceneFboManager::copyRegionFromDefaultFbo(QOpenGLFunctions_4_5_Core* gl, GLint defaultFbo,
    int x, int y, int width, int height) const
{
    if (!gl || !m_sceneFbo || width <= 0 || height <= 0)
        return;

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneFbo);
    gl->glBlitFramebuffer(x, y, x + width, y + height, x, y, x + width, y + height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
}

} // namespace aether
