// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T O O L   C U R S O R   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/ToolCursorOverlayGL.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <QOpenGLContext>

#include <array>
#include <iterator>

namespace aether {

// ==========================================================================
//   I N L I N E   S H A D E R S
// ==========================================================================

static const char* kVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

static const char* kFragmentShaderInvert = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    fragColor = vec4(1.0 - displayRGB, uAlpha);
}
)";

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

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

    GLProgramBinaryCache cache(m_gl);
    auto invertProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("ToolCursorOverlayGL.invert"),
            QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShaderInvert));
    if (!invertProgram) {
        m_iconRenderer->shutdown();
        m_iconRenderer.reset();
        return { invertProgram.error().code, invertProgram.error().message };
    }

    m_invertProgram = invertProgram.value();
    m_locInvertMVP = m_gl->glGetUniformLocation(m_invertProgram, "uMVP");
    m_locInvertAlpha = m_gl->glGetUniformLocation(m_invertProgram, "uAlpha");
    m_locInvertSceneTexture = m_gl->glGetUniformLocation(m_invertProgram, "uSceneTexture");
    m_locInvertViewportSize = m_gl->glGetUniformLocation(m_invertProgram, "uViewportSize");

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 48 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);

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
    if (canDeleteGlObjects) {
        if (m_iconRenderer) {
            m_iconRenderer->shutdown();
        }
        if (m_vbo) {
            m_gl->glDeleteBuffers(1, &m_vbo);
        }
        if (m_vao) {
            m_gl->glDeleteVertexArrays(1, &m_vao);
        }
        if (m_invertProgram) {
            m_gl->glDeleteProgram(m_invertProgram);
        }
    }

    m_iconRenderer.reset();
    m_vbo = 0;
    m_vao = 0;
    m_invertProgram = 0;
    m_locInvertMVP = -1;
    m_locInvertAlpha = -1;
    m_locInvertSceneTexture = -1;
    m_locInvertViewportSize = -1;
    m_context.clear();
    m_initialized = false;
}

// ==========================================================================
//   R E N D E R
// ==========================================================================

void ToolCursorOverlayGL::render(float centerX, float centerY, int viewportWidth,
    int viewportHeight, GLuint sceneTextureId, ToolCursorStyle style,
    const QString& toolIconResource)
{
    if (!m_initialized || !sceneTextureId || !m_iconRenderer || style == ToolCursorStyle::None) {
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

    if (style == ToolCursorStyle::Crosshair) {
        drawCrosshair(centerX, centerY, mvp, vpW, vpH);
    } else {
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
    }

    m_gl->glDisable(GL_BLEND);
}

void ToolCursorOverlayGL::drawCrosshair(
    float centerX, float centerY, const std::array<float, 16>& mvp, float vpW, float vpH)
{
    const float half = kCrosshairThickness * 0.5f;
    const float gap = kCrosshairInnerGap;
    const float reach = kCrosshairLength;

    std::vector<float> vertices;
    vertices.reserve(4 * 12);
    appendRect(centerX - reach, centerY - half, centerX - gap, centerY + half, vertices);
    appendRect(centerX + gap, centerY - half, centerX + reach, centerY + half, vertices);
    appendRect(centerX - half, centerY - reach, centerX + half, centerY - gap, vertices);
    appendRect(centerX - half, centerY + gap, centerX + half, centerY + reach, vertices);

    m_gl->glUseProgram(m_invertProgram);
    m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvp.data());
    m_gl->glUniform1f(m_locInvertAlpha, 0.95f);
    m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 2));
    m_gl->glBindVertexArray(0);
}

void ToolCursorOverlayGL::appendRect(
    float left, float top, float right, float bottom, std::vector<float>& vertices)
{
    const float quad[12]
        = { left, top, right, top, right, bottom, left, top, right, bottom, left, bottom };
    vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
}

} // namespace aether
