// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T R A N S F O R M   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/TransformOverlay.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <cmath>
#include <vector>
#include <algorithm>
#include <set>
#include <QColor>
#include <QImage>
#include <QPixmap>

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

static const char* kFragmentShader = R"(
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

static const char* kTexVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char* kTexFragmentShader = R"(
#version 450 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTex, vUV);
    fragColor = vec4(uColor.rgb * t.rgb, uColor.a * t.a);
}
)";

static const char* kTexFragmentShaderInvert = R"(
#version 450 core
in vec2 vUV;
uniform sampler2D uIcon;
uniform sampler2D uSceneTexture;
uniform vec2 uViewportSize;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    float iconA = texture(uIcon, vUV).a * uColor.a;
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec4 under = texture(uSceneTexture, uv);
    vec3 displayRGB = (under.a > 0.001) ? (under.rgb / under.a) : vec3(0.0);
    vec3 invRGB = 1.0 - displayRGB;
    fragColor = vec4(invRGB, iconA);
}
)";

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

TransformOverlay::TransformOverlay(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

TransformOverlay::~TransformOverlay()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> TransformOverlay::initialize()
{
    if (m_initialized)
        return Result<void>::ok();

    GLProgramBinaryCache cache(m_gl);
    const auto loadGraphicsProgram = [&cache](const QString& key, const char* vertexSource,
                                         const char* fragmentSource) -> Result<GLuint> {
        return cache.loadOrCreateGraphicsProgram(
            key, QString::fromUtf8(vertexSource), QString::fromUtf8(fragmentSource));
    };

    auto shaderProgram = loadGraphicsProgram(
        QStringLiteral("TransformOverlay.base"), kVertexShader, kFragmentShader);
    if (!shaderProgram) {
        return { shaderProgram.error().code, shaderProgram.error().message };
    }

    m_shaderProgram = shaderProgram.value();
    m_locMVP = m_gl->glGetUniformLocation(m_shaderProgram, "uMVP");
    m_locColor = m_gl->glGetUniformLocation(m_shaderProgram, "uColor");

    auto invertProgram = loadGraphicsProgram(
        QStringLiteral("TransformOverlay.invert"), kVertexShader, kFragmentShaderInvert);
    if (!invertProgram) {
        if (m_shaderProgram) {
            m_gl->glDeleteProgram(m_shaderProgram);
            m_shaderProgram = 0;
        }
        return { invertProgram.error().code, invertProgram.error().message };
    }

    m_invertProgram = invertProgram.value();
    m_locInvertMVP = m_gl->glGetUniformLocation(m_invertProgram, "uMVP");
    m_locInvertColor = m_gl->glGetUniformLocation(m_invertProgram, "uColor");
    m_locInvertSceneTexture = m_gl->glGetUniformLocation(m_invertProgram, "uSceneTexture");
    m_locInvertViewportSize = m_gl->glGetUniformLocation(m_invertProgram, "uViewportSize");

    // Optional: textured rotation-corner icons
    auto texProgram = loadGraphicsProgram(
        QStringLiteral("TransformOverlay.texture"), kTexVertexShader, kTexFragmentShader);
    if (!texProgram) {
    } else {
        m_texProgram = texProgram.value();
    }

    if (m_texProgram) {
        auto texInvertProgram
            = loadGraphicsProgram(QStringLiteral("TransformOverlay.textureInvert"),
                kTexVertexShader, kTexFragmentShaderInvert);
        if (!texInvertProgram) {
        } else {
            m_texInvertProgram = texInvertProgram.value();
        }
    }

    if (m_texProgram) {
        m_locTexMVP = m_gl->glGetUniformLocation(m_texProgram, "uMVP");
        m_locTexSampler = m_gl->glGetUniformLocation(m_texProgram, "uTex");
        m_locTexColor = m_gl->glGetUniformLocation(m_texProgram, "uColor");
    }
    if (m_texInvertProgram) {
        m_locTexInvMVP = m_gl->glGetUniformLocation(m_texInvertProgram, "uMVP");
        m_locTexInvIcon = m_gl->glGetUniformLocation(m_texInvertProgram, "uIcon");
        m_locTexInvScene = m_gl->glGetUniformLocation(m_texInvertProgram, "uSceneTexture");
        m_locTexInvViewport = m_gl->glGetUniformLocation(m_texInvertProgram, "uViewportSize");
        m_locTexInvColor = m_gl->glGetUniformLocation(m_texInvertProgram, "uColor");
    }

    {
        using ruwa::ui::core::IconProvider;
        QPixmap pm = IconProvider::instance().getPixmap(
            IconProvider::StandardIcon::RotationCorner, QSize(64, 64));
        if (!pm.isNull()) {
            QImage img
                = pm.toImage().convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
            if (!img.isNull()) {
                m_gl->glGenTextures(1, &m_rotationIconTexture);
                m_gl->glBindTexture(GL_TEXTURE_2D, m_rotationIconTexture);
                m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, img.constBits());
                m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                m_gl->glBindTexture(GL_TEXTURE_2D, 0);
                m_rotationIconReady = (m_rotationIconTexture != 0);
            }
        }
    }

    // Create VAO and VBO
    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Pre-allocate enough for dynamic geometry (lines + quads)
    m_gl->glBufferData(GL_ARRAY_BUFFER, 256 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);

    m_initialized = true;
    return Result<void>::ok();
}

