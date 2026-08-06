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
#include "shared/rendering/GLTileTexturePool.h"
#include "features/canvas/scene/Viewport.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <array>

namespace aether {

class GLShaderProgram;
class DisplayPyramid;

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
    ///
    /// When `displayPyramid` is non-null and the camera is minifying, the frame
    /// is drawn from the pyramid's level lattice instead of the level-0 tiles:
    /// ~40 quads at any zoom, and filtering that never changes mid-stroke.
    /// Passing nullptr draws the level-0 tiles bilinearly, which is all a grid
    /// with no pyramid of its own can offer.
    void render(const TileGrid& grid, const Viewport& viewport, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0, float cornerRadiusCanvasPx = 0.0f,
        bool canvasContentFlipH = false, bool canvasContentFlipV = false,
        bool compositeRoundedEdgesOverViewportBackground = false,
        const Color& viewportBackgroundColor = Color::transparent(), bool clipToCanvas = true,
        const DisplayPyramid* displayPyramid = nullptr);
    uint32_t lastRenderDrawCallCount() const { return m_lastRenderDrawCalls; }
    /// Level the last render() actually sampled (0 = level-zero tiles), and how
    /// far it was lerped toward the next level up. Stage 0 instrumentation.
    int lastRenderLevel() const { return m_lastRenderLevel; }
    float lastRenderLevelBlend() const { return m_lastRenderLevelBlend; }

    /// Release a tile's GPU texture (recycled through the tile texture pool)
    void destroyTileTexture(TileData& tile);

    /// Ensure a tile has a GPU texture allocated (lazy creation)
    void ensureTileTexture(TileData& tile);

    /// Reclaim textures that TileData destructors could not free themselves
    /// (no GL context, wrong thread) and feed them back into the pool. Call
    /// once per frame from the GL thread.
    void flushOrphanedTextures();

    /// Upload tile CPU pixel data to its GPU texture
    void uploadTileData(TileData& tile);

    bool isInitialized() const { return m_initialized; }

private:
    /// Box filter the level-zero path applies per screen pixel. `taps` is per
    /// axis; 1 = one bilinear tap, which is what a magnified frame gets.
    struct LevelZeroFilter {
        int taps = 1;
        float stepUV = 0.0f;
    };

    bool drawTileQuad(const TileKey& key, const TileData& tile,
        const std::array<float, 16>& vpMatrix, uint32_t canvasWidth, uint32_t canvasHeight,
        bool clipToCanvas, float cornerRadiusCanvasPx,
        bool compositeRoundedEdgesOverViewportBackground, const Color& viewportBackgroundColor,
        const LevelZeroFilter& filter);

    std::array<float, 16> createTileModelMatrix(const TileKey& key) const;
    std::array<float, 16> createLevelTileModelMatrix(const TileKey& key, int level) const;

    /// World-space AABB of the visible region, in document space when the
    /// content mirror is active.
    struct VisibleWorldBounds {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
    };

    struct PyramidFrame {
        int level = 0;
        float blend = 0.0f;
    };

    /// Draw the visible region from the pyramid's level lattice. Returns the
    /// number of quads drawn.
    uint32_t renderFromPyramid(const TileGrid& grid, const DisplayPyramid& pyramid,
        const PyramidFrame& frame, const std::array<float, 16>& vpMatrix,
        const VisibleWorldBounds& bounds, uint32_t canvasWidth, uint32_t canvasHeight,
        bool clipToCanvas, float cornerRadiusCanvasPx,
        bool compositeRoundedEdgesOverViewportBackground, const Color& viewportBackgroundColor);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_tileProgram;
    // Display pass for the pyramid lattice: one level tile plus the level above
    // it, lerped by the frame's fractional level. Shares tile.vert.glsl.
    std::unique_ptr<GLShaderProgram> m_pyramidTileProgram;

    // LINEAR/LINEAR/CLAMP, bound for display draws only — tile textures carry
    // NEAREST magnification on the object itself, which is what a zoomed-in
    // frame wants and no minified frame does. Serves both the level-0 path and
    // the pyramid's two taps: with the mip chains gone there is nothing left to
    // choose between, which is the point.
    GLuint m_displaySampler = 0;

    // Recycles tile textures instead of paying glTextureStorage2D per tile.
    // Owned here because every tile texture in the document is created and
    // released through this class.
    GLTileTexturePool m_texturePool;

    GLuint m_emptyVAO = 0;
    uint32_t m_lastRenderDrawCalls = 0;
    int m_lastRenderLevel = 0;
    float m_lastRenderLevelBlend = 0.0f;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLTILERENDERER_H
