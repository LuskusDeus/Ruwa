// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_GLLASSOMASKRENDERER_H
#define RUWA_FEATURES_CANVAS_RENDERING_GLLASSOMASKRENDERER_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QRect>
#include <QSize>

#include <array>
#include <memory>
#include <vector>

namespace aether {

class GLShaderProgram;

/// Screen-space lasso fill coverage mask, rasterized by the hardware and
/// accumulated across frames instead of rebuilt.
///
/// The rule reproduced here is the one the commit path uses in
/// OpenGLCanvasWidget::buildLassoFillScreenMask — even-odd parity at pixel
/// centres, UNION the polygon dilated by a 0.75 px edge pad — so the preview and
/// the committed fill agree pixel for pixel. The pad is what keeps a lasso
/// thinner than a pixel from vanishing, so it is not optional.
///
/// Why it is cheap. A lasso polygon only ever grows at its tail, so the three
/// coverage terms live in three stencil bits that can each be extended in place:
///
///   bit0  even-odd parity, drawn as a triangle fan pivoted on the FIRST point.
///         GL_INVERT is an involution, so the fan is an XOR accumulator:
///         appending a point is exactly one more triangle (P0, Pn-1, Pn), with
///         nothing to undo. The chord that closes the polygon back to P0 is
///         implicit in a fan pivoted on P0, so parity needs no fix-up either.
///   bit1  the edge pad of the committed segments — append-only as well.
///   bit2  the edge pad of the closing chord. That chord MOVES whenever a point
///         is added, so it lives in its own bit and is erased and redrawn each
///         time without disturbing bits 0 and 1.
///   bit3  an "inside the canvas" gate. All accumulation is stencil-tested
///         against it, which clips the mask to the document exactly the way the
///         commit clamps its raster — and, unlike clipping the polygon
///         geometrically, without renumbering vertices and breaking the
///         append-only invariant. It depends only on the camera, so it is
///         rebuilt exactly when everything else is.
///
/// A full rebuild (camera move, viewport resize, new stroke) costs
/// O(points + area) on the hardware rasterizer; a normal frame costs only the
/// points added that frame. Nothing here scales with how long the stroke is.
class GLLassoMaskRenderer {
public:
    struct MaskRenderResult {
        GLuint texture = 0;
        /// The mask's sampling window. It is the whole viewport: keeping the
        /// texture at a fixed size is what lets the accumulation survive across
        /// frames, since the polygon's bounding box grows as the user draws.
        QRect bounds;

        bool isValid() const { return texture != 0 && bounds.isValid() && !bounds.isEmpty(); }
    };

    /// The document rectangle projected into the same screen space as the
    /// polygon, in corner order (0,0) (w,0) (w,h) (0,h). Disabled for a document
    /// with no finite bounds.
    struct CanvasGate {
        bool enabled = false;
        std::array<Vector2, 4> corners {};

        bool matches(const CanvasGate& other) const
        {
            if (enabled != other.enabled) {
                return false;
            }
            if (!enabled) {
                return true;
            }
            for (std::size_t i = 0; i < corners.size(); ++i) {
                if (corners[i].x != other.corners[i].x || corners[i].y != other.corners[i].y) {
                    return false;
                }
            }
            return true;
        }
    };

    explicit GLLassoMaskRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLLassoMaskRenderer();

    GLLassoMaskRenderer(const GLLassoMaskRenderer&) = delete;
    GLLassoMaskRenderer& operator=(const GLLassoMaskRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    /// Extends the mask to cover `screenPolygon` and returns it.
    ///
    /// `screenPolygon` is OPEN (the closing chord is implicit) and must be
    /// append-only from one call to the next for the fast path to be taken;
    /// anything else — a different first point, a shorter polygon, a moved
    /// camera (`forceRebuild`), a resized viewport, a different gate — falls
    /// back to a full rebuild, which produces the same mask.
    MaskRenderResult accumulate(const std::vector<Vector2>& screenPolygon,
        const QSize& viewportSize, const CanvasGate& gate, bool forceRebuild);

    /// Drops the accumulated stencil state so the next accumulate() rebuilds.
    /// Safe to call without a current GL context — it touches no GL object.
    void invalidateAccumulation();

    bool isInitialized() const { return m_initialized; }

private:
    struct Batch {
        GLint first = 0;
        GLsizei count = 0;

        bool empty() const { return count <= 0; }
    };

    void ensureTargets(const QSize& size);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_program;
    GLuint m_maskTexture = 0;
    GLuint m_stencilBuffer = 0;
    GLuint m_fbo = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    QSize m_maskSize;

    /// Vertex staging, kept across frames so an incremental step allocates
    /// nothing. Six floats per vertex: position, then the segment endpoints the
    /// edge-pad test needs (zero for non-capsule geometry).
    std::vector<float> m_vertexScratch;

    // Accumulation state — what the stencil currently holds.
    bool m_accumulationValid = false;
    std::size_t m_accumulatedPoints = 0;
    Vector2 m_pivot {};
    CanvasGate m_gate;

    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_GLLASSOMASKRENDERER_H
