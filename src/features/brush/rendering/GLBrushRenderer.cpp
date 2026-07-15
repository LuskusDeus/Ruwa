// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   B R U S H   R E N D E R E R
// ==========================================================================

#include "features/brush/rendering/GLBrushRenderer.h"
#include "features/brush/color/PigmentLutResource.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"
#include "shared/tiles/DabShapeFalloff.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/brush/rendering/DabShapeCache.h"
#include "features/brush/rendering/WetPigmentGlsl.h"
#include "features/brush/rendering/WetShaderSources.h"
#include <QImage>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
namespace aether {

namespace {

constexpr int kRebuildBatchMaxDabs = 64;

QString glsl(std::string_view source)
{
    return QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()));
}

void bindWetLuts(QOpenGLFunctions_4_5_Core* gl, GLShaderProgram* program,
    const GLuint textures[wet_pigment_gpu::kLutTextureCount])
{
    for (std::size_t i = 0; i < wet_pigment_gpu::kLutTextureCount; ++i) {
        const int unit = wet_pigment_gpu::kLutTextureUnits[i];
        program->setUniform(wet_pigment_gpu::kLutSamplerNames[i].data(), unit);
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(GL_TEXTURE_3D, textures[i]);
    }
    gl->glActiveTexture(GL_TEXTURE0);
}

void bindWetReservoir(QOpenGLFunctions_4_5_Core* gl, GLShaderProgram* program,
    const GLuint textures[wet_pigment_gpu::kReservoirPlaneCount], GLuint sampler)
{
    for (std::size_t i = 0; i < wet_pigment_gpu::kReservoirPlaneCount; ++i) {
        const int unit = wet_pigment_gpu::kReservoirTextureUnits[i];
        program->setUniform(wet_pigment_gpu::kReservoirSamplerNames[i].data(), unit);
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        if (sampler != 0)
            gl->glBindSampler(unit, sampler);
    }
    gl->glActiveTexture(GL_TEXTURE0);
}

bool attachWetReservoir(
    QOpenGLFunctions_4_5_Core* gl, const GLuint textures[wet_pigment_gpu::kReservoirPlaneCount])
{
    std::array<GLenum, wet_pigment_gpu::kReservoirPlaneCount> drawBuffers {};
    for (std::size_t i = 0; i < wet_pigment_gpu::kReservoirPlaneCount; ++i) {
        const GLenum attachment
            = GL_COLOR_ATTACHMENT0 + wet_pigment_gpu::kReservoirAttachmentIndices[i];
        drawBuffers[i] = attachment;
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, textures[i], 0);
    }
    gl->glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    return gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void restoreSingleColorTarget(QOpenGLFunctions_4_5_Core* gl)
{
    constexpr GLenum target = GL_COLOR_ATTACHMENT0;
    gl->glDrawBuffers(1, &target);
    for (std::size_t i = 1; i < wet_pigment_gpu::kReservoirPlaneCount; ++i) {
        gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), GL_TEXTURE_2D, 0, 0);
    }
}

void unbindWetTextures(QOpenGLFunctions_4_5_Core* gl)
{
    for (const int unit : wet_pigment_gpu::kReservoirTextureUnits) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glBindSampler(unit, 0);
    }
    for (const int unit : wet_pigment_gpu::kLutTextureUnits) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(GL_TEXTURE_3D, 0);
    }
    gl->glActiveTexture(GL_TEXTURE0);
}

void restoreDefaultPremultipliedBlendState(QOpenGLFunctions_4_5_Core* gl)
{
    if (!gl) {
        return;
    }

    gl->glDisable(GL_BLEND);
    gl->glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    gl->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

GLuint resolveDabTextureId(QOpenGLFunctions_4_5_Core* gl, const TileBrush& brush)
{
    if (!brush.dabCustomImagePath().isEmpty()) {
        return DabShapeCache::instance().getCustomTextureId(gl, brush.dabCustomImagePath(),
            brush.dabThreshold(), brush.dabCompression(), brush.dabInterpolation());
    }
    return DabShapeCache::instance().getTextureId(gl, brush.dabType());
}

float dabCoverageExtent(const TileBrush& brush, float radius, float hardness, float roundness,
    float angleDegrees, bool includeRasterPadding = false)
{
    float extent
        = brush.dabCoverageExtent(radius, hardness, roundness, angleDegrees, includeRasterPadding);
    if (brush.dabType() > 0 && !brush.hasDabShapeMask() && radius > 0.0f) {
        int fallbackWidth = 0;
        int fallbackHeight = 0;
        DabShapeCache::instance().getShapeSize(brush.dabType(), fallbackWidth, fallbackHeight);
        const float fallbackPad
            = dab_shape_falloff::shapePad(fallbackWidth, fallbackHeight, hardness);
        extent += radius * fallbackPad * 1.41421356237f;
    }
    return extent;
}

float dabRotationInvariantCoverageExtent(const TileBrush& brush, float radius, float hardness,
    float roundness, bool includeRasterPadding = false)
{
    float extent = brush.dabRotationInvariantCoverageExtent(
        radius, hardness, roundness, includeRasterPadding);
    if (brush.dabType() > 0 && !brush.hasDabShapeMask() && radius > 0.0f) {
        int fallbackWidth = 0;
        int fallbackHeight = 0;
        DabShapeCache::instance().getShapeSize(brush.dabType(), fallbackWidth, fallbackHeight);
        const float fallbackPad
            = dab_shape_falloff::shapePad(fallbackWidth, fallbackHeight, hardness);
        extent += radius * fallbackPad * 1.41421356237f;
    }
    return extent;
}

// Square reservoir texture pair. Grows geometrically across strokes to
// minimize reallocation when a slightly larger brush appears. The physical
// texture size may be larger than the logical (active) side; shader uses
// uReservoirHalf + uInvReservoirPhys so that's safe.
//
// Format is GL_RGBA16F (half-float) to keep the pickup round-trip
// high-precision. At low pickup rates each dab does mix(reservoir, canvas,
// 0.05ish) and writes back into the reservoir, which in 8-bit storage
// accumulates quantization error across dabs into visible color banding.
// 16-bit float gives ~1000+ perceptual levels per channel — round-trip
// noise stays well below the eventual 8-bit quantization on flatten.
// The reservoir is never copied via glCopyImageSubData; it's only shader-
// rendered and shader-sampled, so no format-compat issues.
bool reservoirTexturesComplete(const GLuint reservoirTex[2][wet_pigment_gpu::kReservoirPlaneCount])
{
    for (int side = 0; side < 2; ++side)
        for (std::size_t plane = 0; plane < wet_pigment_gpu::kReservoirPlaneCount; ++plane)
            if (reservoirTex[side][plane] == 0)
                return false;
    return true;
}

bool ensureSmudgeReservoirTextures(QOpenGLFunctions_4_5_Core* gl,
    GLuint reservoirTex[2][wet_pigment_gpu::kReservoirPlaneCount], GLsizei& physSize,
    GLsizei needSize)
{
    if (needSize <= 0)
        return false;
    if (reservoirTexturesComplete(reservoirTex) && physSize >= needSize) {
        return true;
    }
    GLsizei sz = std::max(physSize, needSize);
    auto createOrResize = [&](GLuint& tex) -> bool {
        if (tex == 0) {
            gl->glGenTextures(1, &tex);
            if (tex == 0)
                return false;
            gl->glBindTexture(GL_TEXTURE_2D, tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            gl->glBindTexture(GL_TEXTURE_2D, tex);
        }
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, sz, sz, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        gl->glClearTexImage(tex, 0, GL_RGBA, GL_FLOAT, nullptr);
        return true;
    };
    for (int side = 0; side < 2; ++side) {
        for (std::size_t plane = 0; plane < wet_pigment_gpu::kReservoirPlaneCount; ++plane) {
            if (!createOrResize(reservoirTex[side][plane])) {
                gl->glBindTexture(GL_TEXTURE_2D, 0);
                return false;
            }
        }
    }
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    physSize = sz;
    return true;
}

// Compute the reservoir logical side length needed to enclose the configured
// brush footprint at every possible angle. Per-dab ROI calculation below still
// uses the actual dynamic radius/shape values and never assumes this reservoir
// bound also encloses the rendered dab.
GLsizei computeSmudgeReservoirLogicalSize(const TileBrush& brush)
{
    const float maxRadius = std::max(0.5f, brush.radius());
    const float extent = dabRotationInvariantCoverageExtent(
        brush, maxRadius, brush.hardness(), brush.roundness(), true);
    // +2 for safe linear-sampling at the borders.
    GLsizei side = static_cast<GLsizei>(std::ceil(2.0f * extent)) + 2;
    return std::max<GLsizei>(side, 4);
}

const QString kBatchRebuildVert
    = QStringLiteral("#version 450 core\n"
                     "uniform vec2 uQuadMin;\n"
                     "uniform vec2 uQuadMax;\n"
                     "out vec2 fragPixelCoord;\n"
                     "vec2 positions[6] = vec2[](\n"
                     "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                     "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                     ");\n"
                     "void main() {\n"
                     "    vec2 t = positions[gl_VertexID];\n"
                     "    vec2 pixel = mix(uQuadMin, uQuadMax, t);\n"
                     "    gl_Position = vec4(pixel / 128.0 - 1.0, 0.0, 1.0);\n"
                     "    fragPixelCoord = pixel;\n"
                     "}\n");

const QString kBatchRebuildFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform int uDabCount;\n"
    "uniform vec2 uDabCenter[64];\n"
    "uniform vec4 uDabParams[64]; // radius, hardness, roundness, angle\n"
    "uniform vec4 uDabColor[64];  // premultiplied rgba\n"
    "uniform int uBlendMode; // 0=src-over, 1=max\n"
    "uniform sampler2D uMaskTexture;\n"
    "uniform sampler2D uTextureTile;\n"
    "uniform sampler2D uDabShapeTexture;\n"
    "uniform int uUseMask;\n"
    "uniform int uMaskAffectsAlpha;\n"
    "uniform int uUseTexture;\n"
    "uniform int uUseDabShapeTexture;\n"
    "uniform vec2 uDabShapeScale;\n"
    "uniform float uTextureEdgeBoost;\n"
    "uniform float uDabSoftEdgeRadiusTexels;\n"
    "uniform float uInvTileSize;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec4 outColor;\n"
    "float hardnessFalloff(float edgeDistance, float hardness) {\n"
    "    hardness = clamp(hardness, 0.0, 1.0);\n"
    "    float softness = max(1.0 - hardness, 0.0);\n"
    "    if (edgeDistance <= 0.0) return 0.0;\n"
    "    if (softness <= 0.001) return 1.0;\n"
    "    return smoothstep(0.0, softness, edgeDistance);\n"
    "}\n"
    "vec2 sampleDabShapeSafe(vec2 uv) {\n"
    "    vec2 clampedUv = clamp(uv, vec2(0.0), vec2(1.0));\n"
    "    vec2 shape = texture(uDabShapeTexture, clampedUv).rg;\n"
    "    vec2 outsideUv = max(max(-uv, uv - vec2(1.0)), vec2(0.0)) * 2.0;\n"
    "    if (outsideUv.x > 0.0 || outsideUv.y > 0.0) {\n"
    "        shape.r = 0.0;\n"
    "        shape.g = clamp(shape.g + length(outsideUv), 0.0, 1.0);\n"
    "    }\n"
    "    return shape;\n"
    "}\n"
    "float customDabSoftEdgeShapePad(float hardness) {\n"
    "    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);\n"
    "    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));\n"
    "    vec2 safeSize = max(texSize - vec2(1.0), vec2(1.0));\n"
    "    vec2 pad = (2.0 * uDabSoftEdgeRadiusTexels * softness) / safeSize;\n"
    "    return max(pad.x, pad.y);\n"
    "}\n"
    "float sampleCustomDabEdgeFalloff(vec2 uv, float hardness) {\n"
    "    vec2 shape = sampleDabShapeSafe(uv);\n"
    "    float baseAlpha = clamp(shape.r, 0.0, 1.0);\n"
    "    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);\n"
    "    if (softness <= 0.0001) return baseAlpha;\n"
    "    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));\n"
    "    float edgeWidth = (2.0 * uDabSoftEdgeRadiusTexels * softness) / max(max(texSize.x, "
    "texSize.y), 1.0);\n"
    "    if (edgeWidth <= 0.0001) return baseAlpha;\n"
    "    float halfTexel = 1.0 / max(max(texSize.x, texSize.y), 1.0);\n"
    "    float edgeDistance = max(shape.g - halfTexel, 0.0);\n"
    "    float signedDistance = baseAlpha >= 0.5 ? edgeDistance : -edgeDistance;\n"
    "    float coverage = smoothstep(0.0, 1.0, (signedDistance + edgeWidth) / (2.0 * edgeWidth));\n"
    "    return clamp(mix(coverage, baseAlpha, clamp(hardness, 0.0, 1.0)), 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    float maskScale = 1.0;\n"
    "    if (uUseMask != 0) {\n"
    "        maskScale = texture(uMaskTexture, fragPixelCoord * uInvTileSize).a;\n"
    "        if (maskScale <= 0.0) { outColor = vec4(0.0); return; }\n"
    "    }\n"
    "    float textureA = 1.0;\n"
    "    if (uUseTexture != 0) {\n"
    "        textureA = texture(uTextureTile, fragPixelCoord * uInvTileSize).r;\n"
    "        if (textureA <= 0.0) { outColor = vec4(0.0); return; }\n"
    "    }\n"
    "    vec4 accum = vec4(0.0);\n"
    "    for (int i = 0; i < 64; ++i) {\n"
    "        if (i >= uDabCount) break;\n"
    "        vec2 delta = fragPixelCoord - uDabCenter[i];\n"
    "        float radius = uDabParams[i].x;\n"
    "        float hardness = uDabParams[i].y;\n"
    "        float roundness = max(0.01, clamp(uDabParams[i].z, 0.0, 1.0));\n"
    "        float angle = uDabParams[i].w;\n"
    "        float c = cos(angle);\n"
    "        float s = sin(angle);\n"
    "        vec2 local = vec2(delta.x * c + delta.y * s,\n"
    "                         (-delta.x * s + delta.y * c) / roundness);\n"
    "        vec2 shapeLocal = local / radius;\n"
    "        shapeLocal /= max(uDabShapeScale, vec2(0.0001));\n"
    "        float edgeDistance = 0.0;\n"
    "        float edgeFactor = 0.0;\n"
    "        if (uUseDabShapeTexture != 0) {\n"
    "            float shapePad = customDabSoftEdgeShapePad(hardness);\n"
    "            if (abs(shapeLocal.x) > 1.0 + shapePad || abs(shapeLocal.y) > 1.0 + shapePad) "
    "continue;\n"
    "            vec2 uv = (shapeLocal + 1.0) * 0.5;\n"
    "            float baseAlpha = sampleDabShapeSafe(uv).r;\n"
    "            float falloff = sampleCustomDabEdgeFalloff(uv, hardness);\n"
    "            if (falloff <= 0.0) continue;\n"
    "            edgeFactor = max(0.0, falloff - baseAlpha);\n"
    "            float dabTextureA = textureA;\n"
    "            if (uUseTexture != 0 && uTextureEdgeBoost > 0.0) {\n"
    "                float contrast = 1.0 + edgeFactor * uTextureEdgeBoost * 8.0;\n"
    "                dabTextureA = clamp(0.5 + (dabTextureA - 0.5) * contrast, 0.0, 1.0);\n"
    "            }\n"
    "            float alpha = uDabColor[i].a * falloff * dabTextureA;\n"
    "            float colorScale = maskScale;\n"
    "            if (uMaskAffectsAlpha != 0) { colorScale = 1.0; }\n"
    "            if (alpha <= 0.0) continue;\n"
    "            vec4 src = vec4(uDabColor[i].rgb * falloff * dabTextureA * colorScale, alpha);\n"
    "            if (uBlendMode == 0) {\n"
    "                accum = src + accum * (1.0 - src.a);\n"
    "            } else {\n"
    "                if (src.a > accum.a) accum = src;\n"
    "            }\n"
    "            continue;\n"
    "        } else {\n"
    "            float t = length(local) / radius;\n"
    "            if (t > 1.0) continue;\n"
    "            edgeDistance = max(0.0, 1.0 - t);\n"
    "            edgeFactor = smoothstep(clamp(hardness + 0.05, 0.05, 0.95), 1.0, t);\n"
    "        }\n"
    "        float falloff = hardnessFalloff(edgeDistance, hardness);\n"
    "        float dabTextureA = textureA;\n"
    "        if (uUseTexture != 0 && uTextureEdgeBoost > 0.0) {\n"
    "            float contrast = 1.0 + edgeFactor * uTextureEdgeBoost * 8.0;\n"
    "            dabTextureA = clamp(0.5 + (dabTextureA - 0.5) * contrast, 0.0, 1.0);\n"
    "        }\n"
    "        float alpha = uDabColor[i].a * falloff * dabTextureA;\n"
    "        float colorScale = maskScale;\n"
    "        if (uMaskAffectsAlpha != 0) { colorScale = 1.0; }\n"
    "        if (alpha <= 0.0) continue;\n"
    "        vec4 src = vec4(uDabColor[i].rgb * falloff * dabTextureA * colorScale, alpha);\n"
    "        if (uBlendMode == 0) {\n"
    "            accum = src + accum * (1.0 - src.a);\n"
    "        } else {\n"
    "            if (src.a > accum.a) accum = src;\n"
    "        }\n"
    "    }\n"
    "    outColor = accum;\n"
    "}\n");

const QString kBrushStampVert
    = QStringLiteral("#version 450 core\n"
                     "uniform vec2 uQuadMin;\n"
                     "uniform vec2 uQuadMax;\n"
                     "out vec2 fragPixelCoord;\n"
                     "vec2 positions[6] = vec2[](\n"
                     "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                     "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                     ");\n"
                     "void main() {\n"
                     "    vec2 t = positions[gl_VertexID];\n"
                     "    vec2 pixel = mix(uQuadMin, uQuadMax, t);\n"
                     "    gl_Position = vec4(pixel / 128.0 - 1.0, 0.0, 1.0);\n"
                     "    fragPixelCoord = pixel;\n"
                     "}\n");

const QString kBlurPassVert = QStringLiteral("#version 450 core\n"
                                             "out vec2 fragTexCoord;\n"
                                             "vec2 positions[6] = vec2[](\n"
                                             "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                                             "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                                             ");\n"
                                             "void main() {\n"
                                             "    vec2 uv = positions[gl_VertexID];\n"
                                             "    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);\n"
                                             "    fragTexCoord = uv;\n"
                                             "}\n");

const QString kBlurPassFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform sampler2D uSourceTexture;\n"
    "uniform vec2 uInvSourceSize;\n"
    "uniform vec2 uTexelStep;\n"
    "uniform float uBlurRadius;\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "const int kMaxPairs = 25;\n"
    "void buildAxisKernel(out float axisOffsets[kMaxPairs],\n"
    "                     out float axisWeights[kMaxPairs],\n"
    "                     out int sampleCount) {\n"
    "    float sigma = max(uBlurRadius * 0.5, 0.5);\n"
    "    float invTwoSigma2 = 1.0 / (2.0 * sigma * sigma);\n"
    "    int supportHalf = int(ceil(min(uBlurRadius * 1.22474487139, 24.0)));\n"
    "    supportHalf = max(supportHalf, 1);\n"
    "    sampleCount = 0;\n"
    "    axisOffsets[sampleCount] = 0.0;\n"
    "    axisWeights[sampleCount] = 1.0;\n"
    "    ++sampleCount;\n"
    "    for (int tap = 1; tap <= supportHalf && sampleCount + 1 < kMaxPairs; tap += 2) {\n"
    "        float pairOffset = float(tap);\n"
    "        float pairWeight = exp(-(pairOffset * pairOffset) * invTwoSigma2);\n"
    "        if (tap < supportHalf) {\n"
    "            float nextOffset = float(tap + 1);\n"
    "            float nextWeight = exp(-(nextOffset * nextOffset) * invTwoSigma2);\n"
    "            pairWeight += nextWeight;\n"
    "            pairOffset += nextWeight / pairWeight;\n"
    "        }\n"
    "        axisOffsets[sampleCount] = -pairOffset;\n"
    "        axisWeights[sampleCount] = pairWeight;\n"
    "        ++sampleCount;\n"
    "        axisOffsets[sampleCount] = pairOffset;\n"
    "        axisWeights[sampleCount] = pairWeight;\n"
    "        ++sampleCount;\n"
    "    }\n"
    "}\n"
    "void main() {\n"
    "    float axisOffsets[kMaxPairs];\n"
    "    float axisWeights[kMaxPairs];\n"
    "    int sampleCount = 0;\n"
    "    buildAxisKernel(axisOffsets, axisWeights, sampleCount);\n"
    "    vec2 centeredUv = fragTexCoord;\n"
    "    vec4 accum = vec4(0.0);\n"
    "    float totalWeight = 0.0;\n"
    "    for (int i = 0; i < kMaxPairs; ++i) {\n"
    "        if (i >= sampleCount) continue;\n"
    "        float w = axisWeights[i];\n"
    "        vec2 uv = centeredUv + uTexelStep * axisOffsets[i];\n"
    "        accum += texture(uSourceTexture, uv) * w;\n"
    "        totalWeight += w;\n"
    "    }\n"
    "    outColor = totalWeight > 0.0 ? (accum / totalWeight) : vec4(0.0);\n"
    "}\n");

const QString kBlurApplyFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uBrushCenter;\n"
    "uniform float uBrushRadius;\n"
    "uniform float uBrushHardness;\n"
    "uniform float uBrushRoundness;\n"
    "uniform float uBrushAngleRad;\n"
    "uniform float uBrushAlpha;\n"
    "uniform sampler2D uOriginalTexture;\n"
    "uniform sampler2D uBlurTexture;\n"
    "uniform sampler2D uMaskTexture;\n"
    "uniform int   uUseMask;\n"
    "uniform float uBlurRadius;\n"
    "uniform vec2  uInvBlurSize;\n"
    "uniform vec2  uInvTileSize;\n"
    "uniform vec2  uTileOriginPx;\n"
    "uniform vec2  uRoiOriginPx;\n"
    "uniform vec2  uInvRoiSize;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec4 outColor;\n"
    "const int kMaxPairs = 25;\n"
    "vec2 worldToUv(vec2 worldPixelCoord) {\n"
    "    return (worldPixelCoord - uRoiOriginPx) * uInvRoiSize;\n"
    "}\n"
    "float stableDither(vec2 pixelCoord) {\n"
    "    vec2 p = floor(pixelCoord);\n"
    "    float n = fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));\n"
    "    return n - 0.5;\n"
    "}\n"
    "void buildAxisKernel(out float axisOffsets[kMaxPairs],\n"
    "                     out float axisWeights[kMaxPairs],\n"
    "                     out int sampleCount) {\n"
    "    float sigma = max(uBlurRadius * 0.5, 0.5);\n"
    "    float invTwoSigma2 = 1.0 / (2.0 * sigma * sigma);\n"
    "    int supportHalf = int(ceil(min(uBlurRadius * 1.22474487139, 24.0)));\n"
    "    supportHalf = max(supportHalf, 1);\n"
    "    sampleCount = 0;\n"
    "    axisOffsets[sampleCount] = 0.0;\n"
    "    axisWeights[sampleCount] = 1.0;\n"
    "    ++sampleCount;\n"
    "    for (int tap = 1; tap <= supportHalf && sampleCount + 1 < kMaxPairs; tap += 2) {\n"
    "        float pairOffset = float(tap);\n"
    "        float pairWeight = exp(-(pairOffset * pairOffset) * invTwoSigma2);\n"
    "        if (tap < supportHalf) {\n"
    "            float nextOffset = float(tap + 1);\n"
    "            float nextWeight = exp(-(nextOffset * nextOffset) * invTwoSigma2);\n"
    "            pairWeight += nextWeight;\n"
    "            pairOffset += nextWeight / pairWeight;\n"
    "        }\n"
    "        axisOffsets[sampleCount] = -pairOffset;\n"
    "        axisWeights[sampleCount] = pairWeight;\n"
    "        ++sampleCount;\n"
    "        axisOffsets[sampleCount] = pairOffset;\n"
    "        axisWeights[sampleCount] = pairWeight;\n"
    "        ++sampleCount;\n"
    "    }\n"
    "}\n"
    "void main() {\n"
    "    vec2 worldPixelCoord = uTileOriginPx + fragPixelCoord;\n"
    "    vec2 sourceUv = worldToUv(worldPixelCoord);\n"
    "    vec4 original = texture(uOriginalTexture, sourceUv);\n"
    "    vec2 delta = fragPixelCoord - uBrushCenter;\n"
    "    float c = cos(uBrushAngleRad);\n"
    "    float s = sin(uBrushAngleRad);\n"
    "    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));\n"
    "    vec2 local = vec2(\n"
    "        delta.x * c + delta.y * s,\n"
    "        (-delta.x * s + delta.y * c) / roundness\n"
    "    );\n"
    "    float t = length(local) / uBrushRadius;\n"
    "    if (t > 1.0) {\n"
    "        discard;\n"
    "    }\n"
    "    float edgeDistance = max(0.0, 1.0 - t);\n"
    "    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);\n"
    "    float falloff = softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, edgeDistance);\n"
    "    if (falloff <= 0.0) {\n"
    "        discard;\n"
    "    }\n"
    "    float maskScale = 1.0;\n"
    "    if (uUseMask != 0) {\n"
    "        maskScale = texture(uMaskTexture, fragPixelCoord * uInvTileSize).a;\n"
    "        if (maskScale <= 0.0) { discard; }\n"
    "    }\n"
    "    float axisOffsets[kMaxPairs];\n"
    "    float axisWeights[kMaxPairs];\n"
    "    int sampleCount = 0;\n"
    "    buildAxisKernel(axisOffsets, axisWeights, sampleCount);\n"
    "    vec4 blurred = vec4(0.0);\n"
    "    float totalWeight = 0.0;\n"
    "    for (int i = 0; i < kMaxPairs; ++i) {\n"
    "        if (i >= sampleCount) continue;\n"
    "        float w = axisWeights[i];\n"
    "        vec2 uv = sourceUv + vec2(0.0, uInvBlurSize.y * axisOffsets[i]);\n"
    "        blurred += texture(uBlurTexture, uv) * w;\n"
    "        totalWeight += w;\n"
    "    }\n"
    "    if (totalWeight <= 0.0) {\n"
    "        discard;\n"
    "    }\n"
    "    blurred /= totalWeight;\n"
    "    outColor = mix(original, blurred, uBrushAlpha * falloff * maskScale);\n"
    "    if (outColor.a > 0.0 && outColor.a < 1.0) {\n"
    "        float dither = stableDither(worldPixelCoord) / 255.0;\n"
    "        float oldAlpha = outColor.a;\n"
    "        float newAlpha = clamp(oldAlpha + dither, 0.0, 1.0);\n"
    "        if (oldAlpha > 1e-6) {\n"
    "            vec3 straightColor = outColor.rgb / oldAlpha;\n"
    "            outColor = vec4(straightColor * newAlpha, newAlpha);\n"
    "        } else {\n"
    "            outColor.a = newAlpha;\n"
    "        }\n"
    "    }\n"
    "}\n");

// ---------------------------------------------------------------------------
//   Wet-rate spacing normalization
// ---------------------------------------------------------------------------
//   The wet sliders (blending/dilution/spread) are exponential mixing rates.
//   Applying the slider value verbatim on every dab makes the brush behavior
//   a function of dab spacing: at 1% spacing a point on the canvas receives
//   ~100 overlapping dabs, so even spread=0.1 converges the whole stroke to
//   the pen color (canvas -> deposit -> reservoir is a positive feedback
//   loop with the pen as its fixed point). Instead, treat the slider as the
//   per-dab rate AT A REFERENCE TRAVEL of half the brush radius and convert
//   to the dab's actual travel:
//       perDab = 1 - (1 - rate)^(dabDist / (0.5 * radius))
//   Dense dabs then each contribute proportionally less and sparse dabs
//   proportionally more, so a stroke mixes by distance travelled, not by
//   dab count. A stationary dab (dist 0) contributes nothing, which keeps a
//   held-in-place wet brush from saturating to the pen color.
inline float wetRatePerDab(float rate, float dabDistPx, float radiusPx)
{
    const float r = std::clamp(rate, 0.0f, 1.0f);
    if (r <= 0.0f) {
        return 0.0f;
    }
    if (r >= 1.0f) {
        return 1.0f;
    }
    const float refDist = std::max(0.5f * radiusPx, 1.0f);
    const float t = std::min(dabDistPx / refDist, 16.0f);
    return 1.0f - std::pow(1.0f - r, t);
}

// Per-dab weight of the layering ("buildup") wet deposit. ROUND 15f: the
// buildup brush lays a translucent coat of the reservoir color OVER the
// canvas (apply-shader uCoatPerDab branch) instead of the legacy lerp.
// `body` = 1 - 0.85*buildup is the coverage ONE PASS should accumulate
// (buildup 100% => 15% per pass => ~15-18 passes to visually reach the
// pen color). "One pass" over a point is ~2R of travel under the
// footprint = 4 of wetRatePerDab's half-radius reference steps, so the
// per-half-radius rate that compounds to `body` over a pass is
// 1 - (1-body)^(1/4). Spacing-normalized like every other wet rate:
// speed/dab-density independent, a held-in-place brush deposits nothing,
// self-crossings get twice the travel => exactly one extra coat.
// The FIRST dab (zero travel) deposits NOTHING — deliberately. Any
// fixed pre-print is a single discrete stamp of the dab texture, and
// against the body of the stroke (a soft wash accumulated from
// thousands of micro-deposits along the travel) one crisp imprint
// always reads as a foreign object (user bug, twice: a full-pass
// pre-print = near-opaque blob, then even a half-radius one still
// visibly popped). The stroke start builds up naturally as the
// footprint sweeps off it. KNOWN TRADE-OFF: a stationary single tap
// with a buildup brush leaves ~nothing; if a visible tap is ever
// needed, print one coat at stroke END when total travel ~= 0 — do NOT
// reintroduce a first-dab stamp.
// radiusPx MUST be the brush's BASE radius (brush.radius(), the size
// slider), NOT the per-dab pressure-scaled radius. With size-by-
// pressure the press point starts at a near-zero tip radius, so "one
// pass = 2R of travel" normalized by the DAB radius counted the slow
// low-pressure start crawl as dozens of passes of a tiny brush over one
// spot — a saturated dab-texture blob at every press point while the
// full-radius body stayed a pale wash (user bug, the third blob round).
// Base-radius normalization makes the coat per pixel of travel constant
// along the stroke: thin light-pressure passages lay the same density
// over a narrower footprint. NOTE: legacy-vs-buildup branch selection
// happens at the CALL SITES (uCoatPerDab = -1 when buildup is 0) — a
// returned 0 means "buildup brush, deposit nothing on this dab", never
// the legacy branch.
inline float buildupCoatPerDab(float buildup, float dabDistPx, float radiusPx)
{
    const float b = std::clamp(buildup, 0.0f, 1.0f);
    if (b <= 0.001f) {
        return 0.0f;
    }
    const float body = 1.0f - 0.85f * b;
    const float ratePerHalfRadius = 1.0f - std::pow(1.0f - body, 0.25f);
    return wetRatePerDab(ratePerHalfRadius, dabDistPx, radiusPx);
}

