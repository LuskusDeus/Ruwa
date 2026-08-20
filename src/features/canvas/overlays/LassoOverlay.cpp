// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   O V E R L A Y
// ==========================================================================

#include "features/canvas/overlays/LassoOverlay.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <algorithm>
#include <cmath>
namespace aether {

// ==========================================================================
//   I N L I N E   S H A D E R S   (per-vertex color for batched rendering)
// ==========================================================================

static const char* kVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* kFragmentShader = R"(
#version 450 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

// Existing-selection edges are immutable between selection mutations. Keep one
// compact instance per merged edge on the GPU and calculate both the two-pixel
// quad and the animated intensity in shaders. The old path rebuilt and uploaded
// 36 floats for every six document pixels on every animation frame.
static const char* kEdgeVertexShader = R"(
#version 450 core
layout(location = 0) in vec4 aSegment;
layout(location = 1) in float aDistanceStart;
uniform mat4 uMVP;
uniform float uThicknessWorld;
out float vDistance;

const vec2 kCorners[6] = vec2[](
    vec2(0.0,  1.0), vec2(0.0, -1.0), vec2(1.0, -1.0),
    vec2(0.0,  1.0), vec2(1.0, -1.0), vec2(1.0,  1.0)
);

void main() {
    vec2 p0 = aSegment.xy;
    vec2 p1 = aSegment.zw;
    vec2 delta = p1 - p0;
    float lengthWorld = length(delta);
    vec2 normal = vec2(-delta.y, delta.x) / lengthWorld;
    vec2 corner = kCorners[gl_VertexID];
    vec2 position = mix(p0, p1, corner.x)
        + normal * corner.y * uThicknessWorld * 0.5;
    gl_Position = uMVP * vec4(position, 0.0, 1.0);
    vDistance = aDistanceStart + corner.x * lengthWorld;
}
)";

static const char* kEdgeFragmentShader = R"(
#version 450 core
uniform float uTime;
uniform float uBaseAlpha;
in float vDistance;
out vec4 fragColor;

void main() {
    const float tau = 6.28318530718;
    float phase = (vDistance / 28.0) * tau + uTime * 2.0 * tau;
    float wave = 0.5 + 0.5 * sin(phase);
    float intensity = uBaseAlpha * mix(0.35, 1.0, wave);
    fragColor = vec4(intensity, intensity, intensity, 1.0);
}
)";

// Shader with mask sampling: modulates alpha where path overlaps existing selection
static const char* kVertexShaderWithMask = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
out vec2 vWorldPos;
void main() {
    vec4 world = vec4(aPos, 0.0, 1.0);
    gl_Position = uMVP * world;
    vColor = aColor;
    vWorldPos = aPos;
}
)";

static const char* kFragmentShaderWithMask = R"(
#version 450 core
uniform sampler2D uMask;
uniform vec2 uMaskOrigin;
uniform vec2 uMaskSize;
uniform float uAlphaInside;
uniform float uAlphaOutside;
in vec4 vColor;
in vec2 vWorldPos;
out vec4 fragColor;
void main() {
    // Mask atlas: world coords, Y-up in texture (OpenGL: row 0 at bottom = world top)
    vec2 uv;
    uv.x = (vWorldPos.x - uMaskOrigin.x) / uMaskSize.x;
    uv.y = (vWorldPos.y - uMaskOrigin.y) / uMaskSize.y;
    float inside = 0.0;
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
        inside = texture(uMask, uv).a;
    }
    // Add: dim inside. Subtract: dim outside.
    float intensity = mix(uAlphaOutside, uAlphaInside, step(0.5, inside));
    fragColor = vec4(vColor.rgb * intensity, vColor.a);
}
)";

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

LassoOverlay::LassoOverlay(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
    m_startTime = std::chrono::steady_clock::now();
}

