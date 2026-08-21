// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H
#define RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H

#include "shared/rendering/CanvasBackdropSource.h"
#include "shared/types/Result.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QtGui/qopengl.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace aether {

class GLShaderProgram;

struct CanvasBackdropRegion {
    /// OpenGLCanvasWidget-local logical coordinates with a top-left origin.
    QRectF rect;
    qreal cornerRadius = 0.0;
    qreal opacity = 1.0;
    /// Theme surface the frost is tinted towards. Invalid leaves it untinted.
    QColor surfaceTint;
    /// Levels of the frost pyramid to run, clamped to the renderer's own
    /// maximum; negative takes that maximum. The reach is dominated by how far
    /// down the pyramid goes, so one level below the default reads as roughly
    /// half the blur. Zero skips the pyramid and leaves the region on the
    /// captured half, which is a mild reduction rather than none at all.
    int frostLevels = -1;
    /// Multiplier on the analytic drop shadow. Zero removes the shadow, and
    /// with it the composite padding it needed.
    qreal shadowStrength = 1.0;
    /// Multiplier on how hard the lens bends what is behind it: it scales the
    /// optical thickness and the screen-space splay together, so the plate
    /// stays the same shape and only distorts less. The lens DEPTH is not
    /// scaled - that is how wide the curved edge is, not how strong it is, and
    /// moving it would change the silhouette the tint has to match.
    qreal refractionStrength = 1.0;
};

/// Composites same-frame backdrop-blur regions directly into the canvas target.
/// All methods require the canvas OpenGL context to be current.
class CanvasBackdropRenderer {
public:
    explicit CanvasBackdropRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~CanvasBackdropRenderer();

