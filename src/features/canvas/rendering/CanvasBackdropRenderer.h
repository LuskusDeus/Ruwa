// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H
#define RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H

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
    struct RegionTarget {
        GLuint halfFbo = 0;
        GLuint halfTexture = 0;
        GLuint quarterFbo = 0;
        GLuint quarterTexture = 0;
        GLuint blurAFbo = 0;
        GLuint blurATexture = 0;
        GLuint blurBFbo = 0;
        GLuint blurBTexture = 0;
        int halfWidth = 0;
        int halfHeight = 0;
        int quarterWidth = 0;
        int quarterHeight = 0;
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
    std::unique_ptr<GLShaderProgram> m_blurProgram;
    std::unique_ptr<GLShaderProgram> m_compositeProgram;
    GLuint m_vao = 0;
    std::vector<RegionTarget> m_targets;
    bool m_initialized = false;

    static constexpr int kDownsampleScale = 8;
    static constexpr int kKernelReachDevicePx = 32;
    static constexpr int kCapturePaddingDevicePx = kKernelReachDevicePx + 2;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPRENDERER_H
