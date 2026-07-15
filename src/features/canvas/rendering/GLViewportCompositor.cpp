// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/GLViewportCompositor.h"

#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"
#include "shared/rendering/GLStateGuard.h"
#include "features/effects/EffectCoverageResolver.h"
#include "features/effects/GLLayerEffectRenderer.h"

#include <algorithm>
#include <cmath>

namespace aether {

namespace {
// Upper bound on an overscan source dimension: distortion reach in preview grows
// with radius*zoom, so cap the enlarged texture the way the committed whole-layer
// path caps its region (kMaxWholeLayerDim). Beyond this the reach clamps — an
// acceptable degradation at extreme zoom, matching the committed path's bound.
constexpr int kMaxOverscanDim = 8192;
} // namespace

GLViewportCompositor::GLViewportCompositor(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLViewportCompositor::~GLViewportCompositor()
{
    shutdown();
}

Result<void> GLViewportCompositor::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_compositeProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto compositeResult = m_compositeProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/composite.frag.glsl");
    if (!compositeResult) {
        m_compositeProgram.reset();
        return compositeResult;
    }

    m_blitProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blitResult = m_blitProgram->loadFromFiles(
        shaderDir + "/composite.vert.glsl", shaderDir + "/fill_blit.frag.glsl");
    if (!blitResult) {
        shutdown();
        return blitResult;
    }

    m_effectRenderer = std::make_unique<GLLayerEffectRenderer>(m_gl);
    auto effectResult = m_effectRenderer->initialize(shaderDir);
    if (!effectResult) {
        shutdown();
        return effectResult;
    }

    m_gl->glGenFramebuffers(1, &m_fbo);
    m_gl->glGenVertexArrays(1, &m_emptyVao);
    if (!m_fbo || !m_emptyVao) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed,
            "Failed to create viewport compositor objects" };
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLViewportCompositor::shutdown()
{
    deleteTexture(m_gl, m_pingPongTextures[0]);
    deleteTexture(m_gl, m_pingPongTextures[1]);
    deleteTexture(m_gl, m_clipGroupTextures[0]);
    deleteTexture(m_gl, m_clipGroupTextures[1]);
    deleteTexture(m_gl, m_transparentTexture);
    deleteTexture(m_gl, m_maskRevealTexture);
    deleteTexture(m_gl, m_overscanCropTexture);
    destroyGroupCompositeFrames();
    m_maskRevealWidth = 0;
    m_maskRevealHeight = 0;
    m_overscanCropWidth = 0;
    m_overscanCropHeight = 0;

    if (m_emptyVao) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    m_width = 0;
    m_height = 0;
    m_currentPing = 0;
    m_effectRenderer.reset();
    m_blitProgram.reset();
    m_compositeProgram.reset();
    m_initialized = false;
}

void GLViewportCompositor::beginFrame(uint32_t width, uint32_t height)
{
    if ((m_width != width || m_height != height) && m_initialized) {
        deleteTexture(m_gl, m_pingPongTextures[0]);
        deleteTexture(m_gl, m_pingPongTextures[1]);
        deleteTexture(m_gl, m_clipGroupTextures[0]);
        deleteTexture(m_gl, m_clipGroupTextures[1]);
        deleteTexture(m_gl, m_transparentTexture);
        deleteTexture(m_gl, m_maskRevealTexture);
        deleteTexture(m_gl, m_overscanCropTexture);
        destroyGroupCompositeFrames();
        m_maskRevealWidth = 0;
        m_maskRevealHeight = 0;
        m_overscanCropWidth = 0;
        m_overscanCropHeight = 0;
    }
    m_width = width;
    m_height = height;
    ensureRenderTargets();
}

GLuint GLViewportCompositor::compositeLayers(const std::vector<CompositeLayerInfo>& layers,
    const SourceResolver& sourceResolver, const Color& backdropColor, float spaceScale,
    const ruwa::core::effects::EffectRegionFrame& effectRegion,
    const OverscanSourceResolver& overscanResolver, const LayerMaskResolver& layerMaskResolver)
{
    if (!m_initialized || layers.empty() || m_width == 0 || m_height == 0) {
        return 0;
    }

    GLFboViewportBlendGuard guard(m_gl);

    m_spaceScale = spaceScale > 0.0f ? spaceScale : 1.0f;
    m_effectRegion = effectRegion;
    // Only offer the overscan reach path when a resolver was supplied AND the
    // screen frame is a known affine (distortion reach is meaningless otherwise).
    m_overscanResolver = (overscanResolver && m_effectRegion.valid) ? &overscanResolver : nullptr;
    m_currentPing = 0;
    m_groupCompositeDepth = 0;
    clearTexture(m_pingPongTextures[0]);
    const GLuint result = compositeLayerStack(
        layers, 1.0f, false, sourceResolver, layerMaskResolver, backdropColor);
    m_groupCompositeDepth = 0;
    m_overscanResolver = nullptr;
    return result;
}

void GLViewportCompositor::drawTexture(GLuint texture, const CanvasClipParams& clipParams,
    const LassoMaskParams& lassoMask, bool replaceWithCoverage)
{
    if (!m_initialized || !texture || !m_blitProgram || !m_blitProgram->isValid()) {
        return;
    }

    const bool useLassoMask
        = lassoMask.maskTexture != 0 && lassoMask.width > 0 && lassoMask.height > 0;

    m_blitProgram->use();
    m_blitProgram->setUniform("uTileTexture", 0);
    m_blitProgram->setUniform("uUseCanvasClip", clipParams.enabled ? 1 : 0);
    m_blitProgram->setUniform(
        "uViewportSize", static_cast<float>(m_width), static_cast<float>(m_height));
    m_blitProgram->setUniform(
        "uCameraPosition", clipParams.cameraPosition.x, clipParams.cameraPosition.y);
    m_blitProgram->setUniform("uCameraZoom", clipParams.cameraZoom);
    m_blitProgram->setUniform("uCameraRotation", clipParams.cameraRotation);
    m_blitProgram->setUniform("uCanvasSize", clipParams.canvasWidth, clipParams.canvasHeight);
    m_blitProgram->setUniform("uCanvasCornerRadius", clipParams.canvasCornerRadius);
    m_blitProgram->setUniform("uLassoMask", 1);
    m_blitProgram->setUniform("uUseLassoMask", useLassoMask ? 1 : 0);
    m_blitProgram->setUniform("uReplaceWithCoverage", replaceWithCoverage ? 1 : 0);
    m_blitProgram->setUniform("uLassoMaskOrigin", lassoMask.originX, lassoMask.originY);
    m_blitProgram->setUniform("uLassoMaskSize", lassoMask.width, lassoMask.height);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, texture);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, useLassoMask ? lassoMask.maskTexture : 0);
    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint GLViewportCompositor::applyLuminanceRevealMask(
    GLuint colorTex, GLuint maskTex, uint32_t width, uint32_t height)
{
    if (!m_initialized || !colorTex || width == 0 || height == 0 || !m_compositeProgram
        || !m_compositeProgram->isValid()) {
        return 0;
    }

    GLFboViewportBlendGuard guard(m_gl);

    if (!m_maskRevealTexture || m_maskRevealWidth != width || m_maskRevealHeight != height) {
        const TextureParams linear { GL_LINEAR, GL_LINEAR };
        deleteTexture(m_gl, m_maskRevealTexture);
        m_maskRevealTexture = createTexture2D(m_gl, width, height, linear);
        m_maskRevealWidth = width;
        m_maskRevealHeight = height;
    }
    if (!m_maskRevealTexture) {
        return 0;
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_maskRevealTexture, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    m_gl->glDisable(GL_BLEND);

    // uReplaceBase + opacity 1 => outColor = src, and the luminance-reveal branch
    // has already done src *= reveal. So the base texture is unused; bind colorTex
    // to both slots to avoid sampling an unbound unit.
    m_compositeProgram->use();
    m_compositeProgram->setUniform("uBaseTexture", 0);
    m_compositeProgram->setUniform("uSrcTexture", 1);
    m_compositeProgram->setUniform("uClipMaskTexture", 2);
    m_compositeProgram->setUniform("uBlendMode", 0);
    m_compositeProgram->setUniform("uOpacity", 1.0f);
    m_compositeProgram->setUniform("uUseClipMask", 1);
    m_compositeProgram->setUniform("uClipMaskAlphaOnly", 0);
    m_compositeProgram->setUniform("uSubtractClipRevealFromSrc", 0);
    m_compositeProgram->setUniform("uClipMaskLuminanceReveal", 1);
    m_compositeProgram->setUniform("uClipMaskEditPreview", 0);
    m_compositeProgram->setUniform("uClipMaskEditReplace", 0);
    m_compositeProgram->setUniform("uClipMaskEditStrokeOpacity", 0.0f);
    m_compositeProgram->setUniform("uClipMaskAsAlphaCap", 0);
    m_compositeProgram->setUniform("uPreserveBaseAlpha", 0);
    m_compositeProgram->setUniform("uReplaceBase", 1);
    m_compositeProgram->setUniform("uReplaceBaseMixReveal", 0);
    m_compositeProgram->setUniform("uUseGroupComposite", 0);
    m_compositeProgram->setUniform("uUseProgrammaticBlendBase", 0);
    m_compositeProgram->setUniform("uSrcAtop", 0);
    m_compositeProgram->setUniform("uUseRadialReveal", 0);
    m_compositeProgram->setUniform("uRadialRevealInvert", 0);
    m_compositeProgram->setUniform("uRadialRevealOrigin", 0.0f, 0.0f);
    m_compositeProgram->setUniform("uRadialRevealRadius", 0.0f);
    m_compositeProgram->setUniform("uRadialRevealFeather", 0.0f);
    m_compositeProgram->setUniform("uBackdropColor", 0.0f, 0.0f, 0.0f, 0.0f);
    m_compositeProgram->setUniform("uTileWorldOrigin", 0.0f, 0.0f);

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, colorTex);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, colorTex);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, maskTex);

    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);

    // Restore neutral state for callers that use the shared program directly.
    // blendPass sets these uniforms explicitly for every layer as well.
    m_compositeProgram->setUniform("uClipMaskLuminanceReveal", 0);
    m_compositeProgram->setUniform("uClipMaskEditPreview", 0);
    m_compositeProgram->setUniform("uClipMaskEditReplace", 0);
    m_compositeProgram->setUniform("uClipMaskAsAlphaCap", 0);
    m_compositeProgram->setUniform("uUseProgrammaticBlendBase", 0);
    m_compositeProgram->setUniform("uUseGroupComposite", 0);
    m_compositeProgram->setUniform("uReplaceBase", 0);

    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    return m_maskRevealTexture;
}

