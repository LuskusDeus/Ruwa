// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/overlays/TextEditOverlayGL.h"

#include "shared/rendering/GLProgramBinaryCache.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aether {
namespace {

static const char* kVertexShaderWorld = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

static const char* kFragmentColor = R"(
#version 450 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

static const char* kFragmentInvert = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    fragColor = vec4(1.0 - displayRGB, uColor.a);
}
)";

std::array<Vector2, 4> transformedRectCorners(const TransformState& transform, const Rect& rect)
{
    return { transform.transformPoint({ rect.left(), rect.top() }),
        transform.transformPoint({ rect.right(), rect.top() }),
        transform.transformPoint({ rect.right(), rect.bottom() }),
        transform.transformPoint({ rect.left(), rect.bottom() }) };
}

Vector2 worldToScreenWithMatrix(
    const Vector2& world, const std::array<float, 16>& matrix, int surfaceWidth, int surfaceHeight)
{
    const float clipX = matrix[0] * world.x + matrix[4] * world.y + matrix[12];
    const float clipY = matrix[1] * world.x + matrix[5] * world.y + matrix[13];
    return { (clipX * 0.5f + 0.5f) * static_cast<float>(surfaceWidth),
        (0.5f - clipY * 0.5f) * static_cast<float>(surfaceHeight) };
}

std::array<float, 16> screenProjectionMatrix(int surfaceWidth, int surfaceHeight)
{
    const float width = static_cast<float>(std::max(1, surfaceWidth));
    const float height = static_cast<float>(std::max(1, surfaceHeight));
    return { 2.0f / width, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f / height, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, -1.0f, 1.0f, 0.0f, 1.0f };
}

} // namespace

TextEditOverlayGL::TextEditOverlayGL(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

TextEditOverlayGL::~TextEditOverlayGL()
{
    shutdown();
}

Result<void> TextEditOverlayGL::initialize()
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    GLProgramBinaryCache cache(m_gl);
    auto colorProgram = cache.loadOrCreateGraphicsProgram(QStringLiteral("TextEditOverlayGL.color"),
        QString::fromUtf8(kVertexShaderWorld), QString::fromUtf8(kFragmentColor));
    if (!colorProgram) {
        return { colorProgram.error().code, colorProgram.error().message };
    }

    auto invertProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("TextEditOverlayGL.invert"),
            QString::fromUtf8(kVertexShaderWorld), QString::fromUtf8(kFragmentInvert));
    if (!invertProgram) {
        if (colorProgram.value()) {
            m_gl->glDeleteProgram(colorProgram.value());
        }
        return { invertProgram.error().code, invertProgram.error().message };
    }

    m_colorProgram = colorProgram.value();
    m_invertProgram = invertProgram.value();
    m_locColorMvp = m_gl->glGetUniformLocation(m_colorProgram, "uMVP");
    m_locColor = m_gl->glGetUniformLocation(m_colorProgram, "uColor");
    m_locInvertMvp = m_gl->glGetUniformLocation(m_invertProgram, "uMVP");
    m_locInvertColor = m_gl->glGetUniformLocation(m_invertProgram, "uColor");
    m_locInvertSceneTexture = m_gl->glGetUniformLocation(m_invertProgram, "uSceneTexture");
    m_locInvertViewportSize = m_gl->glGetUniformLocation(m_invertProgram, "uViewportSize");

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);

    m_initialized = true;
    return Result<void>::ok();
}

void TextEditOverlayGL::shutdown()
{
    if (!m_initialized) {
        return;
    }
    if (m_vbo) {
        m_gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_colorProgram) {
        m_gl->glDeleteProgram(m_colorProgram);
        m_colorProgram = 0;
    }
    if (m_invertProgram) {
        m_gl->glDeleteProgram(m_invertProgram);
        m_invertProgram = 0;
    }
    m_initialized = false;
}

void TextEditOverlayGL::setState(TextEditOverlayState state)
{
    m_state = std::move(state);
}

void TextEditOverlayGL::render(const Viewport& viewport, GLuint sceneTextureId, int surfaceWidth,
    int surfaceHeight, const std::array<float, 16>* viewProjectionContent)
{
    if (!m_initialized || !m_state.active || surfaceWidth <= 0 || surfaceHeight <= 0) {
        return;
    }

    m_surfaceWidth = surfaceWidth;
    m_surfaceHeight = surfaceHeight;
    m_sceneTextureId = sceneTextureId;
    const auto vpMatrix
        = viewProjectionContent ? *viewProjectionContent : viewport.viewProjectionMatrix();

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    const bool canInvertScene = sceneTextureId != 0;
    for (const Rect& rect : m_state.selectionSourceRects) {
        if (canInvertScene) {
            drawSourceRect(rect, 1.0f, 1.0f, 1.0f, 1.0f, vpMatrix, true);
        } else {
            drawSourceRect(rect, 0.17f, 0.47f, 1.0f, 0.32f, vpMatrix, false);
        }
    }
    if (m_state.caretVisible && m_state.caretSourceRect.height > 0.0f) {
        drawCaretScreenRect(
            m_state.caretSourceRect, 1.0f, 1.0f, 1.0f, 0.95f, vpMatrix, sceneTextureId != 0);
    }

    m_gl->glDisable(GL_BLEND);
}

