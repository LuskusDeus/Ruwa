// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_EFFECTS_GLLAYEREFFECTRENDERREGISTRY_H
#define RUWA_FEATURES_EFFECTS_GLLAYEREFFECTRENDERREGISTRY_H

#include "features/effects/LayerEffectTypes.h"
#include "shared/types/Result.h"

#include <QHash>
#include <QList>
#include <QOpenGLFunctions_4_5_Core>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

namespace aether {

struct GLLayerEffectSourceInfo {
    GLuint originalSourceTexture = 0;
    /// The layer's content BEFORE this effect chain ran (== the chain input). Its
    /// alpha channel is the layer's coverage / shape, which shape-driven effects
    /// (stroke/outline, inner & outer glow, bevel) read instead of the in-flight
    /// `sourceTexture` that earlier effects may already have recoloured. In the
    /// padded neighbourhood path this is the padded source, so an outline can grow
    /// outside the layer's own tile.
    GLuint layerAlphaTexture = 0;
};

struct GLLayerEffectBackdropInfo {
    GLuint texture = 0;
};

struct GLLayerEffectRenderContext {
    QOpenGLFunctions_4_5_Core* gl = nullptr;
    GLuint fbo = 0;
    GLuint emptyVao = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    ruwa::core::effects::EffectEvaluationSpace evaluationSpace
        = ruwa::core::effects::EffectEvaluationSpace::DocumentTile;
    /// Multiplies pixel-radius sampling distances so a radius expressed in
    /// DOCUMENT pixels produces the right magnitude in the current space. 1.0 in
    /// document-tile space; the camera zoom in viewport/screen space (otherwise a
    /// document-space blur reads at screen pixels and looks zoom-scaled).
    float spaceScale = 1.0f;
    /// Document-pixel mapping of this region (see EffectRegionFrame). Lets an
    /// effect be a function of absolute document position; `valid == false` when
    /// unknown (positional effects then no-op / fall back).
    ruwa::core::effects::EffectRegionFrame region;
    /// True when `sourceTexture`/`outputWidth`x`outputHeight` span the WHOLE layer
    /// (its materialised bbox), not just one tile — the guarantee a distortion
    /// (readsWholeLayer) needs before it may sample far from a fragment. When
    /// false a distortion pass must pass its input through unchanged (like the
    /// positional effects that gate on `region.valid`), because sampling beyond
    /// the current tile/screen region would read unrelated or clamped texels.
    bool wholeLayerSource = false;
    /// Allocate an extra working texture sized to the current region. Pass
    /// `highPrecision == true` for an RGBA16F buffer (banding-free curves/levels,
    /// large-radius accumulation, linear-space work); false for the default
    /// RGBA8. The returned texture is owned by the renderer and reused across
    /// frames — do not delete it.
    std::function<GLuint(bool highPrecision)> allocateScratchTexture;
    /// Output-space rect (framebuffer pixels) that downstream consumers of this
    /// pass's target actually read: in the padded-neighbourhood path it is the
    /// final centre-tile crop grown by the declared sampling reach
    /// (pixelExpansionRadius) of every LATER effect in the chain. A pass MAY
    /// clip its writes to it (see beginRoiScissor) because pixels outside are
    /// never read; honouring it is optional and purely an optimization.
    /// roiWidth == 0 means "unknown / everything is needed" (the viewport path).
    int roiX = 0;
    int roiY = 0;
    uint32_t roiWidth = 0;
    uint32_t roiHeight = 0;
    GLLayerEffectSourceInfo source;
    GLLayerEffectBackdropInfo backdrop;
};

/// Enables a scissor clipping writes to the context ROI, expanded by
/// expandX/expandY for intermediate sub-passes whose output a later sub-pass
/// samples around (e.g. the horizontal half of a separable blur is read
/// +/- radius vertically by the vertical half). A 1-texel guard ring is always
/// added so bilinear reads at ROI edges (crop blit, chained effects) never
/// touch unwritten texels. Returns true when a scissor was enabled; the caller
/// must pass that value to endRoiScissor after drawing. No-op (returns false)
/// when the ROI is unknown or already covers the whole output.
inline bool beginRoiScissor(const GLLayerEffectRenderContext& context, int expandX, int expandY)
{
    if (!context.gl || context.roiWidth == 0 || context.roiHeight == 0) {
        return false;
    }
    constexpr int64_t kGuardTexels = 1;
    const int64_t outW = static_cast<int64_t>(context.outputWidth);
    const int64_t outH = static_cast<int64_t>(context.outputHeight);
    const int64_t x0
        = std::max<int64_t>(0, static_cast<int64_t>(context.roiX) - expandX - kGuardTexels);
    const int64_t y0
        = std::max<int64_t>(0, static_cast<int64_t>(context.roiY) - expandY - kGuardTexels);
    const int64_t x1 = std::min(outW,
        static_cast<int64_t>(context.roiX) + static_cast<int64_t>(context.roiWidth) + expandX
            + kGuardTexels);
    const int64_t y1 = std::min(outH,
        static_cast<int64_t>(context.roiY) + static_cast<int64_t>(context.roiHeight) + expandY
            + kGuardTexels);
    if (x0 >= x1 || y0 >= y1 || (x0 == 0 && y0 == 0 && x1 == outW && y1 == outH)) {
        return false;
    }
    context.gl->glEnable(GL_SCISSOR_TEST);
    context.gl->glScissor(static_cast<GLint>(x0), static_cast<GLint>(y0),
        static_cast<GLsizei>(x1 - x0), static_cast<GLsizei>(y1 - y0));
    return true;
}

inline void endRoiScissor(const GLLayerEffectRenderContext& context, bool scissorActive)
{
    if (scissorActive && context.gl) {
        context.gl->glDisable(GL_SCISSOR_TEST);
    }
}

/// The resolved fragment<->document mapping a positional / distortion pass needs:
/// a document origin, the two basis vectors spanned by fragTexCoord (frag->doc),
/// and the inverse rows (doc-relative->frag) for the sample step. For the
/// axis-aligned document-tile / whole-layer paths the basis is derived from the
/// pass output size and documentPxPerTexel (bit-identical to the former
/// origin + uv*outputSize*docPxPerTexel formula); the viewport path supplies a
/// full affine (rotation / flip / zoom) via EffectRegionFrame::useAffine.
struct EffectDocFrame {
    float originX = 0.0f, originY = 0.0f;
    float basisXx = 0.0f, basisXy = 0.0f; ///< frag.x -> doc
    float basisYx = 0.0f, basisYy = 0.0f; ///< frag.y -> doc
    float inv0x = 0.0f, inv0y = 0.0f; ///< doc-relative -> frag.x
    float inv1x = 0.0f, inv1y = 0.0f; ///< doc-relative -> frag.y
    bool valid = false;
};

inline EffectDocFrame computeEffectDocFrame(const GLLayerEffectRenderContext& context)
{
    EffectDocFrame f;
    const auto& region = context.region;
    f.valid = region.valid;
    f.originX = region.originX;
    f.originY = region.originY;
    if (region.useAffine) {
        f.basisXx = region.basisXx;
        f.basisXy = region.basisXy;
        f.basisYx = region.basisYx;
        f.basisYy = region.basisYy;
    } else {
        const float sx = static_cast<float>(context.outputWidth) * region.documentPxPerTexel;
        const float sy = static_cast<float>(context.outputHeight) * region.documentPxPerTexel;
        f.basisXx = sx;
        f.basisXy = 0.0f;
        f.basisYx = 0.0f;
        f.basisYy = sy;
    }
    // Inverse of the 2x2 [basisX basisY] (columns) -> rows map doc-relative to frag.
    const float det = f.basisXx * f.basisYy - f.basisYx * f.basisXy;
    const float invDet = std::abs(det) > 1e-12f ? 1.0f / det : 0.0f;
    f.inv0x = f.basisYy * invDet;
    f.inv0y = -f.basisYx * invDet;
    f.inv1x = -f.basisXy * invDet;
    f.inv1y = f.basisXx * invDet;
    return f;
}

/// Bundles the arguments of GLLayerEffectRenderer::applyEffects. Collapsing the
/// former 10 positional parameters (most defaulted) into one struct keeps call
/// sites readable and lets new inputs (whole-layer materialisation, future
/// aux textures) be added as named fields without re-threading every caller.
/// `effects` is held by pointer to avoid copying the chain per tile — it must
/// outlive the call.
struct EffectChainRequest {
    GLuint sourceTexture = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    const QList<ruwa::core::effects::LayerEffectState>* effects = nullptr;
    ruwa::core::effects::EffectEvaluationSpace space
        = ruwa::core::effects::EffectEvaluationSpace::DocumentTile;
    bool realtimeOnly = false;
    GLuint finalTargetTexture = 0;
    GLuint backdropTexture = 0;
    float spaceScale = 1.0f;
    ruwa::core::effects::EffectRegionFrame region {};
    /// See GLLayerEffectRenderContext::wholeLayerSource — set when sourceTexture
    /// spans the whole layer so distortion passes may sample freely.
    bool wholeLayerSource = false;
    /// Non-null only during a continuous parameter edit. The renderer caches
    /// the unchanged prefix before this effect for the lifetime of that edit.
    QUuid liveEditedEffectId;
    quint64 liveEditSourceVariant = 0;
};

class IGLLayerEffectPass {
public:
    virtual ~IGLLayerEffectPass() = default;

