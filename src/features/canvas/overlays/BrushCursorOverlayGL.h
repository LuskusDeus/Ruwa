// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   B R U S H   C U R S O R   O V E R L A Y   ( G L )
// ==========================================================================

#ifndef AETHER_ENGINE_QT_BRUSHCURSOROVERLAYGL_H
#define AETHER_ENGINE_QT_BRUSHCURSOROVERLAYGL_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QPointer>
#include <QtGui/qopengl.h>
#include <array>
#include <vector>

class QOpenGLContext;

namespace aether {

/**
 * @brief Renders brush cursor circle with per-pixel color inversion.
 * Draws a ring (circle outline) that inverts the scene underneath for visibility.
 */
class BrushCursorOverlayGL {
public:
    explicit BrushCursorOverlayGL(QOpenGLFunctions_4_5_Core* gl);
    ~BrushCursorOverlayGL();

    BrushCursorOverlayGL(const BrushCursorOverlayGL&) = delete;
    BrushCursorOverlayGL& operator=(const BrushCursorOverlayGL&) = delete;

    Result<void> initialize();
    void shutdown();

    /// Render brush cursor. Position in widget pixels (origin top-left).
    /// Radius in screen pixels. Requires sceneTextureId for inversion.
    void render(float centerX, float centerY, float radiusPx, int viewportWidth, int viewportHeight,
        GLuint sceneTextureId, float rotationRadians = 0.0f);

    /// Set custom stamp contours (normalized to [-1,1], centered around the
    /// brush center). One entry per connected blob of the dab; pass an empty
    /// list to fall back to a plain circle.
    void setStampContours(const std::vector<std::vector<Vector2>>& contours);

    bool isInitialized() const { return m_initialized; }

private:
    void drawCircle(float cx, float cy, float radius, int segments,
        const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH);
    void drawRing(float cx, float cy, float outerRadius, float innerRadius, int segments,
        const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH);
    void drawPolygonRing(float cx, float cy, float outerScale, float innerScale,
        const std::vector<Vector2>& contour, float rotationCos, float rotationSin,
        const std::array<float, 16>& mvp, GLuint sceneTextureId, float vpW, float vpH);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    QPointer<QOpenGLContext> m_context;

    GLuint m_invertProgram = 0;
    GLuint m_passthroughProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locInvertMVP = -1;
    GLint m_locInvertColor = -1;
    GLint m_locInvertSceneTexture = -1;
    GLint m_locInvertViewportSize = -1;
    GLint m_locPassthroughMVP = -1;
    GLint m_locPassthroughSceneTexture = -1;
    GLint m_locPassthroughViewportSize = -1;

    bool m_initialized = false;

    std::vector<std::vector<Vector2>> m_stampContours;
    static constexpr float kStrokeWidth = 2.0f;
};

} // namespace aether

#endif // AETHER_ENGINE_QT_BRUSHCURSOROVERLAYGL_H