// ---------------------------------------------------------------------------
//   Smudge tool (carry-buffer / reservoir) shaders
// ---------------------------------------------------------------------------
//   Each dab does two passes:
//     1. Pickup — sample canvas under the brush and uniformly blend it into
//        the travelling reservoir by pickupRate. The reservoir is an axis-
//        aligned RGBA8 texture that "travels" with the brush position:
//        reservoir pixel (rx, ry) maps to canvas (brushWorld + (rx,ry) -
//        reservoirHalf). The first dab copies the complete canvas footprint
//        into the reservoir (uInit=1), so the brush starts fully loaded.
//     2. Apply — blend the reservoir back onto the canvas, weighted by
//        (strength * falloff). Reservoir holds memory of pigment picked
//        up at earlier dab positions, so the deposit at the new position
//        smears earlier content forward.
//
//   Dab geometry, custom shape and soft-edge falloff belong to the apply pass.
//   Keeping pickup uniform avoids reservoir seams at the brush boundary.
// ---------------------------------------------------------------------------

const QString kSmudgeApplyFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uBrushCenter;\n"
    "uniform float uBrushRadius;\n"
    "uniform float uBrushHardness;\n"
    "uniform float uBrushRoundness;\n"
    "uniform float uBrushAngleRad;\n"
    "uniform float uBrushAlpha;\n"
    "uniform sampler2D uOriginalTexture;\n"
    "uniform sampler2D uReservoirTexture;\n"
    "uniform sampler2D uMaskTexture;\n"
    "uniform int   uUseMask;\n"
    "uniform sampler2D uDabShapeTexture;\n"
    "uniform int   uUseDabShapeTexture;\n"
    "uniform vec2  uDabShapeScale;\n"
    "uniform float uDabSoftEdgeRadiusTexels;\n"
    "uniform vec2  uTileOriginPx;\n"
    "uniform vec2  uInvTileSize;\n"
    "uniform vec2  uRoiOriginPx;\n"
    "uniform vec2  uInvRoiSize;\n"
    "uniform float uReservoirHalf;\n"
    "uniform vec2  uInvReservoirPhys;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec4 outColor;\n"
    "vec4 sanitizePremultiplied(vec4 color) {\n"
    "    if (color.a <= 1e-6) { return vec4(0.0); }\n"
    "    color.rgb = min(color.rgb, vec3(color.a));\n"
    "    return color;\n"
    "}\n"
    "vec2 sampleDabShapeSafe(vec2 uv) {\n"
    "    vec2 clampedUv = clamp(uv, vec2(0.0), vec2(1.0));\n"
    "    vec2 shape = texture(uDabShapeTexture, clampedUv).rg;\n"
    "    vec2 outsideUv = max(max(-uv, uv - vec2(1.0)), vec2(0.0)) * 2.0;\n"
    "    if (outsideUv.x > 0.0 || outsideUv.y > 0.0) {\n"
    "        shape.r = 0.0;\n"
    "        shape.g = clamp(shape.g + length(outsideUv), 0.0, 1.0);\n"
    "    }\n"
    "    return shape;\n"
    "}\n"
    "float customDabSoftEdgeShapePad(float hardness) {\n"
    "    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);\n"
    "    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));\n"
    "    vec2 safeSize = max(texSize - vec2(1.0), vec2(1.0));\n"
    "    vec2 pad = (2.0 * uDabSoftEdgeRadiusTexels * softness) / safeSize;\n"
    "    return max(pad.x, pad.y);\n"
    "}\n"
    "float sampleCustomDabEdgeFalloff(vec2 uv, float hardness) {\n"
    "    vec2 shape = sampleDabShapeSafe(uv);\n"
    "    float baseAlpha = clamp(shape.r, 0.0, 1.0);\n"
    "    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);\n"
    "    if (softness <= 0.0001) { return baseAlpha; }\n"
    "    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));\n"
    "    float edgeWidth = (2.0 * uDabSoftEdgeRadiusTexels * softness) / max(max(texSize.x, "
    "texSize.y), 1.0);\n"
    "    if (edgeWidth <= 0.0001) { return baseAlpha; }\n"
    "    float halfTexel = 1.0 / max(max(texSize.x, texSize.y), 1.0);\n"
    "    float edgeDistance = max(shape.g - halfTexel, 0.0);\n"
    "    float signedDistance = baseAlpha >= 0.5 ? edgeDistance : -edgeDistance;\n"
    "    float coverage = smoothstep(0.0, 1.0, (signedDistance + edgeWidth) / (2.0 * "
    "edgeWidth));\n"
    "    return clamp(mix(coverage, baseAlpha, clamp(hardness, 0.0, 1.0)), 0.0, 1.0);\n"
    "}\n"
    "float brushCoverage(vec2 local) {\n"
    "    if (uUseDabShapeTexture != 0) {\n"
    "        vec2 shapeLocal = local / uBrushRadius;\n"
    "        shapeLocal /= max(uDabShapeScale, vec2(0.0001));\n"
    "        float shapePad = customDabSoftEdgeShapePad(uBrushHardness);\n"
    "        if (abs(shapeLocal.x) > 1.0 + shapePad || abs(shapeLocal.y) > 1.0 + shapePad) {\n"
    "            return 0.0;\n"
    "        }\n"
    "        return sampleCustomDabEdgeFalloff((shapeLocal + 1.0) * 0.5, uBrushHardness);\n"
    "    }\n"
    "    float t = length(local) / uBrushRadius;\n"
    "    if (t > 1.0) { return 0.0; }\n"
    "    float edgeDistance = max(0.0, 1.0 - t);\n"
    "    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);\n"
    "    return softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, edgeDistance);\n"
    "}\n"
    "void main() {\n"
    "    vec2 delta = fragPixelCoord - uBrushCenter;\n"
    "    float c = cos(uBrushAngleRad);\n"
    "    float s = sin(uBrushAngleRad);\n"
    "    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));\n"
    "    vec2 local = vec2(\n"
    "        delta.x * c + delta.y * s,\n"
    "        (-delta.x * s + delta.y * c) / roundness\n"
    "    );\n"
    "    float falloff = brushCoverage(local);\n"
    "    if (falloff <= 0.0) { discard; }\n"
    "    float maskScale = 1.0;\n"
    "    if (uUseMask != 0) {\n"
    "        maskScale = texture(uMaskTexture, fragPixelCoord * uInvTileSize).a;\n"
    "        if (maskScale <= 0.0) { discard; }\n"
    "    }\n"
    "    vec2 worldPixelCoord = uTileOriginPx + fragPixelCoord;\n"
    "    vec2 originalUv = (worldPixelCoord - uRoiOriginPx) * uInvRoiSize;\n"
    "    vec4 canvas = sanitizePremultiplied(texture(uOriginalTexture, originalUv));\n"
    // Reservoir lookup: reservoir is centered on brush, so reservoir pixel
    // equals (delta + reservoirHalf). Phys-size division handles the case
    // where the physical texture is larger than the active logical area
    // (geometric growth across strokes).
    "    vec2 reservoirPx = delta + vec2(uReservoirHalf);\n"
    "    vec2 reservoirUv = reservoirPx * uInvReservoirPhys;\n"
    "    vec4 reservoir = sanitizePremultiplied(texture(uReservoirTexture, reservoirUv));\n"
    "    float strength = clamp(uBrushAlpha, 0.0, 1.0);\n"
    "    float intensity = clamp(strength * falloff * maskScale, 0.0, 1.0);\n"
    // Smudge transports the carried premultiplied color in both directions,
    // including coverage, so the reservoir can smear paint into transparency.
    "    outColor = sanitizePremultiplied(mix(canvas, reservoir, intensity));\n"
    // Quantization-aware dither: noise ∈ [0, 1) is used as a rounding
    // offset for the implicit 8-bit quantization on tile write, NOT as
    // an additive perturbation. This preserves saturated values exactly
    // (1.0 stays 1.0, 0.0 stays 0.0) and is statistically zero-mean for
    // mid-range values — unlike additive dither, which drifts saturated
    // channels down through asymmetric clamp at the boundaries and
    // erodes opaque areas to transparency after many overlapping dabs.
    "    vec2 ditherSeed = floor(uTileOriginPx + fragPixelCoord);\n"
    "    float ditherN = fract(52.9829189 * fract(dot(ditherSeed, vec2(0.06711056, "
    "0.00583715))));\n"
    "    outColor.rgb = floor(outColor.rgb * 255.0 + vec3(ditherN)) / 255.0;\n"
    "    outColor.a   = floor(outColor.a   * 255.0 + ditherN)       / 255.0;\n"
    // Re-enforce premultiplied invariant (rgb may have rounded above alpha
    // by at most 1/255).
    "    outColor.rgb = min(outColor.rgb, vec3(outColor.a));\n"
    "    if (outColor.a <= 1e-6) { outColor = vec4(0.0); }\n"
    "}\n");

// ---------------------------------------------------------------------------
//   Per-dab pickup shader (renders into the reservoir).
//   Viewport = reservoir logical size. fragPixelCoord ∈ [0, reservoirSide).
//   uInit=1 forces a full canvas-to-reservoir copy on the first dab.
// ---------------------------------------------------------------------------

const QString kSmudgePickupFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uBrushWorldPos;\n"
    "uniform float uPickupRate;\n"
    "uniform int   uInit;\n"
    "uniform sampler2D uOriginalTexture;\n"
    "uniform sampler2D uReservoirSrc;\n"
    "uniform vec2  uRoiOriginPx;\n"
    "uniform vec2  uInvRoiSize;\n"
    "uniform float uReservoirHalf;\n"
    "uniform vec2  uInvReservoirPhys;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec4 outColor;\n"
    "vec4 sanitizePremultiplied(vec4 color) {\n"
    "    if (color.a <= 1e-6) { return vec4(0.0); }\n"
    "    color.rgb = min(color.rgb, vec3(color.a));\n"
    "    return color;\n"
    "}\n"
    "void main() {\n"
    // fragPixelCoord ∈ [0, reservoirLogicalSize). Local brush-space offset
    // is (fragPixelCoord - reservoirHalf) — axis-aligned, so the same
    // reservoir pixel maps to a different canvas pos every dab.
    "    vec2 local = fragPixelCoord - vec2(uReservoirHalf);\n"
    "    vec2 worldPos = uBrushWorldPos + local;\n"
    "    vec2 canvasUv = (worldPos - uRoiOriginPx) * uInvRoiSize;\n"
    // CLAMP-TO-EDGE rather than zero-on-miss: reservoir pixels that map
    // outside the ROI (= outside the canvas, when the brush is wider
    // than the room around it) get loaded with the nearest valid canvas
    // edge value instead of vec4(0). Otherwise those pixels carry
    // transparency, and when the brush later moves so they fall inside
    // the apply ellipse they deposit alpha=0 onto opaque content — that
    // was the "smudge eats holes in opaque areas" bug.
    "    vec2 clampedUv = clamp(canvasUv, vec2(0.0), vec2(1.0));\n"
    "    vec4 canvasSample = sanitizePremultiplied(texture(uOriginalTexture, clampedUv));\n"
    // The first dab initializes the carry buffer from the canvas.
    "    if (uInit != 0) {\n"
    "        outColor = canvasSample;\n"
    "        return;\n"
    "    }\n"
    // IMPORTANT: pickup is UNIFORM across the whole reservoir (no falloff
    // weighting). Falloff-weighted pickup creates stamping seams at the
    // brush-disk edge — pixels just-inside vs just-outside evolve at very
    // different rates, and overlapping dabs reveal that boundary as ribbed
    // lines in the stroke direction. The soft edge comes from apply-time
    // falloff; pickup just keeps the reservoir continuously refreshed.
    "    vec2 reservoirUv = fragPixelCoord * uInvReservoirPhys;\n"
    "    vec4 prev = sanitizePremultiplied(texture(uReservoirSrc, reservoirUv));\n"
    "    float t = clamp(uPickupRate, 0.0, 1.0);\n"
    "    outColor = sanitizePremultiplied(mix(prev, canvasSample, t));\n"
    "    if (outColor.a <= 1e-6) { outColor = vec4(0.0); }\n"
    "}\n");

// ---------------------------------------------------------------------------
//   Batched smudge shader: renders a full-ROI quad into a ping-pong work
//   buffer. Outside the brush ellipse the fragment outputs the source
//   texture unchanged (it would otherwise discard, but for ping-pong we
//   need every fragment to write so the destination buffer ends up with
//   correct content everywhere — not just inside the ellipse).
// ---------------------------------------------------------------------------

const QString kSmudgeBatchVert
    = QStringLiteral("#version 450 core\n"
                     "uniform vec2 uViewportSize;\n"
                     "out vec2 fragPixelCoord;\n"
                     "vec2 positions[6] = vec2[](\n"
                     "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                     "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                     ");\n"
                     "void main() {\n"
                     "    vec2 t = positions[gl_VertexID];\n"
                     "    gl_Position = vec4(t * 2.0 - 1.0, 0.0, 1.0);\n"
                     "    fragPixelCoord = t * uViewportSize;\n"
                     "}\n");

// Exact color-format conversion for document <-> Wet working storage. The
// shared batched vertex shader supplies target-local pixel coordinates;
// texelFetch avoids filtering and half-texel ambiguity at tile/ROI edges.
const QString kFormatCopyFrag
    = QStringLiteral("#version 450 core\n"
                     "uniform sampler2D uSourceTexture;\n"
                     "uniform vec2 uSourceOrigin;\n"
                     "in vec2 fragPixelCoord;\n"
                     "out vec4 outColor;\n"
                     "void main() {\n"
                     "    ivec2 sourcePixel = ivec2(uSourceOrigin + floor(fragPixelCoord));\n"
                     "    outColor = texelFetch(uSourceTexture, sourcePixel, 0);\n"
                     "}\n");

const QString kSmudgeBatchFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uBrushCenter;\n" // in ROI-local pixel coords
    "uniform float uBrushRadius;\n"
    "uniform float uBrushHardness;\n"
    "uniform float uBrushRoundness;\n"
    "uniform float uBrushAngleRad;\n"
    "uniform float uBrushAlpha;\n"
    "uniform sampler2D uOriginalTexture;\n"
    "uniform sampler2D uReservoirTexture;\n"
    "uniform sampler2D uMaskTexture;\n"
    "uniform int   uUseMask;\n"
    "uniform sampler2D uDabShapeTexture;\n"
    "uniform int   uUseDabShapeTexture;\n"
    "uniform vec2  uDabShapeScale;\n"
    "uniform float uDabSoftEdgeRadiusTexels;\n"
    // Work buffers may be larger than the active ROI (geometric growth to
    // avoid reallocating every segment). The valid data lives in
    // [0, viewport]; texel sampling has to go via texel-size so we don't
    // read garbage from the unused tail of the texture.
    "uniform vec2  uInvTexSize;\n" // 1 / physical texture size
    "uniform vec2  uInvMaskSize;\n"
    "uniform vec2  uMaxValidUv;\n" // exclusive valid edge / texture size
    "uniform float uReservoirHalf;\n"
    "uniform vec2  uInvReservoirPhys;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec4 outColor;\n"
    "vec4 sanitizePremultiplied(vec4 color) {\n"
    "    if (color.a <= 1e-6) { return vec4(0.0); }\n"
    "    color.rgb = min(color.rgb, vec3(color.a));\n"
    "    return color;\n"
    "}\n"
    "vec2 sampleDabShapeSafe(vec2 uv) {\n"
    "    vec2 clampedUv = clamp(uv, vec2(0.0), vec2(1.0));\n"
    "    vec2 shape = texture(uDabShapeTexture, clampedUv).rg;\n"
    "    vec2 outsideUv = max(max(-uv, uv - vec2(1.0)), vec2(0.0)) * 2.0;\n"
    "    if (outsideUv.x > 0.0 || outsideUv.y > 0.0) {\n"
    "        shape.r = 0.0;\n"
    "        shape.g = clamp(shape.g + length(outsideUv), 0.0, 1.0);\n"
    "    }\n"
    "    return shape;\n"
    "}\n"
    "float customDabSoftEdgeShapePad(float hardness) {\n"
    "    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);\n"
    "    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));\n"
    "    vec2 safeSize = max(texSize - vec2(1.0), vec2(1.0));\n"
    "    vec2 pad = (2.0 * uDabSoftEdgeRadiusTexels * softness) / safeSize;\n"
    "    return max(pad.x, pad.y);\n"
    "}\n"
    "float sampleCustomDabEdgeFalloff(vec2 uv, float hardness) {\n"
    "    vec2 shape = sampleDabShapeSafe(uv);\n"
    "    float baseAlpha = clamp(shape.r, 0.0, 1.0);\n"
    "    float softness = max(1.0 - clamp(hardness, 0.0, 1.0), 0.0);\n"
    "    if (softness <= 0.0001) { return baseAlpha; }\n"
    "    vec2 texSize = vec2(textureSize(uDabShapeTexture, 0));\n"
    "    float edgeWidth = (2.0 * uDabSoftEdgeRadiusTexels * softness) / max(max(texSize.x, "
    "texSize.y), 1.0);\n"
    "    if (edgeWidth <= 0.0001) { return baseAlpha; }\n"
    "    float halfTexel = 1.0 / max(max(texSize.x, texSize.y), 1.0);\n"
    "    float edgeDistance = max(shape.g - halfTexel, 0.0);\n"
    "    float signedDistance = baseAlpha >= 0.5 ? edgeDistance : -edgeDistance;\n"
    "    float coverage = smoothstep(0.0, 1.0, (signedDistance + edgeWidth) / (2.0 * "
    "edgeWidth));\n"
    "    return clamp(mix(coverage, baseAlpha, clamp(hardness, 0.0, 1.0)), 0.0, 1.0);\n"
    "}\n"
    "float brushCoverage(vec2 local) {\n"
    "    if (uUseDabShapeTexture != 0) {\n"
    "        vec2 shapeLocal = local / uBrushRadius;\n"
    "        shapeLocal /= max(uDabShapeScale, vec2(0.0001));\n"
    "        float shapePad = customDabSoftEdgeShapePad(uBrushHardness);\n"
    "        if (abs(shapeLocal.x) > 1.0 + shapePad || abs(shapeLocal.y) > 1.0 + shapePad) {\n"
    "            return 0.0;\n"
    "        }\n"
    "        return sampleCustomDabEdgeFalloff((shapeLocal + 1.0) * 0.5, uBrushHardness);\n"
    "    }\n"
    "    float t = length(local) / uBrushRadius;\n"
    "    if (t > 1.0) { return 0.0; }\n"
    "    float edgeDistance = max(0.0, 1.0 - t);\n"
    "    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);\n"
    "    return softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, edgeDistance);\n"
    "}\n"
    "void main() {\n"
    "    vec2 originalUv = fragPixelCoord * uInvTexSize;\n"
    "    vec4 canvas = sanitizePremultiplied(texture(uOriginalTexture, originalUv));\n"
    "    vec2 delta = fragPixelCoord - uBrushCenter;\n"
    "    float c = cos(uBrushAngleRad);\n"
    "    float s = sin(uBrushAngleRad);\n"
    "    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));\n"
    "    vec2 local = vec2(\n"
    "        delta.x * c + delta.y * s,\n"
    "        (-delta.x * s + delta.y * c) / roundness\n"
    "    );\n"
    "    float falloff = brushCoverage(local);\n"
    "    if (falloff <= 0.0) { outColor = canvas; return; }\n"
    "    float maskScale = 1.0;\n"
    "    if (uUseMask != 0) {\n"
    "        maskScale = texture(uMaskTexture, fragPixelCoord * uInvMaskSize).a;\n"
    "        if (maskScale <= 0.0) { outColor = canvas; return; }\n"
    "    }\n"
    "    vec2 reservoirPx = delta + vec2(uReservoirHalf);\n"
    "    vec2 reservoirUv = reservoirPx * uInvReservoirPhys;\n"
    "    vec4 reservoir = sanitizePremultiplied(texture(uReservoirTexture, reservoirUv));\n"
    "    float strength = clamp(uBrushAlpha, 0.0, 1.0);\n"
    "    float intensity = clamp(strength * falloff * maskScale, 0.0, 1.0);\n"
    // Batched Smudge preserves the same premultiplied reservoir transport
    // semantics as the per-dab path.
    "    outColor = sanitizePremultiplied(mix(canvas, reservoir, intensity));\n"
    // Quantization-aware dither — see kSmudgeApplyFrag for why we round
    // via floor(v*255 + noise) rather than additive noise. The batched
    // path ping-pongs the work buffer (RGBA8) every dab so each pass is
    // a quantization step; dithered rounding decorrelates the error
    // without dragging saturated channels toward zero.
    "    float ditherN = fract(52.9829189 * fract(dot(floor(fragPixelCoord), vec2(0.06711056, "
    "0.00583715))));\n"
    "    outColor.rgb = floor(outColor.rgb * 255.0 + vec3(ditherN)) / 255.0;\n"
    "    outColor.a   = floor(outColor.a   * 255.0 + ditherN)       / 255.0;\n"
    "    outColor.rgb = min(outColor.rgb, vec3(outColor.a));\n"
    "    if (outColor.a <= 1e-6) { outColor = vec4(0.0); }\n"
    "}\n");

// ---------------------------------------------------------------------------
//   Liquify forward-warp = accumulate a displacement field, sample the source
//   ONCE. Two shaders, both on kSmudgeBatchVert (full-quad, fragPixelCoord =
//   t * viewport):
//
//   * kLiquifyFieldFrag — advects the backward-offset field B. The displayed
//     pixel p shows source[p + B[p]]. A forward drag that moves content under
//     the brush by +d updates B as:
//         B_new[y] = B_old[y - w(y)*d] - w(y)*d
//     (the content now at y came from y - w*d, dragged by +w*d). Ping-ponged.
//
//   * kLiquifyResolveFrag — reads B and samples the frozen source at p + B[p].
//     Because colors are resampled only once from the pristine source (never
//     ping-ponged), the warp stays crisp instead of compounding into a blur
//     the way per-dab color resampling (smudge) does.
// ---------------------------------------------------------------------------

const QString kLiquifyFieldFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uBrushCenter;\n" // field-ROI pixel coords
    "uniform float uBrushRadius;\n"
    "uniform float uBrushHardness;\n"
    "uniform float uBrushRoundness;\n"
    "uniform float uBrushAngleRad;\n"
    "uniform vec2  uStepDelta;\n" // Push: brush forward movement this dab (px)
    "uniform float uStrength;\n" // 0..1
    "uniform int   uMode;\n" // 0 Push 1 TwirlCW 2 TwirlCCW 3 Bloat 4 Pucker
    "uniform float uRate;\n" // per-dab rate for the non-Push modes
    "uniform sampler2D uFieldSrc;\n" // RG32F backward-offset field
    "uniform sampler2D uMaskTexture;\n"
    "uniform int   uUseMask;\n"
    "uniform vec2  uInvFieldSize;\n" // 1 / physical field texture size
    "uniform vec2  uInvMaskSize;\n"
    "uniform vec2  uMaxValidUv;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec2 outField;\n"
    "float brushCoverage(vec2 local) {\n"
    "    float t = length(local) / max(uBrushRadius, 0.0001);\n"
    "    if (t > 1.0) { return 0.0; }\n"
    "    float edgeDistance = max(0.0, 1.0 - t);\n"
    "    float softness = max(1.0 - clamp(uBrushHardness, 0.0, 1.0), 0.0);\n"
    "    return softness <= 0.001 ? 1.0 : smoothstep(0.0, softness, edgeDistance);\n"
    "}\n"
    "void main() {\n"
    "    vec2 delta = fragPixelCoord - uBrushCenter;\n"
    "    float c = cos(uBrushAngleRad);\n"
    "    float s = sin(uBrushAngleRad);\n"
    "    float roundness = max(0.01, clamp(uBrushRoundness, 0.0, 1.0));\n"
    "    vec2 local = vec2(\n"
    "        delta.x * c + delta.y * s,\n"
    "        (-delta.x * s + delta.y * c) / roundness\n"
    "    );\n"
    "    float falloff = brushCoverage(local);\n"
    "    float maskScale = 1.0;\n"
    "    if (uUseMask != 0) {\n"
    "        maskScale = texture(uMaskTexture, fragPixelCoord * uInvMaskSize).a;\n"
    "    }\n"
    "    float w = clamp(falloff * clamp(uStrength, 0.0, 1.0) * maskScale, 0.0, 1.0);\n"
    // Each mode is a per-fragment forward velocity v(y); the field is advected
    // by it: B_new[y] = B_old[y - v] - v. delta = fragment relative to the brush
    // centre, so Twirl is a rotation and Bloat/Pucker a scale about the centre.
    "    vec2 v = vec2(0.0);\n"
    "    if (uMode == 0) {\n"
    "        v = w * uStepDelta;\n" // Push (drag)
    "    } else if (uMode == 1) {\n"
    "        v = w * uRate * vec2(delta.y, -delta.x);\n" // Twirl clockwise
    "    } else if (uMode == 2) {\n"
    "        v = w * uRate * vec2(-delta.y, delta.x);\n" // Twirl counter-cw
    "    } else if (uMode == 3) {\n"
    "        v = w * uRate * delta;\n" // Bloat (outward)
    "    } else if (uMode == 4) {\n"
    "        v = -w * uRate * delta;\n" // Pucker (inward)
    "    }\n"
    "    vec2 sampleUv = clamp((fragPixelCoord - v) * uInvFieldSize, vec2(0.0), uMaxValidUv);\n"
    "    outField = texture(uFieldSrc, sampleUv).rg - v;\n"
    "}\n");

const QString kLiquifyResolveFrag
    = QStringLiteral("#version 450 core\n"
                     "uniform sampler2D uSourceTexture;\n" // RGBA8 frozen layer ROI (premultiplied)
                     "uniform sampler2D uFieldTex;\n" // RG32F backward-offset field
                     "uniform vec2  uInvFieldSize;\n" // 1 / physical field/source texture size
                     "uniform vec2  uMaxValidUv;\n"
                     "uniform vec2  uFieldOffset;\n" // work-pixel -> field-ROI pixel (px)
                     "in vec2 fragPixelCoord;\n" // work-buffer (segment footprint) px
                     "out vec4 outColor;\n"
                     "vec4 sanitizePremultiplied(vec4 color) {\n"
                     "    if (color.a <= 1e-6) { return vec4(0.0); }\n"
                     "    color.rgb = min(color.rgb, vec3(color.a));\n"
                     "    return color;\n"
                     "}\n"
                     "void main() {\n"
                     "    vec2 fieldPx = fragPixelCoord + uFieldOffset;\n"
                     "    vec2 fieldUv = clamp(fieldPx * uInvFieldSize, vec2(0.0), uMaxValidUv);\n"
                     "    vec2 off = texture(uFieldTex, fieldUv).rg;\n"
                     "    vec2 srcPx = fieldPx + off;\n"
                     "    vec2 srcUv = clamp(srcPx * uInvFieldSize, vec2(0.0), uMaxValidUv);\n"
                     "    outColor = sanitizePremultiplied(texture(uSourceTexture, srcUv));\n"
                     "}\n");

// ---------------------------------------------------------------------------
//   Batched pickup shader (renders into the reservoir). Reads the work
//   buffer (current canvas state inside the segment ROI) plus the previous
//   reservoir and blends them uniformly by pickupRate. uInit=1 forces a full
//   canvas-to-reservoir copy on the first dab of a stroke.
// ---------------------------------------------------------------------------

const QString kSmudgePickupBatchFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uBrushCenter;\n" // ROI-local pixel coords of brush
    "uniform float uPickupRate;\n"
    "uniform int   uInit;\n"
    "uniform sampler2D uOriginalTexture;\n" // work buffer (current canvas)
    "uniform sampler2D uReservoirSrc;\n"
    "uniform vec2  uInvTexSize;\n"
    "uniform vec2  uMaxValidUv;\n"
    "uniform float uReservoirHalf;\n"
    "uniform vec2  uInvReservoirPhys;\n"
    "in vec2 fragPixelCoord;\n"
    "out vec4 outColor;\n"
    "vec4 sanitizePremultiplied(vec4 color) {\n"
    "    if (color.a <= 1e-6) { return vec4(0.0); }\n"
    "    color.rgb = min(color.rgb, vec3(color.a));\n"
    "    return color;\n"
    "}\n"
    "void main() {\n"
    "    vec2 local = fragPixelCoord - vec2(uReservoirHalf);\n"
    "    vec2 canvasPx = uBrushCenter + local;\n"
    "    vec2 canvasUv = canvasPx * uInvTexSize;\n"
    // Clamp-to-edge sampling — see kSmudgePickupFrag for the rationale.
    // Out-of-ROI reservoir pixels load with the nearest valid work-buffer
    // edge value rather than transparency, so they can't contaminate
    // the brush carry buffer with alpha=0 when the segment ROI doesn't
    // fully enclose the reservoir.
    // Clamp to the centers of the edge texels, not the exclusive ROI edge.
    // Work textures grow and are commonly larger than the current ROI; sampling
    // exactly at uMaxValidUv linearly blends the last valid texel with the first
    // unused texel and carries that seam into the smudge reservoir.
    "    vec2 halfTexelUv = 0.5 * uInvTexSize;\n"
    "    vec2 validMaxUv = max(uMaxValidUv - halfTexelUv, halfTexelUv);\n"
    "    vec2 clampedUv = clamp(canvasUv, halfTexelUv, validMaxUv);\n"
    "    vec4 canvasSample = sanitizePremultiplied(texture(uOriginalTexture, clampedUv));\n"
    // The first dab initializes the carry buffer from the canvas.
    "    if (uInit != 0) {\n"
    "        outColor = canvasSample;\n"
    "        return;\n"
    "    }\n"
    // IMPORTANT: pickup is UNIFORM across the whole reservoir (no falloff
    // weighting). Falloff-weighted pickup creates stamping seams at the
    // brush-disk edge — pixels just-inside vs just-outside evolve at very
    // different rates, and overlapping dabs reveal that boundary as ribbed
    // lines in the stroke direction. The soft edge comes from apply-time
    // falloff; pickup just keeps the reservoir continuously refreshed.
    "    vec2 reservoirUv = fragPixelCoord * uInvReservoirPhys;\n"
    "    vec4 prev = sanitizePremultiplied(texture(uReservoirSrc, reservoirUv));\n"
    "    float t = clamp(uPickupRate, 0.0, 1.0);\n"
    "    outColor = sanitizePremultiplied(mix(prev, canvasSample, t));\n"
    "    if (outColor.a <= 1e-6) { outColor = vec4(0.0); }\n"
    "}\n");

const QString kWetPerDabPickupFrag = glsl(wet_pigment_gpu::kWetPerDabPickupPreamble)
    + glsl(wet_pigment_gpu::kWetPickupOutputsGlsl) + glsl(wet_pigment_gpu::kLatentGlsl)
    + glsl(wet_pigment_gpu::kWetPickupUpdateGlsl) + glsl(wet_pigment_gpu::kWetPerDabPickupMain);

const QString kWetBatchedPickupFrag = glsl(wet_pigment_gpu::kWetBatchedPickupPreamble)
    + glsl(wet_pigment_gpu::kWetPickupOutputsGlsl) + glsl(wet_pigment_gpu::kLatentGlsl)
    + glsl(wet_pigment_gpu::kWetPickupUpdateGlsl) + glsl(wet_pigment_gpu::kWetBatchedPickupMain);

const QString kWetPerDabApplyFrag = glsl(wet_pigment_gpu::kWetPerDabApplyPreamble)
    + glsl(wet_pigment_gpu::kLatentGlsl) + glsl(wet_pigment_gpu::kWetApplyCoverageGlsl)
    + glsl(wet_pigment_gpu::kWetPerDabApplyMain);

const QString kWetBatchedApplyFrag = glsl(wet_pigment_gpu::kWetBatchedApplyPreamble)
    + glsl(wet_pigment_gpu::kLatentGlsl) + glsl(wet_pigment_gpu::kWetApplyCoverageGlsl)
    + glsl(wet_pigment_gpu::kWetBatchedApplyMain);

// ---------------------------------------------------------------------------
//   GPU procedural texture generation shader
// ---------------------------------------------------------------------------
//   Replaces CPU-side proceduralTextureTileAlpha — renders noise directly
//   into an R8 texture via a fullscreen-quad draw.
// ---------------------------------------------------------------------------

static const QString kProceduralTextureVert
    = QStringLiteral("#version 450 core\n"
                     "out vec2 fragTexCoord;\n"
                     "vec2 positions[6] = vec2[](\n"
                     "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                     "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                     ");\n"
                     "void main() {\n"
                     "    vec2 pos = positions[gl_VertexID];\n"
                     "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
                     "    fragTexCoord = pos;\n"
                     "}\n");

static const QString kProceduralTextureFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform vec2  uTileOrigin;\n"
    "uniform float uScale;\n"
    "uniform float uContrast;\n"
    "uniform float uDepth;\n"
    "uniform float uBlend;\n"
    "uniform float uAmount;\n"
    "uniform int   uTextureType;\n"
    "in vec2 fragTexCoord;\n"
    "out float outValue;\n"

    "uint hash2D(int x, int y, uint seed) {\n"
    "    uint h = uint(x) * 374761393u\n"
    "           + uint(y) * 668265263u\n"
    "           + seed * 982451653u;\n"
    "    h = (h ^ (h >> 13u)) * 1274126177u;\n"
    "    return h ^ (h >> 16u);\n"
    "}\n"
    "float hash01(int x, int y, uint seed) {\n"
    "    uint h = hash2D(x, y, seed);\n"
    "    return float(h & 0x00FFFFFFu) / float(0x01000000u);\n"
    "}\n"
    "float sstep(float t) {\n"
    "    t = clamp(t, 0.0, 1.0);\n"
    "    return t * t * (3.0 - 2.0 * t);\n"
    "}\n"

    "float valueNoise(float x, float y, uint seed) {\n"
    "    int ix = int(floor(x));\n"
    "    int iy = int(floor(y));\n"
    "    float fx = x - float(ix);\n"
    "    float fy = y - float(iy);\n"
    "    float v00 = hash01(ix, iy, seed);\n"
    "    float v10 = hash01(ix + 1, iy, seed);\n"
    "    float v01 = hash01(ix, iy + 1, seed);\n"
    "    float v11 = hash01(ix + 1, iy + 1, seed);\n"
    "    float sx = sstep(fx);\n"
    "    float sy = sstep(fy);\n"
    "    float vx0 = v00 + (v10 - v00) * sx;\n"
    "    float vx1 = v01 + (v11 - v01) * sx;\n"
    "    return vx0 + (vx1 - vx0) * sy;\n"
    "}\n"

    "void grad2(int ix, int iy, uint seed, out float gx, out float gy) {\n"
    "    uint h = hash2D(ix, iy, seed);\n"
    "    float angle = float(h & 0xFFFFu) / 65536.0 * 6.28318530718;\n"
    "    gx = cos(angle);\n"
    "    gy = sin(angle);\n"
    "}\n"
    "float gradientNoise(float x, float y, uint seed) {\n"
    "    int ix = int(floor(x));\n"
    "    int iy = int(floor(y));\n"
    "    float fx = x - float(ix);\n"
    "    float fy = y - float(iy);\n"
    "    float g00x, g00y; grad2(ix, iy, seed, g00x, g00y);\n"
    "    float g10x, g10y; grad2(ix + 1, iy, seed, g10x, g10y);\n"
    "    float g01x, g01y; grad2(ix, iy + 1, seed, g01x, g01y);\n"
    "    float g11x, g11y; grad2(ix + 1, iy + 1, seed, g11x, g11y);\n"
    "    float d00 = fx * g00x + fy * g00y;\n"
    "    float d10 = (fx - 1.0) * g10x + fy * g10y;\n"
    "    float d01 = fx * g01x + (fy - 1.0) * g01y;\n"
    "    float d11 = (fx - 1.0) * g11x + (fy - 1.0) * g11y;\n"
    "    float sx = sstep(fx);\n"
    "    float sy = sstep(fy);\n"
    "    float vx0 = d00 + (d10 - d00) * sx;\n"
    "    float vx1 = d01 + (d11 - d01) * sx;\n"
    "    float v = vx0 + (vx1 - vx0) * sy;\n"
    "    return clamp(v * 0.5 + 0.5, 0.0, 1.0);\n"
    "}\n"

    "float pencilGrain(float wx, float wy, float scale) {\n"
    "    float f0 = 0.018 * scale;\n"
    "    float f1 = f0 * 2.13;\n"
    "    float f2 = f1 * 1.97;\n"
    "    float n0 = valueNoise(wx * f0, wy * f0, 0xA53F91u);\n"
    "    float n1 = valueNoise(wx * f1, wy * f1, 0xC17AB1u);\n"
    "    float n2 = valueNoise(wx * f2, wy * f2, 0x91BB37u);\n"
    "    float streak = valueNoise(wx * (f0 * 2.8) + wy * (f0 * 0.55),\n"
    "                              wy * (f0 * 0.22), 0x7F4A21u);\n"
    "    return clamp(n0 * 0.50 + n1 * 0.25 + n2 * 0.10 + streak * 0.15, 0.0, 1.0);\n"
    "}\n"
    "float noiseGrain(float wx, float wy, float scale) {\n"
    "    float f0 = 0.025 * scale;\n"
    "    float f1 = f0 * 2.5;\n"
    "    float f2 = f1 * 2.2;\n"
    "    float n0 = valueNoise(wx * f0, wy * f0, 0xB2C4D1u);\n"
    "    float n1 = valueNoise(wx * f1, wy * f1, 0x8E3A5Fu);\n"
    "    float n2 = valueNoise(wx * f2, wy * f2, 0xD7F21Au);\n"
    "    return clamp(n0 * 0.55 + n1 * 0.30 + n2 * 0.15, 0.0, 1.0);\n"
    "}\n"
    "float perlinGrain(float wx, float wy, float scale) {\n"
    "    float f0 = 0.015 * scale;\n"
    "    float f1 = f0 * 2.0;\n"
    "    float n0 = gradientNoise(wx * f0, wy * f0, 0x5A3C91u);\n"
    "    float n1 = gradientNoise(wx * f1, wy * f1, 0x7B2D41u);\n"
    "    return clamp(0.5 + n0 * 0.4 + n1 * 0.25, 0.0, 1.0);\n"
    "}\n"

    "void main() {\n"
    "    vec2 pixel = fragTexCoord * 256.0;\n"
    "    float wx = uTileOrigin.x + pixel.x + 0.5;\n"
    "    float wy = uTileOrigin.y + pixel.y + 0.5;\n"
    "    float g;\n"
    "    if (uTextureType == 1) {\n"
    "        g = noiseGrain(wx, wy, uScale);\n"
    "    } else if (uTextureType == 2) {\n"
    "        g = perlinGrain(wx, wy, uScale);\n"
    "    } else {\n"
    "        g = pencilGrain(wx, wy, uScale);\n"
    "    }\n"
    "    float contrastStrength = 0.5 + uContrast * 2.5;\n"
    "    g = clamp(0.5 + (g - 0.5) * contrastStrength, 0.0, 1.0);\n"
    "    float depthMix = 1.0 - uDepth * (1.0 - g);\n"
    "    float blendMix = (1.0 - uBlend) * depthMix\n"
    "                   + uBlend * (depthMix * depthMix);\n"
    "    float factor = (1.0 - uAmount) + uAmount * blendMix;\n"
    "    outValue = clamp(factor, 0.0, 1.0);\n"
    "}\n");

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLBrushRenderer::GLBrushRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLBrushRenderer::~GLBrushRenderer()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> GLBrushRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized)
        return Result<void>::ok();

    m_brushProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_brushProgram->loadFromFiles(
        shaderDir + "/brush_stamp.vert.glsl", shaderDir + "/brush_stamp.frag.glsl");
    if (!result) {
        return result;
    }

    m_blurPassProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blurPassResult = m_blurPassProgram->loadFromSource(kBlurPassVert, kBlurPassFrag);
    if (!blurPassResult) {
        return blurPassResult;
    }

    m_blurProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blurResult = m_blurProgram->loadFromSource(kBrushStampVert, kBlurApplyFrag);
    if (!blurResult) {
        return blurResult;
    }

    m_smudgeProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto smudgeResult = m_smudgeProgram->loadFromSource(kBrushStampVert, kSmudgeApplyFrag);
    if (!smudgeResult) {
        return smudgeResult;
    }

    m_smudgeBatchProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto smudgeBatchResult
        = m_smudgeBatchProgram->loadFromSource(kSmudgeBatchVert, kSmudgeBatchFrag);
    if (!smudgeBatchResult) {
        return smudgeBatchResult;
    }

    // Pickup shaders share the kSmudgeBatchVert (full-viewport quad with
    // fragPixelCoord = viewportSize * t). For the per-dab pickup the
    // viewport is the reservoir; for the batched pickup it's the reservoir
    // too — same vertex shader works for both.
    m_smudgePickupProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto smudgePickupResult
        = m_smudgePickupProgram->loadFromSource(kSmudgeBatchVert, kSmudgePickupFrag);
    if (!smudgePickupResult) {
        return smudgePickupResult;
    }

    m_smudgePickupBatchProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto smudgePickupBatchResult
        = m_smudgePickupBatchProgram->loadFromSource(kSmudgeBatchVert, kSmudgePickupBatchFrag);
    if (!smudgePickupBatchResult) {
        return smudgePickupBatchResult;
    }

    m_wetPickupProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto wetPickupResult
        = m_wetPickupProgram->loadFromSource(kSmudgeBatchVert, kWetPerDabPickupFrag);
    if (!wetPickupResult) {
        return { wetPickupResult.error().code,
            "Latent per-dab pickup: " + wetPickupResult.error().message };
    }

    m_wetApplyProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto wetApplyResult = m_wetApplyProgram->loadFromSource(kBrushStampVert, kWetPerDabApplyFrag);
    if (!wetApplyResult) {
        return { wetApplyResult.error().code,
            "Latent per-dab apply: " + wetApplyResult.error().message };
    }

    m_wetPickupBatchProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto wetPickupBatchResult
        = m_wetPickupBatchProgram->loadFromSource(kSmudgeBatchVert, kWetBatchedPickupFrag);
    if (!wetPickupBatchResult) {
        return { wetPickupBatchResult.error().code,
            "Latent batched pickup: " + wetPickupBatchResult.error().message };
    }

    m_wetApplyBatchProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto wetApplyBatchResult
        = m_wetApplyBatchProgram->loadFromSource(kSmudgeBatchVert, kWetBatchedApplyFrag);
    if (!wetApplyBatchResult) {
        return { wetApplyBatchResult.error().code,
            "Latent batched apply: " + wetApplyBatchResult.error().message };
    }

    m_liquifyFieldProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto liquifyFieldResult
        = m_liquifyFieldProgram->loadFromSource(kSmudgeBatchVert, kLiquifyFieldFrag);
    if (!liquifyFieldResult) {
        return liquifyFieldResult;
    }

    m_liquifyResolveProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto liquifyResolveResult
        = m_liquifyResolveProgram->loadFromSource(kSmudgeBatchVert, kLiquifyResolveFrag);
    if (!liquifyResolveResult) {
        return liquifyResolveResult;
    }

    m_formatCopyProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto formatCopyResult = m_formatCopyProgram->loadFromSource(kSmudgeBatchVert, kFormatCopyFrag);
    if (!formatCopyResult) {
        return formatCopyResult;
    }

    m_rebuildBatchProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto batchResult = m_rebuildBatchProgram->loadFromSource(kBatchRebuildVert, kBatchRebuildFrag);
    if (!batchResult) {
        return batchResult;
    }

    static const QString flattenVert
        = QStringLiteral("#version 450 core\n"
                         "out vec2 fragTexCoord;\n"
                         "vec2 positions[6] = vec2[](\n"
                         "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                         "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                         ");\n"
                         "void main() {\n"
                         "    vec2 pos = positions[gl_VertexID];\n"
                         "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
                         "    fragTexCoord = pos;\n"
                         "}\n");
    static const QString flattenFrag = QStringLiteral(
        "#version 450 core\n"
        "uniform sampler2D uSrcTexture;\n"
        "uniform sampler2D uBaseTexture;\n"
        "uniform sampler2D uDstTexture;\n"
        "uniform sampler2D uFinalSourceMaskTexture;\n"
        "uniform float uStrokeOpacity;\n"
        "uniform int uUseFinalSourceMask;\n"
        "uniform int uReplaceWithBaseMix;\n"
        "uniform int uUseProgrammaticBlend;\n"
        "uniform int uStrokeBlendMode;\n"
        "uniform int uAlphaLock;\n"
        // Soft-selection alpha cap. When 1, the C++ caller has disabled GL blending
        // and bound the layer's pre-stroke pixels to uDstTexture. Shader does manual
        // src-over and clamps result alpha to the mask alpha (or preserves dst when
        // dst.a > mask_alpha).
        "uniform int uClipMaskAsAlphaCap;\n"
        "uniform vec4 uBackdropColor;\n"
        "uniform vec2 uTileWorldOrigin;\n"
        "in vec2 fragTexCoord;\n"
        "out vec4 outColor;\n"
        "const vec3 kLumWeights = vec3(0.299, 0.587, 0.114);\n"
        "float lum(vec3 c) { return dot(c, kLumWeights); }\n"
        "float sat(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }\n"
        "vec3 clipColor(vec3 c) {\n"
        "    float l = lum(c);\n"
        "    float mn = min(c.r, min(c.g, c.b));\n"
        "    float mx = max(c.r, max(c.g, c.b));\n"
        "    if (mn < 0.0) c = vec3(l) + (c - vec3(l)) * l / max(l - mn, 0.00001);\n"
        "    if (mx > 1.0) c = vec3(l) + (c - vec3(l)) * (1.0 - l) / max(mx - l, 0.00001);\n"
        "    return clamp(c, 0.0, 1.0);\n"
        "}\n"
        "vec3 setLum(vec3 c, float l) { return clipColor(c + vec3(l - lum(c))); }\n"
        "vec3 setSat(vec3 c, float s) {\n"
        "    float v[3] = float[3](c.r, c.g, c.b);\n"
        "    int mn = 0; int mx = 0;\n"
        "    for (int i = 1; i < 3; ++i) { if (v[i] < v[mn]) mn = i; if (v[i] > v[mx]) mx = i; }\n"
        "    if (v[mx] <= v[mn]) return vec3(0.0);\n"
        "    int mid = 3 - mn - mx;\n"
        "    float r[3]; r[mn] = 0.0; r[mid] = ((v[mid] - v[mn]) * s) / (v[mx] - v[mn]); r[mx] = "
        "s;\n"
        "    return clamp(vec3(r[0], r[1], r[2]), 0.0, 1.0);\n"
        "}\n"
        "float dodge(float b, float s) { return (s >= 1.0) ? 1.0 : min(1.0, b / max(1.0 - s, "
        "0.00001)); }\n"
        "float burn(float b, float s) { return (s <= 0.0) ? 0.0 : max(0.0, 1.0 - (1.0 - b) / "
        "max(s, 0.00001)); }\n"
        "float vivid(float b, float s) { return (s < 0.5) ? burn(b, clamp(2.0 * s, 0.0, 1.0)) : "
        "dodge(b, clamp(2.0 * (s - 0.5), 0.0, 1.0)); }\n"
        "vec3 blendColor(vec3 b, vec3 s, int mode) {\n"
        "    vec3 overlay = mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));\n"
        "    vec3 hard = mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, s));\n"
        "    vec3 d = mix(sqrt(b), ((16.0 * b - 12.0) * b + 4.0) * b, step(0.25, b));\n"
        "    vec3 soft = mix(b - (1.0 - 2.0 * s) * b * (1.0 - b), b + (2.0 * s - 1.0) * (d - b), "
        "step(0.5, s));\n"
        "    if (mode == 1) return b * s;\n"
        "    if (mode == 2) return b + s - b * s;\n"
        "    if (mode == 3) return overlay;\n"
        "    if (mode == 4) return soft;\n"
        "    if (mode == 5) return hard;\n"
        "    if (mode == 6) return vec3(dodge(b.r, s.r), dodge(b.g, s.g), dodge(b.b, s.b));\n"
        "    if (mode == 7) return vec3(burn(b.r, s.r), burn(b.g, s.g), burn(b.b, s.b));\n"
        "    if (mode == 8) return min(b, s);\n"
        "    if (mode == 9) return max(b, s);\n"
        "    if (mode == 10) return abs(b - s);\n"
        "    if (mode == 11) return b + s - 2.0 * b * s;\n"
        "    if (mode == 13) return max(vec3(0.0), b + s - 1.0);\n"
        "    if (mode == 14) return (lum(s) <= lum(b)) ? s : b;\n"
        "    if (mode == 15) return min(vec3(1.0), b + s);\n"
        "    if (mode == 16) return (lum(s) >= lum(b)) ? s : b;\n"
        "    if (mode == 17) return vec3(vivid(b.r, s.r), vivid(b.g, s.g), vivid(b.b, s.b));\n"
        "    if (mode == 18) return clamp(b + 2.0 * s - 1.0, 0.0, 1.0);\n"
        "    if (mode == 19) return mix(min(b, 2.0 * s), max(b, 2.0 * (s - 0.5)), step(0.5, s));\n"
        "    if (mode == 20) return step(vec3(0.5), vec3(vivid(b.r, s.r), vivid(b.g, s.g), "
        "vivid(b.b, s.b)));\n"
        "    if (mode == 21) return max(vec3(0.0), b - s);\n"
        "    if (mode == 22) return vec3((s.r <= 0.0) ? 1.0 : min(1.0, b.r / max(s.r, 0.001)), "
        "(s.g <= 0.0) ? 1.0 : min(1.0, b.g / max(s.g, 0.001)), (s.b <= 0.0) ? 1.0 : min(1.0, b.b / "
        "max(s.b, 0.001)));\n"
        "    if (mode == 23) return (sat(s) <= 0.00001) ? b : setLum(setSat(s, sat(b)), lum(b));\n"
        "    if (mode == 24) return setLum(setSat(b, sat(s)), lum(b));\n"
        "    if (mode == 25) return setLum(s, lum(b));\n"
        "    if (mode == 26) return setLum(b, lum(s));\n"
        "    return s;\n"
        "}\n"
        "uint hashPixel(uvec2 v) { uint h = v.x * 1597334677u + v.y * 3812015801u + 2246822519u; h "
        "^= h >> 16; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16; return h; }\n"
        "float dissolveAlpha(float a) {\n"
        "    if (a <= 0.0) return 0.0; if (a >= 1.0) return 1.0;\n"
        "    vec2 sz = vec2(textureSize(uSrcTexture, 0));\n"
        "    ivec2 p = ivec2(floor(uTileWorldOrigin + fragTexCoord * sz));\n"
        "    float r = float(hashPixel(uvec2(p)) & 0x00ffffffu) / 16777215.0;\n"
        "    return (r <= a) ? 1.0 : 0.0;\n"
        "}\n"
        "void main() {\n"
        "    float opacity = clamp(uStrokeOpacity, 0.0, 1.0);\n"
        "    vec4 src = texture(uSrcTexture, fragTexCoord);\n"
        "    float maskA = 1.0;\n"
        "    if (uUseFinalSourceMask != 0) {\n"
        "        maskA = texture(uFinalSourceMaskTexture, fragTexCoord).a;\n"
        "        src *= maskA;\n"
        "    }\n"
        "    if (uClipMaskAsAlphaCap != 0) {\n"
        // Manual src-over with per-pixel alpha cap. C++ side: GL_BLEND disabled,
        // uDstTexture bound to layer's pre-stroke pixels.
        "        vec4 dst = texture(uDstTexture, fragTexCoord);\n"
        "        if (dst.a > maskA) { outColor = dst; return; }\n"
        "        vec4 srcEff = src * opacity;\n"
        "        float ao = srcEff.a + dst.a * (1.0 - srcEff.a);\n"
        "        vec3 co = srcEff.rgb + dst.rgb * (1.0 - srcEff.a);\n"
        "        if (ao > maskA) {\n"
        "            if (ao > 0.0) co *= maskA / ao;\n"
        "            ao = maskA;\n"
        "        }\n"
        "        outColor = vec4(clamp(co, vec3(0.0), vec3(maskA)), ao);\n"
        "        return;\n"
        "    }\n"
        "    if (uReplaceWithBaseMix != 0) {\n"
        "        vec4 base = texture(uBaseTexture, fragTexCoord);\n"
        "        outColor = mix(base, src, opacity);\n"
        "        return;\n"
        "    }\n"
        "    if (uUseProgrammaticBlend == 0) {\n"
        "        outColor = src * opacity;\n"
        "        return;\n"
        "    }\n"
        "    vec4 base = texture(uBaseTexture, fragTexCoord);\n"
        "    vec4 dst = texture(uDstTexture, fragTexCoord);\n"
        "    float ab = base.a;\n"
        "    float ad = dst.a;\n"
        "    float asRaw = src.a;\n"
        "    float as = (uStrokeBlendMode == 12) ? dissolveAlpha(asRaw * opacity) : clamp(asRaw * "
        "opacity, 0.0, 1.0);\n"
        "    vec3 Cs = (asRaw > 0.0) ? src.rgb / asRaw : vec3(0.0);\n"
        "    vec4 backdrop = vec4(uBackdropColor.rgb * uBackdropColor.a, uBackdropColor.a);\n"
        "    vec4 visibleBase = base + backdrop * (1.0 - ab);\n"
        "    float visibleBaseAlpha = visibleBase.a;\n"
        "    vec3 Cb = (visibleBaseAlpha > 0.0) ? visibleBase.rgb / visibleBaseAlpha : vec3(0.0);\n"
        "    vec3 Cd = (ad > 0.0) ? dst.rgb / ad : vec3(0.0);\n"
        "    vec3 B = blendColor(Cb, Cs, uStrokeBlendMode);\n"
        "    vec3 strokeColor = mix(Cs, B, visibleBaseAlpha);\n"
        "    if (uAlphaLock != 0) {\n"
        "        vec3 coLocked = ad * (as * strokeColor + (1.0 - as) * Cd);\n"
        "        outColor = vec4(clamp(coLocked, vec3(0.0), vec3(ad)), ad);\n"
        "        return;\n"
        "    }\n"
        "    vec3 co = as * strokeColor + (1.0 - as) * dst.rgb;\n"
        "    float ao = as + ad * (1.0 - as);\n"
        "    outColor = vec4(clamp(co, vec3(0.0), vec3(ao)), ao);\n"
        "}\n");

    m_flattenProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto flatResult = m_flattenProgram->loadFromSource(flattenVert, flattenFrag);
    if (!flatResult) {
        return flatResult;
    }

    m_proceduralTextureProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto procTexResult = m_proceduralTextureProgram->loadFromSource(
        kProceduralTextureVert, kProceduralTextureFrag);
    if (!procTexResult) {
        return procTexResult;
    }

    // Release any partially-created GPU resources on early return.
    auto cleanupOnFailure = [this]() {
        if (m_blurLinearSampler) {
            m_gl->glDeleteSamplers(1, &m_blurLinearSampler);
            m_blurLinearSampler = 0;
        }
        deleteTexture(m_gl, m_blurScratchTempTex);
        deleteTexture(m_gl, m_blurScratchSourceTex);
        deleteTexture(m_gl, m_blurReadTex);
        deleteTexture(m_gl, m_pigmentLutTex[0]);
        deleteTexture(m_gl, m_pigmentLutTex[1]);
        m_pigmentLutSize = 0;
        if (m_emptyVAO) {
            m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
            m_emptyVAO = 0;
        }
        if (m_fbo) {
            m_gl->glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
    };

    m_gl->glGenFramebuffers(1, &m_fbo);
    if (m_fbo == 0) {
        cleanupOnFailure();
        return { ErrorCode::PipelineCreationFailed, "Failed to create brush FBO" };
    }

    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        cleanupOnFailure();
        return { ErrorCode::PipelineCreationFailed, "Failed to create brush VAO" };
    }

    // Content scratch follows the document tile format so glCopyImageSubData
    // to/from tiles stays format-compatible (see header note on these members).
    // NEAREST/NEAREST to match the previous default-params filtering exactly;
    // the sampling paths that need linear bind m_blurLinearSampler explicitly.
    const TextureParams contentScratchParams
        = tileTextureParams(kDefaultTileFormat, GL_NEAREST, GL_NEAREST);
    m_blurReadTex = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, contentScratchParams);
    m_blurScratchSourceTex = createTexture2D(m_gl, 1, 1, contentScratchParams); // resized on demand
    m_blurScratchTempTex = createTexture2D(m_gl, 1, 1, contentScratchParams); // resized on demand

    m_gl->glGenSamplers(1, &m_blurLinearSampler);
    m_gl->glSamplerParameteri(m_blurLinearSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_blurLinearSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_blurLinearSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glSamplerParameteri(m_blurLinearSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Wet encode samples the two packed PigmentLut planes. Failure is fatal to
    // renderer initialization; wet never falls back to RGB reservoir mixing.

    try {
        const auto pigmentLut = ruwa::core::brushes::PigmentLutResource::loadBuiltIn();
        m_pigmentLutSize = static_cast<GLsizei>(pigmentLut.size());
        std::vector<float> packed[wet_pigment_gpu::kLutTextureCount];
        for (auto& plane : packed)
            plane.reserve(pigmentLut.entries().size() * 4);
        for (const auto& entry : pigmentLut.entries()) {
            for (std::size_t plane = 0; plane < wet_pigment_gpu::kLutTextureCount; ++plane) {
                for (int channel = 0; channel < 4; ++channel)
                    packed[plane].push_back(entry[plane * 4 + static_cast<std::size_t>(channel)]);
            }
        }
        while (m_gl->glGetError() != GL_NO_ERROR) { }
        m_gl->glGenTextures(
            static_cast<GLsizei>(wet_pigment_gpu::kLutTextureCount), m_pigmentLutTex);
        if (m_pigmentLutTex[0] == 0 || m_pigmentLutTex[1] == 0) {
            cleanupOnFailure();
            return { ErrorCode::PipelineCreationFailed, "Failed to create pigment LUT textures" };
        }
        for (std::size_t plane = 0; plane < wet_pigment_gpu::kLutTextureCount; ++plane) {
            m_gl->glBindTexture(GL_TEXTURE_3D, m_pigmentLutTex[plane]);
            m_gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            m_gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            m_gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            m_gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            m_gl->glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, m_pigmentLutSize, m_pigmentLutSize,
                m_pigmentLutSize, 0, GL_RGBA, GL_FLOAT, packed[plane].data());
        }
        m_gl->glBindTexture(GL_TEXTURE_3D, 0);
        if (m_gl->glGetError() != GL_NO_ERROR)
            throw std::runtime_error("OpenGL rejected the pigment LUT upload");
    } catch (const std::exception& error) {
        cleanupOnFailure();
        return { ErrorCode::PipelineCreationFailed,
            std::string("Failed to load pigment LUT: ") + error.what() };
    }

    m_pbo = 0;
    m_pboSize = 0;
    m_initialized = true;
    return Result<void>::ok();
}

