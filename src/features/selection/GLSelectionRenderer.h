// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   S E L E C T I O N   R E N D E R E R
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_GLSELECTIONRENDERER_H
#define AETHER_ENGINE_OPENGL_GLSELECTIONRENDERER_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "shared/tiles/TileGrid.h"
#include "features/selection/LassoSelectionManager.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <vector>

namespace aether {

class GLShaderProgram;
class GLTileRenderer;

class GLSelectionRenderer {
public:
    explicit GLSelectionRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLSelectionRenderer();

    GLSelectionRenderer(const GLSelectionRenderer&) = delete;
    GLSelectionRenderer& operator=(const GLSelectionRenderer&) = delete;

    Result<void> initialize();
    void shutdown();

    bool buildJob(const std::vector<Vector2>& polygon, std::vector<Vector2>& outTriVerts,
        std::vector<TileKey>& outTiles);

    size_t applyPolygonBatch(TileGrid& maskGrid, GLTileRenderer* tileRenderer,
        const std::vector<Vector2>& triVerts, const std::vector<TileKey>& tiles, size_t startIndex,
        size_t maxTiles, LassoSelectionMode mode, uint8_t strength,
        std::vector<TileKey>& outProcessed);

    GLsync startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys);
    bool isReadbackComplete(GLsync fence);
    void finishReadback(GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys);
    void deleteFence(GLsync fence);

    bool isInitialized() const { return m_initialized; }

private:
    std::vector<Vector2> triangulate(const std::vector<Vector2>& polygon);
    float polygonArea(const std::vector<Vector2>& polygon) const;

    void renderPolygonToTexture(GLuint targetTex, const std::vector<Vector2>& triVerts,
        const Vector2& tileOrigin, uint8_t strength);

    void subtractPolygonFromTexture(GLuint destTex, const std::vector<Vector2>& triVerts,
        const Vector2& tileOrigin, uint8_t strength);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_fillProgram;
    std::unique_ptr<GLShaderProgram> m_subtractProgram;

    GLuint m_fillVAO = 0;
    GLuint m_fillVBO = 0;
    GLuint m_fbo = 0;
    GLuint m_tempTexA = 0;
    GLuint m_tempTexB = 0;

    GLuint m_emptyVAO = 0;

    GLuint m_pbo = 0;
    size_t m_pboSize = 0;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLSELECTIONRENDERER_H
