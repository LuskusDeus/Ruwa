// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H
#define RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H

#include "shared/rendering/CanvasBackdropSource.h"
#include "shared/types/Result.h"

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

private:
    /// Chain for one region: capture -> half (kDownsampleScale/2) -> refract,
    /// still at half -> blur (kDownsampleScale). Each blit halves at most,
    /// which is all a LINEAR blit filters correctly. The refracted half level
    /// doubles as the unblurred tap the composite uses along the rim.
    struct RegionTarget {
        GLuint halfFbo = 0;
        GLuint halfTexture = 0;
        GLuint refractFbo = 0;
        GLuint refractTexture = 0;
        GLuint blurAFbo = 0;
        GLuint blurATexture = 0;
        GLuint blurBFbo = 0;
        GLuint blurBTexture = 0;
        int halfWidth = 0;
        int halfHeight = 0;
        int blurWidth = 0;
        int blurHeight = 0;
    };

    struct PreparedRegion {
        QRect captureRect;
        QRect targetRect;
        float cornerRadius = 0.0f;
        float opacity = 1.0f;
        float sourceUvMinX = 0.0f;
        float sourceUvMinY = 0.0f;
        float sourceUvMaxX = 1.0f;
        float sourceUvMaxY = 1.0f;
        std::size_t targetIndex = 0;
    };

    bool ensureTarget(RegionTarget& target, int captureWidth, int captureHeight);
    bool createColorTarget(int width, int height, GLuint& fbo, GLuint& texture);
    void releaseTarget(RegionTarget& target);
    void drawFullscreen();

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_refractProgram;
    std::unique_ptr<GLShaderProgram> m_blurProgram;
    std::unique_ptr<GLShaderProgram> m_compositeProgram;
    GLuint m_vao = 0;
    std::vector<RegionTarget> m_targets;
    bool m_initialized = false;

    /// Two blits reach this, so it must stay at or below 4 or the reduction
    /// would drop more than half per step and alias.
    static constexpr int kDownsampleScale = 4;
    static_assert(kDownsampleScale <= 4, "reduction chain halves at most per blit");
    static constexpr int kKernelReachDevicePx = 10;

    /// Brightness multiplier applied to the blurred backdrop before the widget
    /// paints its theme tint on top. Lower is a darker, more matte glass.
    static constexpr float kGlassDarken = 0.78f;
    /// Shared with the widget side; see kGlassSilhouetteInsetPx.
    static constexpr float kSilhouetteInsetLogicalPx
        = static_cast<float>(ruwa::shared::rendering::kGlassSilhouetteInsetPx);
    /// Width of the refracting bevel band along the rim, in logical pixels.
    static constexpr float kRefractionWidthLogicalPx = 22.0f;
    /// Peak outward displacement of the refracted sample, in logical pixels.
    /// This is what makes a straight edge read as glass: it has to reach far
    /// enough outside the panel to visibly squeeze the scene into the bevel.
    static constexpr float kRefractionShiftLogicalPx = 28.0f;
    /// Ceiling for the displacement in device pixels; the capture padding is
    /// sized from it, so the bent sample always lands on real captured source.
    static constexpr int kMaxRefractionShiftDevicePx = 44;

    /// The bent sample reaches kMaxRefractionShiftDevicePx outside the widget
    /// rect (times the ~1.12 peak of the bevel profile), and the blur kernel
    /// then reaches further still from there.
    static constexpr int kCapturePaddingDevicePx
        = kKernelReachDevicePx + (kMaxRefractionShiftDevicePx * 6) / 5 + 2;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H