void TransformOverlay::shutdown()
{
    if (!m_initialized)
        return;

    if (m_vbo) {
        m_gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_invertProgram) {
        m_gl->glDeleteProgram(m_invertProgram);
        m_invertProgram = 0;
    }
    if (m_shaderProgram) {
        m_gl->glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    if (m_texInvertProgram) {
        m_gl->glDeleteProgram(m_texInvertProgram);
        m_texInvertProgram = 0;
    }
    if (m_texProgram) {
        m_gl->glDeleteProgram(m_texProgram);
        m_texProgram = 0;
    }
    if (m_rotationIconTexture) {
        m_gl->glDeleteTextures(1, &m_rotationIconTexture);
        m_rotationIconTexture = 0;
    }
    m_rotationIconReady = false;

    m_initialized = false;
}

// ==========================================================================
//   R E N D E R I N G
// ==========================================================================

void TransformOverlay::render(const TransformState& state, const Viewport& viewport,
    GLuint sceneTextureId, const std::array<float, 16>* viewProjectionContent,
    const TransformMoveAxisGuide* moveAxisGuide, bool drawTransformChrome,
    const std::function<Vector2(const Vector2&)>* documentWorldFromScreen,
    bool drawCornerRotationIcons, const TransformAutoSnapGuides* autoSnapGuides)
{
    if (!m_initialized)
        return;

    m_lastState = state;
    m_hasLastState = true;
    m_lastDrawCornerRotationIcons = drawCornerRotationIcons;
    renderInternal(state, viewport, true, sceneTextureId, viewProjectionContent, moveAxisGuide,
        drawTransformChrome, documentWorldFromScreen, drawCornerRotationIcons, autoSnapGuides);
}

void TransformOverlay::render(const Viewport& viewport, GLuint sceneTextureId,
    const std::array<float, 16>* viewProjectionContent)
{
    if (!m_initialized || !m_hasLastState)
        return;
    renderInternal(m_lastState, viewport, false, sceneTextureId, viewProjectionContent, nullptr,
        true, nullptr, m_lastDrawCornerRotationIcons, nullptr);
}

void TransformOverlay::onTransformModeEntered()
{
    startVisibilityAnimation(1.0f);
}

void TransformOverlay::onTransformModeExited(bool animated)
{
    if (animated) {
        startVisibilityAnimation(0.0f);
    } else {
        setVisibleImmediate(false, 0.0f);
    }
}

namespace {
bool isFinite(float v)
{
    return std::isfinite(v);
}
bool isFinite(const aether::Vector2& p)
{
    return isFinite(p.x) && isFinite(p.y);
}

TransformHandle visualCornerHandle(const TransformState& state, TransformHandle fallback)
{
    Vector2 p = state.handlePosition(fallback);
    const auto corners = state.transformedCorners();
    Vector2 center { 0.0f, 0.0f };
    for (int i = 0; i < 4; ++i) {
        center.x += corners[i].x;
        center.y += corners[i].y;
    }
    center.x *= 0.25f;
    center.y *= 0.25f;

    const float frameRotation = state.rotation;
    const Vector2 refX { std::cos(frameRotation), std::sin(frameRotation) };
    const Vector2 refY { -std::sin(frameRotation), std::cos(frameRotation) };
    const Vector2 delta { p.x - center.x, p.y - center.y };
    const bool right = (delta.x * refX.x + delta.y * refX.y) >= 0.0f;
    const bool bottom = (delta.x * refY.x + delta.y * refY.y) >= 0.0f;

    if (!right && !bottom)
        return TransformHandle::TopLeft;
    if (right && !bottom)
        return TransformHandle::TopRight;
    if (right && bottom)
        return TransformHandle::BottomRight;
    return TransformHandle::BottomLeft;
}

void spanAxisThroughViewport(const Viewport& viewport,
    const std::function<Vector2(const Vector2&)>* documentWorldFromScreen, const Vector2& origin,
    const Vector2& dir, Vector2* outA, Vector2* outB)
{
    const Vector2 sz = viewport.size();
    const std::array<Vector2, 4> sc = { {
        { 0.f, 0.f },
        { sz.x, 0.f },
        { sz.x, sz.y },
        { 0.f, sz.y },
    } };
    float minT = 1.0e30f;
    float maxT = -1.0e30f;
    constexpr float pad = 8192.f;
    auto toWorld = [&](const Vector2& s) -> Vector2 {
        return documentWorldFromScreen ? (*documentWorldFromScreen)(s) : viewport.screenToWorld(s);
    };
    for (const Vector2& s : sc) {
        const Vector2 w = toWorld(s);
        const float t = (w.x - origin.x) * dir.x + (w.y - origin.y) * dir.y;
        minT = std::min(minT, t);
        maxT = std::max(maxT, t);
    }
    minT -= pad;
    maxT += pad;
    *outA = { origin.x + dir.x * minT, origin.y + dir.y * minT };
    *outB = { origin.x + dir.x * maxT, origin.y + dir.y * maxT };
}

} // namespace

void TransformOverlay::renderInternal(const TransformState& state, const Viewport& viewport,
    bool isActive, GLuint sceneTextureId, const std::array<float, 16>* viewProjectionContent,
    const TransformMoveAxisGuide* moveAxisGuide, bool drawTransformChrome,
    const std::function<Vector2(const Vector2&)>* documentWorldFromScreen,
    bool drawCornerRotationIcons, const TransformAutoSnapGuides* autoSnapGuides)
{
    const auto vpMatrix
        = viewProjectionContent ? *viewProjectionContent : viewport.viewProjectionMatrix();
    float zoom = viewport.camera().zoom();

    if (isActive && !isVisible()) {
        setVisibleImmediate(true, 1.0f);
    }

    updateVisibilityAnimation();
    if (!isVisible() && !isActive)
        return;

    const float anim = std::clamp(animationProgress(), 0.0f, 1.0f);
    if (anim <= 0.0001f && !isActive)
        return;

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QColor primary = colors.primary;
    const float a = 0.95f * primary.alphaF();

    if (sceneTextureId && m_invertProgram) {
        m_curProgram = m_invertProgram;
        m_curLocMVP = m_locInvertMVP;
        m_curLocColor = m_locInvertColor;
        m_gl->glUseProgram(m_invertProgram);
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
        m_gl->glUniform1i(m_locInvertSceneTexture, 0);
        aether::Vector2 vpSize = viewport.size();
        m_gl->glUniform2f(m_locInvertViewportSize, vpSize.x, vpSize.y);
        m_gl->glUniform4f(m_locInvertColor, 0.0f, 0.0f, 0.0f, a);
    } else {
        m_curProgram = m_shaderProgram;
        m_curLocMVP = m_locMVP;
        m_curLocColor = m_locColor;
    }

    float r = primary.redF();
    float g = primary.greenF();
    float b = primary.blueF();
    if (sceneTextureId && m_invertProgram) {
        r = 0.0f;
        g = 0.0f;
        b = 0.0f;
    }

    if (moveAxisGuide && moveAxisGuide->opacity > 0.002f && isFinite(moveAxisGuide->originWorld)
        && isFinite(moveAxisGuide->axisDirWorld)) {
        drawMoveAxisGuides(
            *moveAxisGuide, viewport, zoom, r, g, b, a, vpMatrix, documentWorldFromScreen);
    }
    if (autoSnapGuides && autoSnapGuides->opacity > 0.002f) {
        drawAutoSnapGuides(
            *autoSnapGuides, viewport, zoom, r, g, b, a, vpMatrix, documentWorldFromScreen);
    }

    if (!drawTransformChrome) {
        m_gl->glDisable(GL_BLEND);
        return;
    }

    if (state.hasDeformMesh()) {
        drawDeformMesh(state, zoom, r, g, b, a, vpMatrix);
        m_gl->glDisable(GL_BLEND);
        return;
    }

    // Get transformed corners
    auto corners = state.transformedCorners();

    bool cornersValid = true;
    for (int i = 0; i < 4; ++i) {
        if (!isFinite(corners[i])) {
            cornersValid = false;
            break;
        }
    }
    if (!cornersValid)
        return;

    const float lineThicknessWorld = 3.0f / zoom;
    const float cornerLengthWorld = 24.0f / zoom;
    const float edgeGapWorld = 8.0f / zoom;
    const float edgeOffset = cornerLengthWorld + edgeGapWorld;
    const float minLen = 0.0001f;

    // Draw corner "L" lines (grow from the corner point)
    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) % 4;
        int prev = (i + 3) % 4;

        Vector2 c = corners[i];
        Vector2 dir1 = { corners[next].x - c.x, corners[next].y - c.y };
        Vector2 dir2 = { corners[prev].x - c.x, corners[prev].y - c.y };

        float len1 = std::sqrt(dir1.x * dir1.x + dir1.y * dir1.y);
        float len2 = std::sqrt(dir2.x * dir2.x + dir2.y * dir2.y);

        if (len1 > minLen) {
            dir1.x /= len1;
            dir1.y /= len1;
            float cornerLen1 = std::min(cornerLengthWorld * anim, len1 * 0.5f);
            Vector2 p1 = { c.x + dir1.x * cornerLen1, c.y + dir1.y * cornerLen1 };
            if (isFinite(p1))
                drawCapsule(c, p1, lineThicknessWorld, r, g, b, a, vpMatrix);
        }
        if (len2 > minLen) {
            dir2.x /= len2;
            dir2.y /= len2;
            float cornerLen2 = std::min(cornerLengthWorld * anim, len2 * 0.5f);
            Vector2 p2 = { c.x + dir2.x * cornerLen2, c.y + dir2.y * cornerLen2 };
            if (isFinite(p2))
                drawCapsule(c, p2, lineThicknessWorld, r, g, b, a, vpMatrix);
        }
    }

    // Draw edge lines (expand from center, leave gaps near corners)
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        Vector2 p0 = corners[i];
        Vector2 p1 = corners[j];
        Vector2 dir = { p1.x - p0.x, p1.y - p0.y };
        float edgeLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (edgeLen <= (edgeOffset * 2.0f) || edgeLen < minLen)
            continue;
        dir.x /= edgeLen;
        dir.y /= edgeLen;

        Vector2 start = { p0.x + dir.x * edgeOffset, p0.y + dir.y * edgeOffset };
        Vector2 end = { p1.x - dir.x * edgeOffset, p1.y - dir.y * edgeOffset };
        float usableLen = edgeLen - edgeOffset * 2.0f;
        float halfLen = (usableLen * 0.5f) * anim;

        Vector2 center = { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f };
        Vector2 a0 = { center.x - dir.x * halfLen, center.y - dir.y * halfLen };
        Vector2 a1 = { center.x + dir.x * halfLen, center.y + dir.y * halfLen };

        if (isFinite(a0) && isFinite(a1))
            drawCapsule(a0, a1, lineThicknessWorld, r, g, b, a, vpMatrix);
    }

    // Rotation: four corner icons in classic transform, including free quad.
    if (drawCornerRotationIcons) {
        aether::Vector2 vpSz = viewport.size();
        drawRotationCornerHandles(
            state, zoom, anim, r, g, b, a, vpMatrix, sceneTextureId, vpSz.x, vpSz.y);
    } else {
        Vector2 rotHandle = state.rotationHandlePosition(zoom);
        if (isFinite(rotHandle)) {
            float halfSize = (TransformState::HANDLE_SIZE * 0.55f) / zoom * anim;
            drawFilledRect(rotHandle.x, rotHandle.y, halfSize, r, g, b, a, vpMatrix);
        }
    }

    m_gl->glDisable(GL_BLEND);
}

void TransformOverlay::drawMoveAxisGuides(const TransformMoveAxisGuide& guide,
    const Viewport& viewport, float zoom, float r, float g, float b, float a,
    const std::array<float, 16>& vpMatrix,
    const std::function<Vector2(const Vector2&)>* documentWorldFromScreen)
{
    Vector2 dir = guide.axisDirWorld;
    const float dLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (dLen < 1.0e-5f || !isFinite(dir))
        return;
    dir.x /= dLen;
    dir.y /= dLen;

    Vector2 lineA, lineB;
    spanAxisThroughViewport(
        viewport, documentWorldFromScreen, guide.originWorld, dir, &lineA, &lineB);
    if (!isFinite(lineA) || !isFinite(lineB))
        return;

    const float lineThicknessWorld = 2.5f / zoom;
    const float tickHalfWorld = 10.0f / zoom;
    Vector2 perp { -dir.y, dir.x };
    Vector2 tick0 { guide.originWorld.x - perp.x * tickHalfWorld,
        guide.originWorld.y - perp.y * tickHalfWorld };
    Vector2 tick1 { guide.originWorld.x + perp.x * tickHalfWorld,
        guide.originWorld.y + perp.y * tickHalfWorld };

    const float ao = std::clamp(guide.opacity, 0.f, 1.f) * a;
    drawCapsule(lineA, lineB, lineThicknessWorld, r, g, b, ao, vpMatrix);
    if (isFinite(tick0) && isFinite(tick1))
        drawCapsule(tick0, tick1, lineThicknessWorld, r, g, b, ao, vpMatrix);
}

