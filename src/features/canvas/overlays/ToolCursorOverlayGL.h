// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T O O L   C U R S O R   O V E R L A Y
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSOROVERLAYGL_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSOROVERLAYGL_H

#include "features/canvas/overlays/GLCursorIconRenderer.h"
#include "shared/types/Result.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QPointer>
#include <QString>
#include <QtGui/qopengl.h>

#include <memory>

class QOpenGLContext;

namespace aether {

/**
 * @brief Photoshop-style GL cursor: pointer arrow plus a tool badge.
 *
 * The arrow's tip sits on the cursor position, the tool icon hangs to its
 * lower-right. Both are drawn from icon alpha in the inverted scene color, so
 * they stay readable on any artwork. Drawn in GL rather than as a system
 * cursor because Windows delivers the system cursor a frame behind the
 * position the canvas is rendered at.
 */
class ToolCursorOverlayGL {
public:
    explicit ToolCursorOverlayGL(QOpenGLFunctions_4_5_Core* gl);
    ~ToolCursorOverlayGL();

    ToolCursorOverlayGL(const ToolCursorOverlayGL&) = delete;
    ToolCursorOverlayGL& operator=(const ToolCursorOverlayGL&) = delete;

    Result<void> initialize();
    void shutdown();

    /// Render the cursor. Position in widget pixels (origin top-left).
    /// @param toolIconResource QRC path of the tool badge, e.g. ":/icons/Move".
    void render(float centerX, float centerY, int viewportWidth, int viewportHeight,
        GLuint sceneTextureId, const QString& toolIconResource);

    bool isInitialized() const { return m_initialized; }

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    QPointer<QOpenGLContext> m_context;
    std::unique_ptr<GLCursorIconRenderer> m_iconRenderer;
    bool m_initialized = false;

    static constexpr const char* kPointerResourcePath = ":/icons/CursorPointer";
    /// A very small arrow cannot hold a smooth diagonal no matter how it is
    /// filtered: one step of the edge is a whole pixel, so the size itself is
    /// what keeps the stair-stepping down.
    static constexpr float kPointerSizePx = 14.0f;
    /// The arrow's tip, in normalized icon coordinates (0,0 = top-left).
    static constexpr float kPointerHotspotU = 0.09f;
    static constexpr float kPointerHotspotV = 0.09f;
    /// No contrast curve at all (low >= high): the arrow keeps the exact coverage
    /// the downscale produced, which is the most antialiasing available.
    static constexpr float kPointerEdgeLow = 1.0f;
    static constexpr float kPointerEdgeHigh = 0.0f;

    static constexpr float kToolIconSizePx = 18.0f;
    /// Top-left of the badge, relative to the cursor position. Kept proportional
    /// to the arrow so the two overlap by the same amount as before.
    static constexpr float kToolIconOffsetX = 10.0f;
    static constexpr float kToolIconOffsetY = 10.0f;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSOROVERLAYGL_H
