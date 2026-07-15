// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   T I L E   R E N D E R E R
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_GLTILERENDERER_H
#define AETHER_ENGINE_OPENGL_GLTILERENDERER_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileGrid.h"
#include "features/canvas/scene/Viewport.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <array>

namespace aether {

class GLShaderProgram;

class GLTileRenderer {
public:
    explicit GLTileRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLTileRenderer();

    GLTileRenderer(const GLTileRenderer&) = delete;
    GLTileRenderer& operator=(const GLTileRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    /// Upload all dirty tiles to GPU textures.
    void uploadDirtyTiles(TileGrid& grid);

    /// Render tile textures with alpha blending.
    /// Assumes checkerboard was already drawn underneath.
    /// Canvas dimensions remain meaningful when clipping is disabled because
    /// content mirroring still uses the canvas center.
    void render(const TileGrid& grid, const Viewport& viewport, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0, float cornerRadiusCanvasPx = 0.0f,
        bool canvasContentFlipH = false, bool canvasContentFlipV = false,
        bool compositeRoundedEdgesOverViewportBackground = false,
        const Color& viewportBackgroundColor = Color::transparent(), bool clipToCanvas = true);
    uint32_t lastRenderDrawCallCount() const { return m_lastRenderDrawCalls; }

    /// Destroy GPU texture for a tile
    void destroyTileTexture(TileData& tile);

    /// Ensure a tile has a GPU texture allocated (lazy creation)
    void ensureTileTexture(TileData& tile);

    /// Upload tile CPU pixel data to its GPU texture
    void uploadTileData(TileData& tile);

    bool isInitialized() const { return m_initialized; }

private:
    bool drawTileQuad(const TileKey& key, const TileData& tile,
        const std::array<float, 16>& vpMatrix, uint32_t canvasWidth, uint32_t canvasHeight,
        bool clipToCanvas, float cornerRadiusCanvasPx,
        bool compositeRoundedEdgesOverViewportBackground, const Color& viewportBackgroundColor);

    std::array<float, 16> createTileModelMatrix(const TileKey& key) const;

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_tileProgram;

    GLuint m_emptyVAO = 0;
    uint32_t m_lastRenderDrawCalls = 0;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLTILERENDERER_H
