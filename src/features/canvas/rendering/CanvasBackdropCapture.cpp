// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   B A C K D R O P   C A P T U R E
// ==========================================================================

#include "features/canvas/rendering/CanvasBackdropCapture.h"

#include "shared/rendering/GLShaderProgram.h"

#include <algorithm>
#include <cstring>

namespace aether {

CanvasBackdropCapture::CanvasBackdropCapture(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

CanvasBackdropCapture::~CanvasBackdropCapture()
{
    shutdown();
}

Result<void> CanvasBackdropCapture::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }
    if (!m_gl) {
        return { ErrorCode::InvalidArgument, "CanvasBackdropCapture: null GL functions" };
    }

    m_copyProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto copyResult = m_copyProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/backdrop_copy.frag.glsl");
    if (!copyResult) {
        shutdown();
        return copyResult;
    }

    m_blurProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blurResult = m_blurProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/backdrop_blur.frag.glsl");
    if (!blurResult) {
        shutdown();
        return blurResult;
    }

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(2, m_pbo);
    if (!m_vao || !m_pbo[0] || !m_pbo[1]) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed,
            "CanvasBackdropCapture: failed to allocate VAO/PBOs" };
    }

    m_initialized = true;
    return Result<void>::ok();
}

void CanvasBackdropCapture::shutdown()
{
    if (!m_gl) {
        m_initialized = false;
        return;
    }
    releaseTargets();
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_pbo[0] || m_pbo[1]) {
        m_gl->glDeleteBuffers(2, m_pbo);
        m_pbo[0] = m_pbo[1] = 0;
    }
    m_pboHasData[0] = m_pboHasData[1] = false;
    m_pboWrite = 0;
    m_copyProgram.reset();
    m_blurProgram.reset();
    m_snapshot = QImage();
    m_initialized = false;
}

bool CanvasBackdropCapture::createColorTarget(int w, int h, GLuint& fbo, GLuint& tex)
{
    m_gl->glGenTextures(1, &tex);
    m_gl->glBindTexture(GL_TEXTURE_2D, tex);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_gl->glGenFramebuffers(1, &fbo);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const bool ok = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    return ok;
}

bool CanvasBackdropCapture::ensureTargets(int surfaceWidth, int surfaceHeight)
{
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return false;
    }
    if (m_fboA && surfaceWidth == m_surfaceWidth && surfaceHeight == m_surfaceHeight) {
        return true;
    }

    releaseTargets();

    m_surfaceWidth = surfaceWidth;
    m_surfaceHeight = surfaceHeight;
    m_halfWidth = std::max(1, surfaceWidth / 2);
    m_halfHeight = std::max(1, surfaceHeight / 2);
    m_quarterWidth = std::max(1, surfaceWidth / 4);
    m_quarterHeight = std::max(1, surfaceHeight / 4);
    m_snapWidth = std::max(1, surfaceWidth / kScale);
    m_snapHeight = std::max(1, surfaceHeight / kScale);

    bool ok = createColorTarget(m_halfWidth, m_halfHeight, m_fboHalf, m_texHalf);
    ok = ok && createColorTarget(m_quarterWidth, m_quarterHeight, m_fboQuarter, m_texQuarter);
    ok = ok && createColorTarget(m_snapWidth, m_snapHeight, m_fboA, m_texA);
    ok = ok && createColorTarget(m_snapWidth, m_snapHeight, m_fboB, m_texB);
    if (!ok) {
        releaseTargets();
        return false;
    }

    const int bytes = m_snapWidth * m_snapHeight * 4;
    for (int i = 0; i < 2; ++i) {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
        m_gl->glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
        m_pboHasData[i] = false;
    }
    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_pboWrite = 0;
    m_snapshot = QImage();
    return true;
}