GLuint GLViewportCompositor::compositeLayerStack(const std::vector<CompositeLayerInfo>& layers,
    float parentOpacity, bool useSrcAtop, const SourceResolver& sourceResolver,
    const LayerMaskResolver& layerMaskResolver, const Color& backdropColor)
{
    size_t index = 0;
    while (index < layers.size()) {
        const auto& layer = layers[index];
        if (!layer.visible) {
            ++index;
            continue;
        }

        const float effectiveOpacity = layer.opacity * parentOpacity;
        if (effectiveOpacity <= 0.0f) {
            ++index;
            continue;
        }

        const bool isClipBase = !useSrcAtop && !layer.clippedToBelow && (index + 1 < layers.size())
            && layers[index + 1].clippedToBelow;
        if (isClipBase) {
            size_t clipEnd = index + 1;
            while (clipEnd < layers.size() && layers[clipEnd].clippedToBelow) {
                ++clipEnd;
            }

            const GLuint savedPingPong0 = m_pingPongTextures[0];
            const GLuint savedPingPong1 = m_pingPongTextures[1];
            const int savedPing = m_currentPing;

            m_pingPongTextures[0] = m_clipGroupTextures[0];
            m_pingPongTextures[1] = m_clipGroupTextures[1];
            m_currentPing = 0;
            clearTexture(m_pingPongTextures[0]);

            {
                std::vector<CompositeLayerInfo> baseGroup(1, layer);
                baseGroup[0].opacity = 1.0f;
                compositeLayerStack(baseGroup, 1.0f, false, sourceResolver, layerMaskResolver,
                    Color::transparent());
            }

            if (clipEnd > index + 1) {
                const std::vector<CompositeLayerInfo> clippedGroup(
                    layers.begin() + static_cast<ptrdiff_t>(index + 1),
                    layers.begin() + static_cast<ptrdiff_t>(clipEnd));
                compositeLayerStack(clippedGroup, 1.0f, true, sourceResolver, layerMaskResolver,
                    Color::transparent());
            }
            const GLuint clipTexture = m_pingPongTextures[m_currentPing];

            m_pingPongTextures[0] = savedPingPong0;
            m_pingPongTextures[1] = savedPingPong1;
            m_currentPing = savedPing;
            blendPass(m_pingPongTextures[m_currentPing], clipTexture, layer.blendMode,
                effectiveOpacity, false, false, false, backdropColor);
            m_currentPing = 1 - m_currentPing;
            index = clipEnd;
            continue;
        }

        GLuint srcTexture = 0;
        if (layer.isGroup) {
            const bool groupHasRealtimeEffects = m_effectRenderer
                && m_effectRenderer->hasRenderableEffects(layer.effects,
                    ruwa::core::effects::EffectEvaluationSpace::ViewportScreen,
                    /*realtimeOnly=*/true);
            const bool groupHasFinalMask
                = layer.clipMaskLuminanceReveal && layer.externalClipMaskGrid;
            const bool useGroupComposite = !layer.forceIsolation
                && (layer.blendMode != 0 || groupHasRealtimeEffects || groupHasFinalMask);
            if (!useGroupComposite && !layer.forceIsolation) {
                compositeLayerStack(layer.children, effectiveOpacity, false, sourceResolver,
                    layerMaskResolver, backdropColor);
                ++index;
                continue;
            }

            const GLuint savedPingPong0 = m_pingPongTextures[0];
            const GLuint savedPingPong1 = m_pingPongTextures[1];
            const int savedPing = m_currentPing;

            if (useGroupComposite) {
                const GLuint outerComposite = m_pingPongTextures[m_currentPing];
                const size_t frameDepth = m_groupCompositeDepth++;
                GroupCompositeFrame& frame = ensureGroupCompositeFrame(frameDepth);

                m_pingPongTextures[0] = frame.ping[0];
                m_pingPongTextures[1] = frame.ping[1];
                m_currentPing = 0;
                m_gl->glCopyImageSubData(outerComposite, GL_TEXTURE_2D, 0, 0, 0, 0, frame.ping[0],
                    GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height), 1);
                compositeLayerStack(
                    layer.children, 1.0f, false, sourceResolver, layerMaskResolver, backdropColor);
                m_gl->glCopyImageSubData(m_pingPongTextures[m_currentPing], GL_TEXTURE_2D, 0, 0, 0,
                    0, frame.passThrough, GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height), 1);

                m_pingPongTextures[0] = savedPingPong0;
                m_pingPongTextures[1] = savedPingPong1;
                m_currentPing = savedPing;
                const GLuint effected
                    = applyLayerEffects(frame.passThrough, layer, /*realtimeOnly=*/true);
                m_gl->glCopyImageSubData(effected, GL_TEXTURE_2D, 0, 0, 0, 0, frame.effected,
                    GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height), 1);

                m_pingPongTextures[0] = frame.ping[0];
                m_pingPongTextures[1] = frame.ping[1];
                m_currentPing = 0;
                clearTexture(frame.ping[0]);
                compositeLayerStack(layer.children, 1.0f, false, sourceResolver, layerMaskResolver,
                    Color::transparent());
                const GLuint sourceCoverage = m_pingPongTextures[m_currentPing];
                m_gl->glCopyImageSubData(sourceCoverage, GL_TEXTURE_2D, 0, 0, 0, 0,
                    frame.sourceCoverage, GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height), 1);

                m_pingPongTextures[0] = savedPingPong0;
                m_pingPongTextures[1] = savedPingPong1;
                m_currentPing = savedPing;
                const GLuint effectedCoverage
                    = applyLayerEffects(frame.sourceCoverage, layer, /*realtimeOnly=*/true);
                m_gl->glCopyImageSubData(effectedCoverage, GL_TEXTURE_2D, 0, 0, 0, 0,
                    frame.coverage, GL_TEXTURE_2D, 0, 0, 0, 0, static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height), 1);

                const GLuint layerMaskTexture = layerMaskResolver ? layerMaskResolver(layer) : 0;
                blendPass(m_pingPongTextures[m_currentPing], frame.effected, layer.blendMode,
                    effectiveOpacity, layer.preserveBaseAlpha, layer.replaceBase, useSrcAtop,
                    backdropColor, /*useGroupComposite=*/true, frame.passThrough,
                    frame.sourceCoverage, frame.coverage, layerMaskTexture,
                    layer.clipMaskLuminanceReveal);
                m_currentPing = 1 - m_currentPing;
                --m_groupCompositeDepth;
                ++index;
                continue;
            }

            const size_t frameDepth = m_groupCompositeDepth++;
            GroupCompositeFrame& frame = ensureGroupCompositeFrame(frameDepth);
            m_pingPongTextures[0] = frame.ping[0];
            m_pingPongTextures[1] = frame.ping[1];
            m_currentPing = 0;
            clearTexture(m_pingPongTextures[0]);
            srcTexture = compositeLayerStack(layer.children, 1.0f, false, sourceResolver,
                layerMaskResolver, Color::transparent());
            srcTexture = applyLayerEffects(srcTexture, layer, true);

            m_pingPongTextures[0] = savedPingPong0;
            m_pingPongTextures[1] = savedPingPong1;
            m_currentPing = savedPing;
            --m_groupCompositeDepth;
        } else {
            // Distortion / bounds-expanding effects need to sample beyond the
            // visible viewport; the overscan reach path renders this layer's chain
            // on an enlarged source and crops the centre back. It returns 0 when it
            // does not apply (no reach, resolver declined, group/target) — then we
            // take the normal viewport-sized path.
            srcTexture = applyLayerEffectsWithReach(
                layer, m_overscanResolver ? *m_overscanResolver : OverscanSourceResolver {});
            if (!srcTexture) {
                srcTexture = sourceResolver ? sourceResolver(layer) : 0;
                srcTexture = applyLayerEffects(srcTexture, layer, true);
            }
        }

        if (!srcTexture) {
            ++index;
            continue;
        }

        const GLuint layerMaskTexture = layerMaskResolver ? layerMaskResolver(layer) : 0;
        blendPass(m_pingPongTextures[m_currentPing], srcTexture, layer.blendMode, effectiveOpacity,
            layer.preserveBaseAlpha, layer.replaceBase, useSrcAtop, backdropColor,
            /*useGroupComposite=*/false, 0, 0, 0, layerMaskTexture, layer.clipMaskLuminanceReveal);
        m_currentPing = 1 - m_currentPing;
        ++index;
    }

    return m_pingPongTextures[m_currentPing];
}

