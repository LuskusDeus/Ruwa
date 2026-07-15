// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <QOpenGLFunctions_4_5_Core>

class GLFboViewportGuard {
public:
    explicit GLFboViewportGuard(QOpenGLFunctions_4_5_Core* gl)
        : m_gl(gl)
    {
        m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prevFBO);
        m_gl->glGetIntegerv(GL_VIEWPORT, m_prevViewport);
    }

    ~GLFboViewportGuard()
    {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_prevFBO);
        m_gl->glViewport(
            m_prevViewport[0], m_prevViewport[1], m_prevViewport[2], m_prevViewport[3]);
    }

    GLFboViewportGuard(const GLFboViewportGuard&) = delete;
    GLFboViewportGuard& operator=(const GLFboViewportGuard&) = delete;

private:
    QOpenGLFunctions_4_5_Core* m_gl;
    GLint m_prevFBO = 0;
    GLint m_prevViewport[4] = {};
};

class GLFboViewportBlendGuard {
public:
    explicit GLFboViewportBlendGuard(QOpenGLFunctions_4_5_Core* gl)
        : m_gl(gl)
    {
        m_blendWasEnabled = m_gl->glIsEnabled(GL_BLEND);
        m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prevFBO);
        m_gl->glGetIntegerv(GL_VIEWPORT, m_prevViewport);
    }

    ~GLFboViewportBlendGuard()
    {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_prevFBO);
        m_gl->glViewport(
            m_prevViewport[0], m_prevViewport[1], m_prevViewport[2], m_prevViewport[3]);
        if (m_blendWasEnabled)
            m_gl->glEnable(GL_BLEND);
        else
            m_gl->glDisable(GL_BLEND);
    }

    GLFboViewportBlendGuard(const GLFboViewportBlendGuard&) = delete;
    GLFboViewportBlendGuard& operator=(const GLFboViewportBlendGuard&) = delete;

private:
    QOpenGLFunctions_4_5_Core* m_gl;
    GLint m_prevFBO = 0;
    GLint m_prevViewport[4] = {};
    GLboolean m_blendWasEnabled = GL_FALSE;
};