void TransformOverlay::drawAutoSnapGuides(const TransformAutoSnapGuides& guides,
    const Viewport& viewport, float zoom, float r, float g, float b, float a,
    const std::array<float, 16>& vpMatrix,
    const std::function<Vector2(const Vector2&)>* documentWorldFromScreen)
{
    if (!guides.hasVertical && !guides.hasSecondVertical && !guides.hasHorizontal
        && !guides.hasSecondHorizontal)
        return;

    const float lineThicknessWorld = 2.5f / zoom;
    const float ao = std::clamp(guides.opacity, 0.f, 1.f) * a;

    auto drawGuide = [&](const Vector2& origin, const Vector2& dir) {
        if (!isFinite(origin) || !isFinite(dir))
            return;
        Vector2 lineA, lineB;
        spanAxisThroughViewport(viewport, documentWorldFromScreen, origin, dir, &lineA, &lineB);
        if (isFinite(lineA) && isFinite(lineB))
            drawCapsule(lineA, lineB, lineThicknessWorld, r, g, b, ao, vpMatrix);
    };

    if (guides.hasVertical)
        drawGuide({ guides.verticalX, 0.0f }, { 0.0f, 1.0f });
    if (guides.hasSecondVertical)
        drawGuide({ guides.secondVerticalX, 0.0f }, { 0.0f, 1.0f });
    if (guides.hasHorizontal)
        drawGuide({ 0.0f, guides.horizontalY }, { 1.0f, 0.0f });
    if (guides.hasSecondHorizontal)
        drawGuide({ 0.0f, guides.secondHorizontalY }, { 1.0f, 0.0f });
}