LassoOverlay::~LassoOverlay()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> LassoOverlay::initialize()
{
    if (m_initialized)
        return Result<void>::ok();

    GLProgramBinaryCache cache(m_gl);
    auto shaderProgram = cache.loadOrCreateGraphicsProgram(QStringLiteral("LassoOverlay.base"),
        QString::fromUtf8(kVertexShader), QString::fromUtf8(kFragmentShader));
    if (!shaderProgram) {
        return { shaderProgram.error().code, shaderProgram.error().message };
    }

    m_shaderProgram = shaderProgram.value();
    m_locMVP = m_gl->glGetUniformLocation(m_shaderProgram, "uMVP");

    auto maskProgram = cache.loadOrCreateGraphicsProgram(QStringLiteral("LassoOverlay.mask"),
        QString::fromUtf8(kVertexShaderWithMask), QString::fromUtf8(kFragmentShaderWithMask));
    if (!maskProgram) {
        if (m_shaderProgram) {
            m_gl->glDeleteProgram(m_shaderProgram);
            m_shaderProgram = 0;
        }
        return { maskProgram.error().code, maskProgram.error().message };
    }

    m_shaderProgramWithMask = maskProgram.value();
    m_locMaskMVP = m_gl->glGetUniformLocation(m_shaderProgramWithMask, "uMVP");
    m_locMaskTex = m_gl->glGetUniformLocation(m_shaderProgramWithMask, "uMask");
    m_locMaskOrigin = m_gl->glGetUniformLocation(m_shaderProgramWithMask, "uMaskOrigin");
    m_locMaskSize = m_gl->glGetUniformLocation(m_shaderProgramWithMask, "uMaskSize");
    m_locAlphaInside = m_gl->glGetUniformLocation(m_shaderProgramWithMask, "uAlphaInside");
    m_locAlphaOutside = m_gl->glGetUniformLocation(m_shaderProgramWithMask, "uAlphaOutside");

    auto edgeProgram = cache.loadOrCreateGraphicsProgram(QStringLiteral("LassoOverlay.edges"),
        QString::fromUtf8(kEdgeVertexShader), QString::fromUtf8(kEdgeFragmentShader));
    if (!edgeProgram) {
        m_gl->glDeleteProgram(m_shaderProgramWithMask);
        m_gl->glDeleteProgram(m_shaderProgram);
        m_shaderProgramWithMask = 0;
        m_shaderProgram = 0;
        return { edgeProgram.error().code, edgeProgram.error().message };
    }

    m_edgeShaderProgram = edgeProgram.value();
    m_locEdgeMVP = m_gl->glGetUniformLocation(m_edgeShaderProgram, "uMVP");
    m_locEdgeThickness = m_gl->glGetUniformLocation(m_edgeShaderProgram, "uThicknessWorld");
    m_locEdgeTime = m_gl->glGetUniformLocation(m_edgeShaderProgram, "uTime");
    m_locEdgeAlpha = m_gl->glGetUniformLocation(m_edgeShaderProgram, "uBaseAlpha");

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);

    // Vertex layout: [x, y, r, g, b, a] — 6 floats, stride = 24 bytes
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    m_gl->glEnableVertexAttribArray(1);

    m_gl->glBindVertexArray(0);

    m_gl->glGenVertexArrays(1, &m_edgeVao);
    m_gl->glGenBuffers(1, &m_edgeVbo);
    m_gl->glBindVertexArray(m_edgeVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_edgeVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    // One compact instance per merged edge: [x0, y0, x1, y1, cumulativeDistance].
    m_gl->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribDivisor(0, 1);
    m_gl->glVertexAttribPointer(
        1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribDivisor(1, 1);
    m_gl->glBindVertexArray(0);

    m_initialized = true;
    return Result<void>::ok();
}

void LassoOverlay::shutdown()
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
    if (m_edgeVbo) {
        m_gl->glDeleteBuffers(1, &m_edgeVbo);
        m_edgeVbo = 0;
    }
    if (m_edgeVao) {
        m_gl->glDeleteVertexArrays(1, &m_edgeVao);
        m_edgeVao = 0;
    }
    if (m_shaderProgram) {
        m_gl->glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    if (m_shaderProgramWithMask) {
        m_gl->glDeleteProgram(m_shaderProgramWithMask);
        m_shaderProgramWithMask = 0;
    }
    if (m_edgeShaderProgram) {
        m_gl->glDeleteProgram(m_edgeShaderProgram);
        m_edgeShaderProgram = 0;
    }

    m_edgeInstances.clear();
    m_cachedEdgesRevision = 0;
    m_edgeInstanceCount = 0;

    m_initialized = false;
}

// ==========================================================================
//   R E N D E R I N G
// ==========================================================================

void LassoOverlay::render(const Viewport& viewport, const std::vector<Vector2>& activePath,
    bool activeClosed, const std::vector<LassoEdgeSegment>& edges, uint64_t edgesRevision,
    float edgesAlpha, GLuint addPathMaskTexture, float maskAtlasOriginX, float maskAtlasOriginY,
    float maskAtlasWidth, float maskAtlasHeight, float pathAlphaInsideMask,
    float pathAlphaOutsideMask, const std::array<float, 16>* viewProjectionContent)
{
    if (!m_initialized)
        return;

    const float zoom = viewport.camera().zoom();
    updateEdgeInstances(edges, edgesRevision);

    const bool hasActive = activePath.size() >= 2;
    const bool hasEdges = m_edgeInstanceCount > 0;
    m_animating = hasActive || hasEdges;
    if (!m_animating)
        return;

    const float timeSec = elapsedSeconds();
    const auto vpMatrix
        = viewProjectionContent ? *viewProjectionContent : viewport.viewProjectionMatrix();

    const bool usePathMask
        = (addPathMaskTexture != 0) && hasActive && maskAtlasWidth > 0.0f && maskAtlasHeight > 0.0f;

    m_batchVertices.clear();
    m_pathBatchVertices.clear();
    if (!usePathMask)
        m_batchVertices.reserve(activePath.size() * 36);
    if (usePathMask)
        m_pathBatchVertices.reserve(activePath.size() * 36);

    if (hasActive) {
        if (usePathMask) {
            batchPath(activePath, activeClosed, zoom, timeSec, 1.0f, &m_pathBatchVertices);
        } else {
            batchPath(activePath, activeClosed, zoom, timeSec, 1.0f, &m_batchVertices);
        }
    }

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
    m_gl->glDisable(GL_DEPTH_TEST);

    if (hasEdges) {
        drawEdgeInstances(vpMatrix, zoom, timeSec, edgesAlpha);
    }
    if (!m_batchVertices.empty()) {
        flushBatch(vpMatrix);
    }
    if (!m_pathBatchVertices.empty() && usePathMask) {
        flushBatchWithMask(vpMatrix, addPathMaskTexture, maskAtlasOriginX, maskAtlasOriginY,
            maskAtlasWidth, maskAtlasHeight, pathAlphaInsideMask, pathAlphaOutsideMask);
    }

    m_gl->glDisable(GL_BLEND);
}

// ==========================================================================
//   B A T C H   B U I L D I N G
// ==========================================================================

void LassoOverlay::batchPath(const std::vector<Vector2>& points, bool closed, float zoom,
    float timeSec, float baseAlpha, std::vector<float>* outVertices)
{
    if (points.size() < 2)
        return;

    std::vector<float>& target = outVertices ? *outVertices : m_batchVertices;

    const float thicknessWorld = 2.0f / zoom;
    const float stepWorld = 6.0f;
    const float periodWorld = 28.0f;
    const float speed = 2.0f;

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    float accumulated = 0.0f;
    const size_t count = points.size();
    const size_t last = count - 1;

    auto processSegment = [&](const Vector2& p0, const Vector2& p1) {
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float segLen = std::sqrt(dx * dx + dy * dy);
        if (segLen < 0.0001f)
            return;
        float invLen = 1.0f / segLen;
        float dirX = dx * invLen;
        float dirY = dy * invLen;

        for (float d = 0.0f; d < segLen; d += stepWorld) {
            float d2 = std::min(segLen, d + stepWorld);
            float mid = accumulated + (d + d2) * 0.5f;

            float phase = (mid / periodWorld) * 6.2831853f + timeSec * speed * 6.2831853f;
            float t = 0.5f + 0.5f * std::sin(phase);

            // RGB defines inversion strength with this blend mode.
            float intensity = baseAlpha * lerp(0.35f, 1.0f, t);

            Vector2 s = { p0.x + dirX * d, p0.y + dirY * d };
            Vector2 e = { p0.x + dirX * d2, p0.y + dirY * d2 };
            appendQuadTo(s, e, thicknessWorld, intensity, intensity, intensity, 1.0f, target);
        }
        accumulated += segLen;
    };

    for (size_t i = 0; i < last; ++i) {
        processSegment(points[i], points[i + 1]);
    }

    if (closed) {
        processSegment(points[last], points.front());
    }
}

void LassoOverlay::updateEdgeInstances(
    const std::vector<LassoEdgeSegment>& edges, uint64_t edgesRevision)
{
    if (edgesRevision != 0 && m_cachedEdgesRevision == edgesRevision) {
        return;
    }

    // Every edge is kept regardless of zoom. A length threshold looks like a
    // cheap level of detail, but the contour is a pixel staircase: an
    // axis-aligned boundary merges into one long run while a slanted one stays
    // a chain of one-pixel steps, so any such filter erases exactly the
    // diagonal outlines and leaves the horizontal and vertical ones behind.
    // The instance buffer is rebuilt only when the selection itself changes, so
    // keeping the full contour costs one upload per mutation, not per frame.
    m_edgeInstances.clear();
    m_edgeInstances.reserve(edges.size() * 5);

    float accumulated = 0.0f;
    for (const auto& seg : edges) {
        const float dx = seg.b.x - seg.a.x;
        const float dy = seg.b.y - seg.a.y;
        const float length = std::sqrt(dx * dx + dy * dy);

        if (length >= 0.0001f) {
            m_edgeInstances.push_back(seg.a.x);
            m_edgeInstances.push_back(seg.a.y);
            m_edgeInstances.push_back(seg.b.x);
            m_edgeInstances.push_back(seg.b.y);
            m_edgeInstances.push_back(accumulated);
        }
        accumulated += length;
    }

    const size_t instanceCount = m_edgeInstances.size() / 5;
    m_edgeInstanceCount = static_cast<GLsizei>(
        std::min(instanceCount, static_cast<size_t>(std::numeric_limits<GLsizei>::max())));

    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_edgeVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(m_edgeInstances.size() * sizeof(float)),
        m_edgeInstances.empty() ? nullptr : m_edgeInstances.data(), GL_DYNAMIC_DRAW);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_cachedEdgesRevision = edgesRevision;
}

