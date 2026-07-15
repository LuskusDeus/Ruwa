// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   F I L L   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/LassoFillOverlay.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <algorithm>
#include <cmath>
namespace aether {

namespace {

struct Span {
    int32_t y;
    float x1, x2;
};

/// Scanline rasterization (same logic as fillPolygon) — handles self-intersecting polygons.
/// Clips to canvas bounds [0, canvasWidth) x [0, canvasHeight).
void computePolygonSpans(const std::vector<Vector2>& polygon, float minX, float minY, float maxX,
    float maxY, int canvasWidth, int canvasHeight, std::vector<Span>& outSpans)
{
    outSpans.clear();
    if (polygon.size() < 3)
        return;

    const bool clipToCanvas = canvasWidth > 0 && canvasHeight > 0;
    const int32_t y0 = clipToCanvas ? std::max(0, static_cast<int32_t>(std::floor(minY)))
                                    : static_cast<int32_t>(std::floor(minY));
    const int32_t y1 = clipToCanvas
        ? std::min(canvasHeight - 1, static_cast<int32_t>(std::ceil(maxY)))
        : static_cast<int32_t>(std::ceil(maxY));
    const int32_t x0 = clipToCanvas ? std::max(0, static_cast<int32_t>(std::floor(minX)))
                                    : static_cast<int32_t>(std::floor(minX));
    const int32_t x1 = clipToCanvas
        ? std::min(canvasWidth - 1, static_cast<int32_t>(std::ceil(maxX)))
        : static_cast<int32_t>(std::ceil(maxX));
    if (y1 < y0 || x1 < x0)
        return;

    const size_t count = polygon.size();
    for (int32_t y = y0; y <= y1; ++y) {
        float scanY = static_cast<float>(y) + 0.5f;
        std::vector<float> intersections;
        intersections.reserve(count);

        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            const Vector2& a = polygon[j];
            const Vector2& b = polygon[i];
            if ((a.y <= scanY) == (b.y <= scanY))
                continue;
            float t = (scanY - a.y) / (b.y - a.y + 0.0000001f);
            float ix = a.x + t * (b.x - a.x);
            intersections.push_back(ix);
        }

        if (intersections.size() < 2)
            continue;
        std::sort(intersections.begin(), intersections.end());

        if (intersections.size() % 2 != 0) {
            intersections.pop_back();
        }

        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            float xa = std::max(
                static_cast<float>(std::ceil(intersections[k] - 0.5f)), static_cast<float>(x0));
            float xb = std::min(static_cast<float>(std::floor(intersections[k + 1] - 0.5f)),
                static_cast<float>(x1));
            if (xb < xa)
                continue;
            outSpans.push_back({ y, xa, xb });
        }
    }
}

} // anonymous namespace

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

LassoFillOverlay::LassoFillOverlay(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

LassoFillOverlay::~LassoFillOverlay()
{
    shutdown();
}

Result<void> LassoFillOverlay::initialize()
{
    if (m_initialized)
        return Result<void>::ok();

    GLProgramBinaryCache cache(m_gl);
    auto shaderProgram = cache.loadOrCreateGraphicsProgram(QStringLiteral("LassoFillOverlay.fill"),
        QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShader));
    if (!shaderProgram) {
        return { shaderProgram.error().code, shaderProgram.error().message };
    }

    m_shaderProgram = shaderProgram.value();
    m_locMVP = m_gl->glGetUniformLocation(m_shaderProgram, "uMVP");
    m_locColor = m_gl->glGetUniformLocation(m_shaderProgram, "uColor");

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);

    m_initialized = true;
    return Result<void>::ok();
}

void LassoFillOverlay::shutdown()
{
    if (!m_initialized)
        return;

    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo) {
        m_gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_shaderProgram) {
        m_gl->glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }

    m_initialized = false;
}

void LassoFillOverlay::render(const Viewport& viewport, const std::vector<Vector2>& polygon,
    int canvasWidth, int canvasHeight, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    const std::array<float, 16>* viewProjectionContent)
{
    if (!m_initialized || polygon.size() < 3)
        return;

    float minX = polygon[0].x, minY = polygon[0].y, maxX = polygon[0].x, maxY = polygon[0].y;
    for (const auto& p : polygon) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }

    const auto& camera = viewport.camera();
    const Vector2 viewportSize = viewport.size();
    const Vector2 p0 = camera.screenToWorld({ 0.0f, 0.0f }, viewportSize);
    const Vector2 p1 = camera.screenToWorld({ viewportSize.x, 0.0f }, viewportSize);
    const Vector2 p2 = camera.screenToWorld({ 0.0f, viewportSize.y }, viewportSize);
    const Vector2 p3 = camera.screenToWorld({ viewportSize.x, viewportSize.y }, viewportSize);
    const float visibleMinX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    const float visibleMinY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    const float visibleMaxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    const float visibleMaxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
    minX = std::max(minX, visibleMinX);
    minY = std::max(minY, visibleMinY);
    maxX = std::min(maxX, visibleMaxX);
    maxY = std::min(maxY, visibleMaxY);
    if (maxX < minX || maxY < minY) {
        return;
    }

    std::vector<Span> spans;
    computePolygonSpans(polygon, minX, minY, maxX, maxY, canvasWidth, canvasHeight, spans);

    m_vertices.clear();
    m_vertices.reserve(spans.size() * 12);
    for (const auto& s : spans) {
        float yf = static_cast<float>(s.y);
        float yf1 = yf + 1.0f;
        float x2 = s.x2 + 1.0f;
        // Quad: (x1,y) (x2,y) (x2,y+1) (x1,y+1) — two triangles
        m_vertices.insert(m_vertices.end(), { s.x1, yf, x2, yf, x2, yf1 });
        m_vertices.insert(m_vertices.end(), { s.x1, yf, x2, yf1, s.x1, yf1 });
    }

    if (m_vertices.empty())
        return;

    const auto vpMatrix
        = viewProjectionContent ? *viewProjectionContent : viewport.viewProjectionMatrix();

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(float)),
        m_vertices.data(), GL_STREAM_DRAW);

    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    m_gl->glUseProgram(m_shaderProgram);
    m_gl->glUniformMatrix4fv(m_locMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform4f(m_locColor, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size() / 2));

    m_gl->glDisable(GL_BLEND);
    m_gl->glBindVertexArray(0);
}

} // namespace aether