void TextEditOverlayGL::drawSourceRect(const Rect& rect, float r, float g, float b, float a,
    const std::array<float, 16>& vpMatrix, bool invert)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f || a <= 0.0f) {
        return;
    }
    drawWorldQuad(transformedRectCorners(m_state.transform, rect), r, g, b, a, vpMatrix, invert);
}

void TextEditOverlayGL::drawCaretScreenRect(const Rect& rect, float r, float g, float b, float a,
    const std::array<float, 16>& vpMatrix, bool invert)
{
    if (rect.height <= 0.0f || a <= 0.0f) {
        return;
    }

    const float sourceX = rect.left() + rect.width * 0.5f;
    const Vector2 topWorld = m_state.transform.transformPoint({ sourceX, rect.top() });
    const Vector2 bottomWorld = m_state.transform.transformPoint({ sourceX, rect.bottom() });
    const Vector2 topScreen
        = worldToScreenWithMatrix(topWorld, vpMatrix, m_surfaceWidth, m_surfaceHeight);
    const Vector2 bottomScreen
        = worldToScreenWithMatrix(bottomWorld, vpMatrix, m_surfaceWidth, m_surfaceHeight);

    const float dx = bottomScreen.x - topScreen.x;
    const float dy = bottomScreen.y - topScreen.y;
    float len = std::sqrt(dx * dx + dy * dy);
    Vector2 axis { 0.0f, 1.0f };
    if (len > 0.001f) {
        axis = { dx / len, dy / len };
    } else {
        const Vector2 fallbackTopWorld
            = m_state.transform.transformPoint({ sourceX, rect.top() - 1.0f });
        const Vector2 fallbackTopScreen
            = worldToScreenWithMatrix(fallbackTopWorld, vpMatrix, m_surfaceWidth, m_surfaceHeight);
        const float fdx = topScreen.x - fallbackTopScreen.x;
        const float fdy = topScreen.y - fallbackTopScreen.y;
        const float fallbackLen = std::sqrt(fdx * fdx + fdy * fdy);
        if (fallbackLen > 0.001f) {
            axis = { fdx / fallbackLen, fdy / fallbackLen };
        }
    }

    constexpr float kCaretWidthPx = 2.0f;
    constexpr float kMinCaretHeightPx = 14.0f;
    const float halfWidth = kCaretWidthPx * 0.5f;
    const float halfHeight = std::max(len, kMinCaretHeightPx) * 0.5f;
    const Vector2 center { (topScreen.x + bottomScreen.x) * 0.5f,
        (topScreen.y + bottomScreen.y) * 0.5f };
    const Vector2 topVisible { center.x - axis.x * halfHeight, center.y - axis.y * halfHeight };
    const Vector2 bottomVisible { center.x + axis.x * halfHeight, center.y + axis.y * halfHeight };
    const Vector2 normal { -axis.y * halfWidth, axis.x * halfWidth };

    const std::array<Vector2, 4> screenQuad
        = { Vector2 { topVisible.x - normal.x, topVisible.y - normal.y },
              Vector2 { topVisible.x + normal.x, topVisible.y + normal.y },
              Vector2 { bottomVisible.x + normal.x, bottomVisible.y + normal.y },
              Vector2 { bottomVisible.x - normal.x, bottomVisible.y - normal.y } };

    drawWorldQuad(
        screenQuad, r, g, b, a, screenProjectionMatrix(m_surfaceWidth, m_surfaceHeight), invert);
}

void TextEditOverlayGL::drawWorldQuad(const std::array<Vector2, 4>& points, float r, float g,
    float b, float a, const std::array<float, 16>& vpMatrix, bool invert)
{
    const float verts[] = { points[0].x, points[0].y, points[1].x, points[1].y, points[2].x,
        points[2].y, points[0].x, points[0].y, points[2].x, points[2].y, points[3].x, points[3].y };

    const GLuint program = invert && m_sceneTextureId != 0 ? m_invertProgram : m_colorProgram;
    m_gl->glUseProgram(program);
    if (program == m_invertProgram) {
        m_gl->glUniformMatrix4fv(m_locInvertMvp, 1, GL_FALSE, vpMatrix.data());
        m_gl->glUniform4f(m_locInvertColor, r, g, b, a);
        m_gl->glUniform2f(m_locInvertViewportSize, static_cast<float>(m_surfaceWidth),
            static_cast<float>(m_surfaceHeight));
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_sceneTextureId);
        m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    } else {
        m_gl->glUniformMatrix4fv(m_locColorMvp, 1, GL_FALSE, vpMatrix.data());
        m_gl->glUniform4f(m_locColor, r, g, b, a);
    }

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

} // namespace aether
