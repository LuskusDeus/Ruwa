// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/GLLassoMaskRenderer.h"

#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

/// Must stay in lockstep with the commit path's `edgePadWorld` in
/// OpenGLCanvasWidget::buildLassoFillScreenMask, which is 0.75 / zoom document
/// pixels — i.e. exactly this many screen pixels.
constexpr float kEdgePadPx = 0.75f;

/// The oriented box a capsule is drawn with. The capsule itself only reaches
/// kEdgePadPx past the segment; the extra half pixel keeps a pixel centre that
/// sits exactly on the box edge from being dropped by the fill rule before the
/// fragment stage gets to run the exact distance test.
constexpr float kCapsuleBoxMargin = 0.5f;

constexpr GLuint kBitParity = 0x01;
constexpr GLuint kBitSegmentPad = 0x02;
constexpr GLuint kBitChordPad = 0x04;
constexpr GLuint kBitCanvas = 0x08;
constexpr GLuint kCoverageBits = kBitParity | kBitSegmentPad | kBitChordPad;

constexpr std::size_t kFloatsPerVertex = 6;

void appendVertex(std::vector<float>& out, const Vector2& p, const Vector2& a, const Vector2& b)
{
    out.push_back(p.x);
    out.push_back(p.y);
    out.push_back(a.x);
    out.push_back(a.y);
    out.push_back(b.x);
    out.push_back(b.y);
}

void appendTriangle(std::vector<float>& out, const Vector2& a, const Vector2& b, const Vector2& c)
{
    const Vector2 unused {};
    appendVertex(out, a, unused, unused);
    appendVertex(out, b, unused, unused);
    appendVertex(out, c, unused, unused);
}

void appendQuad(std::vector<float>& out, const Vector2& p0, const Vector2& p1, const Vector2& p2,
    const Vector2& p3, const Vector2& segA, const Vector2& segB)
{
    appendVertex(out, p0, segA, segB);
    appendVertex(out, p1, segA, segB);
    appendVertex(out, p2, segA, segB);
    appendVertex(out, p0, segA, segB);
    appendVertex(out, p2, segA, segB);
    appendVertex(out, p3, segA, segB);
}

/// The oriented bounding box of the segment's edge pad. The fragment stage
/// carves the exact capsule out of it, so over-covering here is free.
void appendCapsule(std::vector<float>& out, const Vector2& a, const Vector2& b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSq = dx * dx + dy * dy;
    float ux = 1.0f;
    float uy = 0.0f;
    if (lengthSq > 1e-12f) {
        const float invLength = 1.0f / std::sqrt(lengthSq);
        ux = dx * invLength;
        uy = dy * invLength;
    }

    const float r = kEdgePadPx + kCapsuleBoxMargin;
    const Vector2 along { ux * r, uy * r };
    const Vector2 normal { -uy * r, ux * r };

    const Vector2 p0 { a.x - along.x - normal.x, a.y - along.y - normal.y };
    const Vector2 p1 { b.x + along.x - normal.x, b.y + along.y - normal.y };
    const Vector2 p2 { b.x + along.x + normal.x, b.y + along.y + normal.y };
    const Vector2 p3 { a.x - along.x + normal.x, a.y - along.y + normal.y };
    appendQuad(out, p0, p1, p2, p3, a, b);
}

void appendRect(std::vector<float>& out, const QRect& rect)
{
    const Vector2 unused {};
    const auto x0 = static_cast<float>(rect.x());
    const auto y0 = static_cast<float>(rect.y());
    const auto x1 = static_cast<float>(rect.x() + rect.width());
    const auto y1 = static_cast<float>(rect.y() + rect.height());
    appendQuad(out, { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 }, unused, unused);
}

QRect screenPolygonBounds(const std::vector<Vector2>& polygon, const QSize& viewportSize)
{
    float minX = polygon.front().x;
    float minY = polygon.front().y;
    float maxX = minX;
    float maxY = minY;
    for (const Vector2& point : polygon) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    // One pixel beyond the pad so a partially covered border pixel is included.
    const float pad = kEdgePadPx + 1.0f;
    const int x0 = static_cast<int>(std::floor(minX - pad));
    const int y0 = static_cast<int>(std::floor(minY - pad));
    const int x1 = static_cast<int>(std::ceil(maxX + pad));
    const int y1 = static_cast<int>(std::ceil(maxY + pad));
    return QRect(QPoint(x0, y0), QPoint(x1, y1)).intersected(QRect(QPoint(0, 0), viewportSize));
}