    virtual QString typeId() const = 0;
    virtual Result<void> initialize(QOpenGLFunctions_4_5_Core* gl, const QString& shaderDir) = 0;
    virtual GLuint render(const GLLayerEffectRenderContext& context,
        const ruwa::core::effects::LayerEffectState& effectState, GLuint sourceTexture,
        GLuint targetTexture)
        = 0;
};

class GLLayerEffectRenderRegistry {
public:
    using Factory = std::function<std::unique_ptr<IGLLayerEffectPass>()>;

    static GLLayerEffectRenderRegistry& instance();

    bool registerFactory(const QString& typeId, Factory factory);
    /// Removes a previously registered factory. Returns true if one was removed.
    /// Used to roll back a partially-committed plugin registration.
    bool unregisterFactory(const QString& typeId);
    bool contains(const QString& typeId) const;
    QList<QString> typeIds() const;
    std::unique_ptr<IGLLayerEffectPass> createPass(const QString& typeId) const;

private:
    QHash<QString, Factory> m_factories;
};

class AutoGLLayerEffectPassRegistration {
public:
    AutoGLLayerEffectPassRegistration(
        const QString& typeId, GLLayerEffectRenderRegistry::Factory factory);

    bool registered() const { return m_registered; }

private:
    bool m_registered = false;
};

} // namespace aether

#endif // RUWA_FEATURES_EFFECTS_GLLAYEREFFECTRENDERREGISTRY_H
