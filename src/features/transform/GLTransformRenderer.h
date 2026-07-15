// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   T R A N S F O R M   R E N D E R E R
// ==========================================================================
//
//   GPU-based transform renderer using a source atlas texture.
//   Packs source tiles into a contiguous atlas matching world layout,
//   then renders transformed tiles via inverse-transform shader with
//   hardware bilinear interpolation.
//
//   Used for:
//   - Real-time transform preview (via compositor integration)
//   - Fast GPU apply on confirm (replaces CPU TransformApplicator)
//

#ifndef AETHER_ENGINE_OPENGL_GLTRANSFORMRENDERER_H
#define AETHER_ENGINE_OPENGL_GLTRANSFORMRENDERER_H

#include "shared/types/Result.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileGrid.h"
#include "features/transform/TransformState.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace aether {

class GLShaderProgram;
class GLTileRenderer;

class GLTransformRenderer {
public:
    explicit GLTransformRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLTransformRenderer();

    GLTransformRenderer(const GLTransformRenderer&) = delete;
    GLTransformRenderer& operator=(const GLTransformRenderer&) = delete;

    Result<void> initialize();
    void shutdown();

    // ---- Atlas management ----

    /// Build a contiguous atlas texture from source tile grid.
    /// Tiles are laid out matching their world positions for correct
    /// bilinear interpolation across tile boundaries.
    /// All tile GPU textures must be up-to-date before calling this.
    /// useNearestFilter: when true, use GL_NEAREST (for binary masks to avoid
    /// semi-transparent edges from interpolation). Default false for content.
    void buildSourceAtlas(
        const TileGrid& srcGrid, GLTileRenderer* tileRenderer, bool useNearestFilter = false);

    /// Build a contiguous atlas texture from selection mask grid.
    /// The mask atlas is sampled in shader to limit transform to selection.
    void buildMaskAtlas(const TileGrid& maskGrid, GLTileRenderer* tileRenderer);

    /// Destroy atlas textures (call on cancel/exit transform)
    void destroySourceAtlas();

    /// Check if atlas is available
    bool hasAtlas() const { return m_atlasTexture != 0; }
    GLuint atlasTexture() const { return m_atlasTexture; }
    int32_t atlasMinTX() const { return m_atlasMinTX; }
    int32_t atlasMinTY() const { return m_atlasMinTY; }
    int atlasWidth() const { return m_atlasWidth; }
    int atlasHeight() const { return m_atlasHeight; }

    /// Mask atlas for selection overlay (built by buildMaskAtlas).
    /// World coords: (minTX*TILE_SIZE, minTY*TILE_SIZE) to (minTX*TILE_SIZE+width,
    /// minTY*TILE_SIZE+height).
    GLuint maskAtlasTexture() const { return m_maskAtlasTexture; }
    int32_t maskAtlasMinTX() const { return m_maskAtlasMinTX; }
    int32_t maskAtlasMinTY() const { return m_maskAtlasMinTY; }
    int maskAtlasWidth() const { return m_maskAtlasWidth; }
    int maskAtlasHeight() const { return m_maskAtlasHeight; }

    // ---- Preview rendering (single tile, for compositor) ----

    /// Render the transformed content at the given destination tile position.
    /// Returns a temporary texture ID containing the result (valid until
    /// the next call to renderTransformedTile).
    /// Returns 0 if no content at this position.
    GLuint renderTransformedTile(
        const TileKey& destKey, const TransformState& state, bool preserveMaskedSource = false);

    // ---- GPU Apply (all destination tiles) ----

    /// Apply the transform: render all destination tiles and write into destGrid.
    /// Returns the set of destination tile keys that have content.
    /// destGrid is cleared and rebuilt. Tile textures are created/written.
    std::unordered_set<TileKey, TileKeyHash> applyGPU(const TransformState& state,
        TileGrid& destGrid, GLTileRenderer* tileRenderer, bool preserveMaskedSource = false);

    // ---- Async PBO readback (same pattern as GLBrushRenderer) ----

