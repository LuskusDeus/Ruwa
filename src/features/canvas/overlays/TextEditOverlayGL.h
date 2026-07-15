// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_RENDERING_TEXTEDITOVERLAYGL_H
#define RUWA_CORE_RENDERING_TEXTEDITOVERLAYGL_H

#include "features/canvas/scene/Viewport.h"
#include "features/transform/TransformState.h"
#include "shared/types/Result.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QtGui/qopengl.h>

#include <array>
#include <vector>

namespace aether {

struct TextEditOverlayState {
    bool active = false;
    TransformState transform;
    Rect sourceBounds;
    std::vector<Rect> selectionSourceRects;
    Rect caretSourceRect;
    bool caretVisible = false;
};

class TextEditOverlayGL {
public:
    explicit TextEditOverlayGL(QOpenGLFunctions_4_5_Core* gl);
    ~TextEditOverlayGL();

    Result<void> initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    void setState(TextEditOverlayState state);
    bool isActive() const { return m_state.active; }

    void render(const Viewport& viewport, GLuint sceneTextureId, int surfaceWidth,
        int surfaceHeight, const std::array<float, 16>* viewProjectionContent = nullptr);

private:
    void drawSourceRect(const Rect& rect, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix, bool invert);
    void drawCaretScreenRect(const Rect& rect, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix, bool invert);
    void drawWorldQuad(const std::array<Vector2, 4>& points, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix, bool invert);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_colorProgram = 0;
    GLuint m_invertProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLint m_locColorMvp = -1;
    GLint m_locColor = -1;
    GLint m_locInvertMvp = -1;
    GLint m_locInvertColor = -1;
    GLint m_locInvertSceneTexture = -1;
    GLint m_locInvertViewportSize = -1;
    bool m_initialized = false;
    TextEditOverlayState m_state;
    int m_surfaceWidth = 1;
    int m_surfaceHeight = 1;
    GLuint m_sceneTextureId = 0;
};

} // namespace aether

#endif // RUWA_CORE_RENDERING_TEXTEDITOVERLAYGL_H
