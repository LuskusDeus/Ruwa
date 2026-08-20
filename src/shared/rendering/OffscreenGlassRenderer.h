// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_RENDERING_OFFSCREENGLASSRENDERER_H
#define RUWA_SHARED_RENDERING_OFFSCREENGLASSRENDERER_H

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QtGui/qopengl.h>

#include <memory>
#include <vector>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFunctions_4_5_Core;

namespace aether {
class CanvasBackdropRenderer;
}

namespace ruwa::shared::rendering {

/// One glass plate, in the pixels of the backdrop image it is laid on.
struct GlassPlate {
    QRectF rect;
    qreal cornerRadius = 0.0;
    qreal opacity = 1.0;
    /// Theme surface the frost is pulled towards. Invalid leaves it untinted.
    QColor surfaceTint;
    /// Frost pyramid levels; negative takes the renderer's default. One below
    /// the default is roughly half the blur. See CanvasBackdropRegion.
    int frostLevels = -1;
    /// Multiplier on the analytic drop shadow. Zero removes it.
    qreal shadowStrength = 1.0;
    /// Multiplier on the refraction and the splay together; the lens keeps its
    /// shape and only bends less. See CanvasBackdropRegion.
    qreal refractionStrength = 1.0;
};

/// Runs the canvas glass over a plain QImage, off screen.
///
/// The optics live in CanvasBackdropRenderer, which needs nothing from the
/// canvas: it reads one framebuffer, writes another, and takes its regions in
/// logical pixels. Everything that made it canvas-only was the FBO pair coming
/// from a QOpenGLWidget. This host supplies that pair from an image instead, so
/// a raster QWidget that composes its own backdrop - WelcomeBanner paints its
/// card into a QImage before anything is laid over it - gets the SAME glass as
/// the on-canvas overlays rather than a hand-ported approximation of it.
///
/// It is a round trip: upload, render, read back. That rules it out for glass
/// over live content at frame rate, and makes it right for glass over a
/// backdrop the widget already has in memory and repaints rarely.
///
/// GUI thread only, and the context is created lazily on first use so a machine
/// without GL 4.5 simply reports unavailable and leaves the caller on its
/// fallback path.
class OffscreenGlassRenderer {
public:
    static OffscreenGlassRenderer& instance();

    OffscreenGlassRenderer(const OffscreenGlassRenderer&) = delete;
    OffscreenGlassRenderer& operator=(const OffscreenGlassRenderer&) = delete;

    /// Composites @p plates into @p backdrop, which must be a valid opaque
    /// image. @p deviceScale is how many image pixels one logical pixel
    /// occupies; the plate geometry is given in image pixels, and the optical
    /// lengths inside the shader follow this scale so the glass keeps the same
    /// apparent thickness whatever the UI is scaled to.
    ///
    /// Returns false without touching @p backdrop if the GPU path is
    /// unavailable, which is the signal to use a raster fallback.
    bool composeInPlace(QImage& backdrop, qreal deviceScale, const std::vector<GlassPlate>& plates);

    /// True once the pipeline has been brought up successfully. Does not
    /// attempt initialization; a first composeInPlace() call does that.
    bool isAvailable() const { return m_initialized; }

private:
    OffscreenGlassRenderer() = default;
    ~OffscreenGlassRenderer();

    bool ensureInitialized();
    /// Allocates the source/target pair with at least @p width x @p height.
    /// Capacity is bucketed, and the image content is anchored at the GL
    /// origin - the LOWER left - so every rect the renderer derives from
    /// surfaceHeight lands on the image and not on the unused slack above it.
    bool ensureSurfaces(int width, int height);
    void releaseSurfaces();
    void shutdown();

    QOpenGLContext* m_context = nullptr;
    QOffscreenSurface* m_surface = nullptr;
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<aether::CanvasBackdropRenderer> m_renderer;

    GLuint m_sourceFbo = 0;
    GLuint m_sourceTexture = 0;
    GLuint m_targetFbo = 0;
    GLuint m_targetTexture = 0;
    int m_capacityWidth = 0;
    int m_capacityHeight = 0;

    bool m_initialized = false;
    /// Latched after a failed bring-up so a missing GL 4.5 is diagnosed once
    /// rather than on every repaint.
    bool m_unavailable = false;
};

} // namespace ruwa::shared::rendering

#endif // RUWA_SHARED_RENDERING_OFFSCREENGLASSRENDERER_H
