// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   E Y E D R O P P E R   C U R S O R   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/EyedropperCursorOverlayGL.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <QOpenGLContext>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
    fragColor = vec4(sampled.rgb, 1.0);
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

    m_iconRenderer = std::make_unique<GLCursorIconRenderer>(m_gl);
    auto iconResult = m_iconRenderer->initialize();
    if (!iconResult) {
        for (GLuint* program :
            { &m_colorFromCenterProgram, &m_invertProgram, &m_solidColorProgram }) {
            if (*program) {
                m_gl->glDeleteProgram(*program);
                *program = 0;
            }
        }
        m_iconRenderer.reset();
        return { iconResult.error().code, iconResult.error().message };
    }

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
        if (m_iconRenderer) {
            m_iconRenderer->shutdown();
        }
    }
    m_iconRenderer.reset();

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

    m_gl->glBindTextureUnit(0, sceneTextureId);

    // gl_FragCoord has y from bottom; center in window space
    const float centerWindowX = centerX;
    const float centerWindowY = vpH - centerY;

    std::vector<float> vertices;
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    auto uploadAndDraw = [this, &vertices](GLenum mode) {
        m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
        m_gl->glDrawArrays(mode, 0, static_cast<GLsizei>(vertices.size() / 2));
    };

    auto useInvertProgram = [&]() {
        m_gl->glUseProgram(m_invertProgram);
        m_gl->glUniformMatrix4fv(m_locInvertMVP, 1, GL_FALSE, mvpArr.data());
        m_gl->glUniform4f(m_locInvertColor, 0, 0, 0, 0.95f);
        m_gl->glUniform1i(m_locInvertSceneTexture, 0);
        m_gl->glUniform2f(m_locInvertViewportSize, vpW, vpH);
    };

    constexpr float pi = 3.14159265f;
    const float innerR = kInnerRadius;
    const float outerR = kOuterRadius;
    const float midR = (innerR + outerR) * 0.5f;
    // The seam between the two halves is the same visual width as the outline.
    const float seamDeg = kBorderThickness / midR * 180.0f / pi;

    // 1. One inverted ring, slightly wider than the color band on both rims. The
    //    color halves are drawn on top and leave that overhang uncovered, so the
    //    outline, the inner rim and the two seams all come from this single pass.
    buildAnnulusSector(centerX, centerY, innerR - kBorderThickness, outerR + kBorderThickness, 0.0f,
        360.0f, kArcSegments, vertices);
    useInvertProgram();
    uploadAndDraw(GL_TRIANGLE_STRIP);

    // 2. Upper half: the color sampled exactly under the cursor.
    buildAnnulusSector(centerX, centerY, innerR, outerR, 180.0f + seamDeg, 360.0f - seamDeg,
        kArcSegments / 2, vertices);
    m_gl->glUseProgram(m_colorFromCenterProgram);
    m_gl->glUniformMatrix4fv(m_locColorFromCenterMVP, 1, GL_FALSE, mvpArr.data());
    m_gl->glUniform1i(m_locColorFromCenterSceneTexture, 0);
    m_gl->glUniform2f(m_locColorFromCenterViewportSize, vpW, vpH);
    m_gl->glUniform2f(m_locColorFromCenterCenter, centerWindowX, centerWindowY);
    uploadAndDraw(GL_TRIANGLE_STRIP);

    // 3. Lower half: the currently selected color.
    buildAnnulusSector(
        centerX, centerY, innerR, outerR, seamDeg, 180.0f - seamDeg, kArcSegments / 2, vertices);
    m_gl->glUseProgram(m_solidColorProgram);
    m_gl->glUniformMatrix4fv(m_locSolidColorMVP, 1, GL_FALSE, mvpArr.data());
    m_gl->glUniform4f(m_locSolidColor, static_cast<float>(selectedColor.redF()),
        static_cast<float>(selectedColor.greenF()), static_cast<float>(selectedColor.blueF()),
        1.0f);
    uploadAndDraw(GL_TRIANGLE_STRIP);

    m_gl->glBindVertexArray(0);

    // 4. The pointer itself. Windows delivers the system cursor one frame behind
    //    the position this overlay is drawn at, so the tool icon is drawn here
    //    instead and stays locked to the ring.
    drawIcon(centerX, centerY, mvpArr, vpW, vpH);

    m_gl->glDisable(GL_BLEND);
}

CursorCaptureRect EyedropperCursorOverlayGL::captureRect(float centerX, float centerY)
{
    // The inverted outline overhangs the color band by kBorderThickness on both
    // rims; the extra pixels are the usual guard band for linear sampling.
    constexpr float kPadPx = 3.0f;
    const float reach = kOuterRadius + kBorderThickness + kPadPx;
    return { centerX - reach, centerY - reach, centerX + reach, centerY + reach };
}

void EyedropperCursorOverlayGL::drawIcon(float centerX, float centerY,
    const std::array<float, 16>& mvp, float viewportW, float viewportH)
{
    if (!m_iconRenderer) {
        return;
    }
    m_iconRenderer->drawAtHotspot(QString::fromUtf8(kIconResourcePath), kIconSizePx, kIconHotspotU,
        kIconHotspotV, centerX, centerY, mvp, viewportW, viewportH);
}

void EyedropperCursorOverlayGL::buildAnnulusSector(float cx, float cy, float innerRadius,
    float outerRadius, float startDeg, float endDeg, int segments, std::vector<float>& vertices)
{
    constexpr float pi = 3.14159265f;

    segments = std::max(segments, 2);
    vertices.clear();
    vertices.reserve(static_cast<std::size_t>(segments + 1) * 4);

    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = (startDeg + (endDeg - startDeg) * t) * pi / 180.0f;
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);
        vertices.push_back(cx + innerRadius * cosA);
        vertices.push_back(cy + innerRadius * sinA);
        vertices.push_back(cx + outerRadius * cosA);
        vertices.push_back(cy + outerRadius * sinA);
    }
}

} // namespace aether