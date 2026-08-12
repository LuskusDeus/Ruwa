// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   C U R S O R   I C O N   R E N D E R E R
// ==========================================================================

#include "features/canvas/overlays/GLCursorIconRenderer.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <QImage>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace aether {

// ==========================================================================
//   I N L I N E   S H A D E R S
// ==========================================================================

static const char* kVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

// The icon artwork is a solid black glyph: only its alpha carries the shape, and
// the shape is filled with the inverse of whatever the scene shows underneath.
static const char* kFragmentShader = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform sampler2D uMaskTexture;
uniform vec2 uViewportSize;
uniform float uAlpha;
uniform vec2 uMaskEdge;
in vec2 vUV;
out vec4 fragColor;
void main() {
    float mask = texture(uMaskTexture, vUV).r;
    // The glyph is downscaled a long way, which leaves a soft ramp on every edge.
    // Tightening it around the 0.5 contour restores a crisp silhouette; how tight
    // is the caller's call, since a small glyph has little coverage ramp to spare.
    // An empty or inverted window means "use the coverage as rasterized".
    if (uMaskEdge.y > uMaskEdge.x) {
        mask = smoothstep(uMaskEdge.x, uMaskEdge.y, mask);
    }
    if (mask <= 0.002) {
        discard;
    }
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    fragColor = vec4(1.0 - displayRGB, uAlpha * mask);
}
)";

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLCursorIconRenderer::GLCursorIconRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLCursorIconRenderer::~GLCursorIconRenderer()
{
    shutdown();
}

Result<void> GLCursorIconRenderer::initialize()
{
    if (m_program) {
        return Result<void>::ok();
    }

    GLProgramBinaryCache cache(m_gl);
    auto program = cache.loadOrCreateGraphicsProgram(QStringLiteral("GLCursorIconRenderer.iconMask"),
        QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShader));
    if (!program) {
        return { program.error().code, program.error().message };
    }

    m_program = program.value();
    m_locMVP = m_gl->glGetUniformLocation(m_program, "uMVP");
    m_locSceneTexture = m_gl->glGetUniformLocation(m_program, "uSceneTexture");
    m_locMaskTexture = m_gl->glGetUniformLocation(m_program, "uMaskTexture");
    m_locViewportSize = m_gl->glGetUniformLocation(m_program, "uViewportSize");
    m_locAlpha = m_gl->glGetUniformLocation(m_program, "uAlpha");
    m_locMaskEdge = m_gl->glGetUniformLocation(m_program, "uMaskEdge");

    // Icon quad: position + UV, rewritten every frame as the cursor moves.
    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glBindVertexArray(0);

    return Result<void>::ok();
}

void GLCursorIconRenderer::shutdown()
{
    if (!m_gl) {
        return;
    }

    for (const GLuint texture : m_maskCache) {
        if (texture) {
            m_gl->glDeleteTextures(1, &texture);
        }
    }
    m_maskCache.clear();

    if (m_vbo) {
        m_gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_program) {
        m_gl->glDeleteProgram(m_program);
        m_program = 0;
    }

    m_locMVP = -1;
    m_locSceneTexture = -1;
    m_locMaskTexture = -1;
    m_locViewportSize = -1;
    m_locAlpha = -1;
    m_locMaskEdge = -1;
}

// ==========================================================================
//   M A S K   L O A D I N G
// ==========================================================================

GLuint GLCursorIconRenderer::maskTexture(const QString& resourcePath, int sizePx)
{
    const QString key = QStringLiteral("%1@%2").arg(resourcePath).arg(sizePx);
    const auto cached = m_maskCache.constFind(key);
    if (cached != m_maskCache.constEnd()) {
        return cached.value();
    }

    // The source art is 240x240; scaling on the CPU once beats sampling a large
    // texture down to a handful of pixels every frame.
    QImage image(resourcePath);
    if (!image.isNull()) {
        image = image.scaled(sizePx, sizePx, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_Alpha8);
    }
    if (image.isNull()) {
        m_maskCache.insert(key, 0);
        return 0;
    }

    // Format_Alpha8 rows are padded to 4 bytes; copy them tightly for the upload.
    std::vector<unsigned char> pixels(static_cast<std::size_t>(sizePx) * sizePx);
    for (int y = 0; y < sizePx; ++y) {
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * sizePx, image.constScanLine(y),
            static_cast<std::size_t>(sizePx));
    }

    GLuint texture = 0;
    m_gl->glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    m_gl->glTextureStorage2D(texture, 1, GL_R8, sizePx, sizePx);
    // The rows are packed tightly; without this GL reads each one padded to 4
    // bytes and every scanline of an odd-width icon slides sideways.
    GLint previousAlignment = 4;
    m_gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_gl->glTextureSubImage2D(
        texture, 0, 0, 0, sizePx, sizePx, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    m_gl->glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_maskCache.insert(key, texture);
    return texture;
}

// ==========================================================================
//   D R A W
// ==========================================================================

void GLCursorIconRenderer::draw(const QString& resourcePath, float sizePx, float left, float top,
    const std::array<float, 16>& mvp, float viewportW, float viewportH, float alpha,
    float edgeLow, float edgeHigh)
{
    if (!m_program) {
        return;
    }

    const int size = static_cast<int>(sizePx);
    const GLuint mask = maskTexture(resourcePath, size);
    if (!mask) {
        return;
    }

    // Snap to whole pixels: the mask is already rasterized at its final size, so
    // a fractional quad would resample it a second time and smear the edges.
    const float x0 = std::round(left);
    const float y0 = std::round(top);
    const float x1 = x0 + sizePx;
    const float y1 = y0 + sizePx;

    const float quad[16]
        = { x0, y0, 0.0f, 0.0f, x1, y0, 1.0f, 0.0f, x0, y1, 0.0f, 1.0f, x1, y1, 1.0f, 1.0f };

    m_gl->glBindTextureUnit(1, mask);
    m_gl->glUseProgram(m_program);
    m_gl->glUniformMatrix4fv(m_locMVP, 1, GL_FALSE, mvp.data());
    m_gl->glUniform1i(m_locSceneTexture, 0);
    m_gl->glUniform1i(m_locMaskTexture, 1);
    m_gl->glUniform2f(m_locViewportSize, viewportW, viewportH);
    m_gl->glUniform1f(m_locAlpha, alpha);
    m_gl->glUniform2f(m_locMaskEdge, edgeLow, edgeHigh);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
    m_gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_gl->glBindVertexArray(0);
}

void GLCursorIconRenderer::drawAtHotspot(const QString& resourcePath, float sizePx, float hotspotU,
    float hotspotV, float cursorX, float cursorY, const std::array<float, 16>& mvp,
    float viewportW, float viewportH, float alpha, float edgeLow, float edgeHigh)
{
    draw(resourcePath, sizePx, cursorX - hotspotU * sizePx, cursorY - hotspotV * sizePx, mvp,
        viewportW, viewportH, alpha, edgeLow, edgeHigh);
}

} // namespace aether
