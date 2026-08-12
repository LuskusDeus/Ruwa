// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T O O L   C U R S O R   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/ToolCursorOverlayGL.h"

#include <QOpenGLContext>

#include <array>

namespace aether {

ToolCursorOverlayGL::ToolCursorOverlayGL(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

ToolCursorOverlayGL::~ToolCursorOverlayGL()
{
    shutdown();
}

Result<void> ToolCursorOverlayGL::initialize()
{
    QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if (!currentContext) {
        return { ErrorCode::InvalidArgument, "ToolCursorOverlayGL: no current OpenGL context" };
    }

    if (m_initialized && m_context == currentContext) {
        return Result<void>::ok();
    }

    if (m_initialized) {
        shutdown();
    }

    m_iconRenderer = std::make_unique<GLCursorIconRenderer>(m_gl);
    auto iconResult = m_iconRenderer->initialize();
    if (!iconResult) {
        m_iconRenderer.reset();
        return { iconResult.error().code, iconResult.error().message };
    }

    m_context = currentContext;
    m_initialized = true;
    return Result<void>::ok();
}

void ToolCursorOverlayGL::shutdown()
{
    if (!m_initialized) {
        return;
    }

    const bool canDeleteGlObjects
        = m_gl && m_context && QOpenGLContext::currentContext() == m_context;
    if (canDeleteGlObjects && m_iconRenderer) {
        m_iconRenderer->shutdown();
    }

    m_iconRenderer.reset();
    m_context.clear();
    m_initialized = false;
}

void ToolCursorOverlayGL::render(float centerX, float centerY, int viewportWidth,
    int viewportHeight, GLuint sceneTextureId, const QString& toolIconResource)
{
    if (!m_initialized || !sceneTextureId || !m_iconRenderer) {
        return;
    }

    const float vpW = static_cast<float>(viewportWidth);
    const float vpH = static_cast<float>(viewportHeight);

    const float invW = 1.0f / vpW;
    const float invH = 1.0f / vpH;
    const std::array<float, 16> mvp
        = { { 2.0f * invW, 0, 0, 0, 0, -2.0f * invH, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1 } };

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    m_gl->glBindTextureUnit(0, sceneTextureId);

    // Badge first, arrow on top: where they touch, the arrow stays unbroken.
    if (!toolIconResource.isEmpty()) {
        m_iconRenderer->draw(toolIconResource, kToolIconSizePx, centerX + kToolIconOffsetX,
            centerY + kToolIconOffsetY, mvp, vpW, vpH);
    }
    // The arrow is the smallest glyph here and is mostly long diagonals, so it
    // keeps its coverage ramp untouched instead of the badge's contrast curve.
    m_iconRenderer->drawAtHotspot(QString::fromUtf8(kPointerResourcePath), kPointerSizePx,
        kPointerHotspotU, kPointerHotspotV, centerX, centerY, mvp, vpW, vpH, 1.0f,
        kPointerEdgeLow, kPointerEdgeHigh);

    m_gl->glDisable(GL_BLEND);
}

} // namespace aether
