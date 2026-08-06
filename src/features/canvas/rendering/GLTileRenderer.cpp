// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   T I L E   R E N D E R E R
// ==========================================================================

#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/canvas/rendering/DisplayPyramid.h"
#include "features/canvas/scene/CanvasDisplayTransforms.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"

#include <algorithm>
#include <cmath>

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
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glSamplerParameteri(m_displaySampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_pyramidTileProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto pyramidResult = m_pyramidTileProgram->loadFromFiles(
        shaderDir + "/tile.vert.glsl", shaderDir + "/tile_pyramid.frag.glsl");
    if (!pyramidResult) {
        m_pyramidTileProgram.reset();
        m_gl->glDeleteSamplers(1, &m_displaySampler);
        m_displaySampler = 0;
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
        m_tileProgram.reset();
        return pyramidResult;
    }

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
    m_pyramidTileProgram.reset();

    if (m_displaySampler) {
        m_gl->glDeleteSamplers(1, &m_displaySampler);
        m_displaySampler = 0;
    }

    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }

    m_initialized = false;
}

// ==========================================================================
//   T E X T U R E   M A N A G E M E N T
// ==========================================================================

void GLTileRenderer::ensureTileTexture(TileData& tile)
{
    if (tile.hasTexture())
        return;

    const TextureParams params = tileTextureParams(tile.format());

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
    const Color& viewportBackgroundColor, bool clipToCanvas, const DisplayPyramid* displayPyramid)
{
    m_lastRenderDrawCalls = 0;
    m_lastRenderLevel = 0;
    m_lastRenderLevelBlend = 0.0f;
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

    constexpr float kDisplayFilteringMaxZoom = 2.0f;
    const float zoom = viewport.camera().zoom();
    const bool minifying = zoom < 1.0f;
    const bool useDisplayFiltering = zoom <= kDisplayFilteringMaxZoom;

    // Pyramid path. The level is a per-frame scalar because zoom is uniform and
    // rotation is isotropic: Lf = log2(1/zoom), draw level floor(Lf) lerped
    // toward floor(Lf)+1 by the fraction, so continuous zoom-out never pops.
    // Nothing here is decided per tile — a tile whose rebuild was skipped draws
    // stale, which is still smooth. That is the whole point of the pyramid.
    if (minifying && displayPyramid != nullptr && displayPyramid->isInitialized()
        && m_pyramidTileProgram && m_pyramidTileProgram->isValid()) {
        const float continuousLevel = DisplayPyramid::continuousLevelForZoom(zoom);
        PyramidFrame frame;
        frame.level = std::clamp(
            static_cast<int>(std::floor(continuousLevel)), 0, DisplayPyramid::kMaxLevel - 1);
        frame.blend = std::clamp(continuousLevel - static_cast<float>(frame.level), 0.0f, 1.0f);

        const VisibleWorldBounds bounds { worldMinX, worldMinY, worldMaxX, worldMaxY };

        // Blending stays ON even in the rounded-edges mode, which the level-zero
        // path turns it off for. That path could: every fragment it produced in
        // that mode was opaque. The pyramid produces partial alpha wherever the
        // box filter crosses the edge of the covered tile set, and those
        // fragments must composite over the document background drawn
        // underneath. The corner fringe still writes alpha 1, so a full replace
        // through SRC_ALPHA blending is bit-identical to writing it unblended.
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendFuncSeparate(
            GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        m_lastRenderDrawCalls = renderFromPyramid(grid, *displayPyramid, frame, vpMatrix, bounds,
            canvasWidth, canvasHeight, clipToCanvas, cornerRadiusCanvasPx,
            compositeRoundedEdgesOverViewportBackground, viewportBackgroundColor);

        m_gl->glDisable(GL_BLEND);
        return;
    }

    auto visibleKeys = grid.visibleTiles(worldMinX, worldMinY, worldMaxX, worldMaxY);
    if (visibleKeys.empty())
        return;

    if (compositeRoundedEdgesOverViewportBackground) {
        m_gl->glDisable(GL_BLEND);
    } else {
        m_gl->glEnable(GL_BLEND);
        // Tile shader outputs straight-alpha color for display. Use separate alpha blending
        // so intermediate FBOs keep correct premultiplied-looking alpha for later composition.
        m_gl->glBlendFuncSeparate(
            GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    auto clippedOut = [clipToCanvas, canvasMaxTileX, canvasMaxTileY](const TileKey& key) {
        return clipToCanvas
            && (key.x < 0 || key.y < 0 || key.x > canvasMaxTileX || key.y > canvasMaxTileY);
    };

    // Bilinear level zero, uniformly over the frame. Past the zoom ceiling the
    // texture object's own NEAREST magnification takes over so pixel-peeping
    // still shows hard texels.
    const GLuint frameSampler = useDisplayFiltering ? m_displaySampler : 0;

    // Reaching here while minifying means this grid has no pyramid: a layer's
    // screen-space source (the transform preview composites the whole stack that
    // way), a mask preview, a transient export cache. One bilinear tap would
    // skip most of the source and alias exactly like point sampling, and it is
    // the reason entering a transform used to turn the whole zoomed-out canvas
    // crunchy. So the footprint gets an explicit box instead.
    //
    // Taps are capped: past 4 per axis the box stops being exact and becomes a
    // stratified sample of the footprint, which is where the honest quality
    // ceiling of a per-tile filter sits. The pyramid is what the canvas itself
    // uses precisely because it has no such ceiling.
    LevelZeroFilter filter;
    if (minifying) {
        constexpr int kMaxMinifyTaps = 4;
        const float footprintTexels = 1.0f / std::max(zoom, 1.0e-4f);
        filter.taps = std::clamp(static_cast<int>(std::lround(footprintTexels)), 1, kMaxMinifyTaps);
        filter.stepUV
            = (footprintTexels / static_cast<float>(filter.taps)) / static_cast<float>(TILE_SIZE);
    }

    m_tileProgram->use();
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindSampler(0, frameSampler);

    for (const auto& key : visibleKeys) {
        if (clippedOut(key))
            continue;
        const TileData* tile = grid.getTile(key);
        if (tile && tile->hasTexture()) {
            if (drawTileQuad(key, *tile, vpMatrix, canvasWidth, canvasHeight, clipToCanvas,
                    cornerRadiusCanvasPx, compositeRoundedEdgesOverViewportBackground,
                    viewportBackgroundColor, filter)) {
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
    const Color& viewportBackgroundColor, const LevelZeroFilter& filter)
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
    m_tileProgram->setUniform("uMinifyTaps", filter.taps);
    m_tileProgram->setUniform("uMinifyStepUV", filter.stepUV, filter.stepUV);

    m_gl->glBindTextureUnit(0, tile.textureId());

    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

// ==========================================================================
//   P Y R A M I D   D I S P L A Y   P A T H
// ==========================================================================

uint32_t GLTileRenderer::renderFromPyramid(const TileGrid& grid, const DisplayPyramid& pyramid,
    const PyramidFrame& frame, const std::array<float, 16>& vpMatrix,
    const VisibleWorldBounds& bounds, uint32_t canvasWidth, uint32_t canvasHeight,
    bool clipToCanvas, float cornerRadiusCanvasPx, bool compositeRoundedEdgesOverViewportBackground,
    const Color& viewportBackgroundColor)
{
    const int level = frame.level;
    const int32_t spanPx = DisplayPyramid::levelSpanPixels(level);
    const float spanF = static_cast<float>(spanPx);
    const float coreF = static_cast<float>(TILE_SIZE);
    // Document pixels per core texel of this level.
    const float texelDocPx = static_cast<float>(int32_t { 1 } << level);

    // Level 0 samples the composition cache directly (256x256, no apron);
    // every level above samples pyramid tiles (258x258, apron 1). Addressing
    // both in core-texel units is what keeps this one code path.
    const bool fineIsPyramid = level > 0;
    const float fineTexSize
        = fineIsPyramid ? static_cast<float>(DisplayPyramid::kTextureSize) : coreF;
    const float fineApron = fineIsPyramid ? static_cast<float>(DisplayPyramid::kApron) : 0.0f;
    const float coarseTexSize = static_cast<float>(DisplayPyramid::kTextureSize);
    const float coarseApron = static_cast<float>(DisplayPyramid::kApron);

    const int32_t minKeyX = static_cast<int32_t>(std::floor(bounds.minX / spanF));
    const int32_t minKeyY = static_cast<int32_t>(std::floor(bounds.minY / spanF));
    const int32_t maxKeyX = static_cast<int32_t>(std::floor(bounds.maxX / spanF));
    const int32_t maxKeyY = static_cast<int32_t>(std::floor(bounds.maxY / spanF));

    const uint32_t spanU = static_cast<uint32_t>(spanPx);
    const bool clip = clipToCanvas && canvasWidth > 0 && canvasHeight > 0;
    const int32_t canvasMaxKeyX = clip ? static_cast<int32_t>((canvasWidth - 1u) / spanU) : 0;
    const int32_t canvasMaxKeyY = clip ? static_cast<int32_t>((canvasHeight - 1u) / spanU) : 0;

    struct PyramidQuad {
        TileKey key {};
        GLuint fineTexture = 0;
        GLuint coarseTexture = 0;
    };

    const int64_t keyCount = std::max<int64_t>(0,
        (static_cast<int64_t>(maxKeyX) - minKeyX + 1)
            * (static_cast<int64_t>(maxKeyY) - minKeyY + 1));
    std::vector<PyramidQuad> quads;
    quads.reserve(static_cast<size_t>(std::min<int64_t>(keyCount, 4096)));

    // A quad without its coarse partner would draw sharper than its neighbours,
    // and mixed QUALITY across the frame is exactly what the eye reads as a
    // grid. So the lerp is all-or-nothing for the frame: one missing coarse
    // tile pins the whole frame to the fine level.
    bool everyCoarsePresent = true;

    for (int32_t ky = minKeyY; ky <= maxKeyY; ++ky) {
        for (int32_t kx = minKeyX; kx <= maxKeyX; ++kx) {
            if (clip && (kx < 0 || ky < 0 || kx > canvasMaxKeyX || ky > canvasMaxKeyY)) {
                continue;
            }
            const TileKey key { kx, ky };

            GLuint fineTexture = 0;
            if (level == 0) {
                const TileData* tile = grid.getTile(key);
                if (tile != nullptr && tile->hasTexture()) {
                    fineTexture = tile->textureId();
                }
            } else {
                fineTexture = pyramid.texture(level, key);
            }
            if (fineTexture == 0) {
                continue;
            }

            const TileKey coarseKey { kx >> 1, ky >> 1 };
            const GLuint coarseTexture = pyramid.texture(level + 1, coarseKey);
            if (coarseTexture == 0) {
                everyCoarsePresent = false;
            }
            quads.push_back(PyramidQuad { key, fineTexture, coarseTexture });
        }
    }

    if (quads.empty()) {
        return 0;
    }

    const float blend = everyCoarsePresent ? frame.blend : 0.0f;
    m_lastRenderLevel = level;
    m_lastRenderLevelBlend = blend;

    m_pyramidTileProgram->use();
    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glBindSampler(0, m_displaySampler);
    m_gl->glBindSampler(1, m_displaySampler);

    m_pyramidTileProgram->setUniform("uFineTexture", 0);
    m_pyramidTileProgram->setUniform("uCoarseTexture", 1);
    m_pyramidTileProgram->setUniform("uLevelBlend", blend);
    m_pyramidTileProgram->setUniform("uInvTileSize", 1.0f / coreF);
    m_pyramidTileProgram->setUniform("uTileSpanPx", spanF, spanF);
    m_pyramidTileProgram->setUniform("uCornerRadius", cornerRadiusCanvasPx);
    m_pyramidTileProgram->setUniform("uCompositeRoundedEdgesOverViewportBackground",
        compositeRoundedEdgesOverViewportBackground ? 1 : 0);
    m_pyramidTileProgram->setUniform("uViewportBackgroundColor", viewportBackgroundColor.r,
        viewportBackgroundColor.g, viewportBackgroundColor.b, viewportBackgroundColor.a);
    m_pyramidTileProgram->setUniform(
        "uCanvasSize", static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    // fragTexCoord is the tile-local fraction, so a core texel is coreF of it.
    const float fineUVScale = coreF / fineTexSize;
    m_pyramidTileProgram->setUniform("uFineUVScale", fineUVScale, fineUVScale);
    m_pyramidTileProgram->setUniform(
        "uFineUVOffset", fineApron / fineTexSize, fineApron / fineTexSize);
    // A level tile occupies exactly half of its parent's core on each axis.
    const float coarseUVScale = (coreF * 0.5f) / coarseTexSize;
    m_pyramidTileProgram->setUniform("uCoarseUVScale", coarseUVScale, coarseUVScale);

    // Sampling clamp for one side of one tap, in that tap's own core-texel
    // units. Both taps and all four sides go through this.
    //
    // A side CLIPPED by the document edge stops at the centre of the last texel
    // that lies FULLY inside the document. Past the canvas there are no
    // composition-cache tiles at all, so every texel straddling that edge
    // averaged transparency into itself when the level was built; a bilinear
    // tap that reaches one lets the background show straight through opaque
    // content. Whether such a straddling texel exists depends on the document
    // size modulo the level's texel size, which changes with the level — hence
    // an edge artifact that came and went as the camera moved.
    //
    // At level zero the rule is an identity: a texel IS a document pixel and
    // the canvas size is an integer, so ceil/floor do nothing and this
    // degenerates to the familiar half-pixel inset the level-zero path uses.
    //
    // An UNCLIPPED side is the opposite situation — the neighbouring data is
    // real and, when the quad ends at the tile border, lives in the apron. So
    // the tap reaches half a texel past the quad's own edge, capped at the far
    // side of the apron (level-zero tiles have none, hence the core - 0.5 cap).
    auto clampLowTexel = [](float bound, float apron, bool clipped) {
        return clipped ? std::ceil(bound) + 0.5f : std::max(bound - 0.5f, 0.5f - apron);
    };
    auto clampHighTexel = [coreF](float bound, float apron, bool clipped) {
        return clipped ? std::floor(bound - 1.0f) + 0.5f
                       : std::min(bound + 0.5f, coreF + apron - 0.5f);
    };
    // A quad whose visible sliver is thinner than one texel can invert the two.
    auto orderClamp = [](float& low, float& high) {
        if (high < low) {
            const float mid = 0.5f * (low + high);
            low = mid;
            high = mid;
        }
    };

    uint32_t drawCalls = 0;
    for (const PyramidQuad& quad : quads) {
        const float originX = static_cast<float>(quad.key.x) * spanF;
        const float originY = static_cast<float>(quad.key.y) * spanF;

        float quadMinX = 0.0f;
        float quadMinY = 0.0f;
        float quadMaxX = coreF;
        float quadMaxY = coreF;
        if (clip) {
            // The document clip is in document pixels; convert to this level's
            // core texels so the quad and the sampling clamp stay in one space.
            quadMinX = std::max(0.0f, -originX / texelDocPx);
            quadMinY = std::max(0.0f, -originY / texelDocPx);
            quadMaxX = std::min(coreF, (static_cast<float>(canvasWidth) - originX) / texelDocPx);
            quadMaxY = std::min(coreF, (static_cast<float>(canvasHeight) - originY) / texelDocPx);
            if (quadMaxX <= quadMinX || quadMaxY <= quadMinY) {
                continue;
            }
        }

        const auto model = createLevelTileModelMatrix(quad.key, level);
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

        m_pyramidTileProgram->setUniform("uMVP", mvp);
        m_pyramidTileProgram->setUniform("uQuadMinPx", quadMinX, quadMinY);
        m_pyramidTileProgram->setUniform("uQuadMaxPx", quadMaxX, quadMaxY);
        m_pyramidTileProgram->setUniform("uTileOriginPx", originX, originY);

        // Which sides of THIS quad the document edge runs along. Note this is
        // not the same test as "the quad got narrowed": a quad that ends
        // exactly on the canvas edge was not narrowed, yet its apron there
        // holds nothing but transparency and must stay unsampled.
        const bool clippedLowX = clip && originX <= 0.0f;
        const bool clippedLowY = clip && originY <= 0.0f;
        const bool clippedHighX = clip && (originX + spanF) >= static_cast<float>(canvasWidth);
        const bool clippedHighY = clip && (originY + spanF) >= static_cast<float>(canvasHeight);

        float fineLowX = clampLowTexel(quadMinX, fineApron, clippedLowX);
        float fineLowY = clampLowTexel(quadMinY, fineApron, clippedLowY);
        float fineHighX = clampHighTexel(quadMaxX, fineApron, clippedHighX);
        float fineHighY = clampHighTexel(quadMaxY, fineApron, clippedHighY);
        orderClamp(fineLowX, fineHighX);
        orderClamp(fineLowY, fineHighY);
        m_pyramidTileProgram->setUniform("uFineUVMin", (fineLowX + fineApron) / fineTexSize,
            (fineLowY + fineApron) / fineTexSize);
        m_pyramidTileProgram->setUniform("uFineUVMax", (fineHighX + fineApron) / fineTexSize,
            (fineHighY + fineApron) / fineTexSize);

        // Which half of the coarse tile this one sits in.
        const float parityX = static_cast<float>(quad.key.x - ((quad.key.x >> 1) << 1));
        const float parityY = static_cast<float>(quad.key.y - ((quad.key.y >> 1) << 1));
        const float coarseBaseX = parityX * coreF * 0.5f;
        const float coarseBaseY = parityY * coreF * 0.5f;
        m_pyramidTileProgram->setUniform("uCoarseUVOffset",
            (coarseBaseX + coarseApron) / coarseTexSize,
            (coarseBaseY + coarseApron) / coarseTexSize);

        // Same rule in the coarse tile's core texels — which are twice as wide
        // in document pixels, so its straddling texel is a different one.
        float coarseLowX = clampLowTexel(coarseBaseX + quadMinX * 0.5f, coarseApron, clippedLowX);
        float coarseLowY = clampLowTexel(coarseBaseY + quadMinY * 0.5f, coarseApron, clippedLowY);
        float coarseHighX
            = clampHighTexel(coarseBaseX + quadMaxX * 0.5f, coarseApron, clippedHighX);
        float coarseHighY
            = clampHighTexel(coarseBaseY + quadMaxY * 0.5f, coarseApron, clippedHighY);
        orderClamp(coarseLowX, coarseHighX);
        orderClamp(coarseLowY, coarseHighY);
        m_pyramidTileProgram->setUniform("uCoarseUVMin", (coarseLowX + coarseApron) / coarseTexSize,
            (coarseLowY + coarseApron) / coarseTexSize);
        m_pyramidTileProgram->setUniform("uCoarseUVMax",
            (coarseHighX + coarseApron) / coarseTexSize,
            (coarseHighY + coarseApron) / coarseTexSize);

        m_gl->glBindTextureUnit(0, quad.fineTexture);
        m_gl->glBindTextureUnit(1, quad.coarseTexture != 0 ? quad.coarseTexture : quad.fineTexture);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++drawCalls;
    }

    m_gl->glBindSampler(0, 0);
    m_gl->glBindSampler(1, 0);
    // Same reason as the unbind at the end of DisplayPyramid::update(): unit 0
    // may hold a composition-cache tile, and the compositor renders into those
    // texture objects on the next frame.
    m_gl->glBindTextures(0, 2, nullptr);
    m_gl->glBindVertexArray(0);
    return drawCalls;
}

std::array<float, 16> GLTileRenderer::createLevelTileModelMatrix(
    const TileKey& key, int level) const
{
    const float span = static_cast<float>(DisplayPyramid::levelSpanPixels(level));
    const float originX = static_cast<float>(key.x) * span;
    const float originY = static_cast<float>(key.y) * span;

    return { span, 0.0f, 0.0f, 0.0f, 0.0f, span, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, originX,
        originY, 0.0f, 1.0f };
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
