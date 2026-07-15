// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_GLVIEWPORTCOMPOSITOR_H
#define RUWA_FEATURES_CANVAS_RENDERING_GLVIEWPORTCOMPOSITOR_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "features/canvas/rendering/GLCompositor.h"

#include <QOpenGLFunctions_4_5_Core>

#include <functional>
#include <memory>
#include <vector>

namespace aether {

class GLShaderProgram;
class GLLayerEffectRenderer;

class GLViewportCompositor {
public:
    using SourceResolver = std::function<GLuint(const CompositeLayerInfo&)>;
    // Resolves the layer's painted mask in the same viewport space as its color
    // source. The compositor consumes it only in the final blend, after effects,
    // matching GLCompositor's document-tile ordering.
    using LayerMaskResolver = std::function<GLuint(const CompositeLayerInfo&)>;

    // A raster layer's screen source rendered with `padX`/`padY` extra screen
    // pixels on every side (the SAME camera, so the enlarged surface is centred:
    // overscan pixel (padX+x, padY+y) is the same world point as viewport pixel
    // (x, y), and cropping the centre [pad, pad, W, H] recovers the viewport).
    // `region` is the fragment->document affine of THAT enlarged texture. Lets a
    // distortion sample beyond the visible viewport in live preview without
    // materialising the whole layer in document resolution (keeps the
    // screen-space cost: source is viewport+2*pad, not the full layer).
    struct OverscanLayerSource {
        GLuint texture = 0;
        ruwa::core::effects::EffectRegionFrame region;
    };
    // Returns an OverscanLayerSource for `layer` at the requested pad, or a zero
    // texture to decline (groups / transform-target / infinite-canvas cases the
    // caller cannot overscan) — the compositor then falls back to the normal
    // viewport-sized effect path for that layer.
    using OverscanSourceResolver
        = std::function<OverscanLayerSource(const CompositeLayerInfo&, int padX, int padY)>;

    struct CanvasClipParams {
        bool enabled;
        Vector2 cameraPosition;
        float cameraZoom;
        float cameraRotation;
        float canvasWidth;
        float canvasHeight;
        float canvasCornerRadius;

        CanvasClipParams()
            : enabled(false)
            , cameraPosition()
            , cameraZoom(1.0f)
            , cameraRotation(0.0f)
            , canvasWidth(0.0f)
            , canvasHeight(0.0f)
            , canvasCornerRadius(0.0f)
        {
        }
    };

    struct LassoMaskParams {
        GLuint maskTexture;
        int originX;
        int originY;
        int width;
        int height;

        LassoMaskParams()
            : maskTexture(0)
            , originX(0)
            , originY(0)
            , width(0)
            , height(0)
        {
        }
    };

    explicit GLViewportCompositor(QOpenGLFunctions_4_5_Core* gl);
    ~GLViewportCompositor();

