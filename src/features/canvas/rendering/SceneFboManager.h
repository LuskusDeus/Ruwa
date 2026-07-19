// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S C E N E   F B O   M A N A G E R
// ==========================================================================
// Manages the offscreen FBO used for scene rendering when overlays need
// the scene texture (e.g. brush/eyedropper cursor inversion).
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_RENDERING_SCENEFBOMANAGER_H
#define RUWA_FEATURES_CANVAS_RENDERING_SCENEFBOMANAGER_H

#include <QtGui/qopengl.h>
#include <QString>

class QOpenGLFunctions_4_5_Core;
class QOpenGLContext;

namespace aether {

/**
 * @brief Manages the scene offscreen framebuffer and texture.
 *
 * All methods must be called with an active OpenGL context.
 * GL functions are provided via \a gl (e.g. QOpenGLWidget inheriting QOpenGLFunctions_4_5_Core).
 */
class SceneFboManager {
public:
    SceneFboManager();
    ~SceneFboManager();

    SceneFboManager(const SceneFboManager&) = delete;
    SceneFboManager& operator=(const SceneFboManager&) = delete;
    SceneFboManager(SceneFboManager&&) = delete;
    SceneFboManager& operator=(SceneFboManager&&) = delete;

    /// Ensure FBO exists with given dimensions. Recreates if size changed.
    /// \a gl Must be non-null, initialized, and context must be current.
    void ensureSceneFbo(QOpenGLFunctions_4_5_Core* gl, int w, int h);

    /// Release FBO and texture. Safe to call when already released.
    /// \a gl Must be non-null when context is current.
    void releaseSceneFbo(QOpenGLFunctions_4_5_Core* gl);

    /// FBO id for binding (0 if not created).
    GLuint sceneFbo() const { return m_sceneFbo; }

    /// Texture id for overlay sampling (0 if not created).
    GLuint sceneTexture() const { return m_sceneTexture; }

    /// Blit from scene FBO to the default framebuffer.
    void blitToDefaultFbo(QOpenGLFunctions_4_5_Core* gl, GLint defaultFbo, int w, int h) const;

    /// Copy a small GL-coordinate region from the default framebuffer into the
    /// same region of the scene texture. Used by local sampling overlays that
    /// do not justify a full-surface offscreen render.
    void copyRegionFromDefaultFbo(QOpenGLFunctions_4_5_Core* gl, GLint defaultFbo, int x, int y,
        int width, int height) const;

private:
    GLuint m_sceneFbo = 0;
    GLuint m_sceneTexture = 0;
    int m_sceneFboWidth = 0;
    int m_sceneFboHeight = 0;
    QOpenGLContext* m_context = nullptr;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_SCENEFBOMANAGER_H