void LassoOverlay::drawEdgeInstances(
    const std::array<float, 16>& vpMatrix, float zoom, float timeSec, float baseAlpha)
{
    if (m_edgeInstanceCount <= 0)
        return;

    m_gl->glUseProgram(m_edgeShaderProgram);
    m_gl->glUniformMatrix4fv(m_locEdgeMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform1f(m_locEdgeThickness, 2.0f / std::max(zoom, 0.0001f));
    m_gl->glUniform1f(m_locEdgeTime, timeSec);
    m_gl->glUniform1f(m_locEdgeAlpha, baseAlpha);

    m_gl->glBindVertexArray(m_edgeVao);
    m_gl->glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_edgeInstanceCount);
    m_gl->glBindVertexArray(0);
}

// ==========================================================================
//   Q U A D   G E N E R A T I O N
// ==========================================================================

void LassoOverlay::appendQuadTo(const Vector2& p0, const Vector2& p1, float thickness, float r,
    float g, float b, float a, std::vector<float>& target)
{
    Vector2 dir = { p1.x - p0.x, p1.y - p0.y };
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.0001f)
        return;
    float invLen = 1.0f / len;
    float nx = -dir.y * invLen * thickness * 0.5f;
    float ny = dir.x * invLen * thickness * 0.5f;

    // 4 corners of the oriented rectangle
    float x0 = p0.x + nx, y0 = p0.y + ny;
    float x1 = p0.x - nx, y1 = p0.y - ny;
    float x2 = p1.x - nx, y2 = p1.y - ny;
    float x3 = p1.x + nx, y3 = p1.y + ny;

    // 2 triangles = 6 vertices, 6 floats each = 36 floats
    size_t base = target.size();
    target.resize(base + 36);
    float* v = target.data() + base;

    // Triangle 1: corners 0, 1, 2
    v[0] = x0;
    v[1] = y0;
    v[2] = r;
    v[3] = g;
    v[4] = b;
    v[5] = a;
    v[6] = x1;
    v[7] = y1;
    v[8] = r;
    v[9] = g;
    v[10] = b;
    v[11] = a;
    v[12] = x2;
    v[13] = y2;
    v[14] = r;
    v[15] = g;
    v[16] = b;
    v[17] = a;
    // Triangle 2: corners 0, 2, 3
    v[18] = x0;
    v[19] = y0;
    v[20] = r;
    v[21] = g;
    v[22] = b;
    v[23] = a;
    v[24] = x2;
    v[25] = y2;
    v[26] = r;
    v[27] = g;
    v[28] = b;
    v[29] = a;
    v[30] = x3;
    v[31] = y3;
    v[32] = r;
    v[33] = g;
    v[34] = b;
    v[35] = a;
}

