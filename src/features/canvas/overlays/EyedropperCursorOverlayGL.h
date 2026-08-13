// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   E Y E D R O P P E R   C U R S O R   O V E R L A Y
// ==========================================================================

#ifndef AETHER_ENGINE_QT_EYEDROPPERCURSOROVERLAYGL_H
#define AETHER_ENGINE_QT_EYEDROPPERCURSOROVERLAYGL_H

#include "features/canvas/overlays/CursorOverlayState.h"
#include "features/canvas/overlays/GLCursorIconRenderer.h"
#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QPointer>
#include <QtGui/qopengl.h>
#include <array>
#include <memory>
#include <vector>

class QOpenGLContext;

namespace aether {

/**
 * @brief Renders eyedropper cursor overlay as a ring around the cursor.
 *
 * The ring is centered on the cursor itself, so the pointer stays visible in
 * the hole:
 * - Upper half: sampled color under cursor
 * - Lower half: currently selected color
 * - Thin outline (both rims and the seam between halves): per-pixel inverted
 *   for visibility on any background
 * - Center: the tool icon itself, drawn from the icon's alpha in the same
 *   inverted color. The system cursor is hidden, because Windows delivers it
 *   a frame behind the position this overlay is drawn at.
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

    /// Surface-pixel bounds of the whole ring — and therefore of every scene
    /// texel render() samples. See CursorCaptureRect.
    static CursorCaptureRect captureRect(float centerX, float centerY);

private:
    /// Builds a triangle strip for an annulus sector. Angles in degrees, measured
    /// in widget space (y grows downward), so 180..360 is the upper half.
    static void buildAnnulusSector(float cx, float cy, float innerRadius, float outerRadius,
        float startDeg, float endDeg, int segments, std::vector<float>& vertices);

    /// Draws the icon, anchored so its hotspot sits exactly on the cursor.
    void drawIcon(float centerX, float centerY, const std::array<float, 16>& mvp, float viewportW,
        float viewportH);

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

    std::unique_ptr<GLCursorIconRenderer> m_iconRenderer;

    bool m_initialized = false;

    static constexpr float kInnerRadius = 66.0f; ///< Hole around the cursor icon.
    static constexpr float kOuterRadius = 81.0f; ///< Band width stays 15px.
    static constexpr float kBorderThickness = 2.0f; ///< Inverted outline width.
    static constexpr int kArcSegments = 128;

    // Cursor icon. The source PNG is a black glyph; only its alpha is used, so the
    // shape can be painted in the same inverted color as the ring outline.
    static constexpr const char* kIconResourcePath = ":/icons/Eyedropper";
    static constexpr float kIconSizePx = 18.0f;
    /// Hotspot in normalized icon coordinates (0,0 = top-left). The eyedropper's
    /// tip sits in the lower-left corner of its glyph; other tools differ.
    static constexpr float kIconHotspotU = 0.075f;
    static constexpr float kIconHotspotV = 0.955f;
};

} // namespace aether

#endif // AETHER_ENGINE_QT_EYEDROPPERCURSOROVERLAYGL_H
