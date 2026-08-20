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

    m_downsampleProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto downsampleResult = m_downsampleProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/backdrop_downsample.frag.glsl");
    if (!downsampleResult) {
        shutdown();
        return downsampleResult;
    }

    m_upsampleProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto upsampleResult = m_upsampleProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/backdrop_upsample.frag.glsl");
    if (!upsampleResult) {
        shutdown();
        return upsampleResult;
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
    m_downsampleProgram.reset();
    m_upsampleProgram.reset();
    m_compositeProgram.reset();
    m_initialized = false;
}

bool CanvasBackdropRenderer::createColorTarget(
    int width, int height, GLenum internalFormat, GLuint& fbo, GLuint& texture)
{
    m_gl->glGenTextures(1, &texture);
    m_gl->glBindTexture(GL_TEXTURE_2D, texture);
    // Immutable storage: every one of these is reallocated wholesale when the
    // region outgrows it, never respecified in place.
    m_gl->glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, width, height);
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
    for (int level = 0; level < kBlurLevels; ++level) {
        releasePair(target.levelFbo[level], target.levelTexture[level]);
    }
    target = {};
}

bool CanvasBackdropRenderer::ensureTarget(RegionTarget& target, int captureWidth, int captureHeight)
{
    // roundedCapacity() buckets to multiples of eight, so every level of the
    // pyramid below stays an even number of texels down to the last one. With
    // the frost off there is no pyramid and the capture is kept whole.
    const auto reduced = [](int extent) {
        return roundedCapacity((extent + kDownsampleScale - 1) / kDownsampleScale);
    };
    const int requiredHalfWidth
        = kFrostEnabled ? reduced(captureWidth) * 2 : roundedCapacity(captureWidth);
    const int requiredHalfHeight
        = kFrostEnabled ? reduced(captureHeight) * 2 : roundedCapacity(captureHeight);
    if (target.halfFbo && requiredHalfWidth <= target.halfWidth
        && requiredHalfHeight <= target.halfHeight) {
        return true;
    }

    target.halfWidth = std::max(requiredHalfWidth, target.halfWidth);
    target.halfHeight = std::max(requiredHalfHeight, target.halfHeight);
    const int halfWidth = target.halfWidth;
    const int halfHeight = target.halfHeight;
    releaseTarget(target);
    target.halfWidth = halfWidth;
    target.halfHeight = halfHeight;

    // The capture arrives sRGB encoded straight out of the framebuffer blit;
    // whoever reads it first decodes it, and everything past that is linear.
    bool ok = createColorTarget(
        target.halfWidth, target.halfHeight, GL_RGBA8, target.halfFbo, target.halfTexture);
    for (int level = 0; ok && kFrostEnabled && level < kBlurLevels; ++level) {
        target.levelWidth[level] = std::max(1, (target.halfWidth / 2) >> level);
        target.levelHeight[level] = std::max(1, (target.halfHeight / 2) >> level);
        ok = createColorTarget(target.levelWidth[level], target.levelHeight[level],
            kLinearTargetFormat, target.levelFbo[level], target.levelTexture[level]);
    }
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
    const float deviceScale
        = static_cast<float>(std::min<qreal>(logicalToSurfaceScaleX, logicalToSurfaceScaleY));
    const float shadowFalloff = kShadowFalloffLogicalPx * deviceScale;
    const float shadowOffsetY = kShadowOffsetYLogicalPx * deviceScale;
    const int shadowPadding = static_cast<int>(
        std::ceil(shadowFalloff * kShadowReachInFalloffRadii + std::abs(shadowOffsetY)));

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
        const float regionShadowOpacity = static_cast<float>(
            kShadowOpacity * std::clamp<qreal>(region.shadowStrength, 0.0, 1.0));
        // A region without a shadow needs no room outside itself for one, and
        // the composite is the most expensive pass here: skip the padding
        // rather than shading five falloff radii of fully transparent pixels.
        const int regionShadowPadding = regionShadowOpacity > 0.0f ? shadowPadding : 0;
        const QRect compositeRect = targetRect
                                        .adjusted(-regionShadowPadding, -regionShadowPadding,
                                            regionShadowPadding, regionShadowPadding)
                                        .intersected(surfaceRect);

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
        item.targetRect = targetRect;
        item.compositeRect = compositeRect;
        const qreal cornerScale = std::min(logicalToSurfaceScaleX, logicalToSurfaceScaleY);
        item.cornerRadius
            = static_cast<float>(std::max<qreal>(0.0, region.cornerRadius * cornerScale));
        item.opacity = static_cast<float>(std::clamp<qreal>(region.opacity, 0.0, 1.0));
        item.shadowOpacity = regionShadowOpacity;
        item.refractionStrength
            = static_cast<float>(std::max<qreal>(0.0, region.refractionStrength));
        item.frostLevels = kFrostEnabled
            ? std::clamp(region.frostLevels < 0 ? kBlurLevels : region.frostLevels, 0, kBlurLevels)
            : 0;
        item.targetIndex = targetIndex;
        if (region.surfaceTint.isValid()) {
            item.tintR = static_cast<float>(region.surfaceTint.redF());
            item.tintG = static_cast<float>(region.surfaceTint.greenF());
            item.tintB = static_cast<float>(region.surfaceTint.blueF());
            item.tintAmount = kGlassSurfaceTint;
        }

        const float captureWidth = static_cast<float>(captureRect.width());
        const float captureHeight = static_cast<float>(captureRect.height());
        item.sourceUvMinX = static_cast<float>(targetRect.x() - captureRect.x()) / captureWidth;
        item.sourceUvMaxX
            = static_cast<float>(targetRect.x() + targetRect.width() - captureRect.x())
            / captureWidth;
        item.sourceUvMinY = static_cast<float>(captureRect.y() + captureRect.height()
                                - targetRect.y() - targetRect.height())
            / captureHeight;
        item.sourceUvMaxY
            = static_cast<float>(captureRect.y() + captureRect.height() - targetRect.y())
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

    const float refractionDepth = kRefractionDepthLogicalPx * deviceScale;
    const float refractionShift
        = std::min(kRefractionShiftLogicalPx * deviceScale, kMaxRefractionShiftDevicePx);

    m_gl->glActiveTexture(GL_TEXTURE0);

    // Capture every source region before compositing any result back into the
    // default framebuffer. This prevents overlap feedback.
    for (const PreparedRegion& item : prepared) {
        RegionTarget& target = m_targets[item.targetIndex];
        const int sourceBottom = surfaceHeight - item.captureRect.y() - item.captureRect.height();
        const int sourceTop = surfaceHeight - item.captureRect.y();

        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.halfFbo);
        m_gl->glBlitFramebuffer(item.captureRect.x(), sourceBottom,
            item.captureRect.x() + item.captureRect.width(), sourceTop, 0, 0, target.halfWidth,
            target.halfHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    for (const PreparedRegion& item : prepared) {
        RegionTarget& target = m_targets[item.targetIndex];

        if (item.frostLevels > 0) {
            // Contracting half of the dual filter. Step 0 reads the captured half
            // and decodes it out of sRGB; every step after that reads the level
            // above it, already linear, at five taps.
            m_downsampleProgram->use();
            m_downsampleProgram->setUniform("uSource", 0);
            m_downsampleProgram->setUniform("uOffset", kBlurSampleOffset);
            for (int level = 0; level < item.frostLevels; ++level) {
                const bool fromCapture = level == 0;
                const GLuint sourceTexture
                    = fromCapture ? target.halfTexture : target.levelTexture[level - 1];
                const int sourceWidth
                    = fromCapture ? target.halfWidth : target.levelWidth[level - 1];
                const int sourceHeight
                    = fromCapture ? target.halfHeight : target.levelHeight[level - 1];

                m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.levelFbo[level]);
                m_gl->glViewport(0, 0, target.levelWidth[level], target.levelHeight[level]);
                m_downsampleProgram->setUniform("uDecodeSrgb", fromCapture ? 1 : 0);
                m_downsampleProgram->setUniform("uHalfPixel",
                    0.5f / static_cast<float>(sourceWidth),
                    0.5f / static_cast<float>(sourceHeight));
                m_gl->glBindTextureUnit(0, sourceTexture);
                drawFullscreen();
            }

            // Expanding half, back down the same ladder. Writing into level-1 is
            // safe: its contracted content has already been consumed by the step
            // that produced level, so no target is ever read and written at once.
            m_upsampleProgram->use();
            m_upsampleProgram->setUniform("uSource", 0);
            m_upsampleProgram->setUniform("uOffset", kBlurSampleOffset);
            for (int level = item.frostLevels - 1; level > 0; --level) {
                m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.levelFbo[level - 1]);
                m_gl->glViewport(0, 0, target.levelWidth[level - 1], target.levelHeight[level - 1]);
                m_upsampleProgram->setUniform("uHalfPixel",
                    0.5f / static_cast<float>(target.levelWidth[level]),
                    0.5f / static_cast<float>(target.levelHeight[level]));
                m_gl->glBindTextureUnit(0, target.levelTexture[level]);
                drawFullscreen();
            }
        }

        const int destinationBottom
            = surfaceHeight - item.compositeRect.y() - item.compositeRect.height();
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
        m_gl->glViewport(item.compositeRect.x(), destinationBottom, item.compositeRect.width(),
            item.compositeRect.height());
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_compositeProgram->use();
        m_compositeProgram->setUniform("uSource", 0);
        m_compositeProgram->setUniform("uSurfaceTint", item.tintR, item.tintG, item.tintB);
        m_compositeProgram->setUniform("uSurfaceTintAmount", item.tintAmount);
        m_compositeProgram->setUniform("uRefractionDepth", refractionDepth);
        m_compositeProgram->setUniform(
            "uRefractionShift", refractionShift * item.refractionStrength);
        m_compositeProgram->setUniform("uMaxTilt", kMaxSurfaceTilt);
        m_compositeProgram->setUniform("uDispersion", kChromaticDispersion);
        m_compositeProgram->setUniform("uSplay", kSplay * item.refractionStrength);
        m_compositeProgram->setUniform("uMaxSplayShift", kMaxSplayShiftDevicePx);
        // With no pyramid the composite reads the capture itself, which is
        // still sRGB encoded - the pass that would have decoded it did not run.
        m_compositeProgram->setUniform("uDecodeSrgb", item.frostLevels > 0 ? 0 : 1);
        m_compositeProgram->setUniform("uEdgeInset", kSilhouetteInsetLogicalPx * deviceScale);
        m_compositeProgram->setUniform("uSourceUvMin", item.sourceUvMinX, item.sourceUvMinY);
        m_compositeProgram->setUniform("uSourceUvMax", item.sourceUvMaxX, item.sourceUvMaxY);
        m_compositeProgram->setUniform("uCompositeSize",
            static_cast<float>(item.compositeRect.width()),
            static_cast<float>(item.compositeRect.height()));
        const float rectOffsetX
            = static_cast<float>(item.targetRect.x() - item.compositeRect.x());
        const float rectOffsetY = static_cast<float>(item.compositeRect.y()
            + item.compositeRect.height() - item.targetRect.y() - item.targetRect.height());
        m_compositeProgram->setUniform("uRectOffset", rectOffsetX, rectOffsetY);
        m_compositeProgram->setUniform("uRectSize", static_cast<float>(item.targetRect.width()),
            static_cast<float>(item.targetRect.height()));
        m_compositeProgram->setUniform("uCornerRadius", item.cornerRadius);
        m_compositeProgram->setUniform("uOpacity", item.opacity);
        m_compositeProgram->setUniform("uShadowOffset", 0.0f, -shadowOffsetY);
        m_compositeProgram->setUniform("uShadowFalloff", shadowFalloff);
        m_compositeProgram->setUniform("uShadowOpacity", item.shadowOpacity);
        m_compositeProgram->setUniform("uShadowReach", kShadowReachInFalloffRadii);
        m_gl->glBindTextureUnit(
            0, item.frostLevels > 0 ? target.levelTexture[0] : target.halfTexture);
        drawFullscreen();
        m_gl->glDisable(GL_BLEND);
    }

    m_gl->glBindTextureUnit(0, 0);
    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    m_gl->glViewport(0, 0, surfaceWidth, surfaceHeight);
    return true;
}

} // namespace aether