void TransformOverlay::drawDeformMesh(const TransformState& state, float zoom, float r, float g,
    float b, float a, const std::array<float, 16>& vpMatrix)
{
    if (!state.deformMesh.has_value()) {
        return;
    }
    const auto& mesh = *state.deformMesh;
    const float lineThicknessWorld = 2.5f / zoom;
    const float thinLine = 1.5f / zoom;
    const float pointRadiusWorld = (TransformState::HANDLE_SIZE * 0.42f) / zoom;
    const float curveAlpha = a * 0.92f;
    const float latticeAlpha = a * 0.35f;

    // 1. Draw smooth B-spline iso-parameter curves
    constexpr int kCurveSegments = 48;
    // Horizontal iso-v curves (one per control row fraction)
    for (int row = 0; row < mesh.rows; ++row) {
        float v = (mesh.rows > 1) ? static_cast<float>(row) / (mesh.rows - 1) : 0.5f;
        std::vector<Vector2> curve;
        curve.reserve(kCurveSegments + 1);
        for (int i = 0; i <= kCurveSegments; ++i) {
            float u = static_cast<float>(i) / kCurveSegments;
            curve.push_back(state.evaluateBSplineSurface(u, v));
        }
        drawPolyline(curve, lineThicknessWorld, r, g, b, curveAlpha, vpMatrix);
    }
    // Vertical iso-u curves (one per control col fraction)
    for (int col = 0; col < mesh.cols; ++col) {
        float u = (mesh.cols > 1) ? static_cast<float>(col) / (mesh.cols - 1) : 0.5f;
        std::vector<Vector2> curve;
        curve.reserve(kCurveSegments + 1);
        for (int i = 0; i <= kCurveSegments; ++i) {
            float v = static_cast<float>(i) / kCurveSegments;
            curve.push_back(state.evaluateBSplineSurface(u, v));
        }
        drawPolyline(curve, lineThicknessWorld, r, g, b, curveAlpha, vpMatrix);
    }

    // 2. Draw only the corner->handle "whiskers" (Photoshop-style tangent lines).
    //    Each edge connects its corner to the adjacent handle, but the two
    //    handles on the same edge are NOT joined to each other. Interior
    //    lattice segments are omitted; interior points are derived, not shown.
    for (int col = 0; col < mesh.cols - 1; ++col) {
        if (col != 0 && col != mesh.cols - 2) {
            continue; // skip the middle handle->handle segment
        }
        drawCapsule(mesh.targetAt(col, 0), mesh.targetAt(col + 1, 0), thinLine, r, g, b,
            latticeAlpha, vpMatrix);
        drawCapsule(mesh.targetAt(col, mesh.rows - 1), mesh.targetAt(col + 1, mesh.rows - 1),
            thinLine, r, g, b, latticeAlpha, vpMatrix);
    }
    for (int row = 0; row < mesh.rows - 1; ++row) {
        if (row != 0 && row != mesh.rows - 2) {
            continue; // skip the middle handle->handle segment
        }
        drawCapsule(mesh.targetAt(0, row), mesh.targetAt(0, row + 1), thinLine, r, g, b,
            latticeAlpha, vpMatrix);
        drawCapsule(mesh.targetAt(mesh.cols - 1, row), mesh.targetAt(mesh.cols - 1, row + 1),
            thinLine, r, g, b, latticeAlpha, vpMatrix);
    }

    // 3. Draw boundary control points only (corners + edge handles).
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (mesh.isInteriorIndex(static_cast<int>(i))) {
            continue;
        }
        const Vector2& t = mesh.vertices[i].target;
        drawCircle(t.x, t.y, pointRadiusWorld, 18, r, g, b, a, vpMatrix);
    }
}