void GLBrushRenderer::shutdown()
{
    if (!m_initialized)
        return;

    if (m_stampBatchActive) {
        endStampBatch();
    }

    DabShapeCache::instance().releaseTextures(m_gl);

    m_brushProgram.reset();
    m_blurPassProgram.reset();
    m_blurProgram.reset();
    m_smudgeProgram.reset();
    m_smudgeBatchProgram.reset();
    m_smudgePickupProgram.reset();
    m_smudgePickupBatchProgram.reset();
    m_wetPickupProgram.reset();
    m_wetApplyProgram.reset();
    m_wetPickupBatchProgram.reset();
    m_wetApplyBatchProgram.reset();
    m_liquifyFieldProgram.reset();
    m_liquifyResolveProgram.reset();
    m_formatCopyProgram.reset();
    m_rebuildBatchProgram.reset();
    m_flattenProgram.reset();

    for (auto& [key, gpuTile] : m_proceduralTextureGpuTiles) {
        if (gpuTile.textureId != 0) {
            m_gl->glDeleteTextures(1, &gpuTile.textureId);
            gpuTile.textureId = 0;
        }
    }
    m_proceduralTextureGpuTiles.clear();

    deleteTexture(m_gl, m_blurReadTex);
    deleteTexture(m_gl, m_blurScratchSourceTex);
    deleteTexture(m_gl, m_blurScratchTempTex);
    deleteTexture(m_gl, m_maskScratchTex);
    deleteTexture(m_gl, m_pigmentLutTex[0]);
    deleteTexture(m_gl, m_pigmentLutTex[1]);
    m_pigmentLutSize = 0;
    m_maskScratchWidth = 0;
    m_maskScratchHeight = 0;
    deleteTexture(m_gl, m_smudgeWorkTex[0]);
    deleteTexture(m_gl, m_smudgeWorkTex[1]);
    m_smudgeWorkWidth = 0;
    m_smudgeWorkHeight = 0;
    deleteTexture(m_gl, m_liquifyFieldTex[0]);
    deleteTexture(m_gl, m_liquifyFieldTex[1]);
    deleteTexture(m_gl, m_liquifySourceTex);
    m_liquifyTexW = 0;
    m_liquifyTexH = 0;
    m_liquifyRoiW = 0;
    m_liquifyRoiH = 0;
    m_liquifyFieldSrcIdx = 0;
    m_liquifyActive = false;
    for (auto& side : m_smudgeReservoirTex) {
        for (GLuint& texture : side)
            deleteTexture(m_gl, texture);
    }
    m_smudgeReservoirSize = 0;
    m_smudgeReservoirActive = 0;
    m_smudgeReservoirSrcIdx = 0;
    m_smudgePrevValid = false;
    if (m_blurLinearSampler) {
        m_gl->glDeleteSamplers(1, &m_blurLinearSampler);
        m_blurLinearSampler = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }
    if (m_pbo) {
        m_gl->glDeleteBuffers(1, &m_pbo);
        m_pbo = 0;
        m_pboSize = 0;
    }

    m_blurScratchWidth = 0;
    m_blurScratchHeight = 0;
    m_drawCallEstimate = 0;
    m_initialized = false;
}

void GLBrushRenderer::beginStampBatch()
{
    if (!m_initialized || !m_brushProgram)
        return;
    if (m_stampBatchActive)
        return;

    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_batchPrevFBO);
    m_gl->glGetIntegerv(GL_VIEWPORT, m_batchPrevViewport);

    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendEquation(GL_MAX);
    m_gl->glBlendFunc(GL_ONE, GL_ONE);

    m_brushProgram->use();
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_stampBatchActive = true;
}

void GLBrushRenderer::endStampBatch()
{
    if (!m_stampBatchActive)
        return;

    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE4);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);

    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_batchPrevFBO);
    m_gl->glViewport(m_batchPrevViewport[0], m_batchPrevViewport[1], m_batchPrevViewport[2],
        m_batchPrevViewport[3]);
    restoreDefaultPremultipliedBlendState(m_gl);
    m_stampBatchActive = false;
}

// ==========================================================================
//   G P U   S T A M P
// ==========================================================================

void GLBrushRenderer::stampGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
    const TileBrush& brush, float worldX, float worldY, float radius, float hardness,
    float roundness, float angleDegrees, bool useMaxBlend, uint8_t r, uint8_t g, uint8_t b,
    uint8_t a, TileGrid* selectionMask, bool useSelectionMask, uint32_t canvasWidth,
    uint32_t canvasHeight, TileGrid* layerGrid)
{
    if (!m_initialized || !m_brushProgram || !tileRenderer)
        return;
    if (radius <= 0.0f || a == 0)
        return;
    if (useSelectionMask && (!selectionMask || selectionMask->empty()))
        return;

    const bool wetMode = brush.isWetMode();
    // Wet must retain low-alpha premultiplied RGB between dabs. RGBA8 rounds
    // those channels to zero and turns the next unpremultiply into false black
    // pigment, so the whole in-progress wet stroke lives in float storage.
    // Other effects continue to use the document format.
    if (layerGrid && strokeBuffer.empty()) {
        strokeBuffer.setFormat(wetMode ? wet_pigment_gpu::workingColorFormat(layerGrid->format())
                                       : layerGrid->format());
    }

    const float coverageExtent
        = dabCoverageExtent(brush, radius, hardness, roundness, angleDegrees);
    int32_t tMinX = static_cast<int32_t>(std::floor((worldX - coverageExtent) / TILE_SIZE));
    int32_t tMinY = static_cast<int32_t>(std::floor((worldY - coverageExtent) / TILE_SIZE));
    int32_t tMaxX = static_cast<int32_t>(std::floor((worldX + coverageExtent) / TILE_SIZE));
    int32_t tMaxY = static_cast<int32_t>(std::floor((worldY + coverageExtent) / TILE_SIZE));

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    if (clipToCanvas) {
        const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
        const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
        tMinX = std::max(tMinX, 0);
        tMinY = std::max(tMinY, 0);
        tMaxX = std::min(tMaxX, canvasMaxX);
        tMaxY = std::min(tMaxY, canvasMaxY);
        if (tMinX > tMaxX || tMinY > tMaxY) {
            return;
        }
    }

    const bool ownBatch = !m_stampBatchActive;
    if (ownBatch) {
        beginStampBatch();
    }
    auto finishOwnBatch = [&]() {
        if (ownBatch) {
            endStampBatch();
        }
    };

    const float colorR = static_cast<float>(r) / 255.0f;
    const float colorG = static_cast<float>(g) / 255.0f;
    const float colorB = static_cast<float>(b) / 255.0f;
    const float alpha = static_cast<float>(a) / 255.0f;

    auto ensureUploadedTileTexture = [&](TileData* tile) {
        if (!tile)
            return;
        if (!tile->hasTexture()) {
            tileRenderer->ensureTileTexture(*tile);
            tileRenderer->uploadTileData(*tile);
        } else if (tile->isDirty()) {
            tileRenderer->uploadTileData(*tile);
        }
    };

    const bool wantsBlur = brush.isBlurMode();
    // Wet and Smudge share the carry-buffer geometry and dispatch pipeline,
    // but use separate shader programs and reservoir color representations.
    // Routing both here keeps single/first dabs consistent with the batched
    // stroke path (stampSmudgeSegmentGPU).
    const bool wantsSmudge = brush.isSmudgeMode() || brush.isWetMode();
    const bool isBlur = wantsBlur && m_blurPassProgram && m_blurProgram && layerGrid;
    if (wantsBlur && !isBlur) {
        finishOwnBatch();
        return;
    }
    const float blurRadiusBase = std::min(radius * 0.35f, 20.0f);
    const float blurRadiusPx = isBlur ? blurRadiusBase * std::max(alpha, 0.05f) : blurRadiusBase;
    const float blurKernelReach = std::max(blurRadiusPx * 1.22474487139f, 1.0f);

    if (isBlur) {
        int32_t roiMinX
            = static_cast<int32_t>(std::floor(worldX - coverageExtent - blurKernelReach));
        int32_t roiMinY
            = static_cast<int32_t>(std::floor(worldY - coverageExtent - blurKernelReach));
        int32_t roiMaxX
            = static_cast<int32_t>(std::ceil(worldX + coverageExtent + blurKernelReach)) + 1;
        int32_t roiMaxY
            = static_cast<int32_t>(std::ceil(worldY + coverageExtent + blurKernelReach)) + 1;

        if (clipToCanvas) {
            roiMinX = std::max<int32_t>(roiMinX, 0);
            roiMinY = std::max<int32_t>(roiMinY, 0);
            roiMaxX = std::min<int32_t>(roiMaxX, static_cast<int32_t>(canvasWidth));
            roiMaxY = std::min<int32_t>(roiMaxY, static_cast<int32_t>(canvasHeight));
        }
        if (roiMaxX <= roiMinX || roiMaxY <= roiMinY) {
            finishOwnBatch();
            return;
        }

        const GLsizei roiWidth = static_cast<GLsizei>(roiMaxX - roiMinX);
        const GLsizei roiHeight = static_cast<GLsizei>(roiMaxY - roiMinY);
        if (!ensureBlurScratchSize(
                roiWidth, roiHeight, layerGrid ? layerGrid->format() : kDefaultTileFormat)) {
            finishOwnBatch();
            return;
        }

        m_gl->glBindVertexArray(m_emptyVAO);
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glDisable(GL_BLEND);
        clearTexture(m_blurScratchSourceTex, layerGrid ? layerGrid->defaultFillPacked() : 0u);

        int32_t srcMinTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMinX) / TILE_SIZE));
        int32_t srcMinTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMinY) / TILE_SIZE));
        int32_t srcMaxTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMaxX - 1) / TILE_SIZE));
        int32_t srcMaxTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMaxY - 1) / TILE_SIZE));

        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            srcMinTileX = std::max(srcMinTileX, 0);
            srcMinTileY = std::max(srcMinTileY, 0);
            srcMaxTileX = std::min(srcMaxTileX, canvasMaxX);
            srcMaxTileY = std::min(srcMaxTileY, canvasMaxY);
        }

        for (int32_t srcTy = srcMinTileY; srcTy <= srcMaxTileY; ++srcTy) {
            for (int32_t srcTx = srcMinTileX; srcTx <= srcMaxTileX; ++srcTx) {
                TileKey srcKey { srcTx, srcTy };
                TileData* srcTile = strokeBuffer.getTile(srcKey);
                ensureUploadedTileTexture(srcTile);
                if (!srcTile || !srcTile->hasTexture()) {
                    srcTile = layerGrid->getTile(srcKey);
                    ensureUploadedTileTexture(srcTile);
                }
                if (!srcTile || !srcTile->hasTexture()) {
                    continue;
                }

                const int32_t tileMinX = srcTx * static_cast<int32_t>(TILE_SIZE);
                const int32_t tileMinY = srcTy * static_cast<int32_t>(TILE_SIZE);
                const int32_t copyMinX = std::max(roiMinX, tileMinX);
                const int32_t copyMinY = std::max(roiMinY, tileMinY);
                const int32_t copyMaxX
                    = std::min(roiMaxX, tileMinX + static_cast<int32_t>(TILE_SIZE));
                const int32_t copyMaxY
                    = std::min(roiMaxY, tileMinY + static_cast<int32_t>(TILE_SIZE));
                if (copyMaxX <= copyMinX || copyMaxY <= copyMinY) {
                    continue;
                }

                if (!copyColorRegion(srcTile->textureId(), srcTile->format(), copyMinX - tileMinX,
                        copyMinY - tileMinY, m_blurScratchSourceTex, m_blurScratchFormat,
                        copyMinX - roiMinX, copyMinY - roiMinY, copyMaxX - copyMinX,
                        copyMaxY - copyMinY)) {
                    finishOwnBatch();
                    return;
                }
            }
        }

        clearTexture(m_blurScratchTempTex);

        m_gl->glViewport(0, 0, roiWidth, roiHeight);
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurScratchTempTex, 0);

        m_blurPassProgram->use();
        m_blurPassProgram->setUniform("uSourceTexture", 0);
        m_blurPassProgram->setUniform("uInvSourceSize", 1.0f / static_cast<float>(roiWidth),
            1.0f / static_cast<float>(roiHeight));
        m_blurPassProgram->setUniform("uTexelStep", 1.0f / static_cast<float>(roiWidth), 0.0f);
        m_blurPassProgram->setUniform("uBlurRadius", blurRadiusPx);

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchSourceTex);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(0, m_blurLinearSampler);
        }

        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;

        if (m_blurLinearSampler) {
            m_gl->glBindSampler(0, 0);
        }

        m_blurProgram->use();
        m_blurProgram->setUniform("uOriginalTexture", 0);
        m_blurProgram->setUniform("uBlurTexture", 1);
        m_blurProgram->setUniform("uMaskTexture", 2);
        m_blurProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
        m_blurProgram->setUniform("uBrushRadius", radius);
        m_blurProgram->setUniform("uBrushHardness", hardness);
        m_blurProgram->setUniform("uBrushRoundness", std::clamp(roundness, 0.0f, 1.0f));
        m_blurProgram->setUniform(
            "uBrushAngleRad", angleDegrees * (3.14159265358979323846f / 180.0f));
        m_blurProgram->setUniform("uBrushAlpha", 1.0f);
        m_blurProgram->setUniform("uBlurRadius", blurRadiusPx);
        m_blurProgram->setUniform("uInvBlurSize", 1.0f / static_cast<float>(roiWidth),
            1.0f / static_cast<float>(roiHeight));
        m_blurProgram->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE),
            1.0f / static_cast<float>(TILE_SIZE));
        m_blurProgram->setUniform(
            "uRoiOriginPx", static_cast<float>(roiMinX), static_cast<float>(roiMinY));
        m_blurProgram->setUniform("uInvRoiSize", 1.0f / static_cast<float>(roiWidth),
            1.0f / static_cast<float>(roiHeight));

        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchTempTex);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(1, m_blurLinearSampler);
        }
        m_gl->glActiveTexture(GL_TEXTURE0);

        m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                TileKey key { tx, ty };
                const float tileOriginX = tx * static_cast<float>(TILE_SIZE);
                const float tileOriginY = ty * static_cast<float>(TILE_SIZE);
                const float localCenterX = worldX - tileOriginX;
                const float localCenterY = worldY - tileOriginY;

                float quadMinX = std::max(0.0f, std::floor(localCenterX - coverageExtent));
                float quadMinY = std::max(0.0f, std::floor(localCenterY - coverageExtent));
                float quadMaxX = std::min(
                    static_cast<float>(TILE_SIZE), std::ceil(localCenterX + coverageExtent) + 1.0f);
                float quadMaxY = std::min(
                    static_cast<float>(TILE_SIZE), std::ceil(localCenterY + coverageExtent) + 1.0f);

                if (clipToCanvas) {
                    const float canvasLocalMinX = std::max(0.0f, -tileOriginX);
                    const float canvasLocalMinY = std::max(0.0f, -tileOriginY);
                    const float canvasLocalMaxX = std::min(static_cast<float>(TILE_SIZE),
                        static_cast<float>(canvasWidth) - tileOriginX);
                    const float canvasLocalMaxY = std::min(static_cast<float>(TILE_SIZE),
                        static_cast<float>(canvasHeight) - tileOriginY);
                    quadMinX = std::max(quadMinX, canvasLocalMinX);
                    quadMinY = std::max(quadMinY, canvasLocalMinY);
                    quadMaxX = std::min(quadMaxX, canvasLocalMaxX);
                    quadMaxY = std::min(quadMaxY, canvasLocalMaxY);
                }
                if (quadMaxX <= quadMinX || quadMaxY <= quadMinY) {
                    continue;
                }

                if (useSelectionMask) {
                    TileData* maskTile = selectionMask->getTile(key);
                    if (!maskTile) {
                        continue;
                    }
                    ensureUploadedTileTexture(maskTile);
                    if (!maskTile->hasTexture()) {
                        continue;
                    }
                    m_gl->glActiveTexture(GL_TEXTURE2);
                    m_gl->glBindTexture(GL_TEXTURE_2D, maskTile->textureId());
                    m_gl->glActiveTexture(GL_TEXTURE0);
                }

                const bool tileAlreadyExists = strokeBuffer.hasTile(key);
                TileData& tile = strokeBuffer.getOrCreateTile(key);
                const bool hadTexture = tile.hasTexture();
                if (!hadTexture) {
                    tileRenderer->ensureTileTexture(tile);
                }

                if (!tileAlreadyExists || !hadTexture) {
                    TileData* layerTile = layerGrid->getTile(key);
                    ensureUploadedTileTexture(layerTile);
                    if (layerTile && layerTile->hasTexture()) {
                        if (!copyColorRegion(layerTile->textureId(), layerTile->format(), 0, 0,
                                tile.textureId(), tile.format(), 0, 0, TILE_SIZE, TILE_SIZE)) {
                            finishOwnBatch();
                            return;
                        }
                    } else {
                        clearTexture(
                            tile.textureId(), layerGrid ? layerGrid->defaultFillPacked() : 0u);
                    }
                }

                const GLint quadCopyMinX = static_cast<GLint>(quadMinX);
                const GLint quadCopyMinY = static_cast<GLint>(quadMinY);
                const GLint quadCopyMaxX = static_cast<GLint>(quadMaxX);
                const GLint quadCopyMaxY = static_cast<GLint>(quadMaxY);
                const GLsizei quadCopyWidth = quadCopyMaxX - quadCopyMinX;
                const GLsizei quadCopyHeight = quadCopyMaxY - quadCopyMinY;
                if (quadCopyWidth > 0 && quadCopyHeight > 0) {
                    const GLint sourceQuadX
                        = static_cast<GLint>(tileOriginX) + quadCopyMinX - roiMinX;
                    const GLint sourceQuadY
                        = static_cast<GLint>(tileOriginY) + quadCopyMinY - roiMinY;
                    m_gl->glCopyImageSubData(m_blurScratchSourceTex, GL_TEXTURE_2D, 0, sourceQuadX,
                        sourceQuadY, 0, tile.textureId(), GL_TEXTURE_2D, 0, quadCopyMinX,
                        quadCopyMinY, 0, quadCopyWidth, quadCopyHeight, 1);
                }

                m_gl->glFramebufferTexture2D(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);
                m_blurProgram->setUniform("uTileOriginPx", tileOriginX, tileOriginY);
                m_blurProgram->setUniform("uBrushCenter", localCenterX, localCenterY);
                m_blurProgram->setUniform("uQuadMin", quadMinX, quadMinY);
                m_blurProgram->setUniform("uQuadMax", quadMaxX, quadMaxY);

                m_gl->glActiveTexture(GL_TEXTURE0);
                m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchSourceTex);
                m_gl->glActiveTexture(GL_TEXTURE1);
                m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchTempTex);
                m_gl->glActiveTexture(GL_TEXTURE0);

                m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
                ++m_drawCallEstimate;

                tile.clearDirty();
                strokeBuffer.removeDirty(key);
            }
        }

        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(1, 0);
        }
        if (useSelectionMask) {
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        }

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(0, 0);
        }

        finishOwnBatch();
        return;
    }

    const bool hasCarryPrograms = wetMode ? (m_wetApplyProgram && m_wetPickupProgram)
                                          : (m_smudgeProgram && m_smudgePickupProgram);
    const bool isSmudge = wantsSmudge && hasCarryPrograms && layerGrid;
    if (wantsSmudge && !isSmudge) {
        finishOwnBatch();
        return;
    }

    if (isSmudge) {
        // Wet keeps the four-plane pigment latent path. Ordinary Smudge uses
        // the original premultiplied-RGBA carry shaders and reservoir plane 0;
        // transparent canvas pixels therefore cannot introduce pigment state.
        GLShaderProgram* pickupProgram
            = wetMode ? m_wetPickupProgram.get() : m_smudgePickupProgram.get();
        GLShaderProgram* applyProgram = wetMode ? m_wetApplyProgram.get() : m_smudgeProgram.get();
        // --- Carry-buffer (reservoir) smudge ---------------------------------
        // Allocate the reservoir lazily on first use. Logical size is chosen
        // to enclose the max possible brush footprint for this stroke; the
        // physical texture may be larger due to geometric growth across
        // strokes. Half-size centers the brush in the reservoir.
        const GLsizei reservoirLogical = computeSmudgeReservoirLogicalSize(brush);
        // If the reservoir gets reallocated mid-stroke (brush.radius()
        // changed enough to need a larger texture), the old contents are
        // gone — force a re-init on this dab.
        const bool reservoirRealloc = !reservoirTexturesComplete(m_smudgeReservoirTex)
            || m_smudgeReservoirSize < reservoirLogical;
        if (!ensureSmudgeReservoirTextures(
                m_gl, m_smudgeReservoirTex, m_smudgeReservoirSize, reservoirLogical)) {
            finishOwnBatch();
            return;
        }
        if (reservoirRealloc)
            m_smudgePrevValid = false;
        // Wipe stale pigment from a previous stroke (e.g. after undo) before
        // this stroke loads its own. Clears the full physical texture so no
        // edge ring survives to bleed into the new stroke.
        if (m_smudgeReservoirNeedsClear) {
            for (auto& side : m_smudgeReservoirTex) {
                for (const GLuint texture : side)
                    clearTexture(texture);
            }
            m_smudgeReservoirSrcIdx = 0;
            m_smudgePrevValid = false;
            m_smudgeReservoirNeedsClear = false;
        }
        m_smudgeReservoirActive = reservoirLogical;
        const float reservoirHalf = 0.5f * static_cast<float>(reservoirLogical);
        const float invReservoirPhys = 1.0f / static_cast<float>(m_smudgeReservoirSize);

        const bool firstDab = !m_smudgePrevValid;
        const float previousWorldX = m_smudgePrevWorldX;
        const float previousWorldY = m_smudgePrevWorldY;
        const float paintSupplyBeforePickup = m_wetPaintSupply;

        // Pickup needs the reservoir footprint, while apply needs the actual
        // dynamically evaluated dab footprint. Neither is guaranteed to contain
        // the other (angle/size/hardness jitter can expand the rendered dab).
        const float roiHalfExtent = std::max(reservoirHalf, coverageExtent);
        int32_t roiMinX = static_cast<int32_t>(std::floor(worldX - roiHalfExtent));
        int32_t roiMinY = static_cast<int32_t>(std::floor(worldY - roiHalfExtent));
        int32_t roiMaxX = static_cast<int32_t>(std::ceil(worldX + roiHalfExtent)) + 1;
        int32_t roiMaxY = static_cast<int32_t>(std::ceil(worldY + roiHalfExtent)) + 1;

        if (clipToCanvas) {
            roiMinX = std::max<int32_t>(roiMinX, 0);
            roiMinY = std::max<int32_t>(roiMinY, 0);
            roiMaxX = std::min<int32_t>(roiMaxX, static_cast<int32_t>(canvasWidth));
            roiMaxY = std::min<int32_t>(roiMaxY, static_cast<int32_t>(canvasHeight));
        }
        if (roiMaxX <= roiMinX || roiMaxY <= roiMinY) {
            finishOwnBatch();
            return;
        }

        const GLsizei roiWidth = static_cast<GLsizei>(roiMaxX - roiMinX);
        const GLsizei roiHeight = static_cast<GLsizei>(roiMaxY - roiMinY);
        const TilePixelFormat workFormat = wetMode
            ? wet_pigment_gpu::workingColorFormat(layerGrid->format())
            : layerGrid->format();
        if (!ensureBlurScratchSize(roiWidth, roiHeight, workFormat)) {
            finishOwnBatch();
            return;
        }

        int32_t srcMinTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMinX) / TILE_SIZE));
        int32_t srcMinTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMinY) / TILE_SIZE));
        int32_t srcMaxTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMaxX - 1) / TILE_SIZE));
        int32_t srcMaxTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(roiMaxY - 1) / TILE_SIZE));

        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            srcMinTileX = std::max(srcMinTileX, 0);
            srcMinTileY = std::max(srcMinTileY, 0);
            srcMaxTileX = std::min(srcMaxTileX, canvasMaxX);
            srcMaxTileY = std::min(srcMaxTileY, canvasMaxY);
        }

        m_gl->glBindVertexArray(m_emptyVAO);
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glDisable(GL_BLEND);
        clearTexture(m_blurScratchSourceTex, layerGrid ? layerGrid->defaultFillPacked() : 0u);

        // --- 1. Snapshot canvas (stroke-buffer-preferred) into ROI scratch ---
        // Reading from the stroke buffer where it exists lets the drag
        // accumulate across overlapping dabs (dab N picks up pigment painted
        // by dab N-1), which is what produces a continuous smear instead of
        // per-dab translations of the original canvas. Wet uses the SAME
        // evolving snapshot: pickup must die out once the stroke has covered
        // the paint underneath (a pre-stroke-layer source was tried and
        // reverted — the brush kept re-tinting forever from buried colors).
        for (int32_t srcTy = srcMinTileY; srcTy <= srcMaxTileY; ++srcTy) {
            for (int32_t srcTx = srcMinTileX; srcTx <= srcMaxTileX; ++srcTx) {
                TileKey srcKey { srcTx, srcTy };
                TileData* srcTile = strokeBuffer.getTile(srcKey);
                ensureUploadedTileTexture(srcTile);
                if (!srcTile || !srcTile->hasTexture()) {
                    srcTile = layerGrid->getTile(srcKey);
                    if (srcTile && srcTile->isEmpty() && !layerGrid->hasDefaultFill()) {
                        continue;
                    }
                    ensureUploadedTileTexture(srcTile);
                }
                if (!srcTile || !srcTile->hasTexture()) {
                    continue;
                }

                const int32_t tileMinX = srcTx * static_cast<int32_t>(TILE_SIZE);
                const int32_t tileMinY = srcTy * static_cast<int32_t>(TILE_SIZE);
                const int32_t copyMinX = std::max(roiMinX, tileMinX);
                const int32_t copyMinY = std::max(roiMinY, tileMinY);
                const int32_t copyMaxX
                    = std::min(roiMaxX, tileMinX + static_cast<int32_t>(TILE_SIZE));
                const int32_t copyMaxY
                    = std::min(roiMaxY, tileMinY + static_cast<int32_t>(TILE_SIZE));
                if (copyMaxX <= copyMinX || copyMaxY <= copyMinY) {
                    continue;
                }

                if (!copyColorRegion(srcTile->textureId(), srcTile->format(), copyMinX - tileMinX,
                        copyMinY - tileMinY, m_blurScratchSourceTex, m_blurScratchFormat,
                        copyMinX - roiMinX, copyMinY - roiMinY, copyMaxX - copyMinX,
                        copyMaxY - copyMinY)) {
                    finishOwnBatch();
                    return;
                }
            }
        }

        const float roiOriginX = static_cast<float>(roiMinX);
        const float roiOriginY = static_cast<float>(roiMinY);
        const float invRoiW = 1.0f / static_cast<float>(roiWidth);
        const float invRoiH = 1.0f / static_cast<float>(roiHeight);
        const float brushAngleRad = angleDegrees * (3.14159265358979323846f / 180.0f);
        const float clampedRoundness = std::clamp(roundness, 0.0f, 1.0f);
        const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);

        GLuint dabTexId = 0;
        if (brush.dabType() > 0) {
            dabTexId = resolveDabTextureId(m_gl, brush);
        }
        const int useDabShape = dabTexId != 0 ? 1 : 0;
        if (dabTexId != 0) {
            m_gl->glActiveTexture(GL_TEXTURE3);
            m_gl->glBindTexture(GL_TEXTURE_2D, dabTexId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }

        // --- 2. Pickup pass: update reservoir from canvas ROI snapshot --------
        // Render into the reservoir ping-pong destination. The pickup shader
        // samples canvas at (brushWorld + (frag - reservoirHalf)) and blends
        // into the previous reservoir by (pickupRate * falloff). On the
        // first dab of the stroke uInit=1 forces a full canvas copy so the
        // brush starts fully loaded — without this, low-intensity strokes
        // would converge to a fixed point in a couple of dabs.
        const int reservoirSrcIdx = m_smudgeReservoirSrcIdx;
        const int reservoirDstIdx = reservoirSrcIdx ^ 1;
        pickupProgram->use();
        pickupProgram->setUniform("uOriginalTexture", 0);
        pickupProgram->setUniform("uBrushWorldPos", worldX, worldY);
        if (wetMode) {
            pickupProgram->setUniform("uBrushRadius", radius);
            pickupProgram->setUniform("uBrushRoundness", clampedRoundness);
            pickupProgram->setUniform("uBrushAngleRad", brushAngleRad);
            pickupProgram->setUniform("uUsePen", 1);
        } else {
            pickupProgram->setUniform("uReservoirSrc", 1);
            pickupProgram->setUniform("uPickupRate", std::clamp(brush.wetMix(), 0.0f, 1.0f));
        }
        // Travel since the previous dab — wet rates are defined per
        // half-radius of travel, not per dab (see wetRatePerDab).
        const float dabDist = firstDab
            ? 0.0f
            : std::hypot(worldX - m_smudgePrevWorldX, worldY - m_smudgePrevWorldY);
        // Advection offset for the wet reservoir: the world-space travel
        // since the previous dab (= strokeDir * dabDist). Zero when
        // stationary / first dab => the advected sample degenerates to
        // prev(here), weight-neutral.
        float advectPxX = 0.0f;
        float advectPxY = 0.0f;
        if (dabDist > 0.01f) {
            advectPxX = worldX - m_smudgePrevWorldX;
            advectPxY = worldY - m_smudgePrevWorldY;
        }
        m_smudgePrevWorldX = worldX;
        m_smudgePrevWorldY = worldY;
        if (wetMode) {
            // Finite paint supply (the "Drying" slider). The supply starts
            // full each stroke and decays exponentially with travel — same
            // per-half-radius rate convention as the other wet sliders. It
            // scales the EFFECTIVE spread, so the exchange target slides
            // from mix(canvas, pen, spread) toward the bare canvas: the
            // stroke starts at the pen color and runs dry into a pure
            // smudge/blend of whatever the reservoir still carries.
            if (firstDab) {
                m_wetPaintSupply = 1.0f;
            } else {
                m_wetPaintSupply *= 1.0f - wetRatePerDab(brush.colorDryRate(), dabDist, radius);
            }
            const float effectiveSpread = brush.colorSpread() * m_wetPaintSupply;
            // The reservoir is the single wet state: dilution drains it and
            // it exchanges with the canvas at the blending rate, toward a
            // target of mix(canvas, pen, spread) — uSpread is the (supply-
            // scaled) slider, a target fraction, NOT a spacing-normalized
            // rate. The same fraction pre-loads the brush on the first dab
            // (uInit).
            // Exchange rate floor of `spread`: a brush with spread > 0 must
            // keep delivering its paint even at blending = 0 — otherwise the
            // reservoir freezes after the first dab and drags a stale canvas
            // snapshot along the whole stroke. The floor uses the supply-
            // scaled spread, so a dried-out brush falls back to the plain
            // blending-driven exchange.
            const float exchangeRate
                = std::max(brush.colorBlending() * (1.0f - brush.colorLength()), effectiveSpread);
            pickupProgram->setUniform(
                "uCanvasPickup", wetRatePerDab(exchangeRate, dabDist, radius));
            // Dilution slider -> drain rate: squared for fine control at the
            // low end while 100% reaches the full per-half-radius drain (the
            // previous fixed 0.15 scale capped the slider at a barely
            // visible effect).
            pickupProgram->setUniform("uDilution",
                wetRatePerDab(brush.colorDilution() * brush.colorDilution(), dabDist, radius));
            pickupProgram->setUniform("uSpread", effectiveSpread);
            pickupProgram->setUniform("uWetFlow", brush.colorWetFlow());
            // Pen-fill gate: refill the reservoir's blank-canvas alpha
            // shortfall from the pen only for non-watery brushes (dilution 0).
            // A dilution brush deliberately thins paint, so its low canvas
            // alpha must NOT be refilled — leaving its behaviour unchanged.
            pickupProgram->setUniform(
                "uPenFillGate", brush.colorDilution() <= 0.001f ? 1.0f : 0.0f);
            pickupProgram->setUniform("uAdvectPx", advectPxX, advectPxY);
            pickupProgram->setUniform("uPenColor", static_cast<float>(r) / 255.0f,
                static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f);
        }
        pickupProgram->setUniform("uInit", firstDab ? 1 : 0);
        pickupProgram->setUniform("uRoiOriginPx", roiOriginX, roiOriginY);
        pickupProgram->setUniform("uInvRoiSize", invRoiW, invRoiH);
        pickupProgram->setUniform("uReservoirHalf", reservoirHalf);
        pickupProgram->setUniform("uInvReservoirPhys", invReservoirPhys, invReservoirPhys);
        // The pickup vertex shader (kSmudgeBatchVert) outputs fragPixelCoord =
        // t * uViewportSize, so we set the viewport-size uniform to the
        // reservoir logical side. The actual GL viewport is set to the same
        // value below.
        pickupProgram->setUniform("uViewportSize", static_cast<float>(reservoirLogical),
            static_cast<float>(reservoirLogical));
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchSourceTex);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(0, m_blurLinearSampler);
        }
        bool pickupFramebufferComplete = false;
        if (wetMode) {
            bindWetLuts(m_gl, pickupProgram, m_pigmentLutTex);
            bindWetReservoir(
                m_gl, pickupProgram, m_smudgeReservoirTex[reservoirSrcIdx], m_blurLinearSampler);
            pickupFramebufferComplete
                = attachWetReservoir(m_gl, m_smudgeReservoirTex[reservoirDstIdx]);
        } else {
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, m_smudgeReservoirTex[reservoirSrcIdx][0]);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(1, m_blurLinearSampler);
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                m_smudgeReservoirTex[reservoirDstIdx][0], 0);
            pickupFramebufferComplete
                = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        if (!pickupFramebufferComplete) {
            m_smudgePrevWorldX = previousWorldX;
            m_smudgePrevWorldY = previousWorldY;
            m_wetPaintSupply = paintSupplyBeforePickup;
            restoreSingleColorTarget(m_gl);
            unbindWetTextures(m_gl);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(0, 0);
            if (dabTexId != 0) {
                m_gl->glActiveTexture(GL_TEXTURE3);
                m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            }
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            finishOwnBatch();
            return;
        }
        m_gl->glViewport(0, 0, reservoirLogical, reservoirLogical);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;
        restoreSingleColorTarget(m_gl);

        m_smudgeReservoirSrcIdx = reservoirDstIdx;

        // First dab of a SMUDGE stroke: reservoir is a copy of the canvas,
        // so the apply pass would be a visible no-op — skip it. A WET brush
        // must NOT skip: its reservoir was just pre-loaded with pen paint
        // (uInitLoad), so the first dab already deposits color (single
        // click/tap with a wet brush has to paint).
        if (firstDab && !wetMode) {
            m_smudgePrevValid = true;
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(1, 0);
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(0, 0);
            if (dabTexId != 0) {
                m_gl->glActiveTexture(GL_TEXTURE3);
                m_gl->glBindTexture(GL_TEXTURE_2D, 0);
                m_gl->glActiveTexture(GL_TEXTURE0);
            }
            unbindWetTextures(m_gl);
            finishOwnBatch();
            return;
        }

        // --- 3. Apply pass: deposit reservoir onto canvas per affected tile -
        applyProgram->use();
        applyProgram->setUniform("uOriginalTexture", 0);
        if (wetMode) {
            applyProgram->setUniform("uPreserveCanvasAlpha", 0);
            applyProgram->setUniform(
                "uQuantizeTo8Bit", m_blurScratchFormat == TilePixelFormat::RGBA8 ? 1 : 0);
            // Layering uses a spacing-normalized thin coat. A negative value
            // selects the normal wet deposit path.
            applyProgram->setUniform("uCoatPerDab",
                brush.colorBuildup() > 0.001f ? m_wetPaintSupply
                        * buildupCoatPerDab(brush.colorBuildup(), dabDist, brush.radius())
                                              : -1.0f);
            applyProgram->setUniform("uDepositRate",
                firstDab ? clampedAlpha : wetRatePerDab(clampedAlpha, dabDist, radius));
        } else {
            applyProgram->setUniform("uReservoirTexture", 1);
        }
        applyProgram->setUniform("uMaskTexture", 2);
        applyProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
        applyProgram->setUniform("uBrushRadius", radius);
        applyProgram->setUniform("uBrushHardness", hardness);
        applyProgram->setUniform("uBrushRoundness", clampedRoundness);
        applyProgram->setUniform("uBrushAngleRad", brushAngleRad);
        applyProgram->setUniform("uBrushAlpha", clampedAlpha);
        applyProgram->setUniform("uRoiOriginPx", roiOriginX, roiOriginY);
        applyProgram->setUniform("uInvRoiSize", invRoiW, invRoiH);
        applyProgram->setUniform("uReservoirHalf", reservoirHalf);
        applyProgram->setUniform("uInvReservoirPhys", invReservoirPhys, invReservoirPhys);
        applyProgram->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE),
            1.0f / static_cast<float>(TILE_SIZE));
        applyProgram->setUniform("uDabShapeScale", brush.dabXScale(), brush.dabYScale());
        applyProgram->setUniform(
            "uDabSoftEdgeRadiusTexels", dab_shape_falloff::kSoftEdgeRadiusTexels);
        applyProgram->setUniform("uUseDabShapeTexture", useDabShape);
        if (dabTexId != 0) {
            applyProgram->setUniform("uDabShapeTexture", 3);
        }

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchSourceTex);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(0, m_blurLinearSampler);
        }
        if (wetMode) {
            bindWetReservoir(m_gl, applyProgram, m_smudgeReservoirTex[m_smudgeReservoirSrcIdx],
                m_blurLinearSampler);
        } else {
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, m_smudgeReservoirTex[m_smudgeReservoirSrcIdx][0]);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(1, m_blurLinearSampler);
        }
        m_gl->glActiveTexture(GL_TEXTURE0);

        m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                TileKey key { tx, ty };
                const float tileOriginX = tx * static_cast<float>(TILE_SIZE);
                const float tileOriginY = ty * static_cast<float>(TILE_SIZE);
                const float localCenterX = worldX - tileOriginX;
                const float localCenterY = worldY - tileOriginY;

                float quadMinX = std::max(0.0f, std::floor(localCenterX - coverageExtent));
                float quadMinY = std::max(0.0f, std::floor(localCenterY - coverageExtent));
                float quadMaxX = std::min(
                    static_cast<float>(TILE_SIZE), std::ceil(localCenterX + coverageExtent) + 1.0f);
                float quadMaxY = std::min(
                    static_cast<float>(TILE_SIZE), std::ceil(localCenterY + coverageExtent) + 1.0f);

                if (clipToCanvas) {
                    const float canvasLocalMinX = std::max(0.0f, -tileOriginX);
                    const float canvasLocalMinY = std::max(0.0f, -tileOriginY);
                    const float canvasLocalMaxX = std::min(static_cast<float>(TILE_SIZE),
                        static_cast<float>(canvasWidth) - tileOriginX);
                    const float canvasLocalMaxY = std::min(static_cast<float>(TILE_SIZE),
                        static_cast<float>(canvasHeight) - tileOriginY);
                    quadMinX = std::max(quadMinX, canvasLocalMinX);
                    quadMinY = std::max(quadMinY, canvasLocalMinY);
                    quadMaxX = std::min(quadMaxX, canvasLocalMaxX);
                    quadMaxY = std::min(quadMaxY, canvasLocalMaxY);
                }
                if (quadMaxX <= quadMinX || quadMaxY <= quadMinY) {
                    continue;
                }

                if (useSelectionMask) {
                    TileData* maskTile = selectionMask->getTile(key);
                    if (!maskTile) {
                        continue;
                    }
                    ensureUploadedTileTexture(maskTile);
                    if (!maskTile->hasTexture()) {
                        continue;
                    }
                    m_gl->glActiveTexture(GL_TEXTURE2);
                    m_gl->glBindTexture(GL_TEXTURE_2D, maskTile->textureId());
                    m_gl->glActiveTexture(GL_TEXTURE0);
                }

                const bool tileAlreadyExists = strokeBuffer.hasTile(key);
                TileData& tile = strokeBuffer.getOrCreateTile(key);
                const bool hadTexture = tile.hasTexture();
                if (!hadTexture) {
                    tileRenderer->ensureTileTexture(tile);
                }
                if (!tileAlreadyExists || !hadTexture) {
                    TileData* layerTile = layerGrid->getTile(key);
                    if (layerTile && layerTile->isEmpty() && !layerGrid->hasDefaultFill()) {
                        layerTile = nullptr;
                    }
                    ensureUploadedTileTexture(layerTile);
                    if (layerTile && layerTile->hasTexture()) {
                        if (!copyColorRegion(layerTile->textureId(), layerTile->format(), 0, 0,
                                tile.textureId(), tile.format(), 0, 0, TILE_SIZE, TILE_SIZE)) {
                            finishOwnBatch();
                            return;
                        }
                    } else {
                        clearTexture(
                            tile.textureId(), layerGrid ? layerGrid->defaultFillPacked() : 0u);
                    }
                }

                // Pre-fill the to-be-drawn quad with the canvas snapshot so
                // discard-ed fragments retain identity (smudge shader uses
                // `discard` outside the brush ellipse).
                const GLint quadCopyMinX = static_cast<GLint>(quadMinX);
                const GLint quadCopyMinY = static_cast<GLint>(quadMinY);
                const GLint quadCopyMaxX = static_cast<GLint>(quadMaxX);
                const GLint quadCopyMaxY = static_cast<GLint>(quadMaxY);
                const GLsizei quadCopyWidth = quadCopyMaxX - quadCopyMinX;
                const GLsizei quadCopyHeight = quadCopyMaxY - quadCopyMinY;
                if (quadCopyWidth > 0 && quadCopyHeight > 0) {
                    const GLint sourceQuadX
                        = static_cast<GLint>(tileOriginX) + quadCopyMinX - roiMinX;
                    const GLint sourceQuadY
                        = static_cast<GLint>(tileOriginY) + quadCopyMinY - roiMinY;
                    m_gl->glCopyImageSubData(m_blurScratchSourceTex, GL_TEXTURE_2D, 0, sourceQuadX,
                        sourceQuadY, 0, tile.textureId(), GL_TEXTURE_2D, 0, quadCopyMinX,
                        quadCopyMinY, 0, quadCopyWidth, quadCopyHeight, 1);
                }

                m_gl->glFramebufferTexture2D(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);
                applyProgram->setUniform("uTileOriginPx", tileOriginX, tileOriginY);
                applyProgram->setUniform("uBrushCenter", localCenterX, localCenterY);
                applyProgram->setUniform("uQuadMin", quadMinX, quadMinY);
                applyProgram->setUniform("uQuadMax", quadMaxX, quadMaxY);

                // Tile allocation/upload helpers re-bind GL_TEXTURE_2D on the
                // active unit, so restore the smudge inputs after them.
                if (wetMode) {
                    bindWetReservoir(m_gl, applyProgram,
                        m_smudgeReservoirTex[m_smudgeReservoirSrcIdx], m_blurLinearSampler);
                } else {
                    m_gl->glActiveTexture(GL_TEXTURE1);
                    m_gl->glBindTexture(
                        GL_TEXTURE_2D, m_smudgeReservoirTex[m_smudgeReservoirSrcIdx][0]);
                    if (m_blurLinearSampler)
                        m_gl->glBindSampler(1, m_blurLinearSampler);
                }
                m_gl->glActiveTexture(GL_TEXTURE0);
                m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchSourceTex);

                m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
                ++m_drawCallEstimate;

                tile.clearDirty();
                strokeBuffer.removeDirty(key);
            }
        }

        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (m_blurLinearSampler)
            m_gl->glBindSampler(1, 0);
        if (useSelectionMask) {
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        }
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (m_blurLinearSampler)
            m_gl->glBindSampler(0, 0);
        if (dabTexId != 0) {
            m_gl->glActiveTexture(GL_TEXTURE3);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
        unbindWetTextures(m_gl);

        m_smudgePrevValid = true;

        finishOwnBatch();
        return;
    }

    GLShaderProgram* prog = m_brushProgram.get();
    prog->use();
    prog->setUniform("uBrushRadius", radius);
    prog->setUniform("uBrushHardness", hardness);
    prog->setUniform("uBrushRoundness", std::clamp(roundness, 0.0f, 1.0f));
    prog->setUniform("uBrushAngleRad", angleDegrees * (3.14159265358979323846f / 180.0f));
    prog->setUniform("uBrushAlpha", alpha);
    prog->setUniform("uBrushColorRGB", colorR, colorG, colorB);

    const bool useTexture = brush.usesProceduralTexture();
    const bool useDabShape = (brush.dabType() > 0);
    prog->setUniform("uUseMask", useSelectionMask ? 1 : 0);
    prog->setUniform("uMaskAffectsAlpha", brush.selectionMaskAffectsAlpha() ? 1 : 0);
    prog->setUniform("uUseTexture", useTexture ? 1 : 0);
    prog->setUniform("uTextureEdgeBoost", brush.textureEdgeBoost());
    prog->setUniform("uUseDabShapeTexture", useDabShape ? 1 : 0);
    prog->setUniform("uDabShapeScale", brush.dabXScale(), brush.dabYScale());
    prog->setUniform("uDabSoftEdgeRadiusTexels", dab_shape_falloff::kSoftEdgeRadiusTexels);

    if (useSelectionMask) {
        prog->setUniform("uMaskTexture", 1);
        prog->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE));
    }
    if (useTexture) {
        prog->setUniform("uTextureTile", 2);
        prog->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE));
    }
    if (useDabShape) {
        prog->setUniform("uDabShapeTexture", 3);
        const GLuint dabTexId = resolveDabTextureId(m_gl, brush);
        if (dabTexId != 0) {
            m_gl->glActiveTexture(GL_TEXTURE3);
            m_gl->glBindTexture(GL_TEXTURE_2D, dabTexId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
    }

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    if (useMaxBlend) {
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendEquation(GL_MAX);
        m_gl->glBlendFunc(GL_ONE, GL_ONE);
    } else {
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendEquation(GL_FUNC_ADD);
        m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
        for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
            TileKey key { tx, ty };
            float tileOriginX = tx * static_cast<float>(TILE_SIZE);
            float tileOriginY = ty * static_cast<float>(TILE_SIZE);

            float localCenterX = worldX - tileOriginX;
            float localCenterY = worldY - tileOriginY;

            float quadMinX = std::max(0.0f, std::floor(localCenterX - coverageExtent));
            float quadMinY = std::max(0.0f, std::floor(localCenterY - coverageExtent));
            float quadMaxX = std::min(
                static_cast<float>(TILE_SIZE), std::ceil(localCenterX + coverageExtent) + 1.0f);
            float quadMaxY = std::min(
                static_cast<float>(TILE_SIZE), std::ceil(localCenterY + coverageExtent) + 1.0f);

            if (clipToCanvas) {
                const float canvasLocalMinX = std::max(0.0f, -tileOriginX);
                const float canvasLocalMinY = std::max(0.0f, -tileOriginY);
                const float canvasLocalMaxX = std::min(
                    static_cast<float>(TILE_SIZE), static_cast<float>(canvasWidth) - tileOriginX);
                const float canvasLocalMaxY = std::min(
                    static_cast<float>(TILE_SIZE), static_cast<float>(canvasHeight) - tileOriginY);
                quadMinX = std::max(quadMinX, canvasLocalMinX);
                quadMinY = std::max(quadMinY, canvasLocalMinY);
                quadMaxX = std::min(quadMaxX, canvasLocalMaxX);
                quadMaxY = std::min(quadMaxY, canvasLocalMaxY);
            }

            if (quadMaxX <= quadMinX || quadMaxY <= quadMinY)
                continue;

            if (useSelectionMask) {
                TileData* maskTile = selectionMask->getTile(key);
                if (!maskTile)
                    continue;
                if (!maskTile->hasTexture()) {
                    tileRenderer->ensureTileTexture(*maskTile);
                    tileRenderer->uploadTileData(*maskTile);
                }
                m_gl->glActiveTexture(GL_TEXTURE1);
                m_gl->glBindTexture(GL_TEXTURE_2D, maskTile->textureId());
                m_gl->glActiveTexture(GL_TEXTURE0);
            }

            if (useTexture) {
                const GLuint textureTileId = ensureProceduralTextureTile(key, brush);
                m_gl->glActiveTexture(GL_TEXTURE2);
                m_gl->glBindTexture(GL_TEXTURE_2D, textureTileId);
                m_gl->glActiveTexture(GL_TEXTURE0);
            }

            bool isNew = !strokeBuffer.hasTile(key);
            TileData& tile = strokeBuffer.getOrCreateTile(key);

            if (!tile.hasTexture()) {
                tileRenderer->ensureTileTexture(tile);
                clearTexture(tile.textureId());
                m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
                m_gl->glEnable(GL_BLEND);
                if (useMaxBlend) {
                    m_gl->glBlendEquation(GL_MAX);
                    m_gl->glBlendFunc(GL_ONE, GL_ONE);
                } else {
                    m_gl->glBlendEquation(GL_FUNC_ADD);
                    m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                }
                prog->use();
            } else if (isNew) {
                clearTexture(tile.textureId());
                m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
                m_gl->glEnable(GL_BLEND);
                if (useMaxBlend) {
                    m_gl->glBlendEquation(GL_MAX);
                    m_gl->glBlendFunc(GL_ONE, GL_ONE);
                } else {
                    m_gl->glBlendEquation(GL_FUNC_ADD);
                    m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                }
                prog->use();
            }

            m_gl->glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);

            prog->setUniform("uBrushCenter", localCenterX, localCenterY);
            prog->setUniform("uQuadMin", quadMinX, quadMinY);
            prog->setUniform("uQuadMax", quadMaxX, quadMaxY);

            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            ++m_drawCallEstimate;

            tile.clearDirty();
            strokeBuffer.removeDirty(key);
        }
    }

    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }
    if (useDabShape) {
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }
    if (useTexture) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }

    finishOwnBatch();
}