/// Everything this pass touches, put back the way it was found. The lasso mask
/// runs in the middle of paintGL, between passes that assume the canvas
/// renderer's own state.
struct MaskPassStateGuard {
    explicit MaskPassStateGuard(QOpenGLFunctions_4_5_Core* gl)
        : m_gl(gl)
    {
        m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_fbo);
        m_gl->glGetIntegerv(GL_VIEWPORT, m_viewport);
        m_gl->glGetBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
        m_gl->glGetIntegerv(GL_STENCIL_WRITEMASK, &m_stencilWriteMask);
        m_gl->glGetIntegerv(GL_STENCIL_FUNC, &m_stencilFunc);
        m_gl->glGetIntegerv(GL_STENCIL_REF, &m_stencilRef);
        m_gl->glGetIntegerv(GL_STENCIL_VALUE_MASK, &m_stencilValueMask);
        m_gl->glGetIntegerv(GL_STENCIL_FAIL, &m_stencilFail);
        m_gl->glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &m_stencilDepthFail);
        m_gl->glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &m_stencilDepthPass);
        m_blend = m_gl->glIsEnabled(GL_BLEND);
        m_scissor = m_gl->glIsEnabled(GL_SCISSOR_TEST);
        m_stencil = m_gl->glIsEnabled(GL_STENCIL_TEST);
        m_depth = m_gl->glIsEnabled(GL_DEPTH_TEST);
        m_cull = m_gl->glIsEnabled(GL_CULL_FACE);
    }

    ~MaskPassStateGuard()
    {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_fbo));
        m_gl->glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        m_gl->glColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
        m_gl->glStencilMask(static_cast<GLuint>(m_stencilWriteMask));
        m_gl->glStencilFunc(static_cast<GLenum>(m_stencilFunc), m_stencilRef,
            static_cast<GLuint>(m_stencilValueMask));
        m_gl->glStencilOp(static_cast<GLenum>(m_stencilFail),
            static_cast<GLenum>(m_stencilDepthFail), static_cast<GLenum>(m_stencilDepthPass));
        setEnabled(GL_BLEND, m_blend);
        setEnabled(GL_SCISSOR_TEST, m_scissor);
        setEnabled(GL_STENCIL_TEST, m_stencil);
        setEnabled(GL_DEPTH_TEST, m_depth);
        setEnabled(GL_CULL_FACE, m_cull);
    }

    MaskPassStateGuard(const MaskPassStateGuard&) = delete;
    MaskPassStateGuard& operator=(const MaskPassStateGuard&) = delete;

private:
    void setEnabled(GLenum cap, GLboolean on)
    {
        if (on) {
            m_gl->glEnable(cap);
        } else {
            m_gl->glDisable(cap);
        }
    }

    QOpenGLFunctions_4_5_Core* m_gl;
    GLint m_fbo = 0;
    GLint m_viewport[4] = {};
    GLboolean m_colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLint m_stencilWriteMask = 0xFF;
    GLint m_stencilFunc = GL_ALWAYS;
    GLint m_stencilRef = 0;
    GLint m_stencilValueMask = 0xFF;
    GLint m_stencilFail = GL_KEEP;
    GLint m_stencilDepthFail = GL_KEEP;
    GLint m_stencilDepthPass = GL_KEEP;
    GLboolean m_blend = GL_FALSE;
    GLboolean m_scissor = GL_FALSE;
    GLboolean m_stencil = GL_FALSE;
    GLboolean m_depth = GL_FALSE;
    GLboolean m_cull = GL_FALSE;
};

} // namespace

GLLassoMaskRenderer::GLLassoMaskRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLLassoMaskRenderer::~GLLassoMaskRenderer()
{
    shutdown();
}

