// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/CanvasBackdropRenderer.h"

#include "shared/rendering/GLShaderProgram.h"

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

int roundedCapacity(int value)
{
    constexpr int kBucket = 8;
    return std::max(kBucket, ((value + kBucket - 1) / kBucket) * kBucket);
}

} // namespace

CanvasBackdropRenderer::CanvasBackdropRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

CanvasBackdropRenderer::~CanvasBackdropRenderer()
{
    shutdown();
}

Result<void> CanvasBackdropRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }
    if (!m_gl) {
        return { ErrorCode::InvalidArgument, "CanvasBackdropRenderer: null GL functions" };
    }

    m_blurProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blurResult = m_blurProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/backdrop_blur.frag.glsl");
    if (!blurResult) {
        shutdown();
        return blurResult;
    }

    m_compositeProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto compositeResult = m_compositeProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/backdrop_composite.frag.glsl");
    if (!compositeResult) {
        shutdown();
        return compositeResult;
    }

    m_gl->glGenVertexArrays(1, &m_vao);
    if (!m_vao) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed,
            "CanvasBackdropRenderer: failed to allocate fullscreen VAO" };
    }

    m_initialized = true;
    return Result<void>::ok();
}

void CanvasBackdropRenderer::shutdown()
{
    if (!m_gl) {
        m_initialized = false;
        return;
    }
    for (RegionTarget& target : m_targets) {
        releaseTarget(target);
    }
    m_targets.clear();
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_blurProgram.reset();
    m_compositeProgram.reset();
    m_initialized = false;
}

bool CanvasBackdropRenderer::createColorTarget(int width, int height, GLuint& fbo, GLuint& texture)
{
    m_gl->glGenTextures(1, &texture);
    m_gl->glBindTexture(GL_TEXTURE_2D, texture);
    m_gl->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_gl->glGenFramebuffers(1, &fbo);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const bool complete = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    return complete;
}