bool GLBrushRenderer::stampDabSegmentGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
    const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, TileGrid* selectionMask,
    bool useSelectionMask, uint32_t canvasWidth, uint32_t canvasHeight)
{
    if (!m_initialized || !m_rebuildBatchProgram || !tileRenderer)
        return false;
    if (dabs.empty())
        return true;
    if (brush.isBlurMode() || brush.isSmudgeMode() || brush.isWetMode())
        return false;
    if (useSelectionMask && (!selectionMask || selectionMask->empty()))
        return true;

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    std::unordered_map<TileKey, std::vector<uint32_t>, TileKeyHash> tileDabs;
    tileDabs.reserve(dabs.size() * 2u);

    for (size_t idx = 0; idx < dabs.size(); ++idx) {
        const auto& dab = dabs[idx];
        if (dab.alpha == 0 || dab.radius <= 0.0f)
            continue;

        const float rasterExtent = dabCoverageExtent(
            brush, dab.radius, dab.hardness, dab.roundness, dab.angleDegrees, true);
        int32_t tMinX = static_cast<int32_t>(std::floor((dab.worldX - rasterExtent) / TILE_SIZE));
        int32_t tMinY = static_cast<int32_t>(std::floor((dab.worldY - rasterExtent) / TILE_SIZE));
        int32_t tMaxX = static_cast<int32_t>(std::floor((dab.worldX + rasterExtent) / TILE_SIZE));
        int32_t tMaxY = static_cast<int32_t>(std::floor((dab.worldY + rasterExtent) / TILE_SIZE));

        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            tMinX = std::max(tMinX, 0);
            tMinY = std::max(tMinY, 0);
            tMaxX = std::min(tMaxX, canvasMaxX);
            tMaxY = std::min(tMaxY, canvasMaxY);
            if (tMinX > tMaxX || tMinY > tMaxY)
                continue;
        }

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                tileDabs[TileKey { tx, ty }].push_back(static_cast<uint32_t>(idx));
            }
        }
    }

    if (tileDabs.empty())
        return true;

    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glEnable(GL_BLEND);

    m_rebuildBatchProgram->use();
    m_rebuildBatchProgram->setUniform("uMaskTexture", 1);
    m_rebuildBatchProgram->setUniform("uTextureTile", 2);
    m_rebuildBatchProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
    m_rebuildBatchProgram->setUniform(
        "uMaskAffectsAlpha", brush.selectionMaskAffectsAlpha() ? 1 : 0);
    const bool useTexture = brush.usesProceduralTexture();
    m_rebuildBatchProgram->setUniform("uUseTexture", useTexture ? 1 : 0);
    const bool useDabShape = (brush.dabType() > 0);
    m_rebuildBatchProgram->setUniform("uUseDabShapeTexture", useDabShape ? 1 : 0);
    m_rebuildBatchProgram->setUniform("uTextureEdgeBoost", brush.textureEdgeBoost());
    m_rebuildBatchProgram->setUniform("uDabShapeScale", brush.dabXScale(), brush.dabYScale());
    m_rebuildBatchProgram->setUniform(
        "uDabSoftEdgeRadiusTexels", dab_shape_falloff::kSoftEdgeRadiusTexels);
    m_rebuildBatchProgram->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE));
    m_rebuildBatchProgram->setUniform("uQuadMin", 0.0f, 0.0f);
    m_rebuildBatchProgram->setUniform(
        "uQuadMax", static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE));

    if (useDabShape) {
        m_rebuildBatchProgram->setUniform("uDabShapeTexture", 3);
        const GLuint dabTexId = resolveDabTextureId(m_gl, brush);
        if (dabTexId != 0) {
            m_gl->glActiveTexture(GL_TEXTURE3);
            m_gl->glBindTexture(GL_TEXTURE_2D, dabTexId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
    }

    const GLuint batchProgram = m_rebuildBatchProgram->handle();
    const GLint locDabCount = m_gl->glGetUniformLocation(batchProgram, "uDabCount");
    const GLint locDabCenter = m_gl->glGetUniformLocation(batchProgram, "uDabCenter");
    const GLint locDabParams = m_gl->glGetUniformLocation(batchProgram, "uDabParams");
    const GLint locDabColor = m_gl->glGetUniformLocation(batchProgram, "uDabColor");
    const GLint locBlendMode = m_gl->glGetUniformLocation(batchProgram, "uBlendMode");

    std::vector<float> centers(static_cast<size_t>(kRebuildBatchMaxDabs) * 2u);
    std::vector<float> params(static_cast<size_t>(kRebuildBatchMaxDabs) * 4u);
    std::vector<float> colors(static_cast<size_t>(kRebuildBatchMaxDabs) * 4u);

    for (const auto& [key, indices] : tileDabs) {
        if (indices.empty())
            continue;

        if (useSelectionMask) {
            TileData* maskTile = selectionMask->getTile(key);
            if (!maskTile)
                continue;
            if (!maskTile->hasTexture()) {
                tileRenderer->ensureTileTexture(*maskTile);
                tileRenderer->uploadTileData(*maskTile);
            }
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, maskTile->textureId());
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
        if (useTexture) {
            const GLuint textureTileId = ensureProceduralTextureTile(key, brush);
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, textureTileId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }

        const bool isNew = !strokeBuffer.hasTile(key);
        TileData& tile = strokeBuffer.getOrCreateTile(key);
        if (!tile.hasTexture()) {
            tileRenderer->ensureTileTexture(tile);
            clearTexture(tile.textureId());
        } else if (isNew) {
            clearTexture(tile.textureId());
        }

        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);
        m_gl->glEnable(GL_BLEND);

        const float tileOriginX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE);
        const float tileOriginY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE);
        renderDabBatchForTile(dabs, indices, tileOriginX, tileOriginY, locDabCount, locBlendMode,
            locDabCenter, locDabParams, locDabColor, centers, params, colors);

        tile.clearDirty();
        strokeBuffer.removeDirty(key);
    }

    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useDabShape) {
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE0);

    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    restoreDefaultPremultipliedBlendState(m_gl);
    return true;
}

void GLBrushRenderer::rebuildStrokeBufferFromDabsGPU(TileGrid& strokeBuffer,
    GLTileRenderer* tileRenderer, const TileBrush& brush,
    const std::vector<TileBrush::DabPoint>& dabs, size_t maxDabs, TileGrid* selectionMask,
    bool useSelectionMask, uint32_t canvasWidth, uint32_t canvasHeight)
{
    if (!m_initialized || !m_rebuildBatchProgram || !tileRenderer)
        return;
    // Rebuilding the stroke from scratch invalidates any in-flight smudge carry.
    m_smudgePrevValid = false;
    bool maskBlocksAll = useSelectionMask && (!selectionMask || selectionMask->empty());
    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);

    // Snapshot external GL state before any helper clears mutate FBO/blend state.
    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    std::vector<size_t> dabIndices;
    dabIndices.reserve(dabs.size());
    if (!maskBlocksAll) {
        if (maxDabs > 2 && dabs.size() > maxDabs) {
            const size_t last = dabs.size() - 1;
            size_t prev = static_cast<size_t>(-1);
            for (size_t i = 0; i < maxDabs; ++i) {
                float u = static_cast<float>(i) / static_cast<float>(maxDabs - 1);
                size_t idx = static_cast<size_t>(std::round(u * static_cast<float>(last)));
                if (idx == prev)
                    continue;
                dabIndices.push_back(idx);
                prev = idx;
            }
        } else {
            for (size_t i = 0; i < dabs.size(); ++i) {
                dabIndices.push_back(i);
            }
        }
    }

    std::unordered_set<TileKey, TileKeyHash> touched;
    touched.reserve(dabIndices.size() * 4);
    std::unordered_map<TileKey, std::vector<uint32_t>, TileKeyHash> tileDabs;
    tileDabs.reserve(dabIndices.size() * 2);
    for (size_t idx : dabIndices) {
        const auto& dab = dabs[idx];
        if (dab.alpha == 0 || dab.radius <= 0.0f)
            continue;
        const float r
            = dabCoverageExtent(brush, dab.radius, dab.hardness, dab.roundness, dab.angleDegrees);
        int32_t tMinX = static_cast<int32_t>(std::floor((dab.worldX - r) / TILE_SIZE));
        int32_t tMinY = static_cast<int32_t>(std::floor((dab.worldY - r) / TILE_SIZE));
        int32_t tMaxX = static_cast<int32_t>(std::floor((dab.worldX + r) / TILE_SIZE));
        int32_t tMaxY = static_cast<int32_t>(std::floor((dab.worldY + r) / TILE_SIZE));
        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            tMinX = std::max(tMinX, 0);
            tMinY = std::max(tMinY, 0);
            tMaxX = std::min(tMaxX, canvasMaxX);
            tMaxY = std::min(tMaxY, canvasMaxY);
            if (tMinX > tMaxX || tMinY > tMaxY) {
                continue;
            }
        }
        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                const TileKey key { tx, ty };
                touched.insert(key);
                tileDabs[key].push_back(static_cast<uint32_t>(idx));
            }
        }
    }

    // Clear touched tiles in-place so they can be re-stamped.
    for (const auto& key : touched) {
        TileData* tile = strokeBuffer.getTile(key);
        if (!tile)
            continue;
        tile->clear();
        strokeBuffer.markDirty(key);
        if (tile->hasTexture()) {
            clearTexture(tile->textureId());
        }
    }

    // Batched per-tile rebuild:
    // previous path = O(number_of_dabs * covered_tiles_per_dab) draw calls
    // new path = O(number_of_touched_tiles * dabs_per_batch_per_tile)
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glEnable(GL_BLEND);

    m_rebuildBatchProgram->use();
    m_rebuildBatchProgram->setUniform("uMaskTexture", 1);
    m_rebuildBatchProgram->setUniform("uTextureTile", 2);
    m_rebuildBatchProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
    m_rebuildBatchProgram->setUniform(
        "uMaskAffectsAlpha", brush.selectionMaskAffectsAlpha() ? 1 : 0);
    const bool useTexture = brush.usesProceduralTexture();
    m_rebuildBatchProgram->setUniform("uUseTexture", useTexture ? 1 : 0);
    const bool useDabShape = (brush.dabType() > 0);
    m_rebuildBatchProgram->setUniform("uUseDabShapeTexture", useDabShape ? 1 : 0);
    m_rebuildBatchProgram->setUniform("uTextureEdgeBoost", brush.textureEdgeBoost());
    m_rebuildBatchProgram->setUniform("uDabShapeScale", brush.dabXScale(), brush.dabYScale());
    m_rebuildBatchProgram->setUniform(
        "uDabSoftEdgeRadiusTexels", dab_shape_falloff::kSoftEdgeRadiusTexels);
    m_rebuildBatchProgram->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE));
    m_rebuildBatchProgram->setUniform("uQuadMin", 0.0f, 0.0f);
    m_rebuildBatchProgram->setUniform(
        "uQuadMax", static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE));

    if (useDabShape) {
        m_rebuildBatchProgram->setUniform("uDabShapeTexture", 3);
        const GLuint dabTexId = resolveDabTextureId(m_gl, brush);
        if (dabTexId != 0) {
            m_gl->glActiveTexture(GL_TEXTURE3);
            m_gl->glBindTexture(GL_TEXTURE_2D, dabTexId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
    }

    const GLuint batchProgram = m_rebuildBatchProgram->handle();
    const GLint locDabCount = m_gl->glGetUniformLocation(batchProgram, "uDabCount");
    const GLint locDabCenter = m_gl->glGetUniformLocation(batchProgram, "uDabCenter");
    const GLint locDabParams = m_gl->glGetUniformLocation(batchProgram, "uDabParams");
    const GLint locDabColor = m_gl->glGetUniformLocation(batchProgram, "uDabColor");
    const GLint locBlendMode = m_gl->glGetUniformLocation(batchProgram, "uBlendMode");

    std::vector<float> centers;
    std::vector<float> params;
    std::vector<float> colors;
    centers.resize(static_cast<size_t>(kRebuildBatchMaxDabs) * 2u);
    params.resize(static_cast<size_t>(kRebuildBatchMaxDabs) * 4u);
    colors.resize(static_cast<size_t>(kRebuildBatchMaxDabs) * 4u);

    for (const auto& key : touched) {
        auto tileIt = tileDabs.find(key);
        if (tileIt == tileDabs.end() || tileIt->second.empty()) {
            continue;
        }

        if (useSelectionMask) {
            TileData* maskTile = selectionMask->getTile(key);
            if (!maskTile) {
                continue;
            }
            if (!maskTile->hasTexture()) {
                tileRenderer->ensureTileTexture(*maskTile);
                tileRenderer->uploadTileData(*maskTile);
            }
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, maskTile->textureId());
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
        if (useTexture) {
            const GLuint textureTileId = ensureProceduralTextureTile(key, brush);
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, textureTileId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }

        TileData& tile = strokeBuffer.getOrCreateTile(key);
        if (!tile.hasTexture()) {
            tileRenderer->ensureTileTexture(tile);
            clearTexture(tile.textureId());
            // clearTexture disables blending; rebuild relies on GPU blend
            // across chunked dab batches, so restore it for draw passes.
            m_gl->glEnable(GL_BLEND);
        }

        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);

        const float tileOriginX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE);
        const float tileOriginY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE);

        renderDabBatchForTile(dabs, tileIt->second, tileOriginX, tileOriginY, locDabCount,
            locBlendMode, locDabCenter, locDabParams, locDabColor, centers, params, colors);

        tile.clearDirty();
        strokeBuffer.removeDirty(key);
    }

    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useDabShape) {
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE0);

    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    restoreDefaultPremultipliedBlendState(m_gl);

    // Drop stale tiles that are no longer touched by this preview shape.
    auto& tiles = strokeBuffer.tiles();
    for (auto it = tiles.begin(); it != tiles.end();) {
        if (touched.find(it->first) != touched.end()) {
            ++it;
            continue;
        }
        if (it->second.hasTexture()) {
            tileRenderer->destroyTileTexture(it->second);
        }
        TileKey key = it->first;
        it = tiles.erase(it);
        strokeBuffer.removeDirty(key);
    }
}

