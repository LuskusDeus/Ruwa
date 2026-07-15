// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   B A C K D R O P   C A P T U R E
// ==========================================================================
//   Produces a downsampled + Gaussian-blurred snapshot of the rendered scene
//   and reads it back to a CPU QImage through double-buffered async PBOs (so
//   the GL→CPU transfer never stalls the pipeline). Consumed by on-canvas
//   overlay widgets that paint their own frosted backdrop.
//   All methods require a current OpenGL context.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPCAPTURE_H
#define RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPCAPTURE_H

#include "shared/types/Result.h"

#include <QImage>
#include <QString>
#include <QOpenGLFunctions_4_5_Core>
#include <QtGui/qopengl.h>

#include <memory>

namespace aether {

class GLShaderProgram;

class CanvasBackdropCapture {
public:
    explicit CanvasBackdropCapture(QOpenGLFunctions_4_5_Core* gl);
    ~CanvasBackdropCapture();

    CanvasBackdropCapture(const CanvasBackdropCapture&) = delete;
    CanvasBackdropCapture& operator=(const CanvasBackdropCapture&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    /// Downsample+blur \a sceneTexture (device px, surfaceWidth x surfaceHeight),
    /// queue an async readback, and map the PREVIOUS frame's readback into the
    /// snapshot. Mutates GL framebuffer/viewport/texture bindings — the caller is
    /// responsible for restoring whatever state it needs afterwards.
    void capture(GLuint sceneTexture, int surfaceWidth, int surfaceHeight);

    /// Latest blurred snapshot (top-down, Format_RGBA8888), null until ready.
    const QImage& snapshot() const { return m_snapshot; }

    /// Device px per snapshot px (downsample factor).
    int scale() const { return kScale; }

private:
    bool ensureTargets(int surfaceWidth, int surfaceHeight);
    void releaseTargets();
    bool createColorTarget(int w, int h, GLuint& fbo, GLuint& tex);
    void drawFullscreen();

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_copyProgram;
    std::unique_ptr<GLShaderProgram> m_blurProgram;

    GLuint m_vao = 0;

    // Iterative halving chain (clean box reduction, no aliasing): half (1/2) ->
    // quarter (1/4) -> A (1/kScale); A/B ping-pong for separable blur.
    GLuint m_fboHalf = 0, m_texHalf = 0;
    GLuint m_fboQuarter = 0, m_texQuarter = 0;
    GLuint m_fboA = 0, m_texA = 0;
    GLuint m_fboB = 0, m_texB = 0;

    GLuint m_pbo[2] = { 0, 0 };
    int m_pboWrite = 0;
    bool m_pboHasData[2] = { false, false };

    int m_surfaceWidth = 0;
    int m_surfaceHeight = 0;
    int m_halfWidth = 0;
    int m_halfHeight = 0;
    int m_quarterWidth = 0;
    int m_quarterHeight = 0;
    int m_snapWidth = 0;
    int m_snapHeight = 0;

    QImage m_snapshot;

    bool m_initialized = false;

    static constexpr int kScale = 8; // /8 downsample (snapshot resolution)
    // Gaussian tap spacing in snapshot px. Keep at 1.0 so the 9 taps stay dense
    // (no gaps -> no stepping). Blur STRENGTH comes from the /8 box downsample,
    // not from widening tap spacing. Reach ≈ kScale * 4 * kBlurSpread device px.
    static constexpr float kBlurSpread = 1.0f;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_CANVASBACKDROPCAPTURE_H
