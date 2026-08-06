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

    /// Start one top-level viewport/export frame. During an interactive content
    /// mutation stale mip chains are not rebuilt; the frame falls back to
    /// fresh level zero for the entire visible set instead.
    void beginDisplayMipFrame(bool deferRegeneration);
    void beginUnrestrictedDisplayMipFrame();
    bool hasPendingVisibleDisplayMipmaps() const { return m_visibleDisplayMipmapsPending; }

    /// Upload all dirty tiles to GPU textures.
    void uploadDirtyTiles(TileGrid& grid);

    /// Render tile textures with alpha blending.
    /// Assumes checkerboard was already drawn underneath.
    /// Canvas dimensions remain meaningful when clipping is disabled because
    /// content mirroring still uses the canvas center.
    ///
    /// When `displayPyramid` is non-null and the camera is minifying, the frame
    /// is drawn from the pyramid's level lattice instead of the level-0 tiles:
    /// ~40 quads at any zoom, filtering that never changes mid-stroke, and no
    /// per-tile mip chains. Passing nullptr keeps the legacy path exactly.
    void render(const TileGrid& grid, const Viewport& viewport, uint32_t canvasWidth = 0,
        uint32_t canvasHeight = 0, float cornerRadiusCanvasPx = 0.0f,
        bool canvasContentFlipH = false, bool canvasContentFlipV = false,
        bool compositeRoundedEdgesOverViewportBackground = false,
        const Color& viewportBackgroundColor = Color::transparent(), bool clipToCanvas = true,
        bool useDisplayMipmaps = true, const DisplayPyramid* displayPyramid = nullptr);
    uint32_t lastRenderDrawCallCount() const { return m_lastRenderDrawCalls; }
    /// Level the last render() actually sampled (0 = level-zero tiles), and how
    /// far it was lerped toward the next level up. Stage 0 instrumentation.
    int lastRenderLevel() const { return m_lastRenderLevel; }
    float lastRenderLevelBlend() const { return m_lastRenderLevelBlend; }

    /// Release a tile's GPU texture (recycled through the tile texture pool)
    void destroyTileTexture(TileData& tile);

    /// Ensure a tile has a GPU texture allocated (lazy creation)
    void ensureTileTexture(TileData& tile);

    /// Ensure a final composition-cache tile has storage for its display mip chain.
    void ensureDisplayTileTexture(TileData& tile);

    /// Reclaim textures that TileData destructors could not free themselves
    /// (no GL context, wrong thread) and feed them back into the pool. Call
    /// once per frame from the GL thread.
    void flushOrphanedTextures();

    /// Upload tile CPU pixel data to its GPU texture
    void uploadTileData(TileData& tile);

    bool isInitialized() const { return m_initialized; }

private:
    void ensureTileTextureStorage(TileData& tile, const TextureParams& params);

    bool drawTileQuad(const TileKey& key, const TileData& tile,
        const std::array<float, 16>& vpMatrix, uint32_t canvasWidth, uint32_t canvasHeight,
        bool clipToCanvas, float cornerRadiusCanvasPx,
        bool compositeRoundedEdgesOverViewportBackground, const Color& viewportBackgroundColor);

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
    // LINEAR/LINEAR/CLAMP for both pyramid taps. Level-0 tiles carry NEAREST
    // magnification and a (now unused) mip chain on the texture object itself,
    // so the sampler object has to override them when they feed this path.
    GLuint m_pyramidSampler = 0;

    // Overrides the texture object's sampling state only for display draws.
    // One of the two is picked per frame for every visible tile at once: the
    // mip sampler when the whole visible set has valid mip chains, the
    // level-zero sampler otherwise. Mixing them per tile is what makes the
    // dirty tiles of a live stroke visible as a grid, so the choice is never
    // made per tile — see render().
    GLuint m_displaySampler = 0;
    GLuint m_levelZeroLinearSampler = 0;

    static constexpr uint32_t kDisplayMipRegenerationBudget = 2;
    uint32_t m_displayMipRegenerationBudget = kDisplayMipRegenerationBudget;
    uint32_t m_displayMipRegenerationsThisFrame = 0;
    bool m_deferDisplayMipRegeneration = false;
    bool m_visibleDisplayMipmapsPending = false;

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