void CanvasBackdropCapture::releaseTargets()
{
    auto del = [this](GLuint& fbo, GLuint& tex) {
        if (fbo) {
            m_gl->glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
        if (tex) {
            m_gl->glDeleteTextures(1, &tex);
            tex = 0;
        }
    };
    del(m_fboHalf, m_texHalf);
    del(m_fboQuarter, m_texQuarter);
    del(m_fboA, m_texA);
    del(m_fboB, m_texB);
    m_surfaceWidth = m_surfaceHeight = 0;
    m_halfWidth = m_halfHeight = 0;
    m_quarterWidth = m_quarterHeight = 0;
    m_snapWidth = m_snapHeight = 0;
    m_pboHasData[0] = m_pboHasData[1] = false;
}

void CanvasBackdropCapture::drawFullscreen()
{
    m_gl->glBindVertexArray(m_vao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
}

void CanvasBackdropCapture::capture(GLuint sceneTexture, int surfaceWidth, int surfaceHeight)
{
    if (!m_initialized || !sceneTexture || !m_gl) {
        return;
    }
    if (!ensureTargets(surfaceWidth, surfaceHeight) || !m_fboA) {
        return;
    }

    m_gl->glDisable(GL_BLEND);
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_SCISSOR_TEST);
    m_gl->glActiveTexture(GL_TEXTURE0);

    // Pass 0: scene -> half (bilinear 2x box average)
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fboHalf);
    m_gl->glViewport(0, 0, m_halfWidth, m_halfHeight);
    m_copyProgram->use();
    m_copyProgram->setUniform("uSource", 0);
    m_gl->glBindTexture(GL_TEXTURE_2D, sceneTexture);
    drawFullscreen();

    // Pass 1: half -> quarter (another clean 2x step, avoids 4x under-sampling)
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fboQuarter);
    m_gl->glViewport(0, 0, m_quarterWidth, m_quarterHeight);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_texHalf);
    drawFullscreen();

    // Pass 2: quarter -> A (final 2x step to snapshot res)
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fboA);
    m_gl->glViewport(0, 0, m_snapWidth, m_snapHeight);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_texQuarter);
    drawFullscreen();

    // Pass 3: horizontal blur A -> B
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fboB);
    m_gl->glViewport(0, 0, m_snapWidth, m_snapHeight);
    m_blurProgram->use();
    m_blurProgram->setUniform("uSource", 0);
    m_blurProgram->setUniform("uDither", 0);
    m_blurProgram->setUniform("uTexelStep", kBlurSpread / static_cast<float>(m_snapWidth), 0.0f);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_texA);
    drawFullscreen();

    // Pass 4: vertical blur B -> A (final result in A). Dither only here, before
    // the RGBA8 write, to break 8-bit banding on smooth dark gradients.
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fboA);
    m_gl->glViewport(0, 0, m_snapWidth, m_snapHeight);
    m_blurProgram->setUniform("uDither", 1);
    m_blurProgram->setUniform("uTexelStep", 0.0f, kBlurSpread / static_cast<float>(m_snapHeight));
    m_gl->glBindTexture(GL_TEXTURE_2D, m_texB);
    drawFullscreen();

    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glBindVertexArray(0);

    const int bytes = m_snapWidth * m_snapHeight * 4;

    // Map the readback issued on a previous frame (already complete -> no stall).
    const int readIdx = m_pboWrite ^ 1;
    if (m_pboHasData[readIdx]) {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[readIdx]);
        const auto* src = static_cast<const uchar*>(
            m_gl->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT));
        if (src) {
            if (m_snapshot.width() != m_snapWidth || m_snapshot.height() != m_snapHeight) {
                m_snapshot = QImage(m_snapWidth, m_snapHeight, QImage::Format_RGBA8888);
            }
            // glReadPixels rows are bottom-up; flip into the top-down QImage.
            const int rowBytes = m_snapWidth * 4;
            for (int y = 0; y < m_snapHeight; ++y) {
                std::memcpy(m_snapshot.scanLine(y),
                    src + static_cast<size_t>(m_snapHeight - 1 - y) * rowBytes, rowBytes);
            }
            m_gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }

    // Issue this frame's async readback from A (still bound as the draw FBO).
    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[m_pboWrite]);
    m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
    m_gl->glReadPixels(0, 0, m_snapWidth, m_snapHeight, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_pboHasData[m_pboWrite] = true;
    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_pboWrite ^= 1;
}

} // namespace aether