    GLsync startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys);
    bool isReadbackComplete(GLsync fence);
    void finishReadback(GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys);
    void deleteFence(GLsync fence);

    bool isInitialized() const { return m_initialized; }

private:
    struct DeformRasterVertex {
        float destX = 0.0f;
        float destY = 0.0f;
        float srcX = 0.0f;
        float srcY = 0.0f;
    };

    struct DeformTileBatch {
        GLintptr indexOffset = 0;
        GLsizei indexCount = 0;
    };

    /// Compute the 3x3 inverse affine transform matrix (column-major, for shader)
    static std::array<float, 9> computeInverseMatrix(const TransformState& state);
    Rect computeRenderBounds(const TransformState& state, bool useMask) const;
    bool tileIntersectsRenderBounds(
        const TileKey& destKey, const TransformState& state, bool useMask) const;
    void bindSharedUniforms(GLShaderProgram* program, bool useMask, bool preserveMaskedSource,
        float tileOriginX, float tileOriginY);
    bool ensureForwardDeformMesh(const TransformState& state);
    bool drawForwardDeformTile(const TileKey& destKey, bool useMask, bool preserveMaskedSource);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    std::unique_ptr<GLShaderProgram> m_transformProgram;
    std::unique_ptr<GLShaderProgram> m_quadTransformProgram;
    std::unique_ptr<GLShaderProgram> m_forwardDeformProgram;
    std::unique_ptr<GLShaderProgram> m_maskBaseProgram;

    GLuint m_fbo = 0;
    GLuint m_emptyVAO = 0;
    GLuint m_deformMeshVAO = 0;
    GLuint m_deformMeshVBO = 0;
    GLuint m_deformMeshEBO = 0;

    // Source atlas
    GLuint m_atlasTexture = 0;
    int32_t m_atlasMinTX = 0; // minimum tile X in atlas
    int32_t m_atlasMinTY = 0; // minimum tile Y in atlas
    int32_t m_atlasCols = 0; // number of tile columns
    int32_t m_atlasRows = 0; // number of tile rows
    int m_atlasWidth = 0; // atlas width in pixels
    int m_atlasHeight = 0; // atlas height in pixels

    // Source grid's default-fill background (premultiplied, normalized 0..1).
    // The atlas only stores the content tile bbox; everywhere else the source
    // grid implicitly carries this value (e.g. a hide-all layer mask's opaque
    // black = reveal 0). The apply shaders return it instead of transparent for
    // positions that fall outside the atlas / source content, so transforming a
    // mask with a non-transparent background keeps the vacated area hidden
    // instead of revealing it. Transparent (0,0,0,0) for every normal grid.
    float m_srcBgR = 0.0f;
    float m_srcBgG = 0.0f;
    float m_srcBgB = 0.0f;
    float m_srcBgA = 0.0f;

    // Selection mask atlas
    GLuint m_maskAtlasTexture = 0;
    int32_t m_maskAtlasMinTX = 0;
    int32_t m_maskAtlasMinTY = 0;
    int32_t m_maskAtlasCols = 0;
    int32_t m_maskAtlasRows = 0;
    int m_maskAtlasWidth = 0;
    int m_maskAtlasHeight = 0;

    // Temporary texture for single-tile preview rendering. Re-formatted per
    // transform in buildSourceAtlas to match the source grid's per-document
    // format (kept compatible with the tile textures it renders from).
    GLuint m_tempTex = 0;
    TilePixelFormat m_tempTexFormat = kDefaultTileFormat;

    // PBO for async readback
    GLuint m_pbo = 0;
    size_t m_pboSize = 0;

    std::vector<Vector2> m_cachedDeformTargets;
    std::unordered_map<TileKey, DeformTileBatch, TileKeyHash> m_cachedDeformTileBatches;
    Rect m_cachedDeformBounds;
    int m_cachedDeformRows = 0;
    int m_cachedDeformCols = 0;
    GLsizei m_cachedDeformIndexCount = 0;
    bool m_hasCachedDeformMesh = false;
    bool m_cachedDeformForwardReady = false;

    bool m_initialized = false;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLTRANSFORMRENDERER_H
