// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   C U R S O R   O V E R L A Y   ( G L )
// ==========================================================================

#include "features/canvas/overlays/BrushCursorOverlayGL.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <QOpenGLContext>

#include <algorithm>
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

static const char* kFragmentShaderInvert = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    // Scene uses premultiplied alpha — un-premultiply before inverting display color
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    vec3 invRGB = 1.0 - displayRGB;
    fragColor = vec4(invRGB, uColor.a);
}
)";

static const char* kFragmentShaderPassthrough = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    fragColor = texture(uSceneTexture, uv);
}
)";

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

BrushCursorOverlayGL::BrushCursorOverlayGL(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

BrushCursorOverlayGL::~BrushCursorOverlayGL()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> BrushCursorOverlayGL::initialize()
{
    QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if (!currentContext) {
        return { ErrorCode::InvalidArgument, "BrushCursorOverlayGL: no current OpenGL context" };
    }

    if (m_initialized && m_context == currentContext) {
        return Result<void>::ok();
    }

    if (m_initialized) {
        shutdown();
    }

    GLProgramBinaryCache cache(m_gl);
    auto invertProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("BrushCursorOverlayGL.invert"),
            QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShaderInvert));
    if (!invertProgram) {
        return { invertProgram.error().code, invertProgram.error().message };
    }

    m_invertProgram = invertProgram.value();
    m_locInvertMVP = m_gl->glGetUniformLocation(m_invertProgram, "uMVP");
    m_locInvertColor = m_gl->glGetUniformLocation(m_invertProgram, "uColor");
    m_locInvertSceneTexture = m_gl->glGetUniformLocation(m_invertProgram, "uSceneTexture");
    m_locInvertViewportSize = m_gl->glGetUniformLocation(m_invertProgram, "uViewportSize");

    auto passthroughProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("BrushCursorOverlayGL.passthrough"),
            QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShaderPassthrough));
    if (!passthroughProgram) {
        if (m_invertProgram) {
            m_gl->glDeleteProgram(m_invertProgram);
            m_invertProgram = 0;
        }
        return { passthroughProgram.error().code, passthroughProgram.error().message };
    }

    m_passthroughProgram = passthroughProgram.value();
    m_locPassthroughMVP = m_gl->glGetUniformLocation(m_passthroughProgram, "uMVP");
    m_locPassthroughSceneTexture
        = m_gl->glGetUniformLocation(m_passthroughProgram, "uSceneTexture");
    m_locPassthroughViewportSize
        = m_gl->glGetUniformLocation(m_passthroughProgram, "uViewportSize");

    // VAO / VBO
    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_vboCapacityBytes = 4096 * static_cast<GLsizeiptr>(sizeof(float));
    m_gl->glBufferData(GL_ARRAY_BUFFER, m_vboCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);

    m_context = currentContext;
    m_initialized = true;
    return Result<void>::ok();
}

void BrushCursorOverlayGL::shutdown()
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
        if (m_passthroughProgram) {
            m_gl->glDeleteProgram(m_passthroughProgram);
        }
        if (m_invertProgram) {
            m_gl->glDeleteProgram(m_invertProgram);
        }
    }

    m_vbo = 0;
    m_vboCapacityBytes = 0;
    m_vao = 0;
    m_passthroughProgram = 0;
    m_invertProgram = 0;
    m_locInvertMVP = -1;
    m_locInvertColor = -1;
    m_locInvertSceneTexture = -1;
    m_locInvertViewportSize = -1;
    m_locPassthroughMVP = -1;
    m_locPassthroughSceneTexture = -1;
    m_locPassthroughViewportSize = -1;
    m_context.clear();

    m_initialized = false;
}

// ==========================================================================
//   R E N D E R
// ==========================================================================

void BrushCursorOverlayGL::setStampContours(const std::vector<std::vector<Vector2>>& contours)
{
    m_stampContours = contours;
}

void BrushCursorOverlayGL::render(float centerX, float centerY, float radiusPx, int viewportWidth,
    int viewportHeight, GLuint sceneTextureId, float rotationRadians)
{
    if (!m_initialized || !sceneTextureId || radiusPx < 0.5f)
        return;
    const float rotCos = std::cos(rotationRadians);
    const float rotSin = std::sin(rotationRadians);

    const float innerRadius = std::max(0.0f, radiusPx - kStrokeWidth);

    // Ortho: Qt coords (origin top-left), GL NDC: x 0..w->-1..1, y 0 at top->1, h at bottom->-1
    const float invW = 1.0f / viewportWidth;
    const float invH = 1.0f / viewportHeight;
    std::array<float, 16> mvpArr
        = { { 2.0f * invW, 0, 0, 0, 0, -2.0f * invH, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1 } };

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    const float vpW = static_cast<float>(viewportWidth);
    const float vpH = static_cast<float>(viewportHeight);

    if (!m_stampContours.empty()) {
        for (const auto& contour : m_stampContours) {
            if (contour.size() < 3)
                continue;
            drawPolygonStroke(centerX, centerY, radiusPx, kStrokeWidth, contour, rotCos, rotSin,
                mvpArr, sceneTextureId, vpW, vpH);
        }
    } else {
        if (innerRadius > 0.001f) {
            drawRing(centerX, centerY, radiusPx, innerRadius, 64, mvpArr, sceneTextureId, vpW, vpH);
        } else {
            drawCircle(centerX, centerY, radiusPx, 64, mvpArr, sceneTextureId, vpW, vpH);
        }
    }

    m_gl->glDisable(GL_BLEND);
}