void GLViewportCompositor::ensureRenderTargets()
{
    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    auto recreate = [&](GLuint& texture) {
        deleteTexture(m_gl, texture);
        texture = createTexture2D(m_gl, m_width, m_height, linear);
    };

    if (!m_pingPongTextures[0] || !m_pingPongTextures[1] || !m_clipGroupTextures[0]
        || !m_clipGroupTextures[1] || !m_transparentTexture) {
        recreate(m_pingPongTextures[0]);
        recreate(m_pingPongTextures[1]);
        recreate(m_clipGroupTextures[0]);
        recreate(m_clipGroupTextures[1]);
        recreate(m_transparentTexture);
        clearTexture(m_transparentTexture);
        return;
    }
}

GLViewportCompositor::GroupCompositeFrame& GLViewportCompositor::ensureGroupCompositeFrame(
    size_t depth)
{
    if (m_groupCompositeFrames.size() <= depth) {
        m_groupCompositeFrames.resize(depth + 1);
    }
    if (!m_groupCompositeFrames[depth]) {
        m_groupCompositeFrames[depth] = std::make_unique<GroupCompositeFrame>();
    }

    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    GroupCompositeFrame& frame = *m_groupCompositeFrames[depth];
    auto ensureTexture = [&](GLuint& texture) {
        if (!texture) {
            texture = createTexture2D(m_gl, m_width, m_height, linear);
        }
    };
    ensureTexture(frame.ping[0]);
    ensureTexture(frame.ping[1]);
    ensureTexture(frame.passThrough);
    ensureTexture(frame.effected);
    ensureTexture(frame.sourceCoverage);
    ensureTexture(frame.coverage);
    return frame;
}