    CanvasBackdropRenderer(const CanvasBackdropRenderer&) = delete;
    CanvasBackdropRenderer& operator=(const CanvasBackdropRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    bool render(GLuint sourceFbo, GLuint defaultFbo, int surfaceWidth, int surfaceHeight,
        qreal logicalToSurfaceScaleX, qreal logicalToSurfaceScaleY,
        const std::vector<CanvasBackdropRegion>& regions);

    /// Device-pixel margin this pass needs around a region at @p deviceScale:
    /// enough captured backdrop for the refraction and the frost to sample,
    /// and - unless @p withShadow is false - enough room outside the region
    /// for the analytic shadow to fade out in.
    ///
    /// A caller that hands the renderer a cropped backdrop has to leave at
    /// least this much of it around the region, or the frost reads clamped
    /// edge texels and the shadow ends on a straight line.
    static int requiredMarginDevicePx(qreal deviceScale, bool withShadow = true);

private:
    /// Two blits reach this, so it must stay at or below 4 or the reduction
    /// would drop more than half per step and alias.
    static constexpr int kDownsampleScale = 4;
    static_assert(kDownsampleScale <= 4, "reduction chain halves at most per blit");

    /// Levels in the dual-filter pyramid, level 0 being capture/kDownsampleScale
    /// and fed from the captured half. The reach comes almost entirely from how
    /// far down the pyramid goes - the coarsest level's texel dominates the
    /// summed variance - so this, not the tap offset, is the knob for frost
    /// width. Dropping from three levels to two roughly halves it; going the
    /// other way with kBlurSampleOffset instead would push the taps back under
    /// one texel, which is the failure the dual filter replaced.
    static constexpr int kBlurLevels = 2;

    /// Driven by kGlassBareOpticsMode, the one flag that also zeroes the tints.
    /// False skips the pyramid entirely
    /// and points the composite at the captured backdrop, which is then held at
    /// full capture resolution rather than a half - a quarter-resolution source
    /// would read as blur however the levels are set, and blur is exactly what
    /// this switch is meant to remove.
    static constexpr bool kFrostEnabled = !ruwa::shared::rendering::kGlassBareOpticsMode;
    /// Tap spacing in half-texels of the source level, shared by both passes.
    /// The reach is dominated by the pyramid, so this only trims the shape -
    /// beyond ~2 the five-tap reduction shows its cross.
    static constexpr float kBlurSampleOffset = 1.25f;
    /// Approximate outer reach of the whole chain, in device pixels. Variance
    /// adds across the three passes, whose source texels are 2, 4 and 8 device
    /// px wide, giving sigma ~= 7 px and a usable reach of ~2.5 sigma.
    /// It is a budget, not a measurement: the capture padding below is sized
    /// from it, and the frost may not reach past what was captured.
    static constexpr int kBlurReachDevicePx = 16;

    /// The reduction targets stay in linear light, so they cannot be 8-bit: encoded values must not
    /// be averaged, and linear 8-bit would band in the darks. R11F_G11F_B10F keeps the four bytes
    /// per texel the old RGBA8 chain used - no bandwidth or footprint regression - and the chain
    /// never carried alpha anyway (the composite makes its own from the coverage field).
    static constexpr GLenum kLinearTargetFormat = GL_R11F_G11F_B10F;

    /// GPU glass optics. Depth and refraction are independent, matching the
    /// controls exposed by Figma: depth determines how far the curved edge
    /// reaches into the plate, while shift determines how strongly that edge
    /// bends the captured scene. Keeping the depth short of the plate's medial
    /// axis is essential; the rounded-rect SDF has no unique normal there.
    static constexpr float kRefractionDepthLogicalPx = 26.0f;
    static constexpr float kRefractionShiftLogicalPx = 22.0f;
    static constexpr float kMaxRefractionShiftDevicePx = 36.0f;
    static constexpr float kMaxSurfaceTilt = 0.90f;

    /// Small chromatic separation at strongly refracted edges. Kept well below
    /// the exaggerated reference value so it reads as a glass fringe rather
    /// than as three displaced copies of the backdrop.
    static constexpr float kChromaticDispersion = 0.08f;

    /// Projected refraction spread. Texture lookups converge towards the centre
    /// in screen-space so the visible image fans outwards. Its amplitude follows
    /// the rounded-rect SDF through a circular sagitta; zero removes only that
    /// extra spread. One matches the Figma 100 reference, while values above one
    /// deliberately exaggerate it.
    static constexpr float kSplay = 1.35f;
    static constexpr float kMaxSplayShiftDevicePx = 48.0f;

    /// How far the blurred backdrop is pulled towards the region's theme
    /// surface colour before the widget paints its own tint on top. This is a
    /// mix, not a multiply: it darkens bright artwork and lifts dark artwork,
    /// so the frost keeps a predictable value on any theme instead of crushing
    /// to black on the dark ones. Higher is more matte and more opaque.
    static constexpr float kGlassSurfaceTint
        = ruwa::shared::rendering::kGlassBareOpticsMode ? 0.0f : 0.28f;
    /// Shared with the widget side; see kGlassSilhouetteInsetPx.
    static constexpr float kSilhouetteInsetLogicalPx
        = static_cast<float>(ruwa::shared::rendering::kGlassSilhouetteInsetPx);
    /// Broad, low-opacity analytic drop shadow. The falloff radius is its
    /// exponential e-folding distance; the composite viewport stops at five
    /// radii. The final 1.5 radii fade only after alpha is below one 8-bit step,
    /// so the viewport boundary stays invisible. This adds no blur pass or
    /// cached texture.
    static constexpr float kShadowFalloffLogicalPx = 14.0f;
    static constexpr float kShadowOffsetYLogicalPx = 4.0f;
    static constexpr float kShadowOpacity = 0.12f;
    static constexpr float kShadowReachInFalloffRadii = 5.0f;
    /// Room the shadow needs outside its region, in device pixels. Shared by
    /// the composite viewport and requiredMarginDevicePx() so a caller cannot
    /// be told a smaller number than the pass actually paints into.
    static int shadowPaddingDevicePx(float deviceScale);
    /// The optical shift is a conservative upper bound on the actual Snell
    /// displacement, so adding it whole leaves room for refraction, dispersion
    /// and the blur footprint without coupling this GPU path to QWidget optics.
    static constexpr int kCapturePaddingDevicePx = kBlurReachDevicePx
        + static_cast<int>(kMaxRefractionShiftDevicePx + kMaxSplayShiftDevicePx) + 2;

    /// Chain for one region: capture -> half (one LINEAR blit, which is all a
    /// blit filters correctly) -> dual-filter pyramid down to level
    /// kBlurLevels-1 and back up. The refraction is not a stage here at all:
    /// the composite bends its own sampling coordinates, at full target
    /// resolution, off the finished frost.
    ///
    /// The pyramid needs no ping-pong pair: the expanding passes read level i
    /// and write level i-1, whose downward content is dead by then, so the
    /// blurred result lands back in level 0 - the one the composite samples.
    struct RegionTarget {
        GLuint halfFbo = 0;
        GLuint halfTexture = 0;
        GLuint levelFbo[kBlurLevels] = {};
        GLuint levelTexture[kBlurLevels] = {};
        int halfWidth = 0;
        int halfHeight = 0;
        int levelWidth[kBlurLevels] = {};
        int levelHeight[kBlurLevels] = {};
    };

    struct PreparedRegion {
        QRect captureRect;
        QRect targetRect;
        QRect compositeRect;
        float cornerRadius = 0.0f;
        float opacity = 1.0f;
        float tintR = 0.0f;
        float tintG = 0.0f;
        float tintB = 0.0f;
        float tintAmount = 0.0f;
        int frostLevels = 0;
        float shadowOpacity = 0.0f;
        float refractionStrength = 1.0f;
        float sourceUvMinX = 0.0f;
        float sourceUvMinY = 0.0f;
        float sourceUvMaxX = 1.0f;
        float sourceUvMaxY = 1.0f;
        std::size_t targetIndex = 0;
    };

    bool ensureTarget(RegionTarget& target, int captureWidth, int captureHeight);
    bool createColorTarget(
        int width, int height, GLenum internalFormat, GLuint& fbo, GLuint& texture);
    void releaseTarget(RegionTarget& target);
    void drawFullscreen();

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_downsampleProgram;
    std::unique_ptr<GLShaderProgram> m_upsampleProgram;
    std::unique_ptr<GLShaderProgram> m_compositeProgram;
    GLuint m_vao = 0;
    std::vector<RegionTarget> m_targets;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H
