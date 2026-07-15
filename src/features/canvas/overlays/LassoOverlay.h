// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   O V E R L A Y
// ==========================================================================

#ifndef RUWA_CORE_SELECTION_LASSOOVERLAY_H
#define RUWA_CORE_SELECTION_LASSOOVERLAY_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "features/canvas/scene/Viewport.h"
#include "features/selection/LassoSelectionManager.h"

#include <QOpenGLFunctions_4_5_Core>

#include <array>
#include <chrono>
#include <vector>

namespace aether {

class LassoOverlay {
public:
    explicit LassoOverlay(QOpenGLFunctions_4_5_Core* gl);
    ~LassoOverlay();

    LassoOverlay(const LassoOverlay&) = delete;
    LassoOverlay& operator=(const LassoOverlay&) = delete;

    Result<void> initialize();
    void shutdown();

    void render(const Viewport& viewport, const std::vector<Vector2>& activePath, bool activeClosed,
        const std::vector<LassoEdgeSegment>& edges, float edgesAlpha = 0.9f,
        GLuint addPathMaskTexture = 0, float maskAtlasOriginX = 0.0f, float maskAtlasOriginY = 0.0f,
        float maskAtlasWidth = 0.0f, float maskAtlasHeight = 0.0f, float pathAlphaInsideMask = 0.2f,
        float pathAlphaOutsideMask = 1.0f,
        const std::array<float, 16>* viewProjectionContent = nullptr);

    bool isInitialized() const { return m_initialized; }
    bool isAnimating() const { return m_animating; }

private:
    // Batch building — appends quads to m_batchVertices
    void batchPath(const std::vector<Vector2>& points, bool closed, float zoom, float timeSec,
        float baseAlpha, std::vector<float>* outVertices = nullptr);

    void batchSegments(const std::vector<LassoEdgeSegment>& edges, float zoom, float timeSec,
        float baseAlpha, float viewMinX, float viewMinY, float viewMaxX, float viewMaxY);

    void appendQuad(
        const Vector2& p0, const Vector2& p1, float thickness, float r, float g, float b, float a);
    void appendQuadTo(const Vector2& p0, const Vector2& p1, float thickness, float r, float g,
        float b, float a, std::vector<float>& target);

    // Upload batch and issue single draw call
    void flushBatch(const std::array<float, 16>& vpMatrix);
    void flushBatchWithMask(const std::array<float, 16>& vpMatrix, GLuint maskTexture,
        float maskOriginX, float maskOriginY, float maskWidth, float maskHeight, float alphaInside,
        float alphaOutside);

    float elapsedSeconds() const;

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    GLuint m_shaderProgram = 0;
    GLuint m_shaderProgramWithMask = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locMVP = -1;
    GLint m_locMaskMVP = -1;
    GLint m_locMaskTex = -1;
    GLint m_locMaskOrigin = -1;
    GLint m_locMaskSize = -1;
    GLint m_locAlphaInside = -1;
    GLint m_locAlphaOutside = -1;

    bool m_initialized = false;
    bool m_animating = false;

    std::chrono::steady_clock::time_point m_startTime;

    // CPU-side vertex batch: interleaved [x, y, r, g, b, a] per vertex
    std::vector<float> m_batchVertices;
    std::vector<float> m_pathBatchVertices;
};

} // namespace aether

#endif // RUWA_CORE_SELECTION_LASSOOVERLAY_H
