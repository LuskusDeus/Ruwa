// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   E Y E D R O P P E R   C U R S O R   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/EyedropperCursorOverlayGL.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <QOpenGLContext>

#include <cmath>
#include <vector>

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

static const char* kFragmentShaderColorFromCenter = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform vec2 uCenter;
out vec4 fragColor;
void main() {
    ivec2 texelCoord = ivec2(clamp(floor(uCenter.x), 0.0, uViewportSize.x - 1.0),
                              clamp(floor(uCenter.y), 0.0, uViewportSize.y - 1.0));
    vec4 sampled = texelFetch(uSceneTexture, texelCoord, 0);
    fragColor = vec4(sampled.rgb, 0.95);
}
)";

static const char* kFragmentShaderSolidColor = R"(
#version 450 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

static const char* kFragmentShaderInvert = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    vec3 invRGB = 1.0 - displayRGB;
    fragColor = vec4(invRGB, uColor.a);
}
)";

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

EyedropperCursorOverlayGL::EyedropperCursorOverlayGL(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

EyedropperCursorOverlayGL::~EyedropperCursorOverlayGL()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> EyedropperCursorOverlayGL::initialize()
{
    QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if (!currentContext) {
        return { ErrorCode::InvalidArgument,
            "EyedropperCursorOverlayGL: no current OpenGL context" };
    }

    if (m_initialized && m_context == currentContext) {
        return Result<void>::ok();
    }

    if (m_initialized) {
        shutdown();
    }

    GLProgramBinaryCache cache(m_gl);
    auto colorFromCenterProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("EyedropperCursorOverlayGL.centerColor"),
            QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShaderColorFromCenter));
    if (!colorFromCenterProgram) {
        return { colorFromCenterProgram.error().code, colorFromCenterProgram.error().message };
    }

    m_colorFromCenterProgram = colorFromCenterProgram.value();
    m_locColorFromCenterMVP = m_gl->glGetUniformLocation(m_colorFromCenterProgram, "uMVP");
    m_locColorFromCenterSceneTexture
        = m_gl->glGetUniformLocation(m_colorFromCenterProgram, "uSceneTexture");
    m_locColorFromCenterViewportSize
        = m_gl->glGetUniformLocation(m_colorFromCenterProgram, "uViewportSize");
    m_locColorFromCenterCenter = m_gl->glGetUniformLocation(m_colorFromCenterProgram, "uCenter");

    auto invertProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("EyedropperCursorOverlayGL.invert"),
            QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShaderInvert));
    if (!invertProgram) {
        if (m_colorFromCenterProgram) {
            m_gl->glDeleteProgram(m_colorFromCenterProgram);
            m_colorFromCenterProgram = 0;
        }
        return { invertProgram.error().code, invertProgram.error().message };
    }

    m_invertProgram = invertProgram.value();
    m_locInvertMVP = m_gl->glGetUniformLocation(m_invertProgram, "uMVP");
    m_locInvertColor = m_gl->glGetUniformLocation(m_invertProgram, "uColor");
    m_locInvertSceneTexture = m_gl->glGetUniformLocation(m_invertProgram, "uSceneTexture");
    m_locInvertViewportSize = m_gl->glGetUniformLocation(m_invertProgram, "uViewportSize");

    auto solidColorProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("EyedropperCursorOverlayGL.solidColor"),
            QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShaderSolidColor));
    if (!solidColorProgram) {
        if (m_colorFromCenterProgram) {
            m_gl->glDeleteProgram(m_colorFromCenterProgram);
            m_colorFromCenterProgram = 0;
        }
        if (m_invertProgram) {
            m_gl->glDeleteProgram(m_invertProgram);
            m_invertProgram = 0;
        }
        return { solidColorProgram.error().code, solidColorProgram.error().message };
    }

    m_solidColorProgram = solidColorProgram.value();
    m_locSolidColorMVP = m_gl->glGetUniformLocation(m_solidColorProgram, "uMVP");
    m_locSolidColor = m_gl->glGetUniformLocation(m_solidColorProgram, "uColor");

    // VAO / VBO
    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);

    m_context = currentContext;
    m_initialized = true;
    return Result<void>::ok();
}

void EyedropperCursorOverlayGL::shutdown()
{
    if (!m_initialized)
        return;

    const bool canDeleteGlObjects
        = m_gl && m_context && QOpenGLContext::currentContext() == m_context;

    if (canDeleteGlObjects) {
        if (m_vbo) {
            m_gl->glDeleteBuffers(1, &m_vbo);
        }
        if (m_vao) {
            m_gl->glDeleteVertexArrays(1, &m_vao);
        }
        if (m_colorFromCenterProgram) {
            m_gl->glDeleteProgram(m_colorFromCenterProgram);
        }
        if (m_invertProgram) {
            m_gl->glDeleteProgram(m_invertProgram);
        }
        if (m_solidColorProgram) {
            m_gl->glDeleteProgram(m_solidColorProgram);
        }
    }

    m_vbo = 0;
    m_vao = 0;
    m_colorFromCenterProgram = 0;
    m_invertProgram = 0;
    m_solidColorProgram = 0;
    m_locColorFromCenterMVP = -1;
    m_locColorFromCenterSceneTexture = -1;
    m_locColorFromCenterViewportSize = -1;
    m_locColorFromCenterCenter = -1;
    m_locInvertMVP = -1;
    m_locInvertColor = -1;
    m_locInvertSceneTexture = -1;
    m_locInvertViewportSize = -1;
    m_locSolidColorMVP = -1;
    m_locSolidColor = -1;
    m_context.clear();

    m_initialized = false;
}