void GLBrushRenderer::rebuildStrokeBufferRangeFromDabsGPU(TileGrid& strokeBuffer,
    GLTileRenderer* tileRenderer, const TileBrush& brush,
    const std::vector<TileBrush::DabPoint>& dabs, size_t startDabIndex, size_t dabCount,
    TileGrid* selectionMask, bool useSelectionMask, uint32_t canvasWidth, uint32_t canvasHeight)
{
    if (!m_initialized || !m_rebuildBatchProgram || !tileRenderer)
        return;
    if (dabCount == 0 || startDabIndex >= dabs.size())
        return;
    // Range rebuild also invalidates smudge carry — replay from scratch next time.
    m_smudgePrevValid = false;

    const size_t endDabIndex = std::min(startDabIndex + dabCount, dabs.size());
    if (startDabIndex == 0 && endDabIndex == dabs.size()) {
        rebuildStrokeBufferFromDabsGPU(strokeBuffer, tileRenderer, brush, dabs, 0, selectionMask,
            useSelectionMask, canvasWidth, canvasHeight);
        return;
    }

    const bool maskBlocksAll = useSelectionMask && (!selectionMask || selectionMask->empty());
    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);

    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    std::unordered_set<TileKey, TileKeyHash> rebuildTiles;
    rebuildTiles.reserve((endDabIndex - startDabIndex) * 4u);
    for (size_t idx = startDabIndex; idx < endDabIndex; ++idx) {
        const auto& dab = dabs[idx];
        const float radius = std::max(dab.radius, dab.baseRadius);
        if (radius <= 0.0f)
            continue;
        if (dab.alpha == 0 && dab.baseAlpha == 0)
            continue;

        const float rasterExtent
            = dabCoverageExtent(brush, radius, dab.hardness, dab.roundness, dab.angleDegrees, true);
        int32_t tMinX = static_cast<int32_t>(std::floor((dab.worldX - rasterExtent) / TILE_SIZE));
        int32_t tMinY = static_cast<int32_t>(std::floor((dab.worldY - rasterExtent) / TILE_SIZE));
        int32_t tMaxX = static_cast<int32_t>(std::floor((dab.worldX + rasterExtent) / TILE_SIZE));
        int32_t tMaxY = static_cast<int32_t>(std::floor((dab.worldY + rasterExtent) / TILE_SIZE));
        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            tMinX = std::max(tMinX, 0);
            tMinY = std::max(tMinY, 0);
            tMaxX = std::min(tMaxX, canvasMaxX);
            tMaxY = std::min(tMaxY, canvasMaxY);
            if (tMinX > tMaxX || tMinY > tMaxY) {
                continue;
            }
        }

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                rebuildTiles.insert(TileKey { tx, ty });
            }
        }
    }
    if (rebuildTiles.empty()) {
        return;
    }

    for (const auto& key : rebuildTiles) {
        TileData* tile = strokeBuffer.getTile(key);
        if (!tile)
            continue;
        tile->clear();
        strokeBuffer.markDirty(key);
        if (tile->hasTexture()) {
            clearTexture(tile->textureId());
        }
    }

    if (maskBlocksAll) {
        for (const auto& key : rebuildTiles) {
            TileData* tile = strokeBuffer.getTile(key);
            if (!tile)
                continue;
            tile->clearDirty();
            strokeBuffer.removeDirty(key);
        }
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        restoreDefaultPremultipliedBlendState(m_gl);
        return;
    }

    std::unordered_map<TileKey, std::vector<uint32_t>, TileKeyHash> tileDabs;
    tileDabs.reserve(rebuildTiles.size() * 2u);
    for (size_t idx = 0; idx < dabs.size(); ++idx) {
        const auto& dab = dabs[idx];
        if (dab.alpha == 0 || dab.radius <= 0.0f)
            continue;

        const float rasterExtent = dabCoverageExtent(
            brush, dab.radius, dab.hardness, dab.roundness, dab.angleDegrees, true);
        int32_t tMinX = static_cast<int32_t>(std::floor((dab.worldX - rasterExtent) / TILE_SIZE));
        int32_t tMinY = static_cast<int32_t>(std::floor((dab.worldY - rasterExtent) / TILE_SIZE));
        int32_t tMaxX = static_cast<int32_t>(std::floor((dab.worldX + rasterExtent) / TILE_SIZE));
        int32_t tMaxY = static_cast<int32_t>(std::floor((dab.worldY + rasterExtent) / TILE_SIZE));
        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            tMinX = std::max(tMinX, 0);
            tMinY = std::max(tMinY, 0);
            tMaxX = std::min(tMaxX, canvasMaxX);
            tMaxY = std::min(tMaxY, canvasMaxY);
            if (tMinX > tMaxX || tMinY > tMaxY) {
                continue;
            }
        }

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                const TileKey key { tx, ty };
                if (rebuildTiles.find(key) == rebuildTiles.end()) {
                    continue;
                }
                tileDabs[key].push_back(static_cast<uint32_t>(idx));
            }
        }
    }

    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glEnable(GL_BLEND);

    m_rebuildBatchProgram->use();
    m_rebuildBatchProgram->setUniform("uMaskTexture", 1);
    m_rebuildBatchProgram->setUniform("uTextureTile", 2);
    m_rebuildBatchProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
    m_rebuildBatchProgram->setUniform(
        "uMaskAffectsAlpha", brush.selectionMaskAffectsAlpha() ? 1 : 0);
    const bool useTexture = brush.usesProceduralTexture();
    m_rebuildBatchProgram->setUniform("uUseTexture", useTexture ? 1 : 0);
    const bool useDabShape = (brush.dabType() > 0);
    m_rebuildBatchProgram->setUniform("uUseDabShapeTexture", useDabShape ? 1 : 0);
    m_rebuildBatchProgram->setUniform("uTextureEdgeBoost", brush.textureEdgeBoost());
    m_rebuildBatchProgram->setUniform("uDabShapeScale", brush.dabXScale(), brush.dabYScale());
    m_rebuildBatchProgram->setUniform(
        "uDabSoftEdgeRadiusTexels", dab_shape_falloff::kSoftEdgeRadiusTexels);
    m_rebuildBatchProgram->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE));
    m_rebuildBatchProgram->setUniform("uQuadMin", 0.0f, 0.0f);
    m_rebuildBatchProgram->setUniform(
        "uQuadMax", static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE));

    if (useDabShape) {
        m_rebuildBatchProgram->setUniform("uDabShapeTexture", 3);
        const GLuint dabTexId = resolveDabTextureId(m_gl, brush);
        if (dabTexId != 0) {
            m_gl->glActiveTexture(GL_TEXTURE3);
            m_gl->glBindTexture(GL_TEXTURE_2D, dabTexId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
    }

    const GLuint batchProgram = m_rebuildBatchProgram->handle();
    const GLint locDabCount = m_gl->glGetUniformLocation(batchProgram, "uDabCount");
    const GLint locDabCenter = m_gl->glGetUniformLocation(batchProgram, "uDabCenter");
    const GLint locDabParams = m_gl->glGetUniformLocation(batchProgram, "uDabParams");
    const GLint locDabColor = m_gl->glGetUniformLocation(batchProgram, "uDabColor");
    const GLint locBlendMode = m_gl->glGetUniformLocation(batchProgram, "uBlendMode");

    std::vector<float> centers(static_cast<size_t>(kRebuildBatchMaxDabs) * 2u);
    std::vector<float> params(static_cast<size_t>(kRebuildBatchMaxDabs) * 4u);
    std::vector<float> colors(static_cast<size_t>(kRebuildBatchMaxDabs) * 4u);

    for (const auto& key : rebuildTiles) {
        auto tileIt = tileDabs.find(key);
        const bool hasContributingDabs = (tileIt != tileDabs.end() && !tileIt->second.empty());
        TileData* existingTile = strokeBuffer.getTile(key);

        if (useSelectionMask) {
            TileData* maskTile = selectionMask->getTile(key);
            if (!maskTile) {
                if (existingTile) {
                    existingTile->clearDirty();
                    strokeBuffer.removeDirty(key);
                }
                continue;
            }
            if (!maskTile->hasTexture()) {
                tileRenderer->ensureTileTexture(*maskTile);
                tileRenderer->uploadTileData(*maskTile);
            }
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, maskTile->textureId());
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
        if (useTexture) {
            const GLuint textureTileId = ensureProceduralTextureTile(key, brush);
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, textureTileId);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }

        if (!existingTile && !hasContributingDabs) {
            continue;
        }

        TileData& tile = existingTile ? *existingTile : strokeBuffer.getOrCreateTile(key);
        if (!tile.hasTexture()) {
            tileRenderer->ensureTileTexture(tile);
            clearTexture(tile.textureId());
            m_gl->glEnable(GL_BLEND);
        }

        if (!hasContributingDabs) {
            tile.clearDirty();
            strokeBuffer.removeDirty(key);
            continue;
        }

        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);

        const float tileOriginX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE);
        const float tileOriginY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE);

        renderDabBatchForTile(dabs, tileIt->second, tileOriginX, tileOriginY, locDabCount,
            locBlendMode, locDabCenter, locDabParams, locDabColor, centers, params, colors);

        tile.clearDirty();
        strokeBuffer.removeDirty(key);
    }

    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE2);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useDabShape) {
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE0);

    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    restoreDefaultPremultipliedBlendState(m_gl);
}

// ==========================================================================
//   G P U   F L A T T E N   (no readback)
// ==========================================================================

std::unordered_set<TileKey, TileKeyHash> GLBrushRenderer::flattenStrokeGPU(TileGrid& strokeBuffer,
    TileGrid& layerGrid, GLTileRenderer* tileRenderer, bool eraseMode, float strokeOpacity,
    ruwa::core::layers::BlendMode strokeBlendMode, bool alphaLock, bool blurMode,
    TileGrid* strokeBlendBackdrop, const Color& strokeBlendBackdropColor, TileGrid* finalSourceMask,
    bool selectionAlphaCap, bool maskErase)
{
    std::unordered_set<TileKey, TileKeyHash> affectedKeys;
    // End of stroke — drop any smudge carry buffer so the next stroke starts fresh.
    m_smudgePrevValid = false;
    if (!m_initialized || !m_flattenProgram || !tileRenderer) {
        strokeBuffer.setFormat(layerGrid.format());
        return affectedKeys;
    }
    if (strokeBuffer.empty()) {
        strokeBuffer.setFormat(layerGrid.format());
        return affectedKeys;
    }

    // Soft-selection alpha cap path: meaningful only when we have a selection
    // mask AND we are NOT in alpha-lock mode (alpha-lock already caps to dst.a).
    // Erase and blur preserve their existing semantics.
    const bool useAlphaCap
        = selectionAlphaCap && finalSourceMask != nullptr && !alphaLock && !eraseMode && !blurMode;

    std::vector<TileKey> keysVec;
    for (auto& [key, tile] : strokeBuffer.tiles()) {
        if (finalSourceMask && !finalSourceMask->getTile(key)) {
            continue;
        }
        if (!tile.hasTexture()) {
            tileRenderer->ensureTileTexture(tile);
        }
        if (tile.isDirty()) {
            tileRenderer->uploadTileData(tile);
        }
        if (!tile.hasTexture())
            continue;
        keysVec.push_back(key);
        affectedKeys.insert(key);
    }
    if (keysVec.empty())
        return affectedKeys;

    // Background a brand-new layer tile inherits before the stroke is composited
    // over it. For a hide-all mask this is opaque black, so the untouched pixels
    // of a partially painted tile stay hidden instead of reading as transparent
    // (= fully revealed). For every normal grid the default fill is transparent,
    // so this is byte-identical to the previous "clear to transparent".
    uint8_t gridFillPixel[4] = { 0, 0, 0, 0 };
    layerGrid.defaultFill(gridFillPixel[0], gridFillPixel[1], gridFillPixel[2], gridFillPixel[3]);

    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glBindVertexArray(m_emptyVAO);

    const bool programmaticStrokeBlend
        = !eraseMode && !blurMode && strokeBlendMode != ruwa::core::layers::BlendMode::Normal;
    // Cap path also reads the layer's pre-stroke pixels via uDstTexture.
    const bool needsBaseReadTexture = blurMode || programmaticStrokeBlend || useAlphaCap;
    if (blurMode || programmaticStrokeBlend || useAlphaCap) {
        // Blur / programmatic blend / soft-mask cap: shader produces the final
        // pixel value, no GL hardware blending.
        m_gl->glDisable(GL_BLEND);
    } else {
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendEquation(GL_FUNC_ADD);
        if (eraseMode && !maskErase) {
            m_gl->glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        } else if (alphaLock) {
            m_gl->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        } else {
            m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    m_flattenProgram->use();
    m_flattenProgram->setUniform("uSrcTexture", 0);
    m_flattenProgram->setUniform("uBaseTexture", 1);
    m_flattenProgram->setUniform("uDstTexture", 2);
    m_flattenProgram->setUniform("uFinalSourceMaskTexture", 3);
    m_flattenProgram->setUniform("uStrokeOpacity", std::clamp(strokeOpacity, 0.0f, 1.0f));
    m_flattenProgram->setUniform("uUseFinalSourceMask", finalSourceMask ? 1 : 0);
    m_flattenProgram->setUniform("uReplaceWithBaseMix", blurMode ? 1 : 0);
    m_flattenProgram->setUniform("uUseProgrammaticBlend", programmaticStrokeBlend ? 1 : 0);
    m_flattenProgram->setUniform("uStrokeBlendMode", static_cast<int>(strokeBlendMode));
    m_flattenProgram->setUniform("uAlphaLock", alphaLock ? 1 : 0);
    m_flattenProgram->setUniform("uClipMaskAsAlphaCap", useAlphaCap ? 1 : 0);
    m_flattenProgram->setUniform("uBackdropColor", strokeBlendBackdropColor.r,
        strokeBlendBackdropColor.g, strokeBlendBackdropColor.b, strokeBlendBackdropColor.a);

    for (const auto& key : keysVec) {
        const TileData* strokeTile = strokeBuffer.getTile(key);
        if (!strokeTile || !strokeTile->hasTexture())
            continue;
        TileData* finalMaskTile = finalSourceMask ? finalSourceMask->getTile(key) : nullptr;
        if (finalSourceMask) {
            if (finalMaskTile) {
                if (!finalMaskTile->hasTexture()) {
                    tileRenderer->ensureTileTexture(*finalMaskTile);
                    tileRenderer->uploadTileData(*finalMaskTile);
                } else if (finalMaskTile->isDirty()) {
                    tileRenderer->uploadTileData(*finalMaskTile);
                }
            }
            if (!finalMaskTile || !finalMaskTile->hasTexture()) {
                continue;
            }
        }

        const bool layerTileExisted = layerGrid.hasTile(key);
        TileData& layerTile = layerGrid.getOrCreateTile(key);
        // We are about to render per-pixel stroke content into this tile's GPU
        // texture, so it is no longer a uniform-color (solid) tile — its truth is
        // the texture (and, after the async readback, its per-pixel CPU buffer).
        // On a grid with a non-transparent defaultFill (e.g. an INVERTED layer
        // mask = hide-all opaque black) getOrCreateTile materializes a brand-new
        // tile as SOLID. If we left that flag set, GLCompositor's "solid mask tile
        // -> bind solid color" fast path would ignore the painted texture and keep
        // sampling the stale fill (black = reveal 0), so freshly revealed content
        // VANISHES the instant the stroke commits and only reappears once the
        // readback materializes the tile (i.e. on the next stroke over it).
        // Dropping the solid flag now keeps CPU/GPU/compositor state consistent.
        if (layerTile.isSolid()) {
            (void)
                layerTile.pixels(); // materialize: clears solid flag, fills buffer with fill color
        }
        if (!layerTile.hasTexture()) {
            tileRenderer->ensureTileTexture(layerTile);
            if (layerTileExisted) {
                // Pre-existing tile whose GPU texture simply wasn't allocated
                // yet — it carries real pixel data that must be uploaded.
                tileRenderer->uploadTileData(layerTile);
            } else {
                // Brand-new tile: clear the freshly-allocated (undefined) GPU
                // storage to the grid's default-fill background via glClearTexImage
                // instead of pushing 256 KB over the bus. The flatten shader reads
                // dst for src-over blending, so the texture MUST be defined before
                // the draw — this preserves the same guarantee as the old
                // zero-upload (default fill is transparent for normal grids) and
                // guards against the uninitialized-VRAM garbage artifact bug.
                m_gl->glClearTexImage(
                    layerTile.textureId(), 0, GL_RGBA, GL_UNSIGNED_BYTE, gridFillPixel);
                layerTile.clearDirty();
            }
        } else if (layerTile.isDirty()) {
            tileRenderer->uploadTileData(layerTile);
        }

        if (needsBaseReadTexture) {
            // Match m_blurReadTex to the document tile format before the raw
            // texel copy (per-document formats: RGBA8/16F/32F).
            ensureBlurReadTexFormat(layerTile.format());
            if (layerTileExisted) {
                m_gl->glCopyImageSubData(layerTile.textureId(), GL_TEXTURE_2D, 0, 0, 0, 0,
                    m_blurReadTex, GL_TEXTURE_2D, 0, 0, 0, 0, TILE_SIZE, TILE_SIZE, 1);
            } else {
                m_gl->glClearTexImage(m_blurReadTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, gridFillPixel);
            }
        }

        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, layerTile.textureId(), 0);

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, strokeTile->textureId());
        if (needsBaseReadTexture) {
            TileData* blendBaseTile = (programmaticStrokeBlend && strokeBlendBackdrop)
                ? strokeBlendBackdrop->getTile(key)
                : nullptr;
            if (blendBaseTile && !blendBaseTile->hasTexture()) {
                tileRenderer->ensureTileTexture(*blendBaseTile);
                tileRenderer->uploadTileData(*blendBaseTile);
            } else if (blendBaseTile && blendBaseTile->isDirty()) {
                tileRenderer->uploadTileData(*blendBaseTile);
            }
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D,
                (blendBaseTile && blendBaseTile->hasTexture()) ? blendBaseTile->textureId()
                                                               : m_blurReadTex);
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, m_blurReadTex);
            if (blurMode && m_blurLinearSampler) {
                m_gl->glBindSampler(1, m_blurLinearSampler);
            }
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D,
            (finalMaskTile && finalMaskTile->hasTexture()) ? finalMaskTile->textureId() : 0);
        m_gl->glActiveTexture(GL_TEXTURE0);

        m_flattenProgram->setUniform("uTileWorldOrigin",
            static_cast<float>(key.x * static_cast<int32_t>(TILE_SIZE)),
            static_cast<float>(key.y * static_cast<int32_t>(TILE_SIZE)));
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;

        layerTile.clearDirty();
        layerGrid.removeDirty(key);
        // The tile's GPU texture just changed but its CPU buffer/dirty flag were
        // intentionally left untouched (the async readback updates the CPU later),
        // so no markDirty(key) fires here. Bump the whole-grid content counter
        // directly so cross-frame consumers that cache a whole-grid derivation —
        // the distortion whole-layer materialisation — invalidate and rebuild;
        // otherwise painting over an EXISTING tile leaves contentVersion unchanged
        // and the cached distorted region shows stale content until an unrelated
        // edit (e.g. a new tile) happens to bump it.
        layerGrid.notePixelsChangedOutOfBand();
    }

    m_gl->glActiveTexture(GL_TEXTURE3);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    if (needsBaseReadTexture) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (m_blurLinearSampler) {
            m_gl->glBindSampler(1, 0);
        }
    }
    m_gl->glActiveTexture(GL_TEXTURE0);

    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    restoreDefaultPremultipliedBlendState(m_gl);

    // cancelStroke() clears the tiles immediately after this call but retains
    // the grid's format. Restore the document format now so a later non-wet
    // CPU stroke cannot accidentally inherit Wet's float working storage.
    strokeBuffer.setFormat(layerGrid.format());

    return affectedKeys;
}

// ==========================================================================
//   A S Y N C   P B O   R E A D B A C K
// ==========================================================================

GLsync GLBrushRenderer::startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys)
{
    if (keys.empty())
        return nullptr;

    // Tile transport is format-sized: at RGBA8 this equals TILE_BYTE_SIZE, but
    // for wider formats (16F/32F) the readback must move the full per-pixel
    // payload and use the matching GL pixel type, otherwise brush strokes are
    // quantized back to 8-bit on the GPU->CPU round-trip.
    const TilePixelFormat fmt = grid.format();
    const size_t bytesPerTile = tileByteSize(fmt);
    const GLenum pixelType = tileGLPixelType(fmt);

    size_t totalBytes = keys.size() * bytesPerTile;

    if (!m_pbo)
        m_gl->glGenBuffers(1, &m_pbo);

    if (m_pboSize < totalBytes) {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
        m_gl->glBufferData(
            GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(totalBytes), nullptr, GL_STREAM_READ);
        m_pboSize = totalBytes;
    } else {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    size_t offset = 0;
    for (const auto& key : keys) {
        TileData* tile = grid.getTile(key);
        if (!tile || !tile->hasTexture()) {
            offset += bytesPerTile;
            continue;
        }
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile->textureId(), 0);
        m_gl->glReadPixels(
            0, 0, TILE_SIZE, TILE_SIZE, GL_RGBA, pixelType, reinterpret_cast<void*>(offset));
        offset += bytesPerTile;
    }

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GLsync fence = m_gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_gl->glFlush();

    return fence;
}

bool GLBrushRenderer::isReadbackComplete(GLsync fence)
{
    if (!fence)
        return true;
    GLenum result = m_gl->glClientWaitSync(fence, 0, 0);
    return (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED);
}

void GLBrushRenderer::finishReadback(GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys)
{
    if (!fence || keys.empty())
        return;

    // Wait for DMA to complete (should already be done if polled first)
    m_gl->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 500000000ULL);
    m_gl->glDeleteSync(fence);

    // Map PBO and copy to CPU tile buffers
    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    const uint8_t* mapped
        = static_cast<const uint8_t*>(m_gl->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));

    if (mapped) {
        const size_t bytesPerTile = tileByteSize(grid.format());
        size_t offset = 0;
        for (const auto& key : keys) {
            TileData* tile = grid.getTile(key);
            if (tile) {
                std::memcpy(tile->pixels(), mapped + offset, bytesPerTile);
            }
            offset += bytesPerTile;
        }
        m_gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void GLBrushRenderer::deleteFence(GLsync fence)
{
    if (fence)
        m_gl->glDeleteSync(fence);
}

uint32_t GLBrushRenderer::takeAndResetDrawCallEstimate()
{
    uint32_t value = m_drawCallEstimate;
    m_drawCallEstimate = 0;
    return value;
}

// ==========================================================================
//   H E L P E R S
// ==========================================================================

GLuint GLBrushRenderer::ensureProceduralTextureTile(const TileKey& key, const TileBrush& brush)
{
    ++m_proceduralTextureCacheFrame;

    auto it = m_proceduralTextureGpuTiles.find(key);
    const uint32_t revision = brush.textureRevision();
    if (it != m_proceduralTextureGpuTiles.end() && it->second.textureId != 0
        && it->second.revision == revision) {
        it->second.lastUsedFrame = m_proceduralTextureCacheFrame;
        return it->second.textureId;
    }

    // Evict least-recently-used entries when the cache is full.
    // Each entry is a 256x256 R8 texture = 64 KB VRAM.
    constexpr size_t kMaxCachedGpuTextureTiles = 128;
    constexpr size_t kEvictCount = kMaxCachedGpuTextureTiles / 4;
    if (m_proceduralTextureGpuTiles.size() >= kMaxCachedGpuTextureTiles) {
        std::vector<TileKey> keys;
        keys.reserve(m_proceduralTextureGpuTiles.size());
        for (const auto& [k, e] : m_proceduralTextureGpuTiles)
            keys.push_back(k);

        std::nth_element(keys.begin(), keys.begin() + kEvictCount, keys.end(),
            [this](const TileKey& a, const TileKey& b) {
                return m_proceduralTextureGpuTiles[a].lastUsedFrame
                    < m_proceduralTextureGpuTiles[b].lastUsedFrame;
            });

        for (size_t i = 0; i < kEvictCount; ++i) {
            auto& entry = m_proceduralTextureGpuTiles[keys[i]];
            if (entry.textureId != 0) {
                m_gl->glDeleteTextures(1, &entry.textureId);
            }
            m_proceduralTextureGpuTiles.erase(keys[i]);
        }
        it = m_proceduralTextureGpuTiles.find(key);
    }

    ProceduralTextureGpuTile gpuTile;
    if (it != m_proceduralTextureGpuTiles.end()) {
        gpuTile = it->second;
    }
    if (gpuTile.textureId == 0) {
        m_gl->glGenTextures(1, &gpuTile.textureId);
        m_gl->glBindTexture(GL_TEXTURE_2D, gpuTile.textureId);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        m_gl->glTexImage2D(
            GL_TEXTURE_2D, 0, GL_R8, TILE_SIZE, TILE_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }

    // --- GPU-side procedural texture generation ---
    // Save full GL state so the caller's pipeline is not disturbed.
    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevBlend = GL_FALSE;
    m_gl->glGetBooleanv(GL_BLEND, &prevBlend);
    GLint prevBlendEq = GL_FUNC_ADD;
    m_gl->glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEq);
    GLint prevBlendSrc = GL_ONE;
    m_gl->glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    GLint prevBlendDst = GL_ZERO;
    m_gl->glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
    GLint prevProgram = 0;
    m_gl->glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gpuTile.textureId, 0);
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glDisable(GL_BLEND);

    m_proceduralTextureProgram->use();
    m_proceduralTextureProgram->setUniform("uTileOrigin",
        static_cast<float>(key.x) * static_cast<float>(TILE_SIZE),
        static_cast<float>(key.y) * static_cast<float>(TILE_SIZE));
    m_proceduralTextureProgram->setUniform("uScale", brush.textureScale());
    m_proceduralTextureProgram->setUniform("uContrast", brush.textureContrast());
    m_proceduralTextureProgram->setUniform("uDepth", brush.textureDepth());
    m_proceduralTextureProgram->setUniform("uBlend", brush.textureBlend());
    m_proceduralTextureProgram->setUniform("uAmount", brush.textureAmount());
    m_proceduralTextureProgram->setUniform("uTextureType", brush.textureType());

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    ++m_drawCallEstimate;

    // Restore previous GL state
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) {
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendEquation(static_cast<GLenum>(prevBlendEq));
        m_gl->glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));
    }
    m_gl->glUseProgram(static_cast<GLuint>(prevProgram));

    gpuTile.revision = revision;
    gpuTile.lastUsedFrame = m_proceduralTextureCacheFrame;
    m_proceduralTextureGpuTiles[key] = gpuTile;
    return gpuTile.textureId;
}

void GLBrushRenderer::renderDabBatchForTile(const std::vector<TileBrush::DabPoint>& dabs,
    const std::vector<uint32_t>& indices, float tileOriginX, float tileOriginY, GLint locDabCount,
    GLint locBlendMode, GLint locDabCenter, GLint locDabParams, GLint locDabColor,
    std::vector<float>& centers, std::vector<float>& params, std::vector<float>& colors)
{
    size_t cursor = 0;
    while (cursor < indices.size()) {
        const bool blendAsMax = dabs[indices[cursor]].useMaxBlend;
        const int blendMode = blendAsMax ? 1 : 0;

        size_t runEnd = cursor;
        while (runEnd < indices.size() && dabs[indices[runEnd]].useMaxBlend == blendAsMax) {
            ++runEnd;
        }

        size_t runCursor = cursor;
        while (runCursor < runEnd) {
            const size_t chunkCount
                = std::min(static_cast<size_t>(kRebuildBatchMaxDabs), runEnd - runCursor);

            if (blendAsMax) {
                m_gl->glBlendEquation(GL_MAX);
                m_gl->glBlendFunc(GL_ONE, GL_ONE);
            } else {
                m_gl->glBlendEquation(GL_FUNC_ADD);
                m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }

            for (size_t i = 0; i < chunkCount; ++i) {
                const TileBrush::DabPoint& dab = dabs[indices[runCursor + i]];
                const size_t centerBase = i * 2u;
                const size_t vec4Base = i * 4u;

                centers[centerBase + 0] = dab.worldX - tileOriginX;
                centers[centerBase + 1] = dab.worldY - tileOriginY;

                params[vec4Base + 0] = dab.radius;
                params[vec4Base + 1] = std::clamp(dab.hardness, 0.0f, 1.0f);
                params[vec4Base + 2] = std::clamp(dab.roundness, 0.0f, 1.0f);
                params[vec4Base + 3] = dab.angleDegrees * (3.14159265358979323846f / 180.0f);

                const float alpha = static_cast<float>(dab.alpha) / 255.0f;
                const float rPremul = (static_cast<float>(dab.colorR) / 255.0f) * alpha;
                const float gPremul = (static_cast<float>(dab.colorG) / 255.0f) * alpha;
                const float bPremul = (static_cast<float>(dab.colorB) / 255.0f) * alpha;
                colors[vec4Base + 0] = rPremul;
                colors[vec4Base + 1] = gPremul;
                colors[vec4Base + 2] = bPremul;
                colors[vec4Base + 3] = alpha;
            }

            m_gl->glUniform1i(locDabCount, static_cast<GLint>(chunkCount));
            m_gl->glUniform1i(locBlendMode, blendMode);
            m_gl->glUniform2fv(locDabCenter, static_cast<GLsizei>(chunkCount), centers.data());
            m_gl->glUniform4fv(locDabParams, static_cast<GLsizei>(chunkCount), params.data());
            m_gl->glUniform4fv(locDabColor, static_cast<GLsizei>(chunkCount), colors.data());

            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            ++m_drawCallEstimate;

            runCursor += chunkCount;
        }

        cursor = runEnd;
    }
}

