// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   E Y E D R O P P E R   C U R S O R   O V E R L A Y
// ==========================================================================

#ifndef AETHER_ENGINE_QT_EYEDROPPERCURSOROVERLAYGL_H
#define AETHER_ENGINE_QT_EYEDROPPERCURSOROVERLAYGL_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QPointer>
#include <QtGui/qopengl.h>
#include <array>
#include <vector>

class QOpenGLContext;

namespace aether {

/**
 * @brief Renders eyedropper cursor overlay with sampled and selected color swatches.
 *
 * Structure from center outward:
 * - Top head: sampled color under cursor
 * - Bottom head and pointer: currently selected color
 * - Thin outer border and middle divider: per-pixel inverted for visibility
 */
class EyedropperCursorOverlayGL {
public:
    explicit EyedropperCursorOverlayGL(QOpenGLFunctions_4_5_Core* gl);
    ~EyedropperCursorOverlayGL();

    EyedropperCursorOverlayGL(const EyedropperCursorOverlayGL&) = delete;
    EyedropperCursorOverlayGL& operator=(const EyedropperCursorOverlayGL&) = delete;

    Result<void> initialize();
    void shutdown();

    /// Render eyedropper overlay. Position in widget pixels (origin top-left).
    /// Requires sceneTextureId for magnification and inversion.
    void render(float centerX, float centerY, int viewportWidth, int viewportHeight,
        GLuint sceneTextureId, const QColor& selectedColor);

    bool isInitialized() const { return m_initialized; }

private:
    void drawTeardrop(float cx, float cy, float radius, float tipY, std::vector<float>& vertices);
    void drawSemicircle(
        float cx, float cy, float radius, bool upper, int segments, std::vector<float>& vertices);
    void drawRect(float left, float top, float right, float bottom, std::vector<float>& vertices);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    QPointer<QOpenGLContext> m_context;

    GLuint m_colorFromCenterProgram = 0;
    GLuint m_invertProgram = 0;
    GLuint m_solidColorProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locColorFromCenterMVP = -1;
    GLint m_locColorFromCenterSceneTexture = -1;
    GLint m_locColorFromCenterViewportSize = -1;
    GLint m_locColorFromCenterCenter = -1;

    GLint m_locInvertMVP = -1;
    GLint m_locInvertColor = -1;
    GLint m_locInvertSceneTexture = -1;
    GLint m_locInvertViewportSize = -1;

    GLint m_locSolidColorMVP = -1;
    GLint m_locSolidColor = -1;

    bool m_initialized = false;

    static constexpr float kHeadRadius = 29.0f;
    static constexpr float kHeadCenterOffsetY = 68.0f;
    static constexpr float kBorderThickness = 2.0f;
    static constexpr float kDividerThickness = 2.0f;
    static constexpr float kTailCircleOverlapPx = 3.0f;
};

} // namespace aether

#endif // AETHER_ENGINE_QT_EYEDROPPERCURSOROVERLAYGL_H