    GLViewportCompositor(const GLViewportCompositor&) = delete;
    GLViewportCompositor& operator=(const GLViewportCompositor&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    void beginFrame(uint32_t width, uint32_t height);
    /// spaceScale rescales pixel-radius effects (e.g. blur) so a document-pixel
    /// radius reads correctly in this screen-space pass — pass the camera zoom.
    /// `effectRegion` is the fragment->document affine of the screen texture (see
    /// EffectRegionFrame::useAffine); when valid it lets positional & distortion
    /// effects render live in this preview. Pass an invalid (default) frame to
    /// keep them suppressed (e.g. where the mapping is unknown).
    /// `overscanResolver` (optional) supplies enlarged sources so distortion /
    /// bounds-expanding effects can reach beyond the visible viewport in preview
    /// (see OverscanSourceResolver). Only raster layers whose chain declares a
    /// sampling reach use it; everything else stays viewport-resolution.
    /// `layerMaskResolver` supplies painted masks in the final viewport space;
    /// they gate the effected layer in the same final blend as GLCompositor.
    GLuint compositeLayers(const std::vector<CompositeLayerInfo>& layers,
        const SourceResolver& sourceResolver, const Color& backdropColor = Color::transparent(),
        float spaceScale = 1.0f, const ruwa::core::effects::EffectRegionFrame& effectRegion = {},
        const OverscanSourceResolver& overscanResolver = {},
        const LayerMaskResolver& layerMaskResolver = {});
    void drawTexture(GLuint texture, const CanvasClipParams& clipParams = CanvasClipParams(),
        const LassoMaskParams& lassoMask = LassoMaskParams(), bool replaceWithCoverage = false);

    // Gate a screen-space color texture by a layer mask, returning a new texture
    // = colorTex * reveal, where reveal = clamp(lum(maskTexel.rgb) + (1 - maskTexel.a), 0, 1)
    // is the same luminance-reveal the main compositor uses (composite.frag
    // uClipMaskLuminanceReveal). Both textures must already be in the same screen
    // space. Used to apply a viewport-preview target's fixed-position layer mask
    // after the transform / fill pass has produced its color. The result lives in
    // a dedicated scratch texture (not the ping-pong set), so it stays valid while
    // a subsequent compositeLayers() pass consumes it. Returns 0 on failure.
    GLuint applyLuminanceRevealMask(
        GLuint colorTex, GLuint maskTex, uint32_t width, uint32_t height);

    bool isInitialized() const { return m_initialized; }

private:
    GLuint compositeLayerStack(const std::vector<CompositeLayerInfo>& layers, float parentOpacity,
        bool useSrcAtop, const SourceResolver& sourceResolver,
        const LayerMaskResolver& layerMaskResolver,
        const Color& backdropColor = Color::transparent());
    void ensureRenderTargets();
    void clearTexture(GLuint texture);
    void blendPass(GLuint baseTexture, GLuint srcTexture, int blendMode, float opacity,
        bool preserveBaseAlpha = false, bool replaceBase = false, bool srcAtop = false,
        const Color& backdropColor = Color::transparent(), bool useGroupComposite = false,
        GLuint groupPassThroughTexture = 0, GLuint groupSourceCoverageTexture = 0,
        GLuint groupCoverageTexture = 0, GLuint layerMaskTexture = 0,
        bool layerMaskLuminanceReveal = false);
    GLuint applyLayerEffects(
        GLuint sourceTexture, const CompositeLayerInfo& layer, bool realtimeOnly);
    // Runs the layer's effect chain on `sourceTexture` at an explicit output size
    // and region (rather than the compositor's viewport frame). Shared by the
    // normal viewport path (w=m_width, region=m_effectRegion) and the overscan
    // reach path (enlarged size + region).
    GLuint applyLayerEffectsSized(GLuint sourceTexture, const CompositeLayerInfo& layer,
        uint32_t width, uint32_t height, const ruwa::core::effects::EffectRegionFrame& region,
        bool realtimeOnly);
    // Distortion-reach path for a raster layer: obtains an overscanned source via
    // `overscanResolver`, runs the whole chain at overscan size, and blits the
    // centre back to a viewport-sized texture. Returns 0 to signal the caller must
    // fall back to the normal path (no reach / resolver declined / not screen space).
    GLuint applyLayerEffectsWithReach(
        const CompositeLayerInfo& layer, const OverscanSourceResolver& overscanResolver);
    // Screen-space sampling reach of a layer's realtime chain, in screen pixels
    // (document reach * zoom), clamped so the overscan texture stays within a
    // resource bound. 0 when the chain needs no reach.
    int layerReachScreenPixels(const CompositeLayerInfo& layer) const;
    struct GroupCompositeFrame {
        GLuint ping[2] = { 0, 0 };
        GLuint passThrough = 0;
        GLuint effected = 0;
        GLuint sourceCoverage = 0;
        GLuint coverage = 0;
    };
    GroupCompositeFrame& ensureGroupCompositeFrame(size_t depth);
    void destroyGroupCompositeFrames();

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_compositeProgram;
    std::unique_ptr<GLShaderProgram> m_blitProgram;
    std::unique_ptr<GLLayerEffectRenderer> m_effectRenderer;
    GLuint m_fbo = 0;
    GLuint m_emptyVao = 0;
    GLuint m_pingPongTextures[2] = { 0, 0 };
    GLuint m_clipGroupTextures[2] = { 0, 0 };
    std::vector<std::unique_ptr<GroupCompositeFrame>> m_groupCompositeFrames;
    size_t m_groupCompositeDepth = 0;
    GLuint m_transparentTexture = 0;
    GLuint m_maskRevealTexture = 0;
    uint32_t m_maskRevealWidth = 0;
    uint32_t m_maskRevealHeight = 0;
    int m_currentPing = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_spaceScale = 1.0f;
    // Fragment->document affine of the current screen texture, threaded into the
    // effect passes so positional / distortion effects render live in preview.
    ruwa::core::effects::EffectRegionFrame m_effectRegion;
    // Overscan resolver for the in-flight compositeLayers pass (owned by the
    // caller; valid only for the duration of that call). Null = no reach support.
    const OverscanSourceResolver* m_overscanResolver = nullptr;
    // Viewport-sized destination for the centre crop of an overscan chain result.
    GLuint m_overscanCropTexture = 0;
    uint32_t m_overscanCropWidth = 0;
    uint32_t m_overscanCropHeight = 0;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_GLVIEWPORTCOMPOSITOR_H