Result<void> GLLassoMaskRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_program = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_program->loadFromFiles(
        shaderDir + "/lasso_mask.vert.glsl", shaderDir + "/lasso_mask.frag.glsl");
    if (!result) {
        m_program.reset();
        return result;
    }

    m_gl->glCreateFramebuffers(1, &m_fbo);
    m_gl->glCreateVertexArrays(1, &m_vao);
    m_gl->glCreateBuffers(1, &m_vbo);
    if (!m_fbo || !m_vao || !m_vbo) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed, "Failed to create lasso mask objects" };
    }

    constexpr GLsizei kStride = static_cast<GLsizei>(kFloatsPerVertex * sizeof(float));
    m_gl->glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, kStride);
    m_gl->glEnableVertexArrayAttrib(m_vao, 0);
    m_gl->glVertexArrayAttribFormat(m_vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    m_gl->glVertexArrayAttribBinding(m_vao, 0, 0);
    m_gl->glEnableVertexArrayAttrib(m_vao, 1);
    m_gl->glVertexArrayAttribFormat(
        m_vao, 1, 4, GL_FLOAT, GL_FALSE, static_cast<GLuint>(2 * sizeof(float)));
    m_gl->glVertexArrayAttribBinding(m_vao, 1, 0);

    m_gl->glNamedFramebufferDrawBuffer(m_fbo, GL_COLOR_ATTACHMENT0);

    m_initialized = true;
    return Result<void>::ok();
}