void GLViewportCompositor::destroyGroupCompositeFrames()
{
    for (const auto& frame : m_groupCompositeFrames) {
        deleteTexture(m_gl, frame->ping[0]);
        deleteTexture(m_gl, frame->ping[1]);
        deleteTexture(m_gl, frame->passThrough);
        deleteTexture(m_gl, frame->effected);
        deleteTexture(m_gl, frame->sourceCoverage);
        deleteTexture(m_gl, frame->coverage);
    }
    m_groupCompositeFrames.clear();
    m_groupCompositeDepth = 0;
}

void GLViewportCompositor::clearTexture(GLuint texture)
{
    if (!texture) {
        return;
    }

    GLFboViewportGuard guard(m_gl);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
}

void GLViewportCompositor::blendPass(GLuint baseTexture, GLuint srcTexture, int blendMode,
    float opacity, bool preserveBaseAlpha, bool replaceBase, bool srcAtop,
    const Color& backdropColor, bool useGroupComposite, GLuint groupPassThroughTexture,
    GLuint groupSourceCoverageTexture, GLuint groupCoverageTexture, GLuint layerMaskTexture,
    bool layerMaskLuminanceReveal)
{
    if (!baseTexture || !srcTexture || !m_compositeProgram || !m_compositeProgram->isValid()) {
        return;
    }

    const GLuint targetTexture = m_pingPongTextures[1 - m_currentPing];
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
    m_gl->glDisable(GL_BLEND);

    m_compositeProgram->use();
    m_compositeProgram->setUniform("uBaseTexture", 0);
    m_compositeProgram->setUniform("uSrcTexture", 1);
    m_compositeProgram->setUniform("uClipMaskTexture", 2);
    m_compositeProgram->setUniform("uBlendMode", blendMode);
    m_compositeProgram->setUniform("uOpacity", opacity);
    const bool useLayerMask = layerMaskTexture != 0 && layerMaskLuminanceReveal;
    m_compositeProgram->setUniform("uUseClipMask", useLayerMask ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskAlphaOnly", 0);
    m_compositeProgram->setUniform("uSubtractClipRevealFromSrc", 0);
    m_compositeProgram->setUniform("uClipMaskLuminanceReveal", useLayerMask ? 1 : 0);
    m_compositeProgram->setUniform("uClipMaskEditPreview", 0);
    m_compositeProgram->setUniform("uClipMaskEditReplace", 0);
    m_compositeProgram->setUniform("uClipMaskAsAlphaCap", 0);
    m_compositeProgram->setUniform("uUseProgrammaticBlendBase", 0);
    m_compositeProgram->setUniform("uPreserveBaseAlpha", preserveBaseAlpha ? 1 : 0);
    m_compositeProgram->setUniform("uReplaceBase", replaceBase ? 1 : 0);
    m_compositeProgram->setUniform("uReplaceBaseMixReveal", 0);
    m_compositeProgram->setUniform("uUseGroupComposite", useGroupComposite ? 1 : 0);
    m_compositeProgram->setUniform("uGroupPassThroughTexture", 5);
    m_compositeProgram->setUniform("uGroupCoverageTexture", 6);
    m_compositeProgram->setUniform("uGroupSourceCoverageTexture", 7);
    m_compositeProgram->setUniform("uSrcAtop", srcAtop ? 1 : 0);
    m_compositeProgram->setUniform("uUseRadialReveal", 0);
    m_compositeProgram->setUniform("uRadialRevealInvert", 0);
    m_compositeProgram->setUniform("uRadialRevealOrigin", 0.0f, 0.0f);
    m_compositeProgram->setUniform("uRadialRevealRadius", 0.0f);
    m_compositeProgram->setUniform("uRadialRevealFeather", 0.0f);
    m_compositeProgram->setUniform(
        "uBackdropColor", backdropColor.r, backdropColor.g, backdropColor.b, backdropColor.a);
    m_compositeProgram->setUniform("uTileWorldOrigin", 0.0f, 0.0f);

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, baseTexture);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, srcTexture);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, useLayerMask ? layerMaskTexture : 0);
    m_gl->glActiveTexture(GL_TEXTURE5);
    m_gl->glBindTexture(GL_TEXTURE_2D, useGroupComposite ? groupPassThroughTexture : 0);
    m_gl->glActiveTexture(GL_TEXTURE6);
    m_gl->glBindTexture(GL_TEXTURE_2D, useGroupComposite ? groupCoverageTexture : 0);
    m_gl->glActiveTexture(GL_TEXTURE7);
    m_gl->glBindTexture(GL_TEXTURE_2D, useGroupComposite ? groupSourceCoverageTexture : 0);

    m_gl->glBindVertexArray(m_emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);

    m_gl->glActiveTexture(GL_TEXTURE7);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE6);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE5);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint GLViewportCompositor::applyLayerEffects(
    GLuint sourceTexture, const CompositeLayerInfo& layer, bool realtimeOnly)
{
    return applyLayerEffectsSized(
        sourceTexture, layer, m_width, m_height, m_effectRegion, realtimeOnly);
}