void CanvasBackdropRenderer::releaseTarget(RegionTarget& target)
{
    const auto releasePair = [this](GLuint& fbo, GLuint& texture) {
        if (fbo) {
            m_gl->glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
        if (texture) {
            m_gl->glDeleteTextures(1, &texture);
            texture = 0;
        }
    };
    releasePair(target.halfFbo, target.halfTexture);
    releasePair(target.quarterFbo, target.quarterTexture);
    releasePair(target.blurAFbo, target.blurATexture);
    releasePair(target.blurBFbo, target.blurBTexture);
    target = {};
}

bool CanvasBackdropRenderer::ensureTarget(RegionTarget& target, int captureWidth, int captureHeight)
{
    const int requiredBlurWidth
        = roundedCapacity((captureWidth + kDownsampleScale - 1) / kDownsampleScale);
    const int requiredBlurHeight
        = roundedCapacity((captureHeight + kDownsampleScale - 1) / kDownsampleScale);
    if (target.blurAFbo && requiredBlurWidth <= target.blurWidth
        && requiredBlurHeight <= target.blurHeight) {
        return true;
    }

    const int blurWidth = std::max(requiredBlurWidth, target.blurWidth);
    const int blurHeight = std::max(requiredBlurHeight, target.blurHeight);
    releaseTarget(target);

    target.blurWidth = blurWidth;
    target.blurHeight = blurHeight;
    target.quarterWidth = blurWidth * 2;
    target.quarterHeight = blurHeight * 2;
    target.halfWidth = blurWidth * 4;
    target.halfHeight = blurHeight * 4;

    bool ok = createColorTarget(
        target.halfWidth, target.halfHeight, target.halfFbo, target.halfTexture);
    ok = ok
        && createColorTarget(
            target.quarterWidth, target.quarterHeight, target.quarterFbo, target.quarterTexture);
    ok = ok
        && createColorTarget(
            target.blurWidth, target.blurHeight, target.blurAFbo, target.blurATexture);
    ok = ok
        && createColorTarget(
            target.blurWidth, target.blurHeight, target.blurBFbo, target.blurBTexture);
    if (!ok) {
        releaseTarget(target);
    }
    return ok;
}

void CanvasBackdropRenderer::drawFullscreen()
{
    m_gl->glBindVertexArray(m_vao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
}

bool CanvasBackdropRenderer::render(GLuint sourceFbo, GLuint defaultFbo, int surfaceWidth,
    int surfaceHeight, qreal logicalToSurfaceScaleX, qreal logicalToSurfaceScaleY,
    const std::vector<CanvasBackdropRegion>& regions)
{
    if (!m_initialized || regions.empty() || surfaceWidth <= 0 || surfaceHeight <= 0
        || logicalToSurfaceScaleX <= 0.0 || logicalToSurfaceScaleY <= 0.0) {
        return false;
    }

    std::vector<PreparedRegion> prepared;
    prepared.reserve(regions.size());
    m_targets.resize(std::max(m_targets.size(), regions.size()));
    const QRect surfaceRect(0, 0, surfaceWidth, surfaceHeight);

    for (const CanvasBackdropRegion& region : regions) {
        if (region.rect.isEmpty() || region.opacity <= 0.001) {
            continue;
        }

        const int left = static_cast<int>(std::floor(region.rect.x() * logicalToSurfaceScaleX));
        const int top = static_cast<int>(std::floor(region.rect.y() * logicalToSurfaceScaleY));
        const int right = static_cast<int>(
            std::ceil((region.rect.x() + region.rect.width()) * logicalToSurfaceScaleX));
        const int bottom = static_cast<int>(
            std::ceil((region.rect.y() + region.rect.height()) * logicalToSurfaceScaleY));
        const QRect targetRect(left, top, right - left, bottom - top);
        const QRect clippedTarget = targetRect.intersected(surfaceRect);
        if (clippedTarget.isEmpty()) {
            continue;
        }

        const QRect captureRect = targetRect
                                      .adjusted(-kCapturePaddingDevicePx, -kCapturePaddingDevicePx,
                                          kCapturePaddingDevicePx, kCapturePaddingDevicePx)
                                      .intersected(surfaceRect);
        const std::size_t targetIndex = prepared.size();
        RegionTarget& target = m_targets[targetIndex];
        if (!ensureTarget(target, captureRect.width(), captureRect.height())) {
            continue;
        }

        PreparedRegion item;
        item.captureRect = captureRect;
        item.targetRect = clippedTarget;
        const qreal cornerScale = std::min(logicalToSurfaceScaleX, logicalToSurfaceScaleY);
        item.cornerRadius
            = static_cast<float>(std::max<qreal>(0.0, region.cornerRadius * cornerScale));
        item.opacity = static_cast<float>(std::clamp<qreal>(region.opacity, 0.0, 1.0));
        item.targetIndex = targetIndex;

        const float captureWidth = static_cast<float>(captureRect.width());
        const float captureHeight = static_cast<float>(captureRect.height());
        item.sourceUvMinX = static_cast<float>(clippedTarget.x() - captureRect.x()) / captureWidth;
        item.sourceUvMaxX
            = static_cast<float>(clippedTarget.x() + clippedTarget.width() - captureRect.x())
            / captureWidth;
        item.sourceUvMinY = static_cast<float>(captureRect.y() + captureRect.height()
                                - clippedTarget.y() - clippedTarget.height())
            / captureHeight;
        item.sourceUvMaxY
            = static_cast<float>(captureRect.y() + captureRect.height() - clippedTarget.y())
            / captureHeight;
        prepared.push_back(item);
    }

    if (prepared.empty()) {
        return false;
    }

    m_gl->glDisable(GL_BLEND);
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_SCISSOR_TEST);
    m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Capture and reduce every source region before compositing any result back
    // into the default framebuffer. This prevents overlap feedback.
    for (const PreparedRegion& item : prepared) {
        RegionTarget& target = m_targets[item.targetIndex];
        const int sourceBottom = surfaceHeight - item.captureRect.y() - item.captureRect.height();
        const int sourceTop = surfaceHeight - item.captureRect.y();

        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.halfFbo);
        m_gl->glBlitFramebuffer(item.captureRect.x(), sourceBottom,
            item.captureRect.x() + item.captureRect.width(), sourceTop, 0, 0, target.halfWidth,
            target.halfHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, target.halfFbo);
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.quarterFbo);
        m_gl->glBlitFramebuffer(0, 0, target.halfWidth, target.halfHeight, 0, 0,
            target.quarterWidth, target.quarterHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, target.quarterFbo);
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.blurAFbo);
        m_gl->glBlitFramebuffer(0, 0, target.quarterWidth, target.quarterHeight, 0, 0,
            target.blurWidth, target.blurHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    m_gl->glActiveTexture(GL_TEXTURE0);
    for (const PreparedRegion& item : prepared) {
        RegionTarget& target = m_targets[item.targetIndex];
        const float horizontalStep = static_cast<float>(kKernelReachDevicePx)
            / (4.0f * static_cast<float>(item.captureRect.width()));
        const float verticalStep = static_cast<float>(kKernelReachDevicePx)
            / (4.0f * static_cast<float>(item.captureRect.height()));

        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.blurBFbo);
        m_gl->glViewport(0, 0, target.blurWidth, target.blurHeight);
        m_blurProgram->use();
        m_blurProgram->setUniform("uSource", 0);
        m_blurProgram->setUniform("uDither", 0);
        m_blurProgram->setUniform("uTexelStep", horizontalStep, 0.0f);
        m_gl->glBindTexture(GL_TEXTURE_2D, target.blurATexture);
        drawFullscreen();

        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.blurAFbo);
        m_blurProgram->setUniform("uDither", 1);
        m_blurProgram->setUniform("uTexelStep", 0.0f, verticalStep);
        m_gl->glBindTexture(GL_TEXTURE_2D, target.blurBTexture);
        drawFullscreen();

        const int destinationBottom
            = surfaceHeight - item.targetRect.y() - item.targetRect.height();
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
        m_gl->glViewport(item.targetRect.x(), destinationBottom, item.targetRect.width(),
            item.targetRect.height());
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_compositeProgram->use();
        m_compositeProgram->setUniform("uSource", 0);
        m_compositeProgram->setUniform("uSourceUvMin", item.sourceUvMinX, item.sourceUvMinY);
        m_compositeProgram->setUniform("uSourceUvMax", item.sourceUvMaxX, item.sourceUvMaxY);
        m_compositeProgram->setUniform("uRectSize", static_cast<float>(item.targetRect.width()),
            static_cast<float>(item.targetRect.height()));
        m_compositeProgram->setUniform("uCornerRadius", item.cornerRadius);
        m_compositeProgram->setUniform("uOpacity", item.opacity);
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, target.blurATexture);
        drawFullscreen();
        m_gl->glDisable(GL_BLEND);
    }

    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    m_gl->glViewport(0, 0, surfaceWidth, surfaceHeight);
    return true;
}

} // namespace aether