void TransformOverlay::drawPolyline(const std::vector<Vector2>& points, float thicknessWorld,
    float r, float g, float b, float a, const std::array<float, 16>& vpMatrix)
{
    if (points.size() < 2) {
        return;
    }
    for (size_t i = 1; i < points.size(); ++i) {
        drawCapsule(points[i - 1], points[i], thicknessWorld, r, g, b, a, vpMatrix);
    }
}

void TransformOverlay::drawRotationCornerHandles(const TransformState& state, float zoom,
    float anim, float r, float g, float b, float a, const std::array<float, 16>& vpMatrix,
    GLuint sceneTextureId, float invertViewportW, float invertViewportH)
{
    static const TransformHandle kCornerOrder[4] = { TransformHandle::TopLeft,
        TransformHandle::TopRight, TransformHandle::BottomRight, TransformHandle::BottomLeft };

    const float half = (TransformState::CORNER_ROTATION_AFFORDANCE_HALF / zoom) * anim;
    const auto corners = state.transformedCorners();
    bool cornersValid = true;
    for (int i = 0; i < 4; ++i) {
        if (!isFinite(corners[i])) {
            cornersValid = false;
            break;
        }
    }
    for (int i = 0; i < 4; ++i) {
        TransformHandle h = kCornerOrder[i];
        Vector2 p = state.cornerRotationAffordanceWorld(h, zoom);
        if (!isFinite(p))
            continue;
        const TransformHandle visualHandle = cornersValid ? visualCornerHandle(state, h) : h;
        const float angle = state.cornerRotationIconAngleRadians(visualHandle);
        const bool canTex = m_rotationIconReady && m_rotationIconTexture != 0 && m_texProgram;
        if (canTex) {
            drawTexturedRotatedIcon(p.x, p.y, half, angle, r, g, b, a, vpMatrix, sceneTextureId,
                invertViewportW, invertViewportH);
        } else {
            drawFilledRotatedSquare(p.x, p.y, half, angle, r, g, b, a, vpMatrix);
        }
    }
}