void GLBrushRenderer::clearTexture(GLuint texId)
{
    clearTexture(texId, 0u);
}

void GLBrushRenderer::clearTexture(GLuint texId, uint32_t packedColor)
{
    const GLboolean scissorWasEnabled = m_gl->glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissor[4] = { 0, 0, 0, 0 };
    if (scissorWasEnabled) {
        m_gl->glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
        m_gl->glDisable(GL_SCISSOR_TEST);
    }
    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    m_gl->glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);
    m_gl->glDisable(GL_BLEND);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    TileData::unpackColor(packedColor, r, g, b, a);
    m_gl->glClearColor(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_gl->glColorMask(
        previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
    if (scissorWasEnabled) {
        m_gl->glEnable(GL_SCISSOR_TEST);
        m_gl->glScissor(
            previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
    }
}

bool GLBrushRenderer::copyColorRegion(GLuint sourceTexture, TilePixelFormat sourceFormat,
    GLint sourceX, GLint sourceY, GLuint targetTexture, TilePixelFormat targetFormat, GLint targetX,
    GLint targetY, GLsizei width, GLsizei height)
{
    if (sourceTexture == 0 || targetTexture == 0 || width <= 0 || height <= 0)
        return false;

    if (sourceFormat == targetFormat) {
        m_gl->glCopyImageSubData(sourceTexture, GL_TEXTURE_2D, 0, sourceX, sourceY, 0,
            targetTexture, GL_TEXTURE_2D, 0, targetX, targetY, 0, width, height, 1);
        return true;
    }

    if (!m_formatCopyProgram || !m_formatCopyProgram->isValid())
        return false;

    // A shader copy is used at format boundaries. Keeping the source solely as
    // a sampled texture and the destination solely as COLOR_ATTACHMENT0 avoids
    // coupling differently-sized tile/ROI attachments in one framebuffer. The
    // previous blit path did that and left driver-dependent unwritten strips at
    // tile edges. texelFetch is an exact one-to-one conversion with no filtering.
    GLint previousDrawFbo = 0;
    GLint previousReadFbo = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    GLint previousProgram = 0;
    GLint previousVao = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture0 = 0;
    m_gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
    m_gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
    m_gl->glGetIntegerv(GL_VIEWPORT, previousViewport);
    m_gl->glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    m_gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    m_gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);

    const GLboolean blendWasEnabled = m_gl->glIsEnabled(GL_BLEND);
    const GLboolean depthWasEnabled = m_gl->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean stencilWasEnabled = m_gl->glIsEnabled(GL_STENCIL_TEST);
    const GLboolean cullWasEnabled = m_gl->glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorWasEnabled = m_gl->glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissor[4] = { 0, 0, 0, 0 };
    if (scissorWasEnabled)
        m_gl->glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    m_gl->glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    restoreSingleColorTarget(m_gl);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    const bool complete = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        m_gl->glDisable(GL_BLEND);
        m_gl->glDisable(GL_DEPTH_TEST);
        m_gl->glDisable(GL_STENCIL_TEST);
        m_gl->glDisable(GL_CULL_FACE);
        m_gl->glDisable(GL_SCISSOR_TEST);
        m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_gl->glViewport(targetX, targetY, width, height);
        m_gl->glBindVertexArray(m_emptyVAO);
        m_gl->glBindTexture(GL_TEXTURE_2D, sourceTexture);
        m_formatCopyProgram->use();
        m_formatCopyProgram->setUniform("uSourceTexture", 0);
        m_formatCopyProgram->setUniform(
            "uViewportSize", static_cast<float>(width), static_cast<float>(height));
        m_formatCopyProgram->setUniform(
            "uSourceOrigin", static_cast<float>(sourceX), static_cast<float>(sourceY));
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;
    }

    m_gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
    m_gl->glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    m_gl->glUseProgram(static_cast<GLuint>(previousProgram));
    m_gl->glBindVertexArray(static_cast<GLuint>(previousVao));
    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFbo));
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFbo));
    m_gl->glViewport(
        previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    m_gl->glColorMask(
        previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
    if (blendWasEnabled)
        m_gl->glEnable(GL_BLEND);
    else
        m_gl->glDisable(GL_BLEND);
    if (depthWasEnabled)
        m_gl->glEnable(GL_DEPTH_TEST);
    else
        m_gl->glDisable(GL_DEPTH_TEST);
    if (stencilWasEnabled)
        m_gl->glEnable(GL_STENCIL_TEST);
    else
        m_gl->glDisable(GL_STENCIL_TEST);
    if (cullWasEnabled)
        m_gl->glEnable(GL_CULL_FACE);
    else
        m_gl->glDisable(GL_CULL_FACE);
    if (scissorWasEnabled) {
        m_gl->glEnable(GL_SCISSOR_TEST);
        m_gl->glScissor(
            previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
    } else {
        m_gl->glDisable(GL_SCISSOR_TEST);
    }
    return complete;
}

void GLBrushRenderer::ensureBlurReadTexFormat(TilePixelFormat contentFormat)
{
    if (m_blurReadTex == 0 || contentFormat == m_blurReadTexFormat) {
        return;
    }
    m_blurReadTexFormat = contentFormat;
    // Mutable storage (created via createTexture2D like the scratch textures),
    // so glTexImage2D re-specifies format without disturbing filter/wrap params.
    m_gl->glBindTexture(GL_TEXTURE_2D, m_blurReadTex);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, tileGLInternalFormat(contentFormat), TILE_SIZE, TILE_SIZE,
        0, GL_RGBA, tileGLPixelType(contentFormat), nullptr);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

bool GLBrushRenderer::ensureBlurScratchSize(
    GLsizei width, GLsizei height, TilePixelFormat contentFormat)
{
    if (width <= 0 || height <= 0 || !m_blurScratchSourceTex || !m_blurScratchTempTex) {
        return false;
    }
    if (width == m_blurScratchWidth && height == m_blurScratchHeight
        && contentFormat == m_blurScratchFormat) {
        return true;
    }

    m_blurScratchWidth = width;
    m_blurScratchHeight = height;
    m_blurScratchFormat = contentFormat;

    // Content scratch follows the requested working format. Wet requests float
    // storage; other effects request the document format.
    const GLenum internalFmt = tileGLInternalFormat(contentFormat);
    const GLenum pixelType = tileGLPixelType(contentFormat);

    m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchSourceTex);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, m_blurScratchWidth, m_blurScratchHeight, 0,
        GL_RGBA, pixelType, nullptr);

    m_gl->glBindTexture(GL_TEXTURE_2D, m_blurScratchTempTex);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, m_blurScratchWidth, m_blurScratchHeight, 0,
        GL_RGBA, pixelType, nullptr);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool GLBrushRenderer::ensureMaskScratchSize(GLsizei width, GLsizei height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (m_maskScratchTex == 0) {
        // RGBA8 coverage scratch; NEAREST to match the content-scratch default.
        m_maskScratchTex = createTexture2D(
            m_gl, width, height, tileTextureParams(TilePixelFormat::RGBA8, GL_NEAREST, GL_NEAREST));
        if (m_maskScratchTex == 0)
            return false;
        m_maskScratchWidth = width;
        m_maskScratchHeight = height;
        return true;
    }
    if (width == m_maskScratchWidth && height == m_maskScratchHeight) {
        return true;
    }
    m_maskScratchWidth = width;
    m_maskScratchHeight = height;
    m_gl->glBindTexture(GL_TEXTURE_2D, m_maskScratchTex);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_maskScratchWidth, m_maskScratchHeight, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

namespace {
bool ensureSmudgeWorkTextures(QOpenGLFunctions_4_5_Core* gl, GLuint workTex[2], GLsizei& width,
    GLsizei& height, TilePixelFormat& format, GLsizei needWidth, GLsizei needHeight,
    TilePixelFormat contentFormat)
{
    if (needWidth <= 0 || needHeight <= 0)
        return false;
    // Reuse only when big enough AND the format still matches — a format change
    // (different-depth document) requires reallocation for glCopyImageSubData
    // format-compatibility, even if the existing size would fit.
    if (workTex[0] != 0 && workTex[1] != 0 && width >= needWidth && height >= needHeight
        && format == contentFormat) {
        return true;
    }
    // Grow geometrically to avoid thrashing across slightly-different ROIs.
    GLsizei w = std::max(width, needWidth);
    GLsizei h = std::max(height, needHeight);
    auto createOrResize = [&](GLuint& tex) {
        if (tex == 0) {
            gl->glGenTextures(1, &tex);
            gl->glBindTexture(GL_TEXTURE_2D, tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            gl->glBindTexture(GL_TEXTURE_2D, tex);
        }
        // The caller selects document storage for Smudge and float working
        // storage for Wet. Boundary copies perform format conversion as needed.
        gl->glTexImage2D(GL_TEXTURE_2D, 0, tileGLInternalFormat(contentFormat), w, h, 0, GL_RGBA,
            tileGLPixelType(contentFormat), nullptr);
    };
    createOrResize(workTex[0]);
    createOrResize(workTex[1]);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    width = w;
    height = h;
    format = contentFormat;
    return true;
}
} // anonymous namespace

bool GLBrushRenderer::stampSmudgeSegmentGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
    const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, uint32_t canvasWidth,
    uint32_t canvasHeight, TileGrid* layerGrid, TileGrid* selectionMask, bool useSelectionMask)
{
    if (dabs.empty())
        return true;
    const bool wetMode = brush.isWetMode();
    const bool hasPrograms = wetMode ? (m_wetApplyBatchProgram && m_wetPickupBatchProgram)
                                     : (m_smudgeBatchProgram && m_smudgePickupBatchProgram);
    if (!hasPrograms || !layerGrid || !tileRenderer) {
        return false;
    }
    GLShaderProgram* pickupProgram
        = wetMode ? m_wetPickupBatchProgram.get() : m_smudgePickupBatchProgram.get();
    GLShaderProgram* applyProgram
        = wetMode ? m_wetApplyBatchProgram.get() : m_smudgeBatchProgram.get();
    const TilePixelFormat workFormat
        = wetMode ? wet_pigment_gpu::workingColorFormat(layerGrid->format()) : layerGrid->format();
    // Wet keeps the evolving canvas in float storage for the complete stroke;
    // ordinary Smudge retains the document format and its existing behavior.
    if (strokeBuffer.empty()) {
        strokeBuffer.setFormat(workFormat);
    }
    // Smudge and Wet reuse this geometry/ROI scaffolding, but keep separate
    // color paths: premultiplied RGBA for Smudge, four-plane pigment latent
    // data for Wet.
    if (!brush.isSmudgeMode() && !brush.isWetMode()) {
        return false;
    }
    if (useSelectionMask && (!selectionMask || selectionMask->empty())) {
        return true;
    }

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);

    // ----- 0. Allocate reservoir (sized for the stroke's max footprint) -----
    const GLsizei reservoirLogical = computeSmudgeReservoirLogicalSize(brush);
    // If the reservoir gets reallocated mid-stroke (brush.radius() changed
    // enough to need a larger texture), the old contents are gone — force
    // a re-init on this segment.
    const bool reservoirRealloc = !reservoirTexturesComplete(m_smudgeReservoirTex)
        || m_smudgeReservoirSize < reservoirLogical;
    if (!ensureSmudgeReservoirTextures(
            m_gl, m_smudgeReservoirTex, m_smudgeReservoirSize, reservoirLogical)) {
        return false;
    }
    if (reservoirRealloc)
        m_smudgePrevValid = false;
    // Wipe stale pigment from a previous stroke (e.g. after undo) before this
    // stroke loads its own — see the single-dab path / resetSmudgeState().
    if (m_smudgeReservoirNeedsClear) {
        for (auto& side : m_smudgeReservoirTex) {
            for (const GLuint texture : side)
                clearTexture(texture);
        }
        m_smudgeReservoirSrcIdx = 0;
        m_smudgePrevValid = false;
        m_smudgeReservoirNeedsClear = false;
    }
    m_smudgeReservoirActive = reservoirLogical;
    const float reservoirHalf = 0.5f * static_cast<float>(reservoirLogical);
    const float invReservoirPhys = 1.0f / static_cast<float>(m_smudgeReservoirSize);

    // ----- 1. Compute union ROI covering reservoir and rendered dabs --------
    // The pickup pass at dab pos P needs to read canvas at every reservoir
    // pixel — i.e. inside [P - reservoirHalf, P + reservoirHalf]. The apply
    // pass uses actual per-dab geometry, which jitter can expand beyond it.
    float roiMinX = std::numeric_limits<float>::infinity();
    float roiMinY = std::numeric_limits<float>::infinity();
    float roiMaxX = -std::numeric_limits<float>::infinity();
    float roiMaxY = -std::numeric_limits<float>::infinity();
    float writeMinX = std::numeric_limits<float>::infinity();
    float writeMinY = std::numeric_limits<float>::infinity();
    float writeMaxX = -std::numeric_limits<float>::infinity();
    float writeMaxY = -std::numeric_limits<float>::infinity();
    std::unordered_set<TileKey, TileKeyHash> writeTiles;
    for (const auto& d : dabs) {
        const float dabExtent
            = dabCoverageExtent(brush, d.radius, d.hardness, d.roundness, d.angleDegrees, true);
        const float roiHalfExtent = std::max(reservoirHalf, dabExtent);
        roiMinX = std::min(roiMinX, d.worldX - roiHalfExtent);
        roiMinY = std::min(roiMinY, d.worldY - roiHalfExtent);
        roiMaxX = std::max(roiMaxX, d.worldX + roiHalfExtent);
        roiMaxY = std::max(roiMaxY, d.worldY + roiHalfExtent);
        writeMinX = std::min(writeMinX, d.worldX - dabExtent);
        writeMinY = std::min(writeMinY, d.worldY - dabExtent);
        writeMaxX = std::max(writeMaxX, d.worldX + dabExtent);
        writeMaxY = std::max(writeMaxY, d.worldY + dabExtent);

        int32_t dabMinXi = static_cast<int32_t>(std::floor(d.worldX - dabExtent));
        int32_t dabMinYi = static_cast<int32_t>(std::floor(d.worldY - dabExtent));
        int32_t dabMaxXi = static_cast<int32_t>(std::ceil(d.worldX + dabExtent)) + 1;
        int32_t dabMaxYi = static_cast<int32_t>(std::ceil(d.worldY + dabExtent)) + 1;
        if (clipToCanvas) {
            dabMinXi = std::max<int32_t>(dabMinXi, 0);
            dabMinYi = std::max<int32_t>(dabMinYi, 0);
            dabMaxXi = std::min<int32_t>(dabMaxXi, static_cast<int32_t>(canvasWidth));
            dabMaxYi = std::min<int32_t>(dabMaxYi, static_cast<int32_t>(canvasHeight));
        }
        if (dabMaxXi > dabMinXi && dabMaxYi > dabMinYi) {
            const int32_t dabMinTileX
                = static_cast<int32_t>(std::floor(static_cast<float>(dabMinXi) / TILE_SIZE));
            const int32_t dabMinTileY
                = static_cast<int32_t>(std::floor(static_cast<float>(dabMinYi) / TILE_SIZE));
            const int32_t dabMaxTileX
                = static_cast<int32_t>(std::floor(static_cast<float>(dabMaxXi - 1) / TILE_SIZE));
            const int32_t dabMaxTileY
                = static_cast<int32_t>(std::floor(static_cast<float>(dabMaxYi - 1) / TILE_SIZE));
            for (int32_t ty = dabMinTileY; ty <= dabMaxTileY; ++ty)
                for (int32_t tx = dabMinTileX; tx <= dabMaxTileX; ++tx)
                    writeTiles.insert(TileKey { tx, ty });
        }
    }

    int32_t roiMinXi = static_cast<int32_t>(std::floor(roiMinX));
    int32_t roiMinYi = static_cast<int32_t>(std::floor(roiMinY));
    int32_t roiMaxXi = static_cast<int32_t>(std::ceil(roiMaxX)) + 1;
    int32_t roiMaxYi = static_cast<int32_t>(std::ceil(roiMaxY)) + 1;
    int32_t writeMinXi = static_cast<int32_t>(std::floor(writeMinX));
    int32_t writeMinYi = static_cast<int32_t>(std::floor(writeMinY));
    int32_t writeMaxXi = static_cast<int32_t>(std::ceil(writeMaxX)) + 1;
    int32_t writeMaxYi = static_cast<int32_t>(std::ceil(writeMaxY)) + 1;
    if (clipToCanvas) {
        roiMinXi = std::max<int32_t>(roiMinXi, 0);
        roiMinYi = std::max<int32_t>(roiMinYi, 0);
        roiMaxXi = std::min<int32_t>(roiMaxXi, static_cast<int32_t>(canvasWidth));
        roiMaxYi = std::min<int32_t>(roiMaxYi, static_cast<int32_t>(canvasHeight));
        writeMinXi = std::max<int32_t>(writeMinXi, 0);
        writeMinYi = std::max<int32_t>(writeMinYi, 0);
        writeMaxXi = std::min<int32_t>(writeMaxXi, static_cast<int32_t>(canvasWidth));
        writeMaxYi = std::min<int32_t>(writeMaxYi, static_cast<int32_t>(canvasHeight));
    }
    if (roiMaxXi <= roiMinXi || roiMaxYi <= roiMinYi) {
        // ROI fully clipped — mark reservoir as loaded so the next valid
        // segment doesn't re-init from scratch and skip the work entirely.
        m_smudgePrevValid = true;
        return true;
    }

    const GLsizei roiW = static_cast<GLsizei>(roiMaxXi - roiMinXi);
    const GLsizei roiH = static_cast<GLsizei>(roiMaxYi - roiMinYi);

    if (!ensureSmudgeWorkTextures(m_gl, m_smudgeWorkTex, m_smudgeWorkWidth, m_smudgeWorkHeight,
            m_smudgeWorkFormat, roiW, roiH, workFormat)) {
        return false;
    }

    auto ensureUploadedTileTexture = [&](TileData* tile) {
        if (!tile)
            return;
        if (!tile->hasTexture()) {
            tileRenderer->ensureTileTexture(*tile);
            tileRenderer->uploadTileData(*tile);
        } else if (tile->isDirty()) {
            tileRenderer->uploadTileData(*tile);
        }
    };

    const bool ownBatch = !m_stampBatchActive;
    if (ownBatch) {
        beginStampBatch();
    }

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glDisable(GL_BLEND);

    // ----- 2. Snapshot ROI from stroke buffer (preferred) + layer ----------
    clearTexture(m_smudgeWorkTex[0], layerGrid ? layerGrid->defaultFillPacked() : 0u);

    int32_t srcMinTileX
        = static_cast<int32_t>(std::floor(static_cast<float>(roiMinXi) / TILE_SIZE));
    int32_t srcMinTileY
        = static_cast<int32_t>(std::floor(static_cast<float>(roiMinYi) / TILE_SIZE));
    int32_t srcMaxTileX
        = static_cast<int32_t>(std::floor(static_cast<float>(roiMaxXi - 1) / TILE_SIZE));
    int32_t srcMaxTileY
        = static_cast<int32_t>(std::floor(static_cast<float>(roiMaxYi - 1) / TILE_SIZE));
    if (clipToCanvas) {
        const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
        const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
        srcMinTileX = std::max(srcMinTileX, 0);
        srcMinTileY = std::max(srcMinTileY, 0);
        srcMaxTileX = std::min(srcMaxTileX, canvasMaxX);
        srcMaxTileY = std::min(srcMaxTileY, canvasMaxY);
    }
    for (int32_t srcTy = srcMinTileY; srcTy <= srcMaxTileY; ++srcTy) {
        for (int32_t srcTx = srcMinTileX; srcTx <= srcMaxTileX; ++srcTx) {
            TileKey srcKey { srcTx, srcTy };
            TileData* srcTile = strokeBuffer.getTile(srcKey);
            ensureUploadedTileTexture(srcTile);
            if (!srcTile || !srcTile->hasTexture()) {
                srcTile = layerGrid->getTile(srcKey);
                if (srcTile && srcTile->isEmpty() && !layerGrid->hasDefaultFill())
                    continue;
                ensureUploadedTileTexture(srcTile);
            }
            if (!srcTile || !srcTile->hasTexture())
                continue;

            const int32_t tileMinX = srcTx * static_cast<int32_t>(TILE_SIZE);
            const int32_t tileMinY = srcTy * static_cast<int32_t>(TILE_SIZE);
            const int32_t copyMinX = std::max(roiMinXi, tileMinX);
            const int32_t copyMinY = std::max(roiMinYi, tileMinY);
            const int32_t copyMaxX = std::min(roiMaxXi, tileMinX + static_cast<int32_t>(TILE_SIZE));
            const int32_t copyMaxY = std::min(roiMaxYi, tileMinY + static_cast<int32_t>(TILE_SIZE));
            if (copyMaxX <= copyMinX || copyMaxY <= copyMinY)
                continue;

            if (!copyColorRegion(srcTile->textureId(), srcTile->format(), copyMinX - tileMinX,
                    copyMinY - tileMinY, m_smudgeWorkTex[0], m_smudgeWorkFormat,
                    copyMinX - roiMinXi, copyMinY - roiMinYi, copyMaxX - copyMinX,
                    copyMaxY - copyMinY)) {
                if (ownBatch)
                    endStampBatch();
                return false;
            }
        }
    }

    if (useSelectionMask) {
        if (!ensureMaskScratchSize(roiW, roiH)) {
            if (ownBatch) {
                endStampBatch();
            }
            return false;
        }
        clearTexture(m_maskScratchTex);
        for (int32_t srcTy = srcMinTileY; srcTy <= srcMaxTileY; ++srcTy) {
            for (int32_t srcTx = srcMinTileX; srcTx <= srcMaxTileX; ++srcTx) {
                TileKey srcKey { srcTx, srcTy };
                TileData* maskTile = selectionMask->getTile(srcKey);
                if (!maskTile) {
                    continue;
                }
                ensureUploadedTileTexture(maskTile);
                if (!maskTile->hasTexture()) {
                    continue;
                }

                const int32_t tileMinX = srcTx * static_cast<int32_t>(TILE_SIZE);
                const int32_t tileMinY = srcTy * static_cast<int32_t>(TILE_SIZE);
                const int32_t copyMinX = std::max(roiMinXi, tileMinX);
                const int32_t copyMinY = std::max(roiMinYi, tileMinY);
                const int32_t copyMaxX
                    = std::min(roiMaxXi, tileMinX + static_cast<int32_t>(TILE_SIZE));
                const int32_t copyMaxY
                    = std::min(roiMaxYi, tileMinY + static_cast<int32_t>(TILE_SIZE));
                if (copyMaxX <= copyMinX || copyMaxY <= copyMinY) {
                    continue;
                }

                m_gl->glCopyImageSubData(maskTile->textureId(), GL_TEXTURE_2D, 0,
                    copyMinX - tileMinX, copyMinY - tileMinY, 0, m_maskScratchTex, GL_TEXTURE_2D, 0,
                    copyMinX - roiMinXi, copyMinY - roiMinYi, 0, copyMaxX - copyMinX,
                    copyMaxY - copyMinY, 1);
            }
        }
    }

    // ----- 3. Apply each dab via two ping-pongs (reservoir + work buffer) ---
    // For each dab we do:
    //   a) Pickup: reservoir[rDst] = mix(reservoir[rSrc], workBuf[wSrc], rate*falloff)
    //              (or full copy on the first dab of the stroke via uInit=1)
    //   b) Apply : workBuf[wDst]  = mix(workBuf[wSrc], reservoir[rDst], strength*falloff)
    // Both passes use the kSmudgeBatchVert vertex shader (full-viewport quad
    // with fragPixelCoord = t * uViewportSize). The apply pass viewport is
    // the ROI; the pickup pass viewport is the reservoir.
    const float invTexW = 1.0f / static_cast<float>(m_smudgeWorkWidth);
    const float invTexH = 1.0f / static_cast<float>(m_smudgeWorkHeight);
    const float maxValidUvX = static_cast<float>(roiW) * invTexW;
    const float maxValidUvY = static_cast<float>(roiH) * invTexH;

    GLuint dabTexId = 0;
    if (brush.dabType() > 0) {
        dabTexId = resolveDabTextureId(m_gl, brush);
    }
    const int useDabShape = dabTexId != 0 ? 1 : 0;
    if (dabTexId != 0) {
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D, dabTexId);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }

    if (m_blurLinearSampler) {
        m_gl->glBindSampler(0, m_blurLinearSampler);
        m_gl->glBindSampler(1, m_blurLinearSampler);
    }

    // Configure both programs' invariant uniforms once. Per-dab uniforms
    // (brush params, position) are set inside the loop.
    auto configureCommon = [&](GLShaderProgram* p, bool usesDabShape) {
        p->use();
        p->setUniform("uOriginalTexture", 0);
        p->setUniform("uInvTexSize", invTexW, invTexH);
        p->setUniform("uMaxValidUv", maxValidUvX, maxValidUvY);
        if (usesDabShape) {
            p->setUniform("uDabShapeScale", brush.dabXScale(), brush.dabYScale());
            p->setUniform("uDabSoftEdgeRadiusTexels", dab_shape_falloff::kSoftEdgeRadiusTexels);
            p->setUniform("uUseDabShapeTexture", useDabShape);
            if (dabTexId != 0)
                p->setUniform("uDabShapeTexture", 3);
        }
        p->setUniform("uReservoirHalf", reservoirHalf);
        p->setUniform("uInvReservoirPhys", invReservoirPhys, invReservoirPhys);
    };
    configureCommon(pickupProgram, false);
    if (wetMode) {
        pickupProgram->setUniform("uUsePen", 1);
    } else {
        pickupProgram->setUniform("uReservoirSrc", 1);
        pickupProgram->setUniform("uPickupRate", std::clamp(brush.wetMix(), 0.0f, 1.0f));
    }
    if (wetMode) {
        pickupProgram->setUniform("uWetFlow", brush.colorWetFlow());
        // Pen-fill gate (per-stroke constant): refill the blank-canvas alpha
        // shortfall from the pen only for non-watery brushes (dilution 0), so
        // dilution brushes keep their exact lift behaviour. See the single-dab
        // path and the shader's aFill comment.
        pickupProgram->setUniform("uPenFillGate", brush.colorDilution() <= 0.001f ? 1.0f : 0.0f);
        // uSpread (supply-scaled per dab), the per-dab rates
        // (uCanvasPickup/uDilution) and uPenColor are set inside the loop
        // below — see wetRatePerDab.
    }
    configureCommon(applyProgram, true);
    if (wetMode) {
        applyProgram->setUniform("uPreserveCanvasAlpha", 0);
        applyProgram->setUniform(
            "uQuantizeTo8Bit", m_smudgeWorkFormat == TilePixelFormat::RGBA8 ? 1 : 0);
    } else {
        applyProgram->setUniform("uReservoirTexture", 1);
    }
    applyProgram->setUniform("uMaskTexture", 2);
    applyProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
    applyProgram->setUniform(
        "uInvMaskSize", 1.0f / static_cast<float>(roiW), 1.0f / static_cast<float>(roiH));

    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_maskScratchTex);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }

    const float deg2rad = 3.14159265358979323846f / 180.0f;
    int wSrc = 0; // work buffer ping-pong
    int rSrc = m_smudgeReservoirSrcIdx; // reservoir ping-pong
    bool reservoirLoaded = m_smudgePrevValid;
    bool didAnyDraw = false;
    // Previous dab world position for wet-rate spacing normalization;
    // carried across segments via m_smudgePrevWorld*.
    float prevDabX = m_smudgePrevWorldX;
    float prevDabY = m_smudgePrevWorldY;
    const float paintSupplyBeforeSegment = m_wetPaintSupply;

    for (size_t i = 0; i < dabs.size(); ++i) {
        const auto& d = dabs[i];
        const float dabAlpha = static_cast<float>(d.alpha) / 255.0f;
        const float clampedRoundness = std::clamp(d.roundness, 0.0f, 1.0f);
        const float brushAngleRad = d.angleDegrees * deg2rad;
        const float brushCenterX = d.worldX - static_cast<float>(roiMinXi);
        const float brushCenterY = d.worldY - static_cast<float>(roiMinYi);
        const bool firstDabOfStroke = !reservoirLoaded;
        // Smudge skips apply on the first dab (reservoir = canvas copy =>
        // no-op); a wet brush deposits from the first dab since its
        // reservoir is pre-loaded with pen paint (uInitLoad).
        const bool runApply = (wetMode || !firstDabOfStroke) && (d.alpha != 0);
        const float dabDist
            = firstDabOfStroke ? 0.0f : std::hypot(d.worldX - prevDabX, d.worldY - prevDabY);
        // Advection offset for the wet reservoir: the world-space travel
        // since the previous dab (= strokeDir * dabDist). Zero when
        // stationary / first dab => the advected sample degenerates to
        // prev(here), weight-neutral.
        float advectPxX = 0.0f;
        float advectPxY = 0.0f;
        if (dabDist > 0.01f) {
            advectPxX = d.worldX - prevDabX;
            advectPxY = d.worldY - prevDabY;
        }
        prevDabX = d.worldX;
        prevDabY = d.worldY;

        // ----- 3a. Pickup pass (always — even alpha==0 dabs refresh the
        //          reservoir so motion across uniform regions keeps loading
        //          the brush). -----
        const int rDst = rSrc ^ 1;
        pickupProgram->use();
        pickupProgram->setUniform("uViewportSize", static_cast<float>(reservoirLogical),
            static_cast<float>(reservoirLogical));
        pickupProgram->setUniform("uBrushCenter", brushCenterX, brushCenterY);
        pickupProgram->setUniform("uInit", firstDabOfStroke ? 1 : 0);
        if (wetMode) {
            pickupProgram->setUniform("uBrushRadius", d.radius);
            pickupProgram->setUniform("uBrushRoundness", clampedRoundness);
            pickupProgram->setUniform("uBrushAngleRad", brushAngleRad);
            // Finite paint supply (Drying), exchange-rate floor and the
            // squared dilution mapping — see the per-dab path (stampGPU)
            // for the rationale.
            if (firstDabOfStroke) {
                m_wetPaintSupply = 1.0f;
            } else {
                m_wetPaintSupply *= 1.0f - wetRatePerDab(brush.colorDryRate(), dabDist, d.radius);
            }
            const float effectiveSpread = brush.colorSpread() * m_wetPaintSupply;
            const float exchangeRate
                = std::max(brush.colorBlending() * (1.0f - brush.colorLength()), effectiveSpread);
            pickupProgram->setUniform(
                "uCanvasPickup", wetRatePerDab(exchangeRate, dabDist, d.radius));
            pickupProgram->setUniform("uDilution",
                wetRatePerDab(brush.colorDilution() * brush.colorDilution(), dabDist, d.radius));
            pickupProgram->setUniform("uSpread", effectiveSpread);
            pickupProgram->setUniform("uAdvectPx", advectPxX, advectPxY);
            // Per-dab pen color: dab color can vary along the stroke
            // (color dynamics).
            pickupProgram->setUniform("uPenColor", static_cast<float>(d.colorR) / 255.0f,
                static_cast<float>(d.colorG) / 255.0f, static_cast<float>(d.colorB) / 255.0f);
        }

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_smudgeWorkTex[wSrc]);
        bool pickupFramebufferComplete = false;
        if (wetMode) {
            bindWetLuts(m_gl, pickupProgram, m_pigmentLutTex);
            bindWetReservoir(m_gl, pickupProgram, m_smudgeReservoirTex[rSrc], m_blurLinearSampler);
            pickupFramebufferComplete = attachWetReservoir(m_gl, m_smudgeReservoirTex[rDst]);
        } else {
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, m_smudgeReservoirTex[rSrc][0]);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(1, m_blurLinearSampler);
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                m_smudgeReservoirTex[rDst][0], 0);
            pickupFramebufferComplete
                = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        if (!pickupFramebufferComplete) {
            m_wetPaintSupply = paintSupplyBeforeSegment;
            restoreSingleColorTarget(m_gl);
            unbindWetTextures(m_gl);
            if (m_blurLinearSampler) {
                m_gl->glBindSampler(0, 0);
                m_gl->glBindSampler(1, 0);
            }
            if (useSelectionMask) {
                m_gl->glActiveTexture(GL_TEXTURE2);
                m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            }
            if (dabTexId != 0) {
                m_gl->glActiveTexture(GL_TEXTURE3);
                m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            }
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            if (ownBatch)
                endStampBatch();
            return false;
        }
        m_gl->glViewport(0, 0, reservoirLogical, reservoirLogical);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;
        restoreSingleColorTarget(m_gl);
        rSrc = rDst;
        reservoirLoaded = true;

        if (!runApply)
            continue;

        // ----- 3b. Apply pass: deposit reservoir into work buffer ----------
        const int wDst = wSrc ^ 1;
        applyProgram->use();
        applyProgram->setUniform(
            "uViewportSize", static_cast<float>(roiW), static_cast<float>(roiH));
        applyProgram->setUniform("uBrushRadius", d.radius);
        applyProgram->setUniform("uBrushHardness", d.hardness);
        applyProgram->setUniform("uBrushRoundness", clampedRoundness);
        applyProgram->setUniform("uBrushAngleRad", brushAngleRad);
        applyProgram->setUniform("uBrushAlpha", std::clamp(dabAlpha, 0.0f, 1.0f));
        if (wetMode) {
            applyProgram->setUniform("uCoatPerDab",
                brush.colorBuildup() > 0.001f ? m_wetPaintSupply
                        * buildupCoatPerDab(brush.colorBuildup(), dabDist, brush.radius())
                                              : -1.0f);
            const float clampedDabAlpha = std::clamp(dabAlpha, 0.0f, 1.0f);
            applyProgram->setUniform("uDepositRate",
                firstDabOfStroke ? clampedDabAlpha
                                 : wetRatePerDab(clampedDabAlpha, dabDist, d.radius));
        }
        applyProgram->setUniform("uBrushCenter", brushCenterX, brushCenterY);

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_smudgeWorkTex[wSrc]);
        if (wetMode) {
            bindWetReservoir(m_gl, applyProgram, m_smudgeReservoirTex[rSrc], m_blurLinearSampler);
        } else {
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, m_smudgeReservoirTex[rSrc][0]);
            if (m_blurLinearSampler)
                m_gl->glBindSampler(1, m_blurLinearSampler);
        }
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_smudgeWorkTex[wDst], 0);
        m_gl->glViewport(0, 0, roiW, roiH);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;
        wSrc = wDst;
        didAnyDraw = true;
    }

    m_smudgeReservoirSrcIdx = rSrc;
    if (!dabs.empty()) {
        m_smudgePrevWorldX = prevDabX;
        m_smudgePrevWorldY = prevDabY;
    }
    const int srcIdx = wSrc;

    if (m_blurLinearSampler) {
        m_gl->glBindSampler(0, 0);
        m_gl->glBindSampler(1, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (dabTexId != 0) {
        m_gl->glActiveTexture(GL_TEXTURE3);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }
    unbindWetTextures(m_gl);

    // ----- 4. Flush only the rendered-dab bounds into stroke tiles ----------
    // The pickup ROI is reservoir-sized and can be much wider than the actual
    // dab. Materializing every pickup tile creates identity/empty stroke tiles
    // that should never participate in preview or flattening.
    if (didAnyDraw && writeMaxXi > writeMinXi && writeMaxYi > writeMinYi) {
        const GLuint finalTex = m_smudgeWorkTex[srcIdx];
        for (const TileKey& key : writeTiles) {
            const int32_t tileMinX = key.x * static_cast<int32_t>(TILE_SIZE);
            const int32_t tileMinY = key.y * static_cast<int32_t>(TILE_SIZE);
            const int32_t copyMinX = std::max(writeMinXi, tileMinX);
            const int32_t copyMinY = std::max(writeMinYi, tileMinY);
            const int32_t copyMaxX
                = std::min(writeMaxXi, tileMinX + static_cast<int32_t>(TILE_SIZE));
            const int32_t copyMaxY
                = std::min(writeMaxYi, tileMinY + static_cast<int32_t>(TILE_SIZE));
            if (copyMaxX <= copyMinX || copyMaxY <= copyMinY)
                continue;

            const bool tileAlreadyExists = strokeBuffer.hasTile(key);
            TileData& tile = strokeBuffer.getOrCreateTile(key);
            if (!tile.hasTexture()) {
                tileRenderer->ensureTileTexture(tile);
            }
            if (!tileAlreadyExists) {
                // Initialize newly-created stroke tile from the layer so
                // areas outside the copied bounds keep their original content.
                TileData* layerTile = layerGrid->getTile(key);
                if (layerTile && layerTile->isEmpty() && !layerGrid->hasDefaultFill()) {
                    layerTile = nullptr;
                }
                ensureUploadedTileTexture(layerTile);
                if (layerTile && layerTile->hasTexture()) {
                    if (!copyColorRegion(layerTile->textureId(), layerTile->format(), 0, 0,
                            tile.textureId(), tile.format(), 0, 0, TILE_SIZE, TILE_SIZE)) {
                        if (ownBatch)
                            endStampBatch();
                        return false;
                    }
                } else {
                    clearTexture(tile.textureId(), layerGrid ? layerGrid->defaultFillPacked() : 0u);
                }
            }

            m_gl->glCopyImageSubData(finalTex, GL_TEXTURE_2D, 0, copyMinX - roiMinXi,
                copyMinY - roiMinYi, 0, tile.textureId(), GL_TEXTURE_2D, 0, copyMinX - tileMinX,
                copyMinY - tileMinY, 0, copyMaxX - copyMinX, copyMaxY - copyMinY, 1);

            tile.clearDirty();
            strokeBuffer.removeDirty(key);
        }
    }

    m_smudgePrevValid = true;

    if (ownBatch) {
        endStampBatch();
    }
    return true;
}

bool GLBrushRenderer::stampLiquifySegmentGPU(TileGrid& strokeBuffer, GLTileRenderer* tileRenderer,
    const TileBrush& brush, const std::vector<TileBrush::DabPoint>& dabs, uint32_t canvasWidth,
    uint32_t canvasHeight, TileGrid* layerGrid, TileGrid* selectionMask, bool useSelectionMask)
{
    if (dabs.empty())
        return true;
    if (!m_liquifyFieldProgram || !m_liquifyResolveProgram || !layerGrid || !tileRenderer) {
        return false;
    }
    // Stroke buffer must share the document content format so the GPU copies
    // below (layer/result textures <-> stroke buffer) are glCopyImageSubData-safe.
    if (strokeBuffer.empty()) {
        strokeBuffer.setFormat(layerGrid->format());
    }
    if (!brush.isLiquifyMode()) {
        return false;
    }
    if (useSelectionMask && (!selectionMask || selectionMask->empty())) {
        return true;
    }

    const bool clipToCanvas = (canvasWidth > 0 && canvasHeight > 0);
    const float deg2rad = 3.14159265358979323846f / 180.0f;
    constexpr int32_t kFieldMargin = 4; // falloff/advection reach beyond radius
    constexpr int32_t kFieldGrowPad = 128; // amortize realloc on a travelling stroke

    auto ensureUploadedTileTexture = [&](TileData* tile) {
        if (!tile)
            return;
        if (!tile->hasTexture()) {
            tileRenderer->ensureTileTexture(*tile);
            tileRenderer->uploadTileData(*tile);
        } else if (tile->isDirty()) {
            tileRenderer->uploadTileData(*tile);
        }
    };

    // ----- 1. This segment's footprint (canvas px) -------------------------
    float fpMinXf = std::numeric_limits<float>::infinity();
    float fpMinYf = std::numeric_limits<float>::infinity();
    float fpMaxXf = -std::numeric_limits<float>::infinity();
    float fpMaxYf = -std::numeric_limits<float>::infinity();
    for (const auto& d : dabs) {
        const float ext = d.radius + static_cast<float>(kFieldMargin);
        fpMinXf = std::min(fpMinXf, d.worldX - ext);
        fpMinYf = std::min(fpMinYf, d.worldY - ext);
        fpMaxXf = std::max(fpMaxXf, d.worldX + ext);
        fpMaxYf = std::max(fpMaxYf, d.worldY + ext);
    }
    int32_t fpMinX = static_cast<int32_t>(std::floor(fpMinXf));
    int32_t fpMinY = static_cast<int32_t>(std::floor(fpMinYf));
    int32_t fpMaxX = static_cast<int32_t>(std::ceil(fpMaxXf)) + 1;
    int32_t fpMaxY = static_cast<int32_t>(std::ceil(fpMaxYf)) + 1;
    if (clipToCanvas) {
        fpMinX = std::max<int32_t>(fpMinX, 0);
        fpMinY = std::max<int32_t>(fpMinY, 0);
        fpMaxX = std::min<int32_t>(fpMaxX, static_cast<int32_t>(canvasWidth));
        fpMaxY = std::min<int32_t>(fpMaxY, static_cast<int32_t>(canvasHeight));
    }
    if (fpMaxX <= fpMinX || fpMaxY <= fpMinY) {
        return true;
    }

    // ----- 2. Resolve the field ROI (union with existing, padded on growth) -
    const bool firstSegmentOfStroke = !m_liquifyActive;
    const bool haveField
        = m_liquifyFieldTex[0] != 0 && m_liquifyFieldTex[1] != 0 && m_liquifySourceTex != 0;
    const bool reuse = !firstSegmentOfStroke && haveField && fpMinX >= m_liquifyRoiX
        && fpMinY >= m_liquifyRoiY && fpMaxX <= m_liquifyRoiX + m_liquifyRoiW
        && fpMaxY <= m_liquifyRoiY + m_liquifyRoiH;

    int32_t roiX = m_liquifyRoiX;
    int32_t roiY = m_liquifyRoiY;
    GLsizei roiW = m_liquifyRoiW;
    GLsizei roiH = m_liquifyRoiH;

    const bool ownBatch = !m_stampBatchActive;
    if (ownBatch) {
        beginStampBatch();
    }
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glDisable(GL_BLEND);

    if (!reuse) {
        int32_t nMinX = fpMinX, nMinY = fpMinY, nMaxX = fpMaxX, nMaxY = fpMaxY;
        if (!firstSegmentOfStroke && haveField) {
            nMinX = std::min(nMinX, m_liquifyRoiX);
            nMinY = std::min(nMinY, m_liquifyRoiY);
            nMaxX = std::max(nMaxX, m_liquifyRoiX + m_liquifyRoiW);
            nMaxY = std::max(nMaxY, m_liquifyRoiY + m_liquifyRoiH);
        }
        nMinX -= kFieldGrowPad;
        nMinY -= kFieldGrowPad;
        nMaxX += kFieldGrowPad;
        nMaxY += kFieldGrowPad;
        if (clipToCanvas) {
            nMinX = std::max<int32_t>(nMinX, 0);
            nMinY = std::max<int32_t>(nMinY, 0);
            nMaxX = std::min<int32_t>(nMaxX, static_cast<int32_t>(canvasWidth));
            nMaxY = std::min<int32_t>(nMaxY, static_cast<int32_t>(canvasHeight));
        }
        const int32_t newRoiX = nMinX;
        const int32_t newRoiY = nMinY;
        const GLsizei newRoiW = static_cast<GLsizei>(nMaxX - nMinX);
        const GLsizei newRoiH = static_cast<GLsizei>(nMaxY - nMinY);
        if (newRoiW <= 0 || newRoiH <= 0) {
            if (ownBatch)
                endStampBatch();
            return true;
        }

        auto createTex = [&](GLint internalFmt, GLenum fmt, GLenum type) -> GLuint {
            GLuint tex = 0;
            m_gl->glGenTextures(1, &tex);
            m_gl->glBindTexture(GL_TEXTURE_2D, tex);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_gl->glTexImage2D(
                GL_TEXTURE_2D, 0, internalFmt, newRoiW, newRoiH, 0, fmt, type, nullptr);
            return tex;
        };

        // RG32F (not RG16F): the field stores px offsets up to the stroke
        // length; half-float ULP grows to ~0.3 px at large displacement, which
        // shows up as smearing on stretched tips and as drift accumulating
        // through self-intersecting (curling) strokes. Full float keeps it crisp.
        GLuint newField0 = createTex(GL_RG32F, GL_RG, GL_FLOAT);
        GLuint newField1 = createTex(GL_RG32F, GL_RG, GL_FLOAT);
        // Frozen content source: copied from document tiles -> the target grid's
        // per-document format (glCopyImageSubData needs format-compatibility).
        const TilePixelFormat liquifyContentFormat
            = layerGrid ? layerGrid->format() : kDefaultTileFormat;
        m_liquifySourceFormat = liquifyContentFormat;
        GLuint newSource = createTex(tileGLInternalFormat(liquifyContentFormat), GL_RGBA,
            tileGLPixelType(liquifyContentFormat));
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (newField0 == 0 || newField1 == 0 || newSource == 0) {
            if (newField0)
                m_gl->glDeleteTextures(1, &newField0);
            if (newField1)
                m_gl->glDeleteTextures(1, &newField1);
            if (newSource)
                m_gl->glDeleteTextures(1, &newSource);
            if (ownBatch)
                endStampBatch();
            return false;
        }

        // Identity field everywhere, then migrate the old field's content into
        // the new buffer at the shifted origin so the accumulated warp survives
        // a ROI growth mid-stroke.
        clearTexture(newField0);
        clearTexture(newField1);
        if (!firstSegmentOfStroke && haveField && m_liquifyRoiW > 0 && m_liquifyRoiH > 0) {
            const int32_t offX = m_liquifyRoiX - newRoiX; // >= 0 (new ROI ⊇ old)
            const int32_t offY = m_liquifyRoiY - newRoiY;
            if (offX >= 0 && offY >= 0 && offX + m_liquifyRoiW <= newRoiW
                && offY + m_liquifyRoiH <= newRoiH) {
                m_gl->glCopyImageSubData(m_liquifyFieldTex[m_liquifyFieldSrcIdx], GL_TEXTURE_2D, 0,
                    0, 0, 0, newField0, GL_TEXTURE_2D, 0, offX, offY, 0, m_liquifyRoiW,
                    m_liquifyRoiH, 1);
            }
        }

        if (haveField) {
            m_gl->glDeleteTextures(1, &m_liquifyFieldTex[0]);
            m_gl->glDeleteTextures(1, &m_liquifyFieldTex[1]);
            m_gl->glDeleteTextures(1, &m_liquifySourceTex);
        }
        m_liquifyFieldTex[0] = newField0;
        m_liquifyFieldTex[1] = newField1;
        m_liquifySourceTex = newSource;
        m_liquifyFieldSrcIdx = 0; // migrated content lives in field[0]
        m_liquifyRoiX = newRoiX;
        m_liquifyRoiY = newRoiY;
        m_liquifyRoiW = newRoiW;
        m_liquifyRoiH = newRoiH;
        m_liquifyTexW = newRoiW;
        m_liquifyTexH = newRoiH;
        roiX = newRoiX;
        roiY = newRoiY;
        roiW = newRoiW;
        roiH = newRoiH;

        // Snapshot the frozen source (the original layer over the full ROI). The
        // layer is unmodified until the stroke flattens, so this stays valid.
        clearTexture(m_liquifySourceTex, layerGrid ? layerGrid->defaultFillPacked() : 0u);
        int32_t sMinTileX = static_cast<int32_t>(std::floor(static_cast<float>(roiX) / TILE_SIZE));
        int32_t sMinTileY = static_cast<int32_t>(std::floor(static_cast<float>(roiY) / TILE_SIZE));
        int32_t sMaxTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(roiX + roiW - 1) / TILE_SIZE));
        int32_t sMaxTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(roiY + roiH - 1) / TILE_SIZE));
        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            sMinTileX = std::max(sMinTileX, 0);
            sMinTileY = std::max(sMinTileY, 0);
            sMaxTileX = std::min(sMaxTileX, canvasMaxX);
            sMaxTileY = std::min(sMaxTileY, canvasMaxY);
        }
        for (int32_t ty = sMinTileY; ty <= sMaxTileY; ++ty) {
            for (int32_t tx = sMinTileX; tx <= sMaxTileX; ++tx) {
                TileData* layerTile = layerGrid->getTile(TileKey { tx, ty });
                if (layerTile && layerTile->isEmpty() && !layerGrid->hasDefaultFill())
                    continue;
                ensureUploadedTileTexture(layerTile);
                if (!layerTile || !layerTile->hasTexture())
                    continue;
                const int32_t tileMinX = tx * static_cast<int32_t>(TILE_SIZE);
                const int32_t tileMinY = ty * static_cast<int32_t>(TILE_SIZE);
                const int32_t cMinX = std::max(roiX, tileMinX);
                const int32_t cMinY = std::max(roiY, tileMinY);
                const int32_t cMaxX
                    = std::min(roiX + roiW, tileMinX + static_cast<int32_t>(TILE_SIZE));
                const int32_t cMaxY
                    = std::min(roiY + roiH, tileMinY + static_cast<int32_t>(TILE_SIZE));
                if (cMaxX <= cMinX || cMaxY <= cMinY)
                    continue;
                m_gl->glCopyImageSubData(layerTile->textureId(), GL_TEXTURE_2D, 0, cMinX - tileMinX,
                    cMinY - tileMinY, 0, m_liquifySourceTex, GL_TEXTURE_2D, 0, cMinX - roiX,
                    cMinY - roiY, 0, cMaxX - cMinX, cMaxY - cMinY, 1);
            }
        }
    }
    m_liquifyActive = true;

    const float invFieldW = 1.0f / static_cast<float>(roiW);
    const float invFieldH = 1.0f / static_cast<float>(roiH);

    // Optional selection mask snapshot over the full field ROI.
    if (useSelectionMask) {
        if (!ensureMaskScratchSize(roiW, roiH)) {
            if (ownBatch)
                endStampBatch();
            return false;
        }
        clearTexture(m_maskScratchTex);
        int32_t mMinTileX = static_cast<int32_t>(std::floor(static_cast<float>(roiX) / TILE_SIZE));
        int32_t mMinTileY = static_cast<int32_t>(std::floor(static_cast<float>(roiY) / TILE_SIZE));
        int32_t mMaxTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(roiX + roiW - 1) / TILE_SIZE));
        int32_t mMaxTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(roiY + roiH - 1) / TILE_SIZE));
        if (clipToCanvas) {
            const int32_t canvasMaxX = static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE);
            const int32_t canvasMaxY = static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE);
            mMinTileX = std::max(mMinTileX, 0);
            mMinTileY = std::max(mMinTileY, 0);
            mMaxTileX = std::min(mMaxTileX, canvasMaxX);
            mMaxTileY = std::min(mMaxTileY, canvasMaxY);
        }
        for (int32_t ty = mMinTileY; ty <= mMaxTileY; ++ty) {
            for (int32_t tx = mMinTileX; tx <= mMaxTileX; ++tx) {
                TileData* maskTile = selectionMask->getTile(TileKey { tx, ty });
                if (!maskTile)
                    continue;
                ensureUploadedTileTexture(maskTile);
                if (!maskTile->hasTexture())
                    continue;
                const int32_t tileMinX = tx * static_cast<int32_t>(TILE_SIZE);
                const int32_t tileMinY = ty * static_cast<int32_t>(TILE_SIZE);
                const int32_t cMinX = std::max(roiX, tileMinX);
                const int32_t cMinY = std::max(roiY, tileMinY);
                const int32_t cMaxX
                    = std::min(roiX + roiW, tileMinX + static_cast<int32_t>(TILE_SIZE));
                const int32_t cMaxY
                    = std::min(roiY + roiH, tileMinY + static_cast<int32_t>(TILE_SIZE));
                if (cMaxX <= cMinX || cMaxY <= cMinY)
                    continue;
                m_gl->glCopyImageSubData(maskTile->textureId(), GL_TEXTURE_2D, 0, cMinX - tileMinX,
                    cMinY - tileMinY, 0, m_maskScratchTex, GL_TEXTURE_2D, 0, cMinX - roiX,
                    cMinY - roiY, 0, cMaxX - cMinX, cMaxY - cMinY, 1);
            }
        }
    }

    // ----- 3. Advect the displacement field, one pass per dab --------------
    //   B_new[y] = B_old[y - w(y)*step] - w(y)*step.  step = brush travel since
    //   the PREVIOUS dab (carried across segments so slow strokes still warp).
    if (m_blurLinearSampler) {
        m_gl->glBindSampler(0, m_blurLinearSampler);
    }
    m_liquifyFieldProgram->use();
    m_liquifyFieldProgram->setUniform("uFieldSrc", 0);
    m_liquifyFieldProgram->setUniform("uMaskTexture", 2);
    m_liquifyFieldProgram->setUniform("uUseMask", useSelectionMask ? 1 : 0);
    m_liquifyFieldProgram->setUniform("uInvFieldSize", invFieldW, invFieldH);
    m_liquifyFieldProgram->setUniform("uInvMaskSize", invFieldW, invFieldH);
    m_liquifyFieldProgram->setUniform("uMaxValidUv", 1.0f, 1.0f);
    m_liquifyFieldProgram->setUniform(
        "uViewportSize", static_cast<float>(roiW), static_cast<float>(roiH));
    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_maskScratchTex);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }

    // Sub-mode + its per-dab rate (Push reads uStepDelta and ignores uRate).
    const int liquifyMode = brush.liquifyToolMode();
    float modeRate = 0.0f;
    switch (liquifyMode) {
    case 1:
    case 2:
        modeRate = 0.05f;
        break; // Twirl: radians/dab
    case 3:
    case 4:
        modeRate = 0.03f;
        break; // Bloat/Pucker: fraction/dab
    default:
        modeRate = 0.0f;
        break; // Push
    }
    m_liquifyFieldProgram->setUniform("uMode", liquifyMode);
    m_liquifyFieldProgram->setUniform("uRate", modeRate);

    float prevX = firstSegmentOfStroke ? dabs.front().worldX : m_liquifyPrevWorldX;
    float prevY = firstSegmentOfStroke ? dabs.front().worldY : m_liquifyPrevWorldY;
    int fSrc = m_liquifyFieldSrcIdx;
    bool didAdvect = false;
    for (const auto& d : dabs) {
        const float stepX = d.worldX - prevX;
        const float stepY = d.worldY - prevY;
        prevX = d.worldX;
        prevY = d.worldY;
        if (d.alpha == 0)
            continue;
        // Push needs travel to drag; the position-based modes (twirl/bloat/
        // pucker) apply at every dab even on a near-stationary scrub.
        if (liquifyMode == 0 && stepX == 0.0f && stepY == 0.0f)
            continue;

        const int fDst = fSrc ^ 1;
        const float strength = static_cast<float>(d.alpha) / 255.0f;
        m_liquifyFieldProgram->setUniform("uBrushRadius", d.radius);
        m_liquifyFieldProgram->setUniform("uBrushHardness", d.hardness);
        m_liquifyFieldProgram->setUniform("uBrushRoundness", std::clamp(d.roundness, 0.0f, 1.0f));
        m_liquifyFieldProgram->setUniform("uBrushAngleRad", d.angleDegrees * deg2rad);
        m_liquifyFieldProgram->setUniform("uBrushCenter", d.worldX - static_cast<float>(roiX),
            d.worldY - static_cast<float>(roiY));
        m_liquifyFieldProgram->setUniform("uStepDelta", stepX, stepY);
        m_liquifyFieldProgram->setUniform("uStrength", std::clamp(strength, 0.0f, 1.0f));

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_liquifyFieldTex[fSrc]);
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_liquifyFieldTex[fDst], 0);
        m_gl->glViewport(0, 0, roiW, roiH);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCallEstimate;
        fSrc = fDst;
        didAdvect = true;
    }
    m_liquifyFieldSrcIdx = fSrc;
    m_liquifyPrevWorldX = prevX;
    m_liquifyPrevWorldY = prevY;

    if (!didAdvect) {
        if (m_blurLinearSampler)
            m_gl->glBindSampler(0, 0);
        if (useSelectionMask) {
            m_gl->glActiveTexture(GL_TEXTURE2);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            m_gl->glActiveTexture(GL_TEXTURE0);
        }
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (ownBatch)
            endStampBatch();
        return true; // start point only — field ready, nothing to resolve yet
    }

    // ----- 4. Resolve the segment footprint: source sampled ONCE through the
    //          accumulated field, into the work buffer, then to stroke tiles. -
    const GLsizei fpW = static_cast<GLsizei>(fpMaxX - fpMinX);
    const GLsizei fpH = static_cast<GLsizei>(fpMaxY - fpMinY);
    if (!ensureSmudgeWorkTextures(m_gl, m_smudgeWorkTex, m_smudgeWorkWidth, m_smudgeWorkHeight,
            m_smudgeWorkFormat, fpW, fpH, layerGrid ? layerGrid->format() : kDefaultTileFormat)) {
        if (m_blurLinearSampler)
            m_gl->glBindSampler(0, 0);
        if (ownBatch)
            endStampBatch();
        return false;
    }

    if (m_blurLinearSampler) {
        m_gl->glBindSampler(1, m_blurLinearSampler);
    }
    m_liquifyResolveProgram->use();
    m_liquifyResolveProgram->setUniform("uSourceTexture", 0);
    m_liquifyResolveProgram->setUniform("uFieldTex", 1);
    m_liquifyResolveProgram->setUniform("uInvFieldSize", invFieldW, invFieldH);
    m_liquifyResolveProgram->setUniform("uMaxValidUv", 1.0f, 1.0f);
    m_liquifyResolveProgram->setUniform(
        "uFieldOffset", static_cast<float>(fpMinX - roiX), static_cast<float>(fpMinY - roiY));
    m_liquifyResolveProgram->setUniform(
        "uViewportSize", static_cast<float>(fpW), static_cast<float>(fpH));
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_liquifySourceTex);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_liquifyFieldTex[m_liquifyFieldSrcIdx]);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_smudgeWorkTex[0], 0);
    m_gl->glViewport(0, 0, fpW, fpH);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    ++m_drawCallEstimate;

    if (m_blurLinearSampler) {
        m_gl->glBindSampler(0, 0);
        m_gl->glBindSampler(1, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useSelectionMask) {
        m_gl->glActiveTexture(GL_TEXTURE2);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    // ----- 5. Copy resolved footprint into the stroke buffer tiles ---------
    {
        const GLuint finalTex = m_smudgeWorkTex[0];
        int32_t tMinTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(fpMinX) / TILE_SIZE));
        int32_t tMinTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(fpMinY) / TILE_SIZE));
        int32_t tMaxTileX
            = static_cast<int32_t>(std::floor(static_cast<float>(fpMaxX - 1) / TILE_SIZE));
        int32_t tMaxTileY
            = static_cast<int32_t>(std::floor(static_cast<float>(fpMaxY - 1) / TILE_SIZE));
        for (int32_t ty = tMinTileY; ty <= tMaxTileY; ++ty) {
            for (int32_t tx = tMinTileX; tx <= tMaxTileX; ++tx) {
                TileKey key { tx, ty };
                const int32_t tileMinX = tx * static_cast<int32_t>(TILE_SIZE);
                const int32_t tileMinY = ty * static_cast<int32_t>(TILE_SIZE);
                const int32_t cMinX = std::max(fpMinX, tileMinX);
                const int32_t cMinY = std::max(fpMinY, tileMinY);
                const int32_t cMaxX = std::min(fpMaxX, tileMinX + static_cast<int32_t>(TILE_SIZE));
                const int32_t cMaxY = std::min(fpMaxY, tileMinY + static_cast<int32_t>(TILE_SIZE));
                if (cMaxX <= cMinX || cMaxY <= cMinY)
                    continue;

                const bool tileAlreadyExists = strokeBuffer.hasTile(key);
                TileData& tile = strokeBuffer.getOrCreateTile(key);
                if (!tile.hasTexture()) {
                    tileRenderer->ensureTileTexture(tile);
                }
                if (!tileAlreadyExists) {
                    // New stroke tile: seed from the layer so the parts outside
                    // this footprint keep the original content.
                    TileData* layerTile = layerGrid->getTile(key);
                    if (layerTile && layerTile->isEmpty() && !layerGrid->hasDefaultFill()) {
                        layerTile = nullptr;
                    }
                    ensureUploadedTileTexture(layerTile);
                    if (layerTile && layerTile->hasTexture()) {
                        m_gl->glCopyImageSubData(layerTile->textureId(), GL_TEXTURE_2D, 0, 0, 0, 0,
                            tile.textureId(), GL_TEXTURE_2D, 0, 0, 0, 0, TILE_SIZE, TILE_SIZE, 1);
                    } else {
                        clearTexture(
                            tile.textureId(), layerGrid ? layerGrid->defaultFillPacked() : 0u);
                    }
                }

                m_gl->glCopyImageSubData(finalTex, GL_TEXTURE_2D, 0, cMinX - fpMinX, cMinY - fpMinY,
                    0, tile.textureId(), GL_TEXTURE_2D, 0, cMinX - tileMinX, cMinY - tileMinY, 0,
                    cMaxX - cMinX, cMaxY - cMinY, 1);

                tile.clearDirty();
                strokeBuffer.removeDirty(key);
            }
        }
    }

    if (ownBatch) {
        endStampBatch();
    }
    return true;
}

} // namespace aether
