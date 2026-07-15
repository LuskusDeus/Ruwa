// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_RENDERING_OPENGL_CANVASRESIZEOVERLAYGL_H
#define RUWA_CORE_RENDERING_OPENGL_CANVASRESIZEOVERLAYGL_H

#include "shared/types/Result.h"
#include "features/canvas/overlays/OverlayVisualBase.h"
#include "features/canvas/scene/Viewport.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QRectF>
#include <QtGui/qopengl.h>

#include <functional>

namespace aether {

class CanvasResizeOverlayGL : public OverlayVisualBase {
public:
    explicit CanvasResizeOverlayGL(QOpenGLFunctions_4_5_Core* gl);
    ~CanvasResizeOverlayGL();

    Result<void> initialize();
    void shutdown();

    void setSelectionRect(const QRectF& worldRect)
    {
        m_selectionWorldRect = worldRect.normalized();
    }
    void setSelecting(bool selecting) { m_selecting = selecting; }

    void onModeEntered();
    void onModeExited();

    /// When non-null, map document/world points to screen (e.g. with canvas content mirror).
    void render(const Viewport& viewport, GLuint sceneTextureId, int surfaceWidth,
        int surfaceHeight, const std::function<Vector2(Vector2)>* documentWorldToScreen = nullptr);
    bool isInitialized() const { return m_initialized; }

private:
    void drawRectScreen(
        float x, float y, float w, float h, float alpha, int surfaceW, int surfaceH);
    void drawLineScreen(float x1, float y1, float x2, float y2, float thicknessPx, float alpha,
        int surfaceW, int surfaceH);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    GLuint m_colorProgram = 0;
    GLuint m_invertProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locColorViewport = -1;
    GLint m_locColorAlpha = -1;
    GLint m_locInvertViewport = -1;
    GLint m_locInvertSceneTexture = -1;
    GLint m_locInvertAlpha = -1;

    bool m_initialized = false;
    QRectF m_selectionWorldRect;
    bool m_selecting = false;
};

} // namespace aether

#endif // RUWA_CORE_RENDERING_OPENGL_CANVASRESIZEOVERLAYGL_H
