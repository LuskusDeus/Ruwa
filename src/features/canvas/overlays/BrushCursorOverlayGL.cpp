// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   C U R S O R   O V E R L A Y   ( G L )
// ==========================================================================

#include "features/canvas/overlays/BrushCursorOverlayGL.h"
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
    m_gl->glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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
            drawPolygonRing(centerX, centerY, radiusPx, innerRadius, contour, rotCos, rotSin,
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

void BrushCursorOverlayGL::drawPolygonRing(float cx, float cy, float outerScale, float innerScale,
    const std::vector<Vector2>& contour, float rotationCos, float rotationSin,
    const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH)
{
    if (contour.size() < 3)
        return;

    const size_t n = contour.size();
    std::vector<float> outerFan((n + 2) * 2);
    std::vector<float> innerFan((n + 2) * 2);

    // Outer polygon in pixel space. Apply dynamic rotation (from direction
    // dynamics) to each normalized contour point, then scale by radius.
    std::vector<Vector2> outerPts(n);
    float centroidX = 0.0f;
    float centroidY = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float nx = contour[i].x * rotationCos - contour[i].y * rotationSin;
        const float ny = contour[i].x * rotationSin + contour[i].y * rotationCos;
        outerPts[i] = Vector2(cx + nx * outerScale, cy + ny * outerScale);
        centroidX += outerPts[i].x;
        centroidY += outerPts[i].y;
    }
    centroidX /= static_cast<float>(n);
    centroidY /= static_cast<float>(n);

    // Triangle-fan centers. The contour is star-convex from its own centroid
    // (by construction in BrushCursorContourBuilder), so the fan correctly
    // fills the polygon interior even when the brush center (cx, cy) lies
    // outside this sub-contour (multi-blob dabs).
    outerFan[0] = centroidX;
    outerFan[1] = centroidY;
    innerFan[0] = centroidX;
    innerFan[1] = centroidY;

    // Stroke is the radial gap between outer and inner scales — uniform in pixels.
    const float strokeWidth = std::max(0.5f, outerScale - innerScale);
    // Clamp miter to avoid spikes on sharp corners.
    const float maxMiterLen = strokeWidth * 4.0f;
    const float maxMiterLenSq = maxMiterLen * maxMiterLen;

    std::vector<Vector2> innerPts(n);
    for (size_t i = 0; i < n; ++i) {
        const Vector2& prev = outerPts[(i + n - 1) % n];
        const Vector2& curr = outerPts[i];
        const Vector2& next = outerPts[(i + 1) % n];

        // 90°-rotated edge vectors; pick orientation that points toward (cx, cy).
        Vector2 n1(-(curr.y - prev.y), curr.x - prev.x);
        Vector2 n2(-(next.y - curr.y), next.x - curr.x);
        const float l1 = std::sqrt(n1.x * n1.x + n1.y * n1.y);
        const float l2 = std::sqrt(n2.x * n2.x + n2.y * n2.y);
        if (l1 > 0.0001f) {
            n1.x /= l1;
            n1.y /= l1;
        }
        if (l2 > 0.0001f) {
            n2.x /= l2;
            n2.y /= l2;
        }

        Vector2 sum(n1.x + n2.x, n1.y + n2.y);
        // Miter: m = sum * 2 * strokeWidth / |sum|² so that m·n1 = m·n2 = strokeWidth.
        const float denom = sum.x * sum.x + sum.y * sum.y;
        Vector2 miter;
        if (denom > 0.0001f) {
            const float k = 2.0f * strokeWidth / denom;
            miter = Vector2(sum.x * k, sum.y * k);
            const float mlSq = miter.x * miter.x + miter.y * miter.y;
            if (mlSq > maxMiterLenSq) {
                const float s = std::sqrt(maxMiterLenSq / mlSq);
                miter.x *= s;
                miter.y *= s;
            }
        } else {
            // Degenerate (180° turn) — fall back to inset toward polygon centroid.
            const float dx = curr.x - centroidX;
            const float dy = curr.y - centroidY;
            const float dl = std::sqrt(dx * dx + dy * dy);
            if (dl > 0.0001f) {
                miter = Vector2(-dx * strokeWidth / dl, -dy * strokeWidth / dl);
            }
        }

        // Flip miter toward the polygon's own interior (its centroid) if it
        // points outward. Using (cx, cy) here would break for sub-contours
        // that don't enclose the brush center (multi-blob dabs).
        const float toCenterX = centroidX - curr.x;
        const float toCenterY = centroidY - curr.y;
        if (miter.x * toCenterX + miter.y * toCenterY < 0.0f) {
            miter.x = -miter.x;
            miter.y = -miter.y;
        }

        innerPts[i] = Vector2(curr.x + miter.x, curr.y + miter.y);
    }

    for (size_t i = 0; i <= n; ++i) {
        const size_t idx = i % n;
        outerFan[(i + 1) * 2 + 0] = outerPts[idx].x;
        outerFan[(i + 1) * 2 + 1] = outerPts[idx].y;
        innerFan[(i + 1) * 2 + 0] = innerPts[idx].x;
        innerFan[(i + 1) * 2 + 1] = innerPts[idx].y;
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
    m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(n + 2));

    m_gl->glBlendFunc(GL_ONE, GL_ZERO); // passthrough: replace, no blend (avoids premult mismatch)
    m_gl->glUseProgram(m_passthroughProgram);
    m_gl->glUniformMatrix4fv(m_locPassthroughMVP, 1, GL_FALSE, mvp.data());
    m_gl->glUniform1i(m_locPassthroughSceneTexture, 0);
    m_gl->glUniform2f(m_locPassthroughViewportSize, vpW, vpH);

    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(innerFan.size() * sizeof(float)), innerFan.data());
    m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(n + 2));
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // restore for callers

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