GLuint GLViewportCompositor::applyLayerEffectsSized(GLuint sourceTexture,
    const CompositeLayerInfo& layer, uint32_t width, uint32_t height,
    const ruwa::core::effects::EffectRegionFrame& region, bool realtimeOnly)
{
    if (!m_effectRenderer || !m_effectRenderer->isInitialized() || !sourceTexture
        || layer.effects.isEmpty() || width == 0 || height == 0) {
        return sourceTexture;
    }

    EffectChainRequest req;
    req.sourceTexture = sourceTexture;
    req.width = width;
    req.height = height;
    req.effects = &layer.effects;
    req.space = ruwa::core::effects::EffectEvaluationSpace::ViewportScreen;
    req.realtimeOnly = realtimeOnly;
    req.spaceScale = m_spaceScale;
    req.region = region;
    req.liveEditedEffectId = layer.liveEditedEffectId;
    req.liveEditSourceVariant = layer.liveEffectEditGeneration << 8;
    return m_effectRenderer->applyEffects(req);
}

int GLViewportCompositor::layerReachScreenPixels(const CompositeLayerInfo& layer) const
{
    if (layer.effects.isEmpty()) {
        return 0;
    }
    // realtimeOnly=true: a preview-disabled effect is not applied to this preview,
    // so it must not widen the reach (matches applyLayerEffects' realtimeOnly).
    const int reachDoc
        = ruwa::core::effects::EffectCoverageResolver::stableLiveEditNeighborhoodPadPixels(
            layer.effects, layer.liveEditedEffectId, layer.liveEditedEffectParamKey,
            /*realtimeOnly=*/true);
    if (reachDoc <= 0) {
        return 0;
    }
    const float zoom = m_spaceScale > 0.0f ? m_spaceScale : 1.0f;
    int padScreen = static_cast<int>(std::ceil(static_cast<float>(reachDoc) * zoom));
    // Keep the enlarged source within the resource bound on each axis.
    const int maxPadX = (kMaxOverscanDim - static_cast<int>(m_width)) / 2;
    const int maxPadY = (kMaxOverscanDim - static_cast<int>(m_height)) / 2;
    const int maxPad = std::max(0, std::min(maxPadX, maxPadY));
    padScreen = std::min(padScreen, maxPad);
    return std::max(0, padScreen);
}

