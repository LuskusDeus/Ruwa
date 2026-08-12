// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   C U R S O R   I C O N   R E N D E R E R
// ==========================================================================
// Draws tool icons as GL cursors: only the icon's alpha is used as a shape
// mask, filled with the inverse of the scene underneath, so the same artwork
// reads on any background.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_GLCURSORICONRENDERER_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_GLCURSORICONRENDERER_H

#include "shared/types/Result.h"

#include <QHash>
#include <QOpenGLFunctions_4_5_Core>
#include <QString>
#include <QtGui/qopengl.h>

#include <array>

namespace aether {

/**
 * @brief Shared icon-mask cursor renderer used by the GL cursor overlays.
 *
 * The scene texture must be bound to texture unit 0 by the caller; unit 1 is
 * used for the icon mask. Masks are rasterized on the CPU at their final pixel
 * size and cached per (resource, size).
 */
class GLCursorIconRenderer {
public:
    explicit GLCursorIconRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLCursorIconRenderer();

    GLCursorIconRenderer(const GLCursorIconRenderer&) = delete;
    GLCursorIconRenderer& operator=(const GLCursorIconRenderer&) = delete;

    Result<void> initialize();
    void shutdown();
    bool isInitialized() const { return m_program != 0; }

    /// Default contrast window applied to the mask's coverage ramp. Widen it for
    /// small glyphs, whose edges have little antialiasing to spare.
    static constexpr float kDefaultEdgeLow = 0.35f;
    static constexpr float kDefaultEdgeHigh = 0.65f;

    /// Draws the icon with its top-left corner at (left, top), in widget pixels.
    /// Snaps to whole pixels so the pre-rasterized mask is not resampled again.
    void draw(const QString& resourcePath, float sizePx, float left, float top,
        const std::array<float, 16>& mvp, float viewportW, float viewportH, float alpha = 1.0f,
        float edgeLow = kDefaultEdgeLow, float edgeHigh = kDefaultEdgeHigh);

    /// Same, but anchored by a hotspot given in normalized icon coordinates
    /// (0,0 = top-left of the artwork), which is placed on (cursorX, cursorY).
    void drawAtHotspot(const QString& resourcePath, float sizePx, float hotspotU, float hotspotV,
        float cursorX, float cursorY, const std::array<float, 16>& mvp, float viewportW,
        float viewportH, float alpha = 1.0f, float edgeLow = kDefaultEdgeLow,
        float edgeHigh = kDefaultEdgeHigh);

private:
    /// Returns the cached mask texture for the resource, loading it on first use.
    /// Zero means the icon could not be loaded; the failure is cached too.
    GLuint maskTexture(const QString& resourcePath, int sizePx);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locMVP = -1;
    GLint m_locSceneTexture = -1;
    GLint m_locMaskTexture = -1;
    GLint m_locViewportSize = -1;
    GLint m_locAlpha = -1;
    GLint m_locMaskEdge = -1;

    QHash<QString, GLuint> m_maskCache; ///< Key: "<resource>@<size>". 0 = load failed.
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_GLCURSORICONRENDERER_H
