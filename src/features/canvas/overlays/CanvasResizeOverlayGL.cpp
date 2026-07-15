// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/overlays/CanvasResizeOverlayGL.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <algorithm>
#include <array>
#include <cmath>
namespace aether {

namespace {

static const char* kVertexShaderScreen = R"(
#version 450 core
layout(location = 0) in vec2 aPosPx;
uniform vec2 uViewportSize;
void main() {
    vec2 ndc = vec2(
        (aPosPx.x / uViewportSize.x) * 2.0 - 1.0,
        1.0 - (aPosPx.y / uViewportSize.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char* kFragmentColor = R"(
#version 450 core
uniform float uAlpha;
out vec4 fragColor;
void main() {
    fragColor = vec4(0.0, 0.0, 0.0, uAlpha);
}
)";

static const char* kFragmentInvert = R"(
#version 450 core
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    vec3 invRGB = 1.0 - displayRGB;
    fragColor = vec4(invRGB, uAlpha);
}
)";

} // namespace

CanvasResizeOverlayGL::CanvasResizeOverlayGL(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

CanvasResizeOverlayGL::~CanvasResizeOverlayGL()
{
    shutdown();
}

Result<void> CanvasResizeOverlayGL::initialize()
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    GLProgramBinaryCache cache(m_gl);
    auto colorProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("CanvasResizeOverlayGL.color"),
            QString::fromUtf8(kVertexShaderScreen), QString::fromUtf8(kFragmentColor));
    if (!colorProgram) {
        return { colorProgram.error().code, colorProgram.error().message };
    }

    auto invertProgram
        = cache.loadOrCreateGraphicsProgram(QStringLiteral("CanvasResizeOverlayGL.invert"),
            QString::fromUtf8(kVertexShaderScreen), QString::fromUtf8(kFragmentInvert));
    if (!invertProgram) {
        if (colorProgram.value()) {
            m_gl->glDeleteProgram(colorProgram.value());
        }
        return { invertProgram.error().code, invertProgram.error().message };
    }

    m_colorProgram = colorProgram.value();
    m_invertProgram = invertProgram.value();