void TransformOverlay::drawTexturedRotatedIcon(float cx, float cy, float halfWorld, float angleRad,
    float r, float g, float b, float a, const std::array<float, 16>& vpMatrix,
    GLuint sceneTextureId, float invertViewportW, float invertViewportH)
{
    if (!m_rotationIconReady || m_rotationIconTexture == 0 || !m_texProgram)
        return;

    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float lx[4] = { -halfWorld, halfWorld, halfWorld, -halfWorld };
    const float ly[4] = { -halfWorld, -halfWorld, halfWorld, halfWorld };
    const float uv[8] = { 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f };

    float vd[36];
    int o = 0;
    auto pushVert = [&](int i) {
        const float xr = lx[i] * c - ly[i] * s;
        const float yr = lx[i] * s + ly[i] * c;
        vd[o++] = cx + xr;
        vd[o++] = cy + yr;
        vd[o++] = uv[i * 2];
        vd[o++] = uv[i * 2 + 1];
    };
    pushVert(0);
    pushVert(1);
    pushVert(2);
    pushVert(0);
    pushVert(2);
    pushVert(3);

    const bool useInvert = (sceneTextureId != 0 && m_texInvertProgram);
    GLuint prog = useInvert ? m_texInvertProgram : m_texProgram;
    m_gl->glUseProgram(prog);
    m_gl->glUniformMatrix4fv(
        useInvert ? m_locTexInvMVP : m_locTexMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(useInvert ? m_locTexInvColor : m_locTexColor, r, g, b, a);

    if (useInvert) {
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, sceneTextureId);
        m_gl->glUniform1i(m_locTexInvScene, 0);
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_rotationIconTexture);
        m_gl->glUniform1i(m_locTexInvIcon, 1);
    } else {
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_rotationIconTexture);
        m_gl->glUniform1i(m_locTexSampler, 0);
    }

    if (useInvert) {
        m_gl->glUniform2f(m_locTexInvViewport, invertViewportW, invertViewportH);
    }

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vd), vd);
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    m_gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glDisableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glBindVertexArray(0);

    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    m_gl->glUseProgram(m_curProgram);
}

// ==========================================================================
//   D R A W I N G   P R I M I T I V E S
// ==========================================================================