GLuint GLViewportCompositor::applyLayerEffectsWithReach(
    const CompositeLayerInfo& layer, const OverscanSourceResolver& overscanResolver)
{
    if (!overscanResolver || layer.isGroup || !m_effectRenderer
        || !m_effectRenderer->isInitialized()) {
        return 0;
    }
    const int pad = layerReachScreenPixels(layer);
    if (pad <= 0) {
        return 0;
    }

    const OverscanLayerSource source = overscanResolver(layer, pad, pad);
    if (!source.texture || !source.region.valid) {
        return 0; // resolver declined -> caller falls back to the viewport path.
    }

    const uint32_t overscanWidth = m_width + static_cast<uint32_t>(pad) * 2u;
    const uint32_t overscanHeight = m_height + static_cast<uint32_t>(pad) * 2u;

    // Run the WHOLE chain at overscan size (so multi-effect chains stay correct,
    // exactly like the document-tile neighbourhood path) with the enlarged frame.
    const GLuint overscanResult = applyLayerEffectsSized(
        source.texture, layer, overscanWidth, overscanHeight, source.region, /*realtimeOnly=*/true);
    if (!overscanResult) {
        return 0;
    }

    // Crop the centre [pad, pad, m_width, m_height] back to a viewport-sized
    // texture. The overscan surface is centre-anchored on the same camera, so this
    // region is pixel-aligned with the normal viewport source.
    const TextureParams linear { GL_LINEAR, GL_LINEAR };
    if (!m_overscanCropTexture || m_overscanCropWidth != m_width
        || m_overscanCropHeight != m_height) {
        deleteTexture(m_gl, m_overscanCropTexture);
        m_overscanCropTexture = createTexture2D(m_gl, m_width, m_height, linear);
        m_overscanCropWidth = m_width;
        m_overscanCropHeight = m_height;
    }
    if (!m_overscanCropTexture) {
        return 0;
    }

    GLFboViewportGuard guard(m_gl);
    // Read from the overscan result, blit its centre into the crop texture.
    GLuint readFbo = 0;
    m_gl->glGenFramebuffers(1, &readFbo);
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    m_gl->glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, overscanResult, 0);
    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_overscanCropTexture, 0);
    m_gl->glBlitFramebuffer(pad, pad, pad + static_cast<int>(m_width),
        pad + static_cast<int>(m_height), 0, 0, static_cast<int>(m_width),
        static_cast<int>(m_height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    m_gl->glDeleteFramebuffers(1, &readFbo);

    return m_overscanCropTexture;
}

} // namespace aether
