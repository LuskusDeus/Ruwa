// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   O V E R L A Y
// ==========================================================================

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMOVERLAY_H
#define RUWA_CORE_TRANSFORM_TRANSFORMOVERLAY_H

#include "features/transform/TransformState.h"
#include "shared/types/Result.h"
#include "features/canvas/scene/Viewport.h"
#include "features/canvas/overlays/OverlayVisualBase.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QtGui/qopengl.h>
#include <array>
#include <functional>
#include <vector>

namespace aether {

/// Shift-constrained move: full-viewport axis line in document/world space.
struct TransformMoveAxisGuide {
    Vector2 originWorld {};
    Vector2 axisDirWorld {};
    float opacity = 1.f;
};

/// Auto-snap guides: full-viewport canvas X/Y alignment lines.
struct TransformAutoSnapGuides {
    bool hasVertical = false;
    bool hasSecondVertical = false;
    bool hasHorizontal = false;
    bool hasSecondHorizontal = false;
    float verticalX = 0.0f;
    float secondVerticalX = 0.0f;
    float horizontalY = 0.0f;
    float secondHorizontalY = 0.0f;
    float opacity = 1.f;
};

class TransformOverlay : public OverlayVisualBase {
public:
    explicit TransformOverlay(QOpenGLFunctions_4_5_Core* gl);
    ~TransformOverlay();

    TransformOverlay(const TransformOverlay&) = delete;
    TransformOverlay& operator=(const TransformOverlay&) = delete;

    Result<void> initialize();
    void shutdown();

    /// Render the transform frame, handles, and pivot.
    /// If sceneTextureId != 0, overlay color is per-pixel inverse of scene underneath.
    /// When non-null, use *viewProjectionContent (e.g. VP × canvas mirror) instead of
    /// viewport.viewProjectionMatrix(). moveAxisGuide: optional Shift-move axis lines (invert
    /// blend); documentWorldFromScreen required for correct span when canvas is mirrored.
    void render(const TransformState& state, const Viewport& viewport, GLuint sceneTextureId = 0,
        const std::array<float, 16>* viewProjectionContent = nullptr,
        const TransformMoveAxisGuide* moveAxisGuide = nullptr, bool drawTransformChrome = true,
        const std::function<Vector2(const Vector2&)>* documentWorldFromScreen = nullptr,
        bool drawCornerRotationIcons = false,
        const TransformAutoSnapGuides* autoSnapGuides = nullptr);
    void render(const Viewport& viewport, GLuint sceneTextureId = 0,
        const std::array<float, 16>* viewProjectionContent = nullptr);

    void onTransformModeEntered();
    /// @param animated If false, hide immediately (no fade). Use for move-only transform where
    /// chrome was never shown.
    void onTransformModeExited(bool animated = true);

    bool isInitialized() const { return m_initialized; }
    bool isAnimating() const { return OverlayVisualBase::isAnimating(); }

private:
    void renderInternal(const TransformState& state, const Viewport& viewport, bool isActive,
        GLuint sceneTextureId, const std::array<float, 16>* viewProjectionContent,
        const TransformMoveAxisGuide* moveAxisGuide, bool drawTransformChrome,
        const std::function<Vector2(const Vector2&)>* documentWorldFromScreen,
        bool drawCornerRotationIcons, const TransformAutoSnapGuides* autoSnapGuides);

    void drawMoveAxisGuides(const TransformMoveAxisGuide& guide, const Viewport& viewport,
        float zoom, float r, float g, float b, float a, const std::array<float, 16>& vpMatrix,
        const std::function<Vector2(const Vector2&)>* documentWorldFromScreen);
    void drawAutoSnapGuides(const TransformAutoSnapGuides& guides, const Viewport& viewport,
        float zoom, float r, float g, float b, float a, const std::array<float, 16>& vpMatrix,
        const std::function<Vector2(const Vector2&)>* documentWorldFromScreen);
    void drawDeformMesh(const TransformState& state, float zoom, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix);
    void drawPolyline(const std::vector<Vector2>& points, float thicknessWorld, float r, float g,
        float b, float a, const std::array<float, 16>& vpMatrix);

    void drawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix);

    void drawQuad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
        float r, float g, float b, float a, const std::array<float, 16>& vpMatrix);

    void drawCapsule(const Vector2& p1, const Vector2& p2, float thicknessWorld, float r, float g,
        float b, float a, const std::array<float, 16>& vpMatrix);

    void drawRect(float cx, float cy, float halfSize, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix);

    void drawFilledRect(float cx, float cy, float halfSize, float r, float g, float b, float a,
        const std::array<float, 16>& vpMatrix);

    void drawFilledRotatedSquare(float cx, float cy, float halfWorld, float angleRad, float r,
        float g, float b, float a, const std::array<float, 16>& vpMatrix);

    void drawCircle(float cx, float cy, float radius, int segments, float r, float g, float b,
        float a, const std::array<float, 16>& vpMatrix);

    void drawRotationCornerHandles(const TransformState& state, float zoom, float anim, float r,
        float g, float b, float a, const std::array<float, 16>& vpMatrix, GLuint sceneTextureId,
        float invertViewportW, float invertViewportH);

    void drawTexturedRotatedIcon(float cx, float cy, float halfWorld, float angleRad, float r,
        float g, float b, float a, const std::array<float, 16>& vpMatrix, GLuint sceneTextureId,
        float invertViewportW, float invertViewportH);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    GLuint m_shaderProgram = 0;
    GLuint m_invertProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locMVP = -1;
    GLint m_locColor = -1;
    GLint m_locInvertMVP = -1;
    GLint m_locInvertColor = -1;
    GLint m_locInvertSceneTexture = -1;
    GLint m_locInvertViewportSize = -1;

    GLuint m_curProgram = 0;
    GLint m_curLocMVP = -1;
    GLint m_curLocColor = -1;

    bool m_initialized = false;

    bool m_hasLastState = false;
    TransformState m_lastState;
    bool m_lastDrawCornerRotationIcons = false;

    GLuint m_texProgram = 0;
    GLuint m_texInvertProgram = 0;
    GLuint m_rotationIconTexture = 0;
    bool m_rotationIconReady = false;

    GLint m_locTexMVP = -1;
    GLint m_locTexSampler = -1;
    GLint m_locTexColor = -1;

    GLint m_locTexInvMVP = -1;
    GLint m_locTexInvIcon = -1;
    GLint m_locTexInvScene = -1;
    GLint m_locTexInvViewport = -1;
    GLint m_locTexInvColor = -1;
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMOVERLAY_H