void GLLassoMaskRenderer::shutdown()
{
    deleteTexture(m_gl, m_maskTexture);
    if (m_stencilBuffer) {
        m_gl->glDeleteRenderbuffers(1, &m_stencilBuffer);
        m_stencilBuffer = 0;
    }
    if (m_vbo) {
        m_gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    m_maskSize = {};
    m_vertexScratch.clear();
    m_vertexScratch.shrink_to_fit();
    invalidateAccumulation();
    m_program.reset();
    m_initialized = false;
}

void GLLassoMaskRenderer::invalidateAccumulation()
{
    m_accumulationValid = false;
    m_accumulatedPoints = 0;
    m_pivot = {};
    m_gate = {};
}

void GLLassoMaskRenderer::ensureTargets(const QSize& size)
{
    if (m_maskTexture && m_stencilBuffer && m_maskSize == size) {
        return;
    }

    deleteTexture(m_gl, m_maskTexture);
    if (m_stencilBuffer) {
        m_gl->glDeleteRenderbuffers(1, &m_stencilBuffer);
        m_stencilBuffer = 0;
    }
    m_maskSize = {};
    invalidateAccumulation();

    const GLsizei width = size.width();
    const GLsizei height = size.height();
    m_maskTexture = createTexture2D(m_gl, width, height,
        { GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_R8, GL_RED,
            GL_UNSIGNED_BYTE });
    if (!m_maskTexture) {
        return;
    }

    m_gl->glCreateRenderbuffers(1, &m_stencilBuffer);
    if (!m_stencilBuffer) {
        deleteTexture(m_gl, m_maskTexture);
        return;
    }

    m_gl->glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_maskTexture, 0);
    m_gl->glNamedRenderbufferStorage(m_stencilBuffer, GL_STENCIL_INDEX8, width, height);
    m_gl->glNamedFramebufferRenderbuffer(
        m_fbo, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_stencilBuffer);
    if (m_gl->glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // Stencil-only renderbuffers are required by the spec but have a history
        // of being unhappy on real drivers; packed depth-stencil always works.
        m_gl->glNamedFramebufferRenderbuffer(m_fbo, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        m_gl->glNamedRenderbufferStorage(m_stencilBuffer, GL_DEPTH24_STENCIL8, width, height);
        m_gl->glNamedFramebufferRenderbuffer(
            m_fbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_stencilBuffer);
        if (m_gl->glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            m_gl->glDeleteRenderbuffers(1, &m_stencilBuffer);
            m_stencilBuffer = 0;
            deleteTexture(m_gl, m_maskTexture);
            return;
        }
    }

    m_maskSize = size;
}

GLLassoMaskRenderer::MaskRenderResult GLLassoMaskRenderer::accumulate(
    const std::vector<Vector2>& screenPolygon, const QSize& viewportSize, const CanvasGate& gate,
    bool forceRebuild)
{
    MaskRenderResult result;
    if (!m_initialized || !m_program || !m_program->isValid() || screenPolygon.size() < 3
        || !viewportSize.isValid() || viewportSize.isEmpty()) {
        return result;
    }

    ensureTargets(viewportSize);
    if (!m_maskTexture || !m_stencilBuffer || m_maskSize != viewportSize) {
        return result;
    }

    const std::size_t pointCount = screenPolygon.size();
    const bool rebuild = forceRebuild || !m_accumulationValid || m_accumulatedPoints < 3
        || pointCount < m_accumulatedPoints || !gate.matches(m_gate)
        || m_pivot.x != screenPolygon.front().x || m_pivot.y != screenPolygon.front().y;

    const QRect maskRect(QPoint(0, 0), m_maskSize);
    if (!rebuild && pointCount == m_accumulatedPoints) {
        result.texture = m_maskTexture;
        result.bounds = maskRect;
        return result;
    }

    // ---- Stage the geometry -------------------------------------------------
    //
    // Layout, in this order: canvas gate, parity triangles, the chord pad to
    // erase, the new segment pads, the new chord pad, and (on a rebuild) the
    // rectangle the resolve runs over. Everything from the erased chord onward
    // is contiguous so the resolve can replay all capsules in one draw.

    m_vertexScratch.clear();
    auto batchFrom = [this](std::size_t startFloats) {
        Batch batch;
        batch.first = static_cast<GLint>(startFloats / kFloatsPerVertex);
        batch.count
            = static_cast<GLsizei>((m_vertexScratch.size() - startFloats) / kFloatsPerVertex);
        return batch;
    };

    Batch gateBatch;
    Batch triangleBatch;
    Batch oldChordBatch;
    Batch segmentBatch;
    Batch chordBatch;
    Batch resolveRectBatch;

    if (rebuild && gate.enabled) {
        const std::size_t start = m_vertexScratch.size();
        appendQuad(m_vertexScratch, gate.corners[0], gate.corners[1], gate.corners[2],
            gate.corners[3], {}, {});
        gateBatch = batchFrom(start);
    }

    const std::size_t firstNewSegment = rebuild ? 0 : m_accumulatedPoints - 1;
    {
        const std::size_t start = m_vertexScratch.size();
        // Parity fan pivoted on the first point. On a rebuild that is every
        // triangle; incrementally it is one per point added this frame.
        for (std::size_t i = std::max<std::size_t>(firstNewSegment, 1); i + 1 < pointCount; ++i) {
            appendTriangle(
                m_vertexScratch, screenPolygon[0], screenPolygon[i], screenPolygon[i + 1]);
        }
        triangleBatch = batchFrom(start);
    }

    if (!rebuild) {
        const std::size_t start = m_vertexScratch.size();
        appendCapsule(m_vertexScratch, screenPolygon[m_accumulatedPoints - 1], screenPolygon[0]);
        oldChordBatch = batchFrom(start);
    }

    {
        const std::size_t start = m_vertexScratch.size();
        for (std::size_t i = firstNewSegment; i + 1 < pointCount; ++i) {
            appendCapsule(m_vertexScratch, screenPolygon[i], screenPolygon[i + 1]);
        }
        segmentBatch = batchFrom(start);
    }

    {
        const std::size_t start = m_vertexScratch.size();
        appendCapsule(m_vertexScratch, screenPolygon[pointCount - 1], screenPolygon[0]);
        chordBatch = batchFrom(start);
    }

    // Every capsule drawn this frame, in one contiguous range: the erased chord,
    // the new segments and the new chord.
    Batch capsuleBatch;
    capsuleBatch.first = triangleBatch.first + triangleBatch.count;
    capsuleBatch.count = chordBatch.first + chordBatch.count - capsuleBatch.first;

    QRect rebuildResolveRect;
    if (rebuild) {
        rebuildResolveRect = screenPolygonBounds(screenPolygon, m_maskSize);
        if (!rebuildResolveRect.isEmpty()) {
            const std::size_t start = m_vertexScratch.size();
            appendRect(m_vertexScratch, rebuildResolveRect);
            resolveRectBatch = batchFrom(start);
        }
    }

    m_gl->glNamedBufferData(m_vbo, static_cast<GLsizeiptr>(m_vertexScratch.size() * sizeof(float)),
        m_vertexScratch.data(), GL_STREAM_DRAW);

    // ---- Draw ---------------------------------------------------------------

    MaskPassStateGuard guard(m_gl);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glViewport(0, 0, m_maskSize.width(), m_maskSize.height());
    m_gl->glDisable(GL_BLEND);
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_CULL_FACE);
    m_gl->glDisable(GL_SCISSOR_TEST);
    m_gl->glEnable(GL_STENCIL_TEST);
    m_gl->glBindVertexArray(m_vao);

    m_program->use();
    m_program->setUniform("uViewportSize", static_cast<float>(m_maskSize.width()),
        static_cast<float>(m_maskSize.height()));
    m_program->setUniform("uEdgePadSq", kEdgePadPx * kEdgePadPx);
    m_program->setUniform("uCapsule", 0);
    m_program->setUniform("uWriteValue", 0.0f);

    auto draw = [this](const Batch& batch, bool capsule) {
        if (batch.empty()) {
            return;
        }
        m_program->setUniform("uCapsule", capsule ? 1 : 0);
        m_gl->glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
    };

    if (rebuild) {
        m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_gl->glStencilMask(0xFF);
        const GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_gl->glClearBufferfv(GL_COLOR, 0, clearColor);
        // With no canvas to clip against, the gate bit is simply on everywhere.
        const GLint clearStencil = gate.enabled ? 0 : static_cast<GLint>(kBitCanvas);
        m_gl->glClearBufferiv(GL_STENCIL, 0, &clearStencil);
    }

    // Accumulate: stencil only.
    m_gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    m_gl->glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    if (!gateBatch.empty()) {
        m_gl->glStencilFunc(GL_ALWAYS, static_cast<GLint>(kBitCanvas), 0xFF);
        m_gl->glStencilMask(kBitCanvas);
        draw(gateBatch, false);
    }

    // From here on every write is stencil-tested against the gate bit, which is
    // what clips the mask to the canvas. Note the reference value doubles as the
    // GL_REPLACE source, so it carries both the gate bit (for the comparison,
    // which only looks at 0x08) and the bit being written.
    m_gl->glStencilMask(kBitChordPad);
    m_gl->glStencilFunc(GL_EQUAL, static_cast<GLint>(kBitCanvas), kBitCanvas);
    draw(oldChordBatch, true); // reference has no chord bit -> erases it

    m_gl->glStencilMask(kBitParity);
    m_gl->glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    draw(triangleBatch, false);

    m_gl->glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    m_gl->glStencilMask(kBitSegmentPad);
    m_gl->glStencilFunc(GL_EQUAL, static_cast<GLint>(kBitCanvas | kBitSegmentPad), kBitCanvas);
    draw(segmentBatch, true);

    m_gl->glStencilMask(kBitChordPad);
    m_gl->glStencilFunc(GL_EQUAL, static_cast<GLint>(kBitCanvas | kBitChordPad), kBitCanvas);
    draw(chordBatch, true);

    // Resolve: colour only, over exactly the pixels whose stencil just moved.
    // On a rebuild that is the polygon's box (everything else was cleared to 0);
    // incrementally it is the very geometry that was drawn, which is why a long
    // stroke costs no more per frame than a short one.
    m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_gl->glStencilMask(0x00);
    m_gl->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    for (int pass = 0; pass < 2; ++pass) {
        const bool covered = pass == 0;
        m_gl->glStencilFunc(covered ? GL_NOTEQUAL : GL_EQUAL, 0, kCoverageBits);
        m_program->setUniform("uWriteValue", covered ? 1.0f : 0.0f);
        if (rebuild) {
            draw(resolveRectBatch, false);
        } else {
            draw(triangleBatch, false);
            draw(capsuleBatch, true);
        }
    }

    m_gl->glBindVertexArray(0);

    m_accumulationValid = true;
    m_accumulatedPoints = pointCount;
    m_pivot = screenPolygon.front();
    m_gate = gate;

    result.texture = m_maskTexture;
    result.bounds = maskRect;
    return result;
}

} // namespace aether
