// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   F I L L   R E N D E R E R
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_GLFILLRENDERER_H
#define AETHER_ENGINE_OPENGL_GLFILLRENDERER_H

#include "shared/types/Result.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileTypes.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether {

class GLShaderProgram;
class GLTileRenderer;

struct GpuFillResult {
    using RawTileMap = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;

    std::unordered_set<TileKey, TileKeyHash> affectedTiles;
    std::unordered_set<TileKey, TileKeyHash> createdTiles;
    std::unordered_set<TileKey, TileKeyHash> removedTiles;
    RawTileMap beforeTiles;
    RawTileMap afterTiles;
    RawTileMap fillMaskTiles;
    int pixelsFilled = 0;
};

class GLFillRenderer {
public:
    explicit GLFillRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLFillRenderer();

    GLFillRenderer(const GLFillRenderer&) = delete;
    GLFillRenderer& operator=(const GLFillRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    /// GPU flood fill. Returns result with snapshots for Undo.
    /// selectionMask may be null (fill entire canvas).
    GpuFillResult fill(TileGrid& layerGrid, GLTileRenderer* tileRenderer, int seedX, int seedY,
        uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA, const TileGrid* selectionMask,
        int canvasWidth, int canvasHeight);

    /// GPU provisional fill for preview. Does not mutate the live layer.
    GpuFillResult previewFill(TileGrid& layerGrid, GLTileRenderer* tileRenderer, int seedX,
        int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
        const TileGrid* selectionMask, int canvasWidth, int canvasHeight);

    /// Pre-allocate the common preview window resources to avoid first-click hitching.
    bool prewarmPreviewResources(int canvasWidth = 0, int canvasHeight = 0);

    bool isInitialized() const { return m_initialized; }

private:
    GpuFillResult fillInternal(TileGrid& layerGrid, GLTileRenderer* tileRenderer, int seedX,
        int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
        const TileGrid* selectionMask, int canvasWidth, int canvasHeight, bool applyToLayer);
    bool ensureResources(int canvasW, int canvasH);
    void blitLayerToTexture(TileGrid& grid, GLTileRenderer* tileRenderer, int textureW,
        int textureH, int originX, int originY);
    void blitGridToTexture(const TileGrid& grid, GLTileRenderer* tileRenderer, int textureW,
        int textureH, int originX, int originY, GLuint targetTex);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_blitProgram;
    std::unique_ptr<GLShaderProgram> m_initProgram;
    std::unique_ptr<GLShaderProgram> m_expandProgram;
    std::unique_ptr<GLShaderProgram> m_prepareProgram;

    GLuint m_fbo = 0;
    GLuint m_sourceTex = 0;
    GLuint m_selectionTex = 0;
    GLuint m_visitedTex = 0;
    GLuint m_boundaryVisitedTex = 0;
    GLuint m_emptyVAO = 0;
    GLuint m_frontierBuffers[2] = { 0, 0 };
    GLuint m_counterBuffer = 0;
    GLuint m_dispatchBuffer = 0;

    int m_cachedW = 0;
    int m_cachedH = 0;
    size_t m_frontierCapacity = 0;
    int m_maxTextureSize = 0;
    size_t m_maxShaderStorageBlockBytes = 0;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLFILLRENDERER_H