void TransformOverlay::drawLine(float x1, float y1, float x2, float y2, float r, float g, float b,
    float a, const std::array<float, 16>& vpMatrix)
{
    float vertices[] = { x1, y1, x2, y2 };

    m_gl->glUseProgram(m_curProgram);
    m_gl->glUniformMatrix4fv(m_curLocMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(m_curLocColor, r, g, b, a);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    m_gl->glDrawArrays(GL_LINES, 0, 2);
    m_gl->glBindVertexArray(0);
}

void TransformOverlay::drawQuad(float x1, float y1, float x2, float y2, float x3, float y3,
    float x4, float y4, float r, float g, float b, float a, const std::array<float, 16>& vpMatrix)
{
    float vertices[] = { x1, y1, x2, y2, x3, y3, x1, y1, x3, y3, x4, y4 };

    m_gl->glUseProgram(m_curProgram);
    m_gl->glUniformMatrix4fv(m_curLocMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(m_curLocColor, r, g, b, a);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

void TransformOverlay::drawCapsule(const Vector2& p1, const Vector2& p2, float thicknessWorld,
    float r, float g, float b, float a, const std::array<float, 16>& vpMatrix)
{
    Vector2 dir = { p2.x - p1.x, p2.y - p1.y };
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.0001f) {
        drawCircle(p1.x, p1.y, thicknessWorld * 0.5f, 16, r, g, b, a, vpMatrix);
        return;
    }
    dir.x /= len;
    dir.y /= len;
    Vector2 n = { -dir.y, dir.x };
    float half = thicknessWorld * 0.5f;

    Vector2 a0 = { p1.x + n.x * half, p1.y + n.y * half };
    Vector2 a1 = { p1.x - n.x * half, p1.y - n.y * half };
    Vector2 b1 = { p2.x - n.x * half, p2.y - n.y * half };
    Vector2 b0 = { p2.x + n.x * half, p2.y + n.y * half };

    drawQuad(a0.x, a0.y, a1.x, a1.y, b1.x, b1.y, b0.x, b0.y, r, g, b, a, vpMatrix);
    drawCircle(p1.x, p1.y, half, 16, r, g, b, a, vpMatrix);
    drawCircle(p2.x, p2.y, half, 16, r, g, b, a, vpMatrix);
}

void TransformOverlay::drawFilledRect(float cx, float cy, float halfSize, float r, float g, float b,
    float a, const std::array<float, 16>& vpMatrix)
{
    float x0 = cx - halfSize, y0 = cy - halfSize;
    float x1 = cx + halfSize, y1 = cy + halfSize;

    float vertices[] = { x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1 };

    m_gl->glUseProgram(m_curProgram);
    m_gl->glUniformMatrix4fv(m_curLocMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(m_curLocColor, r, g, b, a);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

void TransformOverlay::drawFilledRotatedSquare(float cx, float cy, float halfWorld, float angleRad,
    float r, float g, float b, float a, const std::array<float, 16>& vpMatrix)
{
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float lx[4] = { -halfWorld, halfWorld, halfWorld, -halfWorld };
    const float ly[4] = { -halfWorld, -halfWorld, halfWorld, halfWorld };
    float x[4], y[4];
    for (int i = 0; i < 4; ++i) {
        x[i] = cx + lx[i] * c - ly[i] * s;
        y[i] = cy + lx[i] * s + ly[i] * c;
    }
    drawQuad(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], r, g, b, a, vpMatrix);
}

void TransformOverlay::drawRect(float cx, float cy, float halfSize, float r, float g, float b,
    float a, const std::array<float, 16>& vpMatrix)
{
    float x0 = cx - halfSize, y0 = cy - halfSize;
    float x1 = cx + halfSize, y1 = cy + halfSize;

    float vertices[] = { x0, y0, x1, y0, x1, y0, x1, y1, x1, y1, x0, y1, x0, y1, x0, y0 };

    m_gl->glUseProgram(m_curProgram);
    m_gl->glUniformMatrix4fv(m_curLocMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(m_curLocColor, r, g, b, a);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    m_gl->glDrawArrays(GL_LINES, 0, 8);
    m_gl->glBindVertexArray(0);
}

void TransformOverlay::drawCircle(float cx, float cy, float radius, int segments, float r, float g,
    float b, float a, const std::array<float, 16>& vpMatrix)
{
    // Triangle fan: center + (segments + 1) perimeter points
    const int vertCount = segments + 2;
    std::vector<float> vertices(vertCount * 2);

    // Center
    vertices[0] = cx;
    vertices[1] = cy;

    // Perimeter
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(segments);
        vertices[(i + 1) * 2 + 0] = cx + radius * std::cos(angle);
        vertices[(i + 1) * 2 + 1] = cy + radius * std::sin(angle);
    }

    m_gl->glUseProgram(m_curProgram);
    m_gl->glUniformMatrix4fv(m_curLocMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(m_curLocColor, r, g, b, a);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    m_gl->glDrawArrays(GL_TRIANGLE_FAN, 0, vertCount);
    m_gl->glBindVertexArray(0);
}

} // namespace aether