    m_locColorViewport = m_gl->glGetUniformLocation(m_colorProgram, "uViewportSize");
    m_locColorAlpha = m_gl->glGetUniformLocation(m_colorProgram, "uAlpha");
    m_locInvertViewport = m_gl->glGetUniformLocation(m_invertProgram, "uViewportSize");
    m_locInvertSceneTexture = m_gl->glGetUniformLocation(m_invertProgram, "uSceneTexture");
    m_locInvertAlpha = m_gl->glGetUniformLocation(m_invertProgram, "uAlpha");

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

void CanvasResizeOverlayGL::shutdown()
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

void CanvasResizeOverlayGL::onModeEntered()
{
    startVisibilityAnimation(1.0f);
}

void CanvasResizeOverlayGL::onModeExited()
{
    startVisibilityAnimation(0.0f);
}

void CanvasResizeOverlayGL::render(const Viewport& viewport, GLuint sceneTextureId,
    int surfaceWidth, int surfaceHeight,
    const std::function<Vector2(Vector2)>* documentWorldToScreen)
{
    if (!m_initialized || surfaceWidth <= 0 || surfaceHeight <= 0) {
        return;
    }

    updateVisibilityAnimation(150.0f);
    const float anim = std::clamp(animationProgress(), 0.0f, 1.0f);
    if (!isVisible() && anim <= 0.0001f) {
        return;
    }

    const auto worldToScreen = [&](float x, float y) {
        const Vector2 w { x, y };
        const Vector2 s = documentWorldToScreen && *documentWorldToScreen
            ? (*documentWorldToScreen)(w)
            : viewport.worldToScreen(w);
        return QPointF(static_cast<qreal>(s.x), static_cast<qreal>(s.y));
    };

    QPointF tl = worldToScreen(static_cast<float>(m_selectionWorldRect.left()),
        static_cast<float>(m_selectionWorldRect.top()));
    QPointF br = worldToScreen(static_cast<float>(m_selectionWorldRect.right()),
        static_cast<float>(m_selectionWorldRect.bottom()));
    QRectF screenRect(tl, br);
    screenRect = screenRect.normalized();

    const float panelW = static_cast<float>(surfaceWidth);
    const float panelH = static_cast<float>(surfaceHeight);
    const float x = static_cast<float>(screenRect.left());
    const float y = static_cast<float>(screenRect.top());
    const float w = static_cast<float>(screenRect.width());
    const float h = static_cast<float>(screenRect.height());

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    // Dim everything outside selection.
    const float dimAlpha = 0.55f * anim;
    drawRectScreen(
        0.0f, 0.0f, std::max(0.0f, x), panelH, dimAlpha, surfaceWidth, surfaceHeight); // left
    drawRectScreen(x + w, 0.0f, std::max(0.0f, panelW - (x + w)), panelH, dimAlpha, surfaceWidth,
        surfaceHeight); // right
    drawRectScreen(x, 0.0f, std::max(0.0f, w), std::max(0.0f, y), dimAlpha, surfaceWidth,
        surfaceHeight); // top
    drawRectScreen(x, y + h, std::max(0.0f, w), std::max(0.0f, panelH - (y + h)), dimAlpha,
        surfaceWidth, surfaceHeight); // bottom

    // Additional subtle dim inside selection.
    const float innerDimAlpha = 0.18f * anim;
    drawRectScreen(
        x, y, std::max(0.0f, w), std::max(0.0f, h), innerDimAlpha, surfaceWidth, surfaceHeight);

    // Border (transform-like corner + edge feel, with inversion).
    if (sceneTextureId != 0 && w > 1.0f && h > 1.0f) {
        const auto& m = elementMetrics();
        m_gl->glUseProgram(m_invertProgram);
        m_gl->glUniform2f(m_locInvertViewport, panelW, panelH);
        m_gl->glUniform1f(m_locInvertAlpha, (m_selecting ? 0.88f : 0.98f) * anim);
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
        m_gl->glUniform1i(m_locInvertSceneTexture, 0);

        // Real bounds: thin outline inset inward to avoid edge half-pixel conflicts.
        const float ix = x + m.realBoundsInsetPx;
        const float iy = y + m.realBoundsInsetPx;
        const float iw = std::max(0.0f, w - m.realBoundsInsetPx * 2.0f);
        const float ih = std::max(0.0f, h - m.realBoundsInsetPx * 2.0f);
        if (iw > 0.0f && ih > 0.0f) {
            drawLineScreen(
                ix, iy, ix + iw, iy, m.realBoundsStrokePx, 1.0f, surfaceWidth, surfaceHeight);
            drawLineScreen(ix + iw, iy, ix + iw, iy + ih, m.realBoundsStrokePx, 1.0f, surfaceWidth,
                surfaceHeight);
            drawLineScreen(ix + iw, iy + ih, ix, iy + ih, m.realBoundsStrokePx, 1.0f, surfaceWidth,
                surfaceHeight);
            drawLineScreen(
                ix, iy + ih, ix, iy, m.realBoundsStrokePx, 1.0f, surfaceWidth, surfaceHeight);
        }

        // Interactive frame: expanded by padding from real bounds.
        const float hx = x - m.framePaddingPx;
        const float hy = y - m.framePaddingPx;
        const float hw = w + m.framePaddingPx * 2.0f;
        const float hh = h + m.framePaddingPx * 2.0f;

        const float cornerLen = std::min(m.cornerLengthPx, std::min(hw, hh) * 0.45f) * anim;
        const float edgeGap = m.edgeGapPx;

        // Corners
        drawLineScreen(
            hx, hy, hx + cornerLen, hy, m.handleStrokePx, 1.0f, surfaceWidth, surfaceHeight);
        drawLineScreen(
            hx, hy, hx, hy + cornerLen, m.handleStrokePx, 1.0f, surfaceWidth, surfaceHeight);

        drawLineScreen(hx + hw, hy, hx + hw - cornerLen, hy, m.handleStrokePx, 1.0f, surfaceWidth,
            surfaceHeight);
        drawLineScreen(hx + hw, hy, hx + hw, hy + cornerLen, m.handleStrokePx, 1.0f, surfaceWidth,
            surfaceHeight);

        drawLineScreen(hx, hy + hh, hx + cornerLen, hy + hh, m.handleStrokePx, 1.0f, surfaceWidth,
            surfaceHeight);
        drawLineScreen(hx, hy + hh, hx, hy + hh - cornerLen, m.handleStrokePx, 1.0f, surfaceWidth,
            surfaceHeight);

        drawLineScreen(hx + hw, hy + hh, hx + hw - cornerLen, hy + hh, m.handleStrokePx, 1.0f,
            surfaceWidth, surfaceHeight);
        drawLineScreen(hx + hw, hy + hh, hx + hw, hy + hh - cornerLen, m.handleStrokePx, 1.0f,
            surfaceWidth, surfaceHeight);

        // Middle edge segments
        const float availHorizontal = std::max(0.0f, hw - 2.0f * (cornerLen + edgeGap));
        const float availVertical = std::max(0.0f, hh - 2.0f * (cornerLen + edgeGap));
        const float sideH = std::min(m.sideLengthPx * anim, availHorizontal);
        const float sideV = std::min(m.sideLengthPx * anim, availVertical);
        const float cx = hx + hw * 0.5f;
        const float cy = hy + hh * 0.5f;

        if (sideH > 0.0f) {
            drawLineScreen(cx - sideH * 0.5f, hy, cx + sideH * 0.5f, hy, m.handleStrokePx, 1.0f,
                surfaceWidth, surfaceHeight);
            drawLineScreen(cx - sideH * 0.5f, hy + hh, cx + sideH * 0.5f, hy + hh, m.handleStrokePx,
                1.0f, surfaceWidth, surfaceHeight);
        }
        if (sideV > 0.0f) {
            drawLineScreen(hx, cy - sideV * 0.5f, hx, cy + sideV * 0.5f, m.handleStrokePx, 1.0f,
                surfaceWidth, surfaceHeight);
            drawLineScreen(hx + hw, cy - sideV * 0.5f, hx + hw, cy + sideV * 0.5f, m.handleStrokePx,
                1.0f, surfaceWidth, surfaceHeight);
        }
    }

    m_gl->glDisable(GL_BLEND);
}

void CanvasResizeOverlayGL::drawRectScreen(
    float x, float y, float w, float h, float alpha, int surfaceW, int surfaceH)
{
    if (w <= 0.0f || h <= 0.0f || alpha <= 0.0f) {
        return;
    }

    const float verts[] = { x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h };

    m_gl->glUseProgram(m_colorProgram);
    m_gl->glUniform2f(
        m_locColorViewport, static_cast<float>(surfaceW), static_cast<float>(surfaceH));
    m_gl->glUniform1f(m_locColorAlpha, alpha);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

void CanvasResizeOverlayGL::drawLineScreen(float x1, float y1, float x2, float y2,
    float thicknessPx, float alpha, int surfaceW, int surfaceH)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f) {
        return;
    }
    const float nx = -dy / len * (thicknessPx * 0.5f);
    const float ny = dx / len * (thicknessPx * 0.5f);

    const float verts[] = { x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 - nx, y2 - ny, x1 + nx, y1 + ny,
        x2 - nx, y2 - ny, x2 + nx, y2 + ny };

    m_gl->glUniform2f(
        m_locInvertViewport, static_cast<float>(surfaceW), static_cast<float>(surfaceH));
    m_gl->glUniform1f(m_locInvertAlpha, alpha);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

} // namespace aether
