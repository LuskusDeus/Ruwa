// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   T I L E   R E N D E R E R
// ==========================================================================

#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/canvas/scene/CanvasDisplayTransforms.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"

#include <limits>

namespace aether {

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLTileRenderer::GLTileRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLTileRenderer::~GLTileRenderer()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> GLTileRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_tileProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto tileResult = m_tileProgram->loadFromFiles(
        shaderDir + "/tile.vert.glsl", shaderDir + "/tile.frag.glsl");
    if (!tileResult) {
        return tileResult;
    }

    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create tile VAO" };
    }

    m_gl->glGenSamplers(1, &m_displaySampler);
    if (m_displaySampler == 0) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
        m_tileProgram.reset();
        return { ErrorCode::PipelineCreationFailed, "Failed to create tile display sampler" };
    }
    m_gl->glSamplerParameteri(
        m_displaySampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_gl->glGenSamplers(1, &m_levelZeroLinearSampler);
    if (m_levelZeroLinearSampler == 0) {
        m_gl->glDeleteSamplers(1, &m_displaySampler);
        m_displaySampler = 0;
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
        m_tileProgram.reset();
        return { ErrorCode::PipelineCreationFailed,
            "Failed to create tile level-zero display sampler" };
    }
    m_gl->glSamplerParameteri(m_levelZeroLinearSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_levelZeroLinearSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_levelZeroLinearSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glSamplerParameteri(m_levelZeroLinearSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_initialized = true;
    return Result<void>::ok();
}

void GLTileRenderer::shutdown()
{
    // Unconditional: the pool holds GL objects belonging to this context and
    // must not outlive it, whether or not initialize() ever completed.
    m_texturePool.clear(m_gl);

    if (!m_initialized)
        return;

    m_tileProgram.reset();

    if (m_displaySampler) {
        m_gl->glDeleteSamplers(1, &m_displaySampler);
        m_displaySampler = 0;
    }

    if (m_levelZeroLinearSampler) {
        m_gl->glDeleteSamplers(1, &m_levelZeroLinearSampler);
        m_levelZeroLinearSampler = 0;
    }

    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }

    m_initialized = false;
}

void GLTileRenderer::beginDisplayMipFrame(bool deferRegeneration)
{
    m_deferDisplayMipRegeneration = deferRegeneration;
    m_displayMipRegenerationBudget = kDisplayMipRegenerationBudget;
    m_displayMipRegenerationsThisFrame = 0;
    m_visibleDisplayMipmapsPending = false;
}

void GLTileRenderer::beginUnrestrictedDisplayMipFrame()
{
    m_deferDisplayMipRegeneration = false;
    m_displayMipRegenerationBudget = std::numeric_limits<uint32_t>::max();
    m_displayMipRegenerationsThisFrame = 0;
    m_visibleDisplayMipmapsPending = false;
}

// ==========================================================================
//   T E X T U R E   M A N A G E M E N T
// ==========================================================================

void GLTileRenderer::ensureTileTexture(TileData& tile)
{
    ensureTileTextureStorage(tile, tileTextureParams(tile.format()));
}

void GLTileRenderer::ensureDisplayTileTexture(TileData& tile)
{
    ensureTileTextureStorage(tile, displayTileTextureParams(tile.format()));
}

void GLTileRenderer::ensureTileTextureStorage(TileData& tile, const TextureParams& params)
{
    if (tile.hasTexture())
        return;

    // A recycled texture comes back zero-filled with these sampler params
    // already applied, so this is indistinguishable from the allocation it
    // replaces — only ~100x cheaper.
    GLuint tex = m_texturePool.acquire(m_gl, params);
    if (tex == 0) {
        tex = createTexture2D(m_gl, TILE_SIZE, TILE_SIZE, params);
    }
    tile.setTextureId(tex);
}

void GLTileRenderer::uploadTileData(TileData& tile)
{
    if (!tile.hasTexture())
        return;

    if (tile.isSolid()) {
        // Uniform-color tile: fill the texture with the solid color directly,
        // without materializing a 256 KB CPU buffer. const pixels() would return
        // zeros for a solid tile, so this path is required for raw tile drawing
        // (e.g. a mask grid's solid background/marker tiles in viewport previews).
        uint8_t r = 0, g = 0, b = 0, a = 0;
        tile.solidColor(r, g, b, a);
        const uint8_t rgba[4] = { r, g, b, a };
        m_gl->glClearTexImage(tile.textureId(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        tile.clearDirty();
        return;
    }

    m_gl->glTextureSubImage2D(tile.textureId(), 0, 0, 0, TILE_SIZE, TILE_SIZE, GL_RGBA,
        tileGLPixelType(tile.format()), static_cast<const TileData&>(tile).pixels());

    tile.clearDirty();
}

void GLTileRenderer::uploadDirtyTiles(TileGrid& grid)
{
    auto& dirtySet = grid.dirtyTiles();
    if (dirtySet.empty())
        return;

    std::vector<TileKey> keysToProcess(dirtySet.begin(), dirtySet.end());
    for (const auto& key : keysToProcess) {
        TileData* tile = grid.getTile(key);
        if (!tile) {
            grid.removeDirty(key);
            continue;
        }

        ensureTileTexture(*tile);
        uploadTileData(*tile);
        grid.removeDirty(key);
    }
}

void GLTileRenderer::destroyTileTexture(TileData& tile)
{
    if (tile.hasTexture()) {
        GLuint tex = tile.textureId();
        if (!m_texturePool.release(m_gl, tex)) {
            m_gl->glDeleteTextures(1, &tex);
        }
        tile.setTextureId(0);
    }
}

void GLTileRenderer::flushOrphanedTextures()
{
    // Anything released since the previous frame becomes reusable now, before
    // this frame's passes bind anything.
    m_texturePool.promotePending();

    auto orphaned = OrphanedTextureCollector::instance().takeAll();
    if (orphaned.empty())
        return;

    // Most of these are stroke and composition-cache tiles that will be needed
    // again within a few frames, so they go back into the pool. release()
    // verifies each one against GL, so ids that outlived their context or came
    // from somewhere unexpected fall through to the delete batch.
    std::vector<GLuint> toDelete;
    for (const uint32_t id : orphaned) {
        const GLuint tex = static_cast<GLuint>(id);
        if (!m_texturePool.release(m_gl, tex)) {
            toDelete.push_back(tex);
        }
    }
    if (!toDelete.empty()) {
        m_gl->glDeleteTextures(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    }
}

// ==========================================================================
//   R E N D E R I N G
// ==========================================================================

void GLTileRenderer::render(const TileGrid& grid, const Viewport& viewport, uint32_t canvasWidth,
    uint32_t canvasHeight, float cornerRadiusCanvasPx, bool canvasContentFlipH,
    bool canvasContentFlipV, bool compositeRoundedEdgesOverViewportBackground,
    const Color& viewportBackgroundColor, bool clipToCanvas, bool useDisplayMipmaps)
{
    m_lastRenderDrawCalls = 0;
    if (!m_initialized || grid.empty())
        return;
    if (!m_tileProgram || !m_tileProgram->isValid())
        return;
    clipToCanvas = clipToCanvas && canvasWidth > 0 && canvasHeight > 0;
    const int32_t canvasMaxTileX
        = clipToCanvas ? static_cast<int32_t>((canvasWidth - 1u) / TILE_SIZE) : 0;
    const int32_t canvasMaxTileY
        = clipToCanvas ? static_cast<int32_t>((canvasHeight - 1u) / TILE_SIZE) : 0;

    auto vpMatrix = viewport.viewProjectionMatrix();
    if (canvasContentFlipH || canvasContentFlipV) {
        const float cw = static_cast<float>(canvasWidth);
        const float ch = static_cast<float>(canvasHeight);
        const auto mirrorM
            = canvasContentMirrorMatrix4(cw, ch, canvasContentFlipH, canvasContentFlipV);
        vpMatrix = multiplyMat4ColMajor(vpMatrix, mirrorM);
    }

    // Compute visible world-space AABB (document space when content mirror is active)
    auto& cam = viewport.camera();
    Vector2 vpSize = viewport.size();
    const float cwF = static_cast<float>(canvasWidth);
    const float chF = static_cast<float>(canvasHeight);

    Vector2 p0 = mirrorWorldInCanvas(cam.screenToWorld({ 0.0f, 0.0f }, vpSize), cwF, chF,
        canvasContentFlipH, canvasContentFlipV);
    Vector2 p1 = mirrorWorldInCanvas(cam.screenToWorld({ vpSize.x, 0.0f }, vpSize), cwF, chF,
        canvasContentFlipH, canvasContentFlipV);
    Vector2 p2 = mirrorWorldInCanvas(cam.screenToWorld({ 0.0f, vpSize.y }, vpSize), cwF, chF,
        canvasContentFlipH, canvasContentFlipV);
    Vector2 p3 = mirrorWorldInCanvas(cam.screenToWorld({ vpSize.x, vpSize.y }, vpSize), cwF, chF,
        canvasContentFlipH, canvasContentFlipV);

    float worldMinX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    float worldMinY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    float worldMaxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    float worldMaxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));

    auto visibleKeys = grid.visibleTiles(worldMinX, worldMinY, worldMaxX, worldMaxY);
    if (visibleKeys.empty())
        return;

    constexpr float kDisplayFilteringMaxZoom = 2.0f;
    const float zoom = viewport.camera().zoom();
    const bool minifying = useDisplayMipmaps && zoom < 1.0f;
    const bool useDisplayFiltering = zoom <= kDisplayFilteringMaxZoom;

    if (compositeRoundedEdgesOverViewportBackground) {
        m_gl->glDisable(GL_BLEND);
    } else {
        m_gl->glEnable(GL_BLEND);
        // Tile shader outputs straight-alpha color for display. Use separate alpha blending
        // so intermediate FBOs keep correct premultiplied-looking alpha for later composition.
        m_gl->glBlendFuncSeparate(
            GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_tileProgram->use();
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindSampler(0, 0);
    GLuint boundSampler = 0;
    auto bindDisplaySampler = [this, &boundSampler](GLuint sampler) {
        if (boundSampler == sampler)
            return;
        m_gl->glBindSampler(0, sampler);
        boundSampler = sampler;
    };

    for (const auto& key : visibleKeys) {
        if (clipToCanvas) {
            if (key.x < 0 || key.y < 0 || key.x > canvasMaxTileX || key.y > canvasMaxTileY) {
                continue;
            }
        }
        const TileData* tile = grid.getTile(key);
        if (tile && tile->hasTexture()) {
            GLuint tileSampler = useDisplayFiltering ? m_levelZeroLinearSampler : 0;
            if (minifying && tile->displayMipmapsDirty()) {
                const bool budgetAvailable = m_displayMipRegenerationsThisFrame
                    < m_displayMipRegenerationBudget;
                if (!m_deferDisplayMipRegeneration && budgetAvailable) {
                    m_gl->glGenerateTextureMipmap(tile->textureId());
                    tile->clearDisplayMipmapsDirty();
                    ++m_displayMipRegenerationsThisFrame;
                } else {
                    m_visibleDisplayMipmapsPending = true;
                }
            }
            if (minifying && !tile->displayMipmapsDirty()) {
                tileSampler = m_displaySampler;
            }
            bindDisplaySampler(tileSampler);
            if (drawTileQuad(key, *tile, vpMatrix, canvasWidth, canvasHeight, clipToCanvas,
                    cornerRadiusCanvasPx, compositeRoundedEdgesOverViewportBackground,
                    viewportBackgroundColor)) {
                ++m_lastRenderDrawCalls;
            }
        }
    }

    m_gl->glBindSampler(0, 0);
    m_gl->glBindVertexArray(0);
    m_gl->glDisable(GL_BLEND);
}

bool GLTileRenderer::drawTileQuad(const TileKey& key, const TileData& tile,
    const std::array<float, 16>& vpMatrix, uint32_t canvasWidth, uint32_t canvasHeight,
    bool clipToCanvas, float cornerRadiusCanvasPx, bool compositeRoundedEdgesOverViewportBackground,
    const Color& viewportBackgroundColor)
{
    auto model = createTileModelMatrix(key);
    float originX, originY;
    tileWorldOrigin(key, originX, originY);

    std::array<float, 16> mvp {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += vpMatrix[k * 4 + row] * model[col * 4 + k];
            }
            mvp[col * 4 + row] = sum;
        }
    }

    m_tileProgram->setUniform("uMVP", mvp);
    m_tileProgram->setUniform("uTileTexture", 0);
    m_tileProgram->setUniform("uInvTileSize", 1.0f / static_cast<float>(TILE_SIZE));
    m_tileProgram->setUniform("uCornerRadius", cornerRadiusCanvasPx);
    m_tileProgram->setUniform("uCompositeRoundedEdgesOverViewportBackground",
        compositeRoundedEdgesOverViewportBackground ? 1 : 0);
    m_tileProgram->setUniform("uViewportBackgroundColor", viewportBackgroundColor.r,
        viewportBackgroundColor.g, viewportBackgroundColor.b, viewportBackgroundColor.a);
    m_tileProgram->setUniform(
        "uCanvasSize", static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    m_tileProgram->setUniform("uTileOriginPx", originX, originY);
    float quadMinX = 0.0f;
    float quadMinY = 0.0f;
    float quadMaxX = static_cast<float>(TILE_SIZE);
    float quadMaxY = static_cast<float>(TILE_SIZE);
    if (clipToCanvas) {
        quadMinX = std::max(0.0f, -originX);
        quadMinY = std::max(0.0f, -originY);
        quadMaxX
            = std::min(static_cast<float>(TILE_SIZE), static_cast<float>(canvasWidth) - originX);
        quadMaxY
            = std::min(static_cast<float>(TILE_SIZE), static_cast<float>(canvasHeight) - originY);
        if (quadMaxX <= quadMinX || quadMaxY <= quadMinY) {
            return false;
        }
    }
    m_tileProgram->setUniform("uQuadMinPx", quadMinX, quadMinY);
    m_tileProgram->setUniform("uQuadMaxPx", quadMaxX, quadMaxY);
    // Clamp sampling to the valid clip sub-rect. This avoids linear-filter
    // bleeding with transparent pixels outside the canvas on right/bottom edges.
    const float sampleMinX = (quadMinX + 0.5f) / static_cast<float>(TILE_SIZE);
    const float sampleMinY = (quadMinY + 0.5f) / static_cast<float>(TILE_SIZE);
    const float sampleMaxX = (quadMaxX - 0.5f) / static_cast<float>(TILE_SIZE);
    const float sampleMaxY = (quadMaxY - 0.5f) / static_cast<float>(TILE_SIZE);
    m_tileProgram->setUniform("uSampleMinUV", sampleMinX, sampleMinY);
    m_tileProgram->setUniform("uSampleMaxUV", sampleMaxX, sampleMaxY);

    m_gl->glBindTextureUnit(0, tile.textureId());

    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

std::array<float, 16> GLTileRenderer::createTileModelMatrix(const TileKey& key) const
{
    float originX, originY;
    tileWorldOrigin(key, originX, originY);
    float s = static_cast<float>(TILE_SIZE);

    return { s, 0.0f, 0.0f, 0.0f, 0.0f, s, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, originX, originY,
        0.0f, 1.0f };
}

} // namespace aether