// ==========================================================================
//   R E N D E R
// ==========================================================================

void EyedropperCursorOverlayGL::render(float centerX, float centerY, int viewportWidth,
    int viewportHeight, GLuint sceneTextureId, const QColor& selectedColor)
{
    if (!m_initialized || !sceneTextureId)
        return;

    const float vpW = static_cast<float>(viewportWidth);
    const float vpH = static_cast<float>(viewportHeight);

    const float invW = 1.0f / vpW;
    const float invH = 1.0f / vpH;
    std::array<float, 16> mvpArr
        = { { 2.0f * invW, 0, 0, 0, 0, -2.0f * invH, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1 } };

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);

    // gl_FragCoord has y from bottom; center in window space
    const float centerWindowX = centerX;
    const float centerWindowY = vpH - centerY;

    std::vector<float> vertices;
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    auto uploadAndDraw = [this, &vertices]() {
        m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
        m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(vertices.size() / 2));
    };

    const float headCx = centerX;
    const float headCy = centerY - kHeadCenterOffsetY;
    const float outerR = kHeadRadius + kBorderThickness;
    const float innerR = kHeadRadius;

    // 1. Thin outer border, sampled as an inverse of the scene under the widget.
    drawTeardrop(headCx, headCy, outerR, centerY, vertices);
    m_gl->glUseProgram(m_invertProgram);
    m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvpArr.data());
    m_gl->glUniform4f(m_locInvertColor, 0, 0, 0, 0.95f);
    m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);
    uploadAndDraw();

    // 2. Current selected color fills the bottom half and the pointer tail.
    drawTeardrop(headCx, headCy, innerR, centerY - kBorderThickness, vertices);
    m_gl->glUseProgram(m_solidColorProgram);
    m_gl->glUniformMatrix4fv(m_locSolidColorMVP, 1, GL_FALSE, mvpArr.data());
    m_gl->glUniform4f(m_locSolidColor, static_cast<float>(selectedColor.redF()),
        static_cast<float>(selectedColor.greenF()), static_cast<float>(selectedColor.blueF()),
        0.98f);
    uploadAndDraw();

    // 3. The upper swatch is the color sampled exactly under the cursor tip.
    drawSemicircle(headCx, headCy, innerR, true, 40, vertices);
    m_gl->glUseProgram(m_colorFromCenterProgram);
    m_gl->glUniformMatrix4fv(m_locColorFromCenterMVP, 1, GL_FALSE, mvpArr.data());
    m_gl->glUniform1i(m_locColorFromCenterSceneTexture, 0);
    m_gl->glUniform2f(m_locColorFromCenterViewportSize, vpW, vpH);
    m_gl->glUniform2f(m_locColorFromCenterCenter, centerWindowX, centerWindowY);
    uploadAndDraw();

    // 4. Inverted divider between sampled and selected swatches.
    drawRect(headCx - innerR, headCy - kDividerThickness * 0.5f, headCx + innerR,
        headCy + kDividerThickness * 0.5f, vertices);
    m_gl->glUseProgram(m_invertProgram);
    m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvpArr.data());
    m_gl->glUniform4f(m_locInvertColor, 0, 0, 0, 0.95f);
    m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);
    uploadAndDraw();

    m_gl->glBindVertexArray(0);
    m_gl->glDisable(GL_BLEND);
}

void EyedropperCursorOverlayGL::drawTeardrop(
    float cx, float cy, float radius, float tipY, std::vector<float>& vertices)
{
    constexpr float pi = 3.14159265f;
    constexpr int arcSegments = 52;

    vertices.clear();
    vertices.reserve((arcSegments + 4) * 2);
    vertices.push_back(cx);
    vertices.push_back(cy + radius * 0.22f);

    const float tipDistance = std::abs(tipY - cy);
    const float tangentAngle
        = tipDistance > radius ? std::asin(radius / tipDistance) * 180.0f / pi : 30.0f;
    const float overlapAngle = kTailCircleOverlapPx / radius * 180.0f / pi;
    const float startDeg = tangentAngle - overlapAngle;
    const float endDeg = -180.0f - tangentAngle + overlapAngle;
    for (int i = 0; i <= arcSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(arcSegments);
        const float deg = startDeg + (endDeg - startDeg) * t;
        const float angle = deg * pi / 180.0f;
        vertices.push_back(cx + radius * std::cos(angle));
        vertices.push_back(cy + radius * std::sin(angle));
    }

    vertices.push_back(cx);
    vertices.push_back(tipY);
    vertices.push_back(cx + radius * std::cos(startDeg * pi / 180.0f));
    vertices.push_back(cy + radius * std::sin(startDeg * pi / 180.0f));
}

void EyedropperCursorOverlayGL::drawSemicircle(
    float cx, float cy, float radius, bool upper, int segments, std::vector<float>& vertices)
{
    constexpr float pi = 3.14159265f;

    vertices.clear();
    vertices.reserve((segments + 3) * 2);
    vertices.push_back(cx);
    vertices.push_back(cy);

    const float start = upper ? pi : 0.0f;
    const float end = upper ? 2.0f * pi : pi;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = start + (end - start) * t;
        vertices.push_back(cx + radius * std::cos(angle));
        vertices.push_back(cy + radius * std::sin(angle));
    }
}

void EyedropperCursorOverlayGL::drawRect(
    float left, float top, float right, float bottom, std::vector<float>& vertices)
{
    vertices = { (left + right) * 0.5f, (top + bottom) * 0.5f, left, top, right, top, right, bottom,
        left, bottom, left, top };
}

} // namespace aether