void BrushCursorOverlayGL::drawCircle(float cx, float cy, float radius, int segments,
    const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH)
{
    const int vertCount = segments + 2;
    std::vector<float> vertices(vertCount * 2);

    vertices[0] = cx;
    vertices[1] = cy;

    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(segments);
        vertices[(i + 1) * 2 + 0] = cx + radius * std::cos(angle);
        vertices[(i + 1) * 2 + 1] = cy + radius * std::sin(angle);
    }

    m_gl->glUseProgram(m_invertProgram);
    m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvp.data());
    m_gl->glUniform4f(m_locInvertColor, 0, 0, 0, 0.95f);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
    m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, vertCount);
    m_gl->glBindVertexArray(0);
}

void BrushCursorOverlayGL::drawPolygonStroke(float cx, float cy, float scale, float strokeWidth,
    const std::vector<Vector2>& contour, float rotationCos, float rotationSin,
    const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH)
{
    if (contour.size() < 3)
        return;

    const size_t n = contour.size();
    std::vector<Vector2> points(n);
    for (size_t i = 0; i < n; ++i) {
        const float nx = contour[i].x * rotationCos - contour[i].y * rotationSin;
        const float ny = contour[i].x * rotationSin + contour[i].y * rotationCos;
        points[i] = Vector2(cx + nx * scale, cy + ny * scale);
    }

    const float halfWidth = std::max(0.5f, strokeWidth * 0.5f);
    std::vector<float> vertices;
    vertices.reserve(n * 24);

    const auto appendTriangle = [&vertices](
                                    const Vector2& a, const Vector2& b, const Vector2& c) {
        vertices.push_back(a.x);
        vertices.push_back(a.y);
        vertices.push_back(b.x);
        vertices.push_back(b.y);
        vertices.push_back(c.x);
        vertices.push_back(c.y);
    };

    for (size_t i = 0; i < n; ++i) {
        const Vector2& from = points[i];
        const Vector2& to = points[(i + 1) % n];
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::hypot(dx, dy);
        if (length <= 0.0001f)
            continue;

        const Vector2 normal(-dy * halfWidth / length, dx * halfWidth / length);
        const Vector2 fromLeft(from.x + normal.x, from.y + normal.y);
        const Vector2 fromRight(from.x - normal.x, from.y - normal.y);
        const Vector2 toLeft(to.x + normal.x, to.y + normal.y);
        const Vector2 toRight(to.x - normal.x, to.y - normal.y);
        appendTriangle(fromLeft, fromRight, toLeft);
        appendTriangle(toLeft, fromRight, toRight);

        // A tiny square join covers the wedge between adjacent segment quads. Unlike a miter,
        // it has a fixed one-pixel reach and therefore cannot fold over at short concave turns.
        const Vector2 topLeft(from.x - halfWidth, from.y - halfWidth);
        const Vector2 topRight(from.x + halfWidth, from.y - halfWidth);
        const Vector2 bottomLeft(from.x - halfWidth, from.y + halfWidth);
        const Vector2 bottomRight(from.x + halfWidth, from.y + halfWidth);
        appendTriangle(topLeft, bottomLeft, topRight);
        appendTriangle(topRight, bottomLeft, bottomRight);
    }

    if (vertices.empty())
        return;

    m_gl->glUseProgram(m_invertProgram);
    m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvp.data());
    m_gl->glUniform4f(m_locInvertColor, 0, 0, 0, 0.95f);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
    m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizeiptr requiredBytes
        = static_cast<GLsizeiptr>(vertices.size() * sizeof(float));
    if (requiredBytes > m_vboCapacityBytes) {
        m_vboCapacityBytes = std::max(requiredBytes, m_vboCapacityBytes * 2);
        m_gl->glBufferData(GL_ARRAY_BUFFER, m_vboCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, vertices.data());
    m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 2));

    m_gl->glBindVertexArray(0);
}

void BrushCursorOverlayGL::drawRing(float cx, float cy, float outerRadius, float innerRadius,
    int segments, const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH)
{
    std::vector<float> outerFan((segments + 2) * 2);
    outerFan[0] = cx;
    outerFan[1] = cy;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(segments);
        outerFan[(i + 1) * 2 + 0] = cx + outerRadius * std::cos(angle);
        outerFan[(i + 1) * 2 + 1] = cy + outerRadius * std::sin(angle);
    }

    m_gl->glUseProgram(m_invertProgram);
    m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvp.data());
    m_gl->glUniform4f(m_locInvertColor, 0, 0, 0, 0.95f);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
    m_gl->glUniform1i(m_locInvertSceneTexture, 0);
    m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(outerFan.size() * sizeof(float)), outerFan.data());
    m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);

    std::vector<float> innerFan((segments + 2) * 2);
    innerFan[0] = cx;
    innerFan[1] = cy;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(segments);
        innerFan[(i + 1) * 2 + 0] = cx + innerRadius * std::cos(angle);
        innerFan[(i + 1) * 2 + 1] = cy + innerRadius * std::sin(angle);
    }

    m_gl->glBlendFunc(GL_ONE, GL_ZERO);
    m_gl->glUseProgram(m_passthroughProgram);
    m_gl->glUniformMatrix4fv(m_locPassthroughMVP, 1, GL_FALSE, mvp.data());
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
    m_gl->glUniform1i(m_locPassthroughSceneTexture, 0);
    m_gl->glUniform2f(m_locPassthroughViewportSize, vpW, vpH);

    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(innerFan.size() * sizeof(float)), innerFan.data());
    m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_gl->glBindVertexArray(0);
}

} // namespace aether