// ==========================================================================
//   B A T C H   F L U S H   (single draw call)
// ==========================================================================

void LassoOverlay::flushBatch(const std::array<float, 16>& vpMatrix)
{
    if (m_batchVertices.empty())
        return;

    const GLsizei vertexCount = static_cast<GLsizei>(m_batchVertices.size() / 6);
    const auto byteSize = static_cast<GLsizeiptr>(m_batchVertices.size() * sizeof(float));

    m_gl->glUseProgram(m_shaderProgram);
    m_gl->glUniformMatrix4fv(m_locMVP, 1, GL_FALSE, vpMatrix.data());

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // glBufferData orphans old buffer — avoids GPU sync stalls
    m_gl->glBufferData(GL_ARRAY_BUFFER, byteSize, m_batchVertices.data(), GL_STREAM_DRAW);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    m_gl->glBindVertexArray(0);
}

void LassoOverlay::flushBatchWithMask(const std::array<float, 16>& vpMatrix, GLuint maskTexture,
    float maskOriginX, float maskOriginY, float maskWidth, float maskHeight, float alphaInside,
    float alphaOutside)
{
    if (m_pathBatchVertices.empty() || !maskTexture)
        return;

    const GLsizei vertexCount = static_cast<GLsizei>(m_pathBatchVertices.size() / 6);
    const auto byteSize = static_cast<GLsizeiptr>(m_pathBatchVertices.size() * sizeof(float));

    m_gl->glUseProgram(m_shaderProgramWithMask);
    m_gl->glUniformMatrix4fv(m_locMaskMVP, 1, GL_FALSE, vpMatrix.data());
    m_gl->glUniform1i(m_locMaskTex, 0);
    m_gl->glUniform2f(m_locMaskOrigin, maskOriginX, maskOriginY);
    m_gl->glUniform2f(m_locMaskSize, maskWidth, maskHeight);
    m_gl->glUniform1f(m_locAlphaInside, alphaInside);
    m_gl->glUniform1f(m_locAlphaOutside, alphaOutside);

    m_gl->glBindTextureUnit(0, maskTexture);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, byteSize, m_pathBatchVertices.data(), GL_STREAM_DRAW);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    m_gl->glBindVertexArray(0);

    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

// ==========================================================================
//   U T I L I T I E S
// ==========================================================================

float LassoOverlay::elapsedSeconds() const
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - m_startTime).count();
}

} // namespace aether
