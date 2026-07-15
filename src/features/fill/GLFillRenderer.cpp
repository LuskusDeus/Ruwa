// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   F I L L   R E N D E R E R
// ==========================================================================

#include "GLFillRenderer.h"

#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/fill/FloodFill.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/tiles/TileData.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace aether {

namespace {

constexpr uint32_t kWorkGroupSize = 256u;
constexpr uint32_t kInitWorkGroupSizeX = 16u;
constexpr uint32_t kInitWorkGroupSizeY = 16u;
constexpr uint32_t kGpuBatchPasses = 64u;
constexpr int kInteriorSeedTolerance = 6;
constexpr int kInitialWindowTileRadius = 2;
constexpr int kStrokeMaterialAlphaDistanceScale = 64;

enum class FloodSeedMode : int { SeedPixel = 0, Boundary = 1 };

enum class FillSemanticMode : int { Exterior = 0, Stroke = 1 };

struct PremultPixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

// CONTENT tile byte size is now per-document: derived from the target grid's
// format at each call site (tileByteSize(grid.format())). MASK / coverage tiles
// stay RGBA8 (TILE_BYTE_SIZE).

inline uint8_t quantizeChannelF(float v)
{
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// CONTENT pixel write into a raw document-format buffer.
inline void writeContentPixelRaw(std::vector<uint8_t>& raw, uint32_t localX, uint32_t localY,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, TilePixelFormat contentFormat)
{
    const float f[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    writeTilePixelF(raw.data(), contentFormat, localX, localY, f);
}

// CONTENT pixel write into a live tile (format-aware; materializes + marks dirty).
inline void writeContentPixelTile(
    TileData& tile, uint32_t localX, uint32_t localY, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const float f[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    writeTilePixelF(tile, localX, localY, f);
}

struct CounterState {
    uint32_t currentCount = 0;
    uint32_t nextCount = 0;
    uint32_t pixelsFilled = 0;
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
    uint32_t touchesBoundary = 0;
    uint32_t touchesWindowBoundary = 0;
};

struct FillWindow {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
};

struct FillDomain {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
};

inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

inline uint32_t floorMod(int32_t a, int32_t b)
{
    int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

inline bool colorsMatch(
    uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1, uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2)
{
    return r1 == r2 && g1 == g2 && b1 == b2 && a1 == a2;
}

inline bool samplePixelAt(
    const TileGrid& grid, int32_t x, int32_t y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a)
{
    if (x < 0 || y < 0)
        return false;

    const int32_t tx = floorDiv(x, static_cast<int32_t>(TILE_SIZE));
    const int32_t ty = floorDiv(y, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
    const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
    const TileData* tile = grid.getTile(TileKey { tx, ty });
    if (!tile) {
        r = g = b = a = 0;
        return true;
    }

    // CONTENT read: format-aware, quantized to 8-bit premultiplied.
    float f[4];
    readTilePixelF(*tile, localX, localY, f);
    r = quantizeChannelF(f[0]);
    g = quantizeChannelF(f[1]);
    b = quantizeChannelF(f[2]);
    a = quantizeChannelF(f[3]);
    return true;
}

inline int pixelDistance(const PremultPixel& lhs, const PremultPixel& rhs)
{
    return std::max(std::max(std::abs(static_cast<int>(lhs.r) - static_cast<int>(rhs.r)),
                        std::abs(static_cast<int>(lhs.g) - static_cast<int>(rhs.g))),
        std::max(std::abs(static_cast<int>(lhs.b) - static_cast<int>(rhs.b)),
            std::abs(static_cast<int>(lhs.a) - static_cast<int>(rhs.a))));
}

inline FillSemanticMode fillModeForSeed(const PremultPixel& seedPixel)
{
    return seedPixel.a == 0 ? FillSemanticMode::Exterior : FillSemanticMode::Stroke;
}

inline uint8_t unpremultiplyChannel(uint8_t channel, uint8_t alpha)
{
    if (alpha == 0) {
        return 0;
    }

    return static_cast<uint8_t>(std::clamp(
        (static_cast<int>(channel) * 255 + static_cast<int>(alpha) / 2) / static_cast<int>(alpha),
        0, 255));
}

inline PremultPixel straightPixel(const PremultPixel& px)
{
    PremultPixel straight = px;
    straight.r = unpremultiplyChannel(px.r, px.a);
    straight.g = unpremultiplyChannel(px.g, px.a);
    straight.b = unpremultiplyChannel(px.b, px.a);
    return straight;
}

inline int materialDistance(const PremultPixel& lhs, const PremultPixel& rhs)
{
    const PremultPixel lhsStraight = straightPixel(lhs);
    const PremultPixel rhsStraight = straightPixel(rhs);
    const int colorDistance = std::max(
        std::max(std::abs(static_cast<int>(lhsStraight.r) - static_cast<int>(rhsStraight.r)),
            std::abs(static_cast<int>(lhsStraight.g) - static_cast<int>(rhsStraight.g))),
        std::abs(static_cast<int>(lhsStraight.b) - static_cast<int>(rhsStraight.b)));
    const int alphaDistance = (std::abs(static_cast<int>(lhs.a) - static_cast<int>(rhs.a))
                                  + kStrokeMaterialAlphaDistanceScale - 1)
        / kStrokeMaterialAlphaDistanceScale;
    return std::max(colorDistance, alphaDistance);
}

inline int fillDistance(const PremultPixel& lhs, const PremultPixel& rhs, FillSemanticMode mode)
{
    return mode == FillSemanticMode::Stroke ? materialDistance(lhs, rhs) : pixelDistance(lhs, rhs);
}

inline PremultPixel compositeOver(
    const PremultPixel& src, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    const int premulR = (static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255;
    const int premulG = (static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255;
    const int premulB = (static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255;
    const int invFillA = 255 - static_cast<int>(fillA);
    PremultPixel out;
    out.r = static_cast<uint8_t>(
        std::clamp(premulR + ((static_cast<int>(src.r) * invFillA + 127) / 255), 0, 255));
    out.g = static_cast<uint8_t>(
        std::clamp(premulG + ((static_cast<int>(src.g) * invFillA + 127) / 255), 0, 255));
    out.b = static_cast<uint8_t>(
        std::clamp(premulB + ((static_cast<int>(src.b) * invFillA + 127) / 255), 0, 255));
    out.a = static_cast<uint8_t>(std::clamp(
        static_cast<int>(fillA) + ((static_cast<int>(src.a) * invFillA + 127) / 255), 0, 255));
    return out;
}

inline PremultPixel compositeUnder(
    const PremultPixel& src, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    const int premulR = (static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255;
    const int premulG = (static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255;
    const int premulB = (static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255;
    const int invSrcA = 255 - static_cast<int>(src.a);
    PremultPixel out;
    out.r = static_cast<uint8_t>(
        std::clamp(static_cast<int>(src.r) + ((premulR * invSrcA + 127) / 255), 0, 255));
    out.g = static_cast<uint8_t>(
        std::clamp(static_cast<int>(src.g) + ((premulG * invSrcA + 127) / 255), 0, 255));
    out.b = static_cast<uint8_t>(
        std::clamp(static_cast<int>(src.b) + ((premulB * invSrcA + 127) / 255), 0, 255));
    out.a = static_cast<uint8_t>(std::clamp(
        static_cast<int>(src.a) + ((static_cast<int>(fillA) * invSrcA + 127) / 255), 0, 255));
    return out;
}

inline PremultPixel recolorWithCoverage(
    const PremultPixel& src, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA)
{
    PremultPixel out;
    out.r = static_cast<uint8_t>((static_cast<int>(fillR) * static_cast<int>(src.a) + 127) / 255);
    out.g = static_cast<uint8_t>((static_cast<int>(fillG) * static_cast<int>(src.a) + 127) / 255);
    out.b = static_cast<uint8_t>((static_cast<int>(fillB) * static_cast<int>(src.a) + 127) / 255);
    out.a = static_cast<uint8_t>((static_cast<int>(fillA) * static_cast<int>(src.a) + 127) / 255);
    return out;
}

// CONTENT read from a raw document-format buffer (source / before tiles).
inline PremultPixel samplePixel(const std::vector<uint8_t>* rawTile, uint32_t localX,
    uint32_t localY, TilePixelFormat contentFormat)
{
    PremultPixel px;
    if (!rawTile) {
        return px;
    }

    float f[4];
    readTilePixelF(rawTile->data(), contentFormat, localX, localY, f);
    px.r = quantizeChannelF(f[0]);
    px.g = quantizeChannelF(f[1]);
    px.b = quantizeChannelF(f[2]);
    px.a = quantizeChannelF(f[3]);
    return px;
}

inline bool maskHasPixel(const std::vector<uint8_t>* maskTile, uint32_t localX, uint32_t localY)
{
    if (!maskTile) {
        return false;
    }
    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    return (*maskTile)[idx + 3] != 0;
}

inline void snapshotTileBeforeChange(TileGrid& grid, GpuFillResult& result, const TileKey& key)
{
    if (result.beforeTiles.count(key) > 0 || result.createdTiles.count(key) > 0) {
        return;
    }

    const TileData* tile = grid.getTile(key);
    if (!tile) {
        return;
    }

    const uint32_t contentTileBytes = tileByteSize(grid.format());
    auto& before = result.beforeTiles[key];
    before.resize(contentTileBytes);
    std::memcpy(before.data(), tile->pixels(), contentTileBytes);
}

inline void writeMaskPixel(
    GpuFillResult& result, const TileKey& key, uint32_t localX, uint32_t localY)
{
    auto& maskTile = result.fillMaskTiles[key];
    if (maskTile.empty()) {
        maskTile.assign(TILE_BYTE_SIZE, 0);
    }

    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    maskTile[idx + 0] = 255;
    maskTile[idx + 1] = 255;
    maskTile[idx + 2] = 255;
    maskTile[idx + 3] = 255;
}

inline void writeMaskPixel(
    GpuFillResult::RawTileMap& maskTiles, const TileKey& key, uint32_t localX, uint32_t localY)
{
    auto& maskTile = maskTiles[key];
    if (maskTile.empty()) {
        maskTile.assign(TILE_BYTE_SIZE, 0);
    }

    const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
    maskTile[idx + 0] = 255;
    maskTile[idx + 1] = 255;
    maskTile[idx + 2] = 255;
    maskTile[idx + 3] = 255;
}

inline std::vector<uint8_t>& ensureResultTile(
    const TileGrid& grid, GpuFillResult& result, const TileKey& key)
{
    auto it = result.afterTiles.find(key);
    if (it != result.afterTiles.end()) {
        return it->second;
    }

    const uint32_t contentTileBytes = tileByteSize(grid.format());
    const TileData* sourceTile = grid.getTile(key);
    if (sourceTile) {
        auto& after = result.afterTiles[key];
        after.resize(contentTileBytes);
        std::memcpy(after.data(), sourceTile->pixels(), contentTileBytes);
        return after;
    }

    return result.afterTiles.emplace(key, std::vector<uint8_t>(contentTileBytes, 0)).first->second;
}

inline GpuFillResult convertFloodFillResult(FloodFillResult&& cpuResult)
{
    GpuFillResult gpuResult;
    gpuResult.beforeTiles = std::move(cpuResult.beforeTiles);
    gpuResult.afterTiles = std::move(cpuResult.afterTiles);
    gpuResult.fillMaskTiles = std::move(cpuResult.fillMaskTiles);
    gpuResult.createdTiles = std::move(cpuResult.createdTiles);
    gpuResult.removedTiles = std::move(cpuResult.removedTiles);
    gpuResult.pixelsFilled = cpuResult.pixelsFilled;

    gpuResult.affectedTiles.reserve(gpuResult.beforeTiles.size() + gpuResult.afterTiles.size()
        + gpuResult.fillMaskTiles.size());
    for (const auto& [key, _] : gpuResult.beforeTiles) {
        gpuResult.affectedTiles.insert(key);
    }
    for (const auto& [key, _] : gpuResult.afterTiles) {
        gpuResult.affectedTiles.insert(key);
    }
    for (const auto& [key, _] : gpuResult.fillMaskTiles) {
        gpuResult.affectedTiles.insert(key);
    }

    return gpuResult;
}

inline void clearGlErrors(QOpenGLFunctions_4_5_Core* gl)
{
    if (!gl) {
        return;
    }

    while (gl->glGetError() != GL_NO_ERROR) { }
}

inline bool consumeGlError(QOpenGLFunctions_4_5_Core* gl)
{
    if (!gl) {
        return false;
    }
    return gl->glGetError() != GL_NO_ERROR;
}

} // namespace

GLFillRenderer::GLFillRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLFillRenderer::~GLFillRenderer()
{
    shutdown();
}

Result<void> GLFillRenderer::initialize(const QString& shaderDir)
{
    if (m_initialized) {
        return Result<void>::ok();
    }
    if (!m_gl) {
        return Result<void>::ok();
    }

    m_blitProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto blitResult = m_blitProgram->loadFromFiles(
        shaderDir + "/fill_blit.vert.glsl", shaderDir + "/fill_blit.frag.glsl");
    if (!blitResult) {
        shutdown();
        return Result<void>::ok();
    }

    m_initProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto initResult = m_initProgram->loadComputeFromFile(shaderDir + "/fill_init.comp.glsl");
    if (!initResult) {
        shutdown();
        return Result<void>::ok();
    }

    m_expandProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto expandResult = m_expandProgram->loadComputeFromFile(shaderDir + "/fill_expand.comp.glsl");
    if (!expandResult) {
        shutdown();
        return Result<void>::ok();
    }

    m_prepareProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto prepareResult
        = m_prepareProgram->loadComputeFromFile(shaderDir + "/fill_prepare.comp.glsl");
    if (!prepareResult) {
        shutdown();
        return Result<void>::ok();
    }

    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        shutdown();
        return Result<void>::ok();
    }

    GLint maxTextureSize = 0;
    m_gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_maxTextureSize = std::max(maxTextureSize, 0);

    GLint64 maxShaderStorageBlockSize = 0;
    m_gl->glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxShaderStorageBlockSize);
    m_maxShaderStorageBlockBytes
        = maxShaderStorageBlockSize > 0 ? static_cast<size_t>(maxShaderStorageBlockSize) : 0;

    m_initialized = true;
    return Result<void>::ok();
}

void GLFillRenderer::shutdown()
{
    if (!m_gl) {
        m_blitProgram.reset();
        m_initProgram.reset();
        m_expandProgram.reset();
        m_prepareProgram.reset();
        m_initialized = false;
        return;
    }

    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_sourceTex) {
        m_gl->glDeleteTextures(1, &m_sourceTex);
        m_sourceTex = 0;
    }
    if (m_selectionTex) {
        m_gl->glDeleteTextures(1, &m_selectionTex);
        m_selectionTex = 0;
    }
    if (m_visitedTex) {
        m_gl->glDeleteTextures(1, &m_visitedTex);
        m_visitedTex = 0;
    }
    if (m_boundaryVisitedTex) {
        m_gl->glDeleteTextures(1, &m_boundaryVisitedTex);
        m_boundaryVisitedTex = 0;
    }
    if (m_frontierBuffers[0]) {
        m_gl->glDeleteBuffers(2, m_frontierBuffers);
        m_frontierBuffers[0] = 0;
        m_frontierBuffers[1] = 0;
    }
    if (m_counterBuffer) {
        m_gl->glDeleteBuffers(1, &m_counterBuffer);
        m_counterBuffer = 0;
    }
    if (m_dispatchBuffer) {
        m_gl->glDeleteBuffers(1, &m_dispatchBuffer);
        m_dispatchBuffer = 0;
    }
    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }

    m_blitProgram.reset();
    m_initProgram.reset();
    m_expandProgram.reset();
    m_prepareProgram.reset();
    m_cachedW = 0;
    m_cachedH = 0;
    m_frontierCapacity = 0;
    m_maxTextureSize = 0;
    m_maxShaderStorageBlockBytes = 0;
    m_initialized = false;
}

bool GLFillRenderer::ensureResources(int canvasW, int canvasH)
{
    if (!m_gl || canvasW <= 0 || canvasH <= 0) {
        return false;
    }

    if (m_maxTextureSize > 0 && (canvasW > m_maxTextureSize || canvasH > m_maxTextureSize)) {
        return false;
    }

    const size_t requiredPixels = static_cast<size_t>(canvasW) * static_cast<size_t>(canvasH);
    if (requiredPixels == 0) {
        return false;
    }

    const size_t frontierBytes = requiredPixels * sizeof(uint32_t);
    if (m_maxShaderStorageBlockBytes > 0 && frontierBytes > m_maxShaderStorageBlockBytes) {
        return false;
    }

    if (!m_fbo) {
        m_gl->glGenFramebuffers(1, &m_fbo);
    }

    if (m_cachedW != canvasW || m_cachedH != canvasH) {
        if (m_sourceTex) {
            m_gl->glDeleteTextures(1, &m_sourceTex);
            m_sourceTex = 0;
        }
        if (m_selectionTex) {
            m_gl->glDeleteTextures(1, &m_selectionTex);
            m_selectionTex = 0;
        }
        if (m_visitedTex) {
            m_gl->glDeleteTextures(1, &m_visitedTex);
            m_visitedTex = 0;
        }
        if (m_boundaryVisitedTex) {
            m_gl->glDeleteTextures(1, &m_boundaryVisitedTex);
            m_boundaryVisitedTex = 0;
        }
        m_cachedW = 0;
        m_cachedH = 0;
    }

    auto createTexture = [this](GLuint& tex, GLint internalFormat, GLenum format, GLenum type,
                             int width, int height) -> bool {
        if (tex != 0)
            return true;
        clearGlErrors(m_gl);
        m_gl->glGenTextures(1, &tex);
        if (tex == 0) {
            return false;
        }
        m_gl->glBindTexture(GL_TEXTURE_2D, tex);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gl->glTexImage2D(
            GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        if (consumeGlError(m_gl)) {
            m_gl->glDeleteTextures(1, &tex);
            tex = 0;
            return false;
        }
        return true;
    };

    if (!createTexture(m_sourceTex, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, canvasW, canvasH)
        || !createTexture(m_selectionTex, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, canvasW, canvasH)
        || !createTexture(m_visitedTex, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, canvasW, canvasH)
        || !createTexture(
            m_boundaryVisitedTex, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, canvasW, canvasH)) {
        return false;
    }

    if (m_frontierCapacity != requiredPixels) {
        if (m_frontierBuffers[0]) {
            m_gl->glDeleteBuffers(2, m_frontierBuffers);
            m_frontierBuffers[0] = 0;
            m_frontierBuffers[1] = 0;
        }

        clearGlErrors(m_gl);
        m_gl->glGenBuffers(2, m_frontierBuffers);
        if (m_frontierBuffers[0] == 0 || m_frontierBuffers[1] == 0) {
            if (m_frontierBuffers[0] || m_frontierBuffers[1]) {
                m_gl->glDeleteBuffers(2, m_frontierBuffers);
                m_frontierBuffers[0] = 0;
                m_frontierBuffers[1] = 0;
            }
            return false;
        }
        const GLsizeiptr frontierBufferBytes = static_cast<GLsizeiptr>(frontierBytes);
        for (GLuint& buffer : m_frontierBuffers) {
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            m_gl->glBufferData(
                GL_SHADER_STORAGE_BUFFER, frontierBufferBytes, nullptr, GL_DYNAMIC_DRAW);
            if (consumeGlError(m_gl)) {
                m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
                m_gl->glDeleteBuffers(2, m_frontierBuffers);
                m_frontierBuffers[0] = 0;
                m_frontierBuffers[1] = 0;
                m_frontierCapacity = 0;
                return false;
            }
        }
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        m_frontierCapacity = requiredPixels;
    }

    if (!m_counterBuffer) {
        clearGlErrors(m_gl);
        m_gl->glGenBuffers(1, &m_counterBuffer);
        if (m_counterBuffer == 0) {
            return false;
        }
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
        m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(sizeof(CounterState)),
            nullptr, GL_DYNAMIC_DRAW);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        if (consumeGlError(m_gl)) {
            m_gl->glDeleteBuffers(1, &m_counterBuffer);
            m_counterBuffer = 0;
            return false;
        }
    }

    if (!m_dispatchBuffer) {
        const std::array<uint32_t, 3> initialDispatch { 1u, 1u, 1u };
        clearGlErrors(m_gl);
        m_gl->glGenBuffers(1, &m_dispatchBuffer);
        if (m_dispatchBuffer == 0) {
            return false;
        }
        m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatchBuffer);
        m_gl->glBufferData(GL_DISPATCH_INDIRECT_BUFFER,
            static_cast<GLsizeiptr>(sizeof(initialDispatch)), initialDispatch.data(),
            GL_DYNAMIC_DRAW);
        m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
        if (consumeGlError(m_gl)) {
            m_gl->glDeleteBuffers(1, &m_dispatchBuffer);
            m_dispatchBuffer = 0;
            return false;
        }
    }

    m_cachedW = canvasW;
    m_cachedH = canvasH;
    return true;
}

void GLFillRenderer::blitGridToTexture(const TileGrid& grid, GLTileRenderer* tileRenderer,
    int textureW, int textureH, int originX, int originY, GLuint targetTex)
{
    if (!m_gl || !m_blitProgram || !m_blitProgram->isValid() || !tileRenderer || targetTex == 0
        || m_fbo == 0) {
        return;
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTex, 0);
    m_gl->glViewport(0, 0, textureW, textureH);
    m_gl->glDisable(GL_SCISSOR_TEST);
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_STENCIL_TEST);
    m_gl->glDisable(GL_BLEND);
    m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_blitProgram->use();
    m_blitProgram->setUniform(
        "uCanvasSize", static_cast<float>(textureW), static_cast<float>(textureH));
    m_blitProgram->setUniform("uTileSize", static_cast<float>(TILE_SIZE));
    m_blitProgram->setUniform("uTileTexture", 0);

    m_gl->glBindVertexArray(m_emptyVAO);

    const int minTileX = floorDiv(originX, static_cast<int32_t>(TILE_SIZE));
    const int minTileY = floorDiv(originY, static_cast<int32_t>(TILE_SIZE));
    const int maxTileX = floorDiv(originX + textureW - 1, static_cast<int32_t>(TILE_SIZE));
    const int maxTileY = floorDiv(originY + textureH - 1, static_cast<int32_t>(TILE_SIZE));

    for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
        for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
            TileData* tilePtr = const_cast<TileData*>(grid.getTile(TileKey { tileX, tileY }));
            if (!tilePtr) {
                continue;
            }

            const int worldX = tileX * static_cast<int32_t>(TILE_SIZE);
            const int worldY = tileY * static_cast<int32_t>(TILE_SIZE);

            const bool hadTexture = tilePtr->hasTexture();
            tileRenderer->ensureTileTexture(*tilePtr);
            if (!tilePtr->hasTexture()) {
                continue;
            }
            if (!hadTexture || tilePtr->isDirty()) {
                tileRenderer->uploadTileData(*tilePtr);
            }

            m_blitProgram->setUniform("uTileOrigin", static_cast<float>(worldX - originX),
                static_cast<float>(worldY - originY));

            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_2D, tilePtr->textureId());
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glBindVertexArray(0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLFillRenderer::blitLayerToTexture(TileGrid& grid, GLTileRenderer* tileRenderer, int textureW,
    int textureH, int originX, int originY)
{
    blitGridToTexture(grid, tileRenderer, textureW, textureH, originX, originY, m_sourceTex);
}

GpuFillResult GLFillRenderer::previewFill(TileGrid& layerGrid, GLTileRenderer* tileRenderer,
    int seedX, int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const TileGrid* selectionMask, int canvasWidth, int canvasHeight)
{
    return fillInternal(layerGrid, tileRenderer, seedX, seedY, fillR, fillG, fillB, fillA,
        selectionMask, canvasWidth, canvasHeight, false);
}

bool GLFillRenderer::prewarmPreviewResources(int canvasWidth, int canvasHeight)
{
    if (!m_initialized || !m_gl) {
        return false;
    }

    const int initialWindowSize = (kInitialWindowTileRadius * 2 + 1) * static_cast<int>(TILE_SIZE);
    const int prewarmWidth = std::max(
        1, std::min(initialWindowSize, canvasWidth > 0 ? canvasWidth : initialWindowSize));
    const int prewarmHeight = std::max(
        1, std::min(initialWindowSize, canvasHeight > 0 ? canvasHeight : initialWindowSize));
    return ensureResources(prewarmWidth, prewarmHeight);
}

GpuFillResult GLFillRenderer::fill(TileGrid& layerGrid, GLTileRenderer* tileRenderer, int seedX,
    int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const TileGrid* selectionMask, int canvasWidth, int canvasHeight)
{
    return fillInternal(layerGrid, tileRenderer, seedX, seedY, fillR, fillG, fillB, fillA,
        selectionMask, canvasWidth, canvasHeight, true);
}

GpuFillResult GLFillRenderer::fillInternal(TileGrid& layerGrid, GLTileRenderer* tileRenderer,
    int seedX, int seedY, uint8_t fillR, uint8_t fillG, uint8_t fillB, uint8_t fillA,
    const TileGrid* selectionMask, int canvasWidth, int canvasHeight, bool applyToLayer)
{
    GpuFillResult result;

    // CONTENT tiles adopt the target grid's per-document format (mask-target
    // fills pass an RGBA8 grid → this is RGBA8). MASK/coverage tiles stay RGBA8.
    const TilePixelFormat contentFormat = layerGrid.format();
    const uint32_t contentTileBytes = tileByteSize(contentFormat);

    if (!m_initialized || !m_gl || !tileRenderer) {
        return result;
    }
    if (seedX < 0 || seedX >= canvasWidth || seedY < 0 || seedY >= canvasHeight) {
        return result;
    }

    auto fallbackFloodFillResult = [&]() -> GpuFillResult {
        if (!applyToLayer) {
            return result;
        }
        return convertFloodFillResult(floodFill(layerGrid, seedX, seedY, fillR, fillG, fillB, fillA,
            selectionMask, canvasWidth, canvasHeight));
    };

    const size_t canvasPixels
        = static_cast<size_t>(canvasWidth) * static_cast<size_t>(canvasHeight);
    if (canvasPixels == 0) {
        return fallbackFloodFillResult();
    }

    uint8_t seedR = 0;
    uint8_t seedG = 0;
    uint8_t seedB = 0;
    uint8_t seedA = 0;
    if (!samplePixelAt(layerGrid, seedX, seedY, seedR, seedG, seedB, seedA)) {
        return result;
    }

    if (colorsMatch(seedR, seedG, seedB, seedA, fillR, fillG, fillB, fillA)) {
        return result;
    }

    const bool useSelectionMask = selectionMask && !selectionMask->empty();
    if (useSelectionMask && fillMaskAlphaAt(selectionMask, seedX, seedY) == 0) {
        return result;
    }

    const PremultPixel seedPixel { seedR, seedG, seedB, seedA };
    const FillSemanticMode fillMode = fillModeForSeed(seedPixel);
    FillDomain domain { 0, 0, canvasWidth, canvasHeight };

    auto clampDomain = [&](int originX, int originY, int width, int height) -> FillDomain {
        FillDomain clamped;
        clamped.width = std::clamp(width, 1, canvasWidth);
        clamped.height = std::clamp(height, 1, canvasHeight);
        clamped.originX = std::clamp(originX, 0, std::max(canvasWidth - clamped.width, 0));
        clamped.originY = std::clamp(originY, 0, std::max(canvasHeight - clamped.height, 0));
        return clamped;
    };

    auto expandBoundsByTile = [&](float minX, float minY, float maxX, float maxY) -> FillDomain {
        const int padding = static_cast<int>(TILE_SIZE);
        const int boundedMinX = static_cast<int>(std::floor(minX)) - padding;
        const int boundedMinY = static_cast<int>(std::floor(minY)) - padding;
        const int boundedMaxX = static_cast<int>(std::ceil(maxX)) + padding;
        const int boundedMaxY = static_cast<int>(std::ceil(maxY)) + padding;
        return clampDomain(
            boundedMinX, boundedMinY, boundedMaxX - boundedMinX, boundedMaxY - boundedMinY);
    };

    if (useSelectionMask) {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        if (selectionMask->computeBounds(minX, minY, maxX, maxY)) {
            domain = clampDomain(static_cast<int>(std::floor(minX)),
                static_cast<int>(std::floor(minY)), static_cast<int>(std::ceil(maxX - minX)),
                static_cast<int>(std::ceil(maxY - minY)));
        }
    } else {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        if (layerGrid.computeBounds(minX, minY, maxX, maxY)) {
            const FillDomain contentDomain = expandBoundsByTile(minX, minY, maxX, maxY);
            const bool seedInsideContentDomain = seedX >= contentDomain.originX
                && seedY >= contentDomain.originY
                && seedX < contentDomain.originX + contentDomain.width
                && seedY < contentDomain.originY + contentDomain.height;
            if (seedInsideContentDomain && fillMode == FillSemanticMode::Stroke) {
                domain = contentDomain;
            }
        }
    }

    const auto clearVisitedTexture = [&](GLuint texture) {
        const uint32_t zero = 0;
        m_gl->glClearTexImage(texture, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    };

    auto uploadCounters = [&](CounterState counters) {
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
        m_gl->glBufferSubData(
            GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(CounterState)), &counters);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    };

    auto resetDispatchBuffer = [&]() {
        const std::array<uint32_t, 3> initialDispatch { 1u, 1u, 1u };
        m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatchBuffer);
        m_gl->glBufferSubData(GL_DISPATCH_INDIRECT_BUFFER, 0,
            static_cast<GLsizeiptr>(sizeof(initialDispatch)), initialDispatch.data());
        m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    };

    auto configureFloodUniforms
        = [&](GLShaderProgram* program, const FillWindow& window, int localSeedX, int localSeedY,
              int threshold, FloodSeedMode seedMode) {
              program->use();
              program->setUniform("uSourceLayer", 0);
              program->setUniform("uSelectionMask", 1);
              program->setUniform("uCanvasSize", window.width, window.height);
              program->setUniform("uSeedPixel", localSeedX, localSeedY);
              program->setUniform(
                  "uWindowOrigin", window.originX, canvasHeight - window.originY - window.height);
              program->setUniform(
                  "uDomainOrigin", domain.originX, canvasHeight - domain.originY - domain.height);
              program->setUniform("uDomainSize", domain.width, domain.height);
              program->setUniform("uThreshold", threshold);
              program->setUniform("uSeedMode", static_cast<int>(seedMode));
              program->setUniform("uFillMode", static_cast<int>(fillMode));
              program->setUniform("uUseSelectionMask", useSelectionMask ? 1 : 0);
              program->setUniform("uSeedColor", static_cast<float>(seedR) / 255.0f,
                  static_cast<float>(seedG) / 255.0f, static_cast<float>(seedB) / 255.0f,
                  static_cast<float>(seedA) / 255.0f);
          };

    auto runFlood = [&](const FillWindow& window, int localSeedX, int localSeedY, GLuint visitedTex,
                        int threshold, FloodSeedMode seedMode, CounterState& outCounters) -> bool {
        clearVisitedTexture(visitedTex);

        CounterState counters;
        counters.minX = static_cast<uint32_t>(window.width);
        counters.minY = static_cast<uint32_t>(window.height);
        uploadCounters(counters);
        resetDispatchBuffer();

        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_sourceTex);
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_selectionTex);

        configureFloodUniforms(
            m_initProgram.get(), window, localSeedX, localSeedY, threshold, seedMode);
        m_gl->glBindImageTexture(0, visitedTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_frontierBuffers[0]);
        m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_counterBuffer);
        m_gl->glDispatchCompute(
            (static_cast<uint32_t>(window.width) + kInitWorkGroupSizeX - 1u) / kInitWorkGroupSizeX,
            (static_cast<uint32_t>(window.height) + kInitWorkGroupSizeY - 1u) / kInitWorkGroupSizeY,
            1u);
        m_gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
        m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLintptr>(offsetof(CounterState, currentCount)),
            static_cast<GLsizeiptr>(sizeof(uint32_t)), &counters.currentCount);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        if (counters.currentCount == 0u) {
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
            m_gl->glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
            m_gl->glActiveTexture(GL_TEXTURE1);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            outCounters = counters;
            return true;
        }

        const std::array<uint32_t, 3> initialDispatch {
            std::max((counters.currentCount + kWorkGroupSize - 1u) / kWorkGroupSize, 1u), 1u, 1u
        };
        m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatchBuffer);
        m_gl->glBufferSubData(GL_DISPATCH_INDIRECT_BUFFER, 0,
            static_cast<GLsizeiptr>(sizeof(initialDispatch)), initialDispatch.data());
        m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);

        int currentFrontier = 0;
        int nextFrontier = 1;
        const uint32_t maxPasses
            = static_cast<uint32_t>(window.width) * static_cast<uint32_t>(window.height);
        for (uint32_t pass = 0; pass < maxPasses; ++pass) {
            configureFloodUniforms(
                m_expandProgram.get(), window, localSeedX, localSeedY, threshold, seedMode);
            m_gl->glBindImageTexture(0, visitedTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_frontierBuffers[currentFrontier]);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_frontierBuffers[nextFrontier]);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_counterBuffer);
            m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatchBuffer);
            m_gl->glDispatchComputeIndirect(0);
            m_gl->glMemoryBarrier(
                GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            m_prepareProgram->use();
            m_prepareProgram->setUniform("uWorkGroupSize", static_cast<int>(kWorkGroupSize));
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_counterBuffer);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_dispatchBuffer);
            m_gl->glDispatchCompute(1, 1, 1);
            m_gl->glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
            m_gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);

            std::swap(currentFrontier, nextFrontier);

            const bool shouldPoll = ((pass + 1u) % kGpuBatchPasses) == 0u || pass + 1u == maxPasses;
            if (!shouldPoll) {
                continue;
            }

            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
            m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                static_cast<GLintptr>(offsetof(CounterState, currentCount)),
                static_cast<GLsizeiptr>(sizeof(uint32_t)), &counters.currentCount);
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            if (counters.currentCount == 0u) {
                break;
            }
        }

        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
        m_gl->glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(CounterState)), &counters);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
        m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
        m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
        m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
        m_gl->glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        outCounters = counters;
        return true;
    };

    auto clampWindow = [&](int originX, int originY, int width, int height) -> FillWindow {
        FillWindow window;
        window.width = std::clamp(width, 1, domain.width);
        window.height = std::clamp(height, 1, domain.height);
        window.originX
            = std::clamp(originX, domain.originX, domain.originX + domain.width - window.width);
        window.originY
            = std::clamp(originY, domain.originY, domain.originY + domain.height - window.height);
        return window;
    };

    auto expandWindow = [&](const FillWindow& current) -> FillWindow {
        if (current.width >= domain.width && current.height >= domain.height) {
            return current;
        }

        const int growTilesX = std::max(1, current.width / static_cast<int>(TILE_SIZE));
        const int growTilesY = std::max(1, current.height / static_cast<int>(TILE_SIZE));
        const int growX = growTilesX * static_cast<int>(TILE_SIZE);
        const int growY = growTilesY * static_cast<int>(TILE_SIZE);
        const int newOriginX = std::max(domain.originX, current.originX - growX / 2);
        const int newOriginY = std::max(domain.originY, current.originY - growY / 2);
        const int newWidth
            = std::min(domain.originX + domain.width - newOriginX, current.width + growX);
        const int newHeight
            = std::min(domain.originY + domain.height - newOriginY, current.height + growY);
        return clampWindow(newOriginX, newOriginY, newWidth, newHeight);
    };

    enum class GpuWindowSupport { Supported, Empty, TextureLimit, StorageBlockLimit };

    auto gpuWindowSupport = [&](const FillWindow& candidate) -> GpuWindowSupport {
        const size_t pixels
            = static_cast<size_t>(candidate.width) * static_cast<size_t>(candidate.height);
        if (pixels == 0) {
            return GpuWindowSupport::Empty;
        }
        if (m_maxTextureSize > 0
            && (candidate.width > m_maxTextureSize || candidate.height > m_maxTextureSize)) {
            return GpuWindowSupport::TextureLimit;
        }
        if (m_maxShaderStorageBlockBytes > 0
            && pixels > (m_maxShaderStorageBlockBytes / sizeof(uint32_t))) {
            return GpuWindowSupport::StorageBlockLimit;
        }
        return GpuWindowSupport::Supported;
    };

    auto isGpuWindowSupported = [&](const FillWindow& candidate) -> bool {
        return gpuWindowSupport(candidate) == GpuWindowSupport::Supported;
    };

    auto logGpuWindowFallback = [&](const FillWindow& candidate, const char* stage) {
        switch (gpuWindowSupport(candidate)) {
        case GpuWindowSupport::Supported:
            return;
        case GpuWindowSupport::Empty:
            return;
        case GpuWindowSupport::TextureLimit:
            return;
        case GpuWindowSupport::StorageBlockLimit:
            return;
        }
    };

    const int initialWindowSize = (kInitialWindowTileRadius * 2 + 1) * static_cast<int>(TILE_SIZE);
    FillWindow window = clampWindow(seedX - initialWindowSize / 2, seedY - initialWindowSize / 2,
        initialWindowSize, initialWindowSize);
    if (!isGpuWindowSupported(window)) {
        logGpuWindowFallback(window, "initial-window");
        return fallbackFloodFillResult();
    }

    int escapeLevel = 0;
    int interiorThreshold = 0;
    CounterState sourceCounters;

    for (;;) {
        if (!ensureResources(window.width, window.height) || m_sourceTex == 0 || m_selectionTex == 0
            || m_visitedTex == 0 || m_boundaryVisitedTex == 0 || m_counterBuffer == 0
            || m_frontierBuffers[0] == 0 || m_dispatchBuffer == 0 || !m_initProgram
            || !m_initProgram->isValid() || !m_expandProgram || !m_expandProgram->isValid()
            || !m_prepareProgram || !m_prepareProgram->isValid()) {
            return fallbackFloodFillResult();
        }

        blitLayerToTexture(
            layerGrid, tileRenderer, window.width, window.height, window.originX, window.originY);
        if (useSelectionMask) {
            blitGridToTexture(*selectionMask, tileRenderer, window.width, window.height,
                window.originX, window.originY, m_selectionTex);
        }

        const int localSeedX = seedX - window.originX;
        const int localSeedY = window.height - 1 - (seedY - window.originY);

        bool expanded = false;
        if (fillMode == FillSemanticMode::Stroke) {
            interiorThreshold = kInteriorSeedTolerance;
            runFlood(window, localSeedX, localSeedY, m_visitedTex, interiorThreshold,
                FloodSeedMode::SeedPixel, sourceCounters);
            if (sourceCounters.touchesWindowBoundary != 0u
                && (window.width < domain.width || window.height < domain.height)) {
                const FillWindow nextWindow = expandWindow(window);
                if (!isGpuWindowSupported(nextWindow)) {
                    logGpuWindowFallback(nextWindow, "stroke-expand");
                    return fallbackFloodFillResult();
                }
                if (nextWindow.originX != window.originX || nextWindow.originY != window.originY
                    || nextWindow.width != window.width || nextWindow.height != window.height) {
                    window = nextWindow;
                    expanded = true;
                }
            }
            if (expanded) {
                continue;
            }
            escapeLevel = kInteriorSeedTolerance + 1;
        } else {
            int escapeLow = 0;
            int escapeHigh = 255;
            CounterState probeCounters;
            while (escapeLow < escapeHigh) {
                const int mid = escapeLow + ((escapeHigh - escapeLow) / 2);
                runFlood(window, localSeedX, localSeedY, m_visitedTex, mid,
                    FloodSeedMode::SeedPixel, probeCounters);
                if (probeCounters.touchesWindowBoundary != 0u
                    && (window.width < domain.width || window.height < domain.height)) {
                    const FillWindow nextWindow = expandWindow(window);
                    if (!isGpuWindowSupported(nextWindow)) {
                        logGpuWindowFallback(nextWindow, "escape-search-expand");
                        return fallbackFloodFillResult();
                    }
                    if (nextWindow.originX != window.originX || nextWindow.originY != window.originY
                        || nextWindow.width != window.width || nextWindow.height != window.height) {
                        window = nextWindow;
                        expanded = true;
                        break;
                    }
                }

                if (probeCounters.touchesBoundary != 0u) {
                    escapeHigh = mid;
                } else {
                    escapeLow = mid + 1;
                }
            }
            if (expanded) {
                continue;
            }

            escapeLevel = escapeLow;
            interiorThreshold = std::max(escapeLevel - 1, 0);

            runFlood(window, localSeedX, localSeedY, m_visitedTex, interiorThreshold,
                FloodSeedMode::SeedPixel, sourceCounters);
            if (sourceCounters.touchesWindowBoundary != 0u
                && (window.width < domain.width || window.height < domain.height)) {
                const FillWindow nextWindow = expandWindow(window);
                if (!isGpuWindowSupported(nextWindow)) {
                    logGpuWindowFallback(nextWindow, "interior-expand");
                    return fallbackFloodFillResult();
                }
                if (nextWindow.originX != window.originX || nextWindow.originY != window.originY
                    || nextWindow.width != window.width || nextWindow.height != window.height) {
                    window = nextWindow;
                    continue;
                }
            }
        }
        break;
    }

    if (sourceCounters.pixelsFilled == 0
        || sourceCounters.minX >= static_cast<uint32_t>(window.width)
        || sourceCounters.minY >= static_cast<uint32_t>(window.height)) {
        return result;
    }

    if (fillMode == FillSemanticMode::Exterior && interiorThreshold > 0 && useSelectionMask) {
        CounterState discardedBoundaryCounters;
        runFlood(window, seedX - window.originX, window.height - 1 - (seedY - window.originY),
            m_boundaryVisitedTex, interiorThreshold, FloodSeedMode::Boundary,
            discardedBoundaryCounters);
    } else {
        clearVisitedTexture(m_boundaryVisitedTex);
    }

    const int readX = static_cast<int>(sourceCounters.minX);
    const int readY = static_cast<int>(sourceCounters.minY);
    const int readW = static_cast<int>(sourceCounters.maxX - sourceCounters.minX + 1u);
    const int readH = static_cast<int>(sourceCounters.maxY - sourceCounters.minY + 1u);
    if (readW <= 0 || readH <= 0) {
        return result;
    }

    GpuFillResult::RawTileMap directMaskTiles;
    GpuFillResult::RawTileMap preservedMaskTiles;
    std::unordered_set<uint64_t> acceptedPixels;
    acceptedPixels.reserve(static_cast<size_t>(readW) * static_cast<size_t>(readH));
    bool acceptedTouchesCanvasBoundary = false;
    const int windowBottomY = canvasHeight - window.originY - window.height;
    const int worldReadMinY = canvasHeight - 1 - (windowBottomY + readY + readH - 1);
    const int worldReadMaxY = canvasHeight - 1 - (windowBottomY + readY);
    const int minTileX = floorDiv(window.originX + readX, static_cast<int32_t>(TILE_SIZE));
    const int minTileY = floorDiv(worldReadMinY, static_cast<int32_t>(TILE_SIZE));
    const int maxTileX
        = floorDiv(window.originX + readX + readW - 1, static_cast<int32_t>(TILE_SIZE));
    const int maxTileY = floorDiv(worldReadMaxY, static_cast<int32_t>(TILE_SIZE));

    auto readMaskRect = [&](GLuint texture, int texX, int texY, int texW, int texH,
                            std::vector<uint32_t>& outMask) {
        outMask.assign(static_cast<size_t>(texW) * static_cast<size_t>(texH), 0u);
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        m_gl->glReadPixels(texX, texY, texW, texH, GL_RED_INTEGER, GL_UNSIGNED_INT, outMask.data());
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };

    std::vector<uint32_t> sourceMaskTile;
    std::vector<uint32_t> boundaryMaskTile;
    sourceMaskTile.reserve(static_cast<size_t>(TILE_SIZE) * static_cast<size_t>(TILE_SIZE));
    boundaryMaskTile.reserve(static_cast<size_t>(TILE_SIZE) * static_cast<size_t>(TILE_SIZE));
    auto packCoord = [](int32_t x, int32_t y) -> uint64_t {
        return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(x);
    };

    for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
        const int worldTileMinY = tileY * static_cast<int>(TILE_SIZE);
        const int worldTileMaxY = worldTileMinY + static_cast<int>(TILE_SIZE) - 1;
        const int clippedWorldMinY = std::max(worldTileMinY, worldReadMinY);
        const int clippedWorldMaxY = std::min(worldTileMaxY, worldReadMaxY);
        if (clippedWorldMinY > clippedWorldMaxY) {
            continue;
        }

        const int texMinY = canvasHeight - 1 - clippedWorldMaxY - windowBottomY;
        const int texH = clippedWorldMaxY - clippedWorldMinY + 1;

        for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
            const int worldTileMinX = tileX * static_cast<int>(TILE_SIZE);
            const int worldTileMaxX = worldTileMinX + static_cast<int>(TILE_SIZE) - 1;
            const int clippedWorldMinX = std::max(worldTileMinX, window.originX + readX);
            const int clippedWorldMaxX
                = std::min(worldTileMaxX, window.originX + readX + readW - 1);
            if (clippedWorldMinX > clippedWorldMaxX) {
                continue;
            }

            const int texMinX = clippedWorldMinX - window.originX;
            const int texW = clippedWorldMaxX - clippedWorldMinX + 1;

            readMaskRect(m_visitedTex, texMinX, texMinY, texW, texH, sourceMaskTile);

            bool hasAnySource = false;
            for (uint32_t value : sourceMaskTile) {
                if (value != 0u) {
                    hasAnySource = true;
                    break;
                }
            }
            if (!hasAnySource) {
                continue;
            }

            readMaskRect(m_boundaryVisitedTex, texMinX, texMinY, texW, texH, boundaryMaskTile);

            for (int localTexY = 0; localTexY < texH; ++localTexY) {
                const int worldY = clippedWorldMaxY - localTexY;
                for (int localTexX = 0; localTexX < texW; ++localTexX) {
                    const size_t maskIdx
                        = static_cast<size_t>(localTexY) * static_cast<size_t>(texW)
                        + static_cast<size_t>(localTexX);
                    if (sourceMaskTile[maskIdx] == 0u || boundaryMaskTile[maskIdx] != 0u) {
                        continue;
                    }

                    const int worldX = window.originX + texMinX + localTexX;
                    uint8_t originalR = 0;
                    uint8_t originalG = 0;
                    uint8_t originalB = 0;
                    uint8_t originalA = 0;
                    if (!samplePixelAt(layerGrid, worldX, worldY, originalR, originalG, originalB,
                            originalA)) {
                        continue;
                    }

                    const PremultPixel originalPx { originalR, originalG, originalB, originalA };
                    const TileKey key { floorDiv(worldX, static_cast<int32_t>(TILE_SIZE)),
                        floorDiv(worldY, static_cast<int32_t>(TILE_SIZE)) };
                    const uint32_t localX = floorMod(worldX, static_cast<int32_t>(TILE_SIZE));
                    const uint32_t localY = floorMod(worldY, static_cast<int32_t>(TILE_SIZE));
                    acceptedPixels.insert(packCoord(worldX, worldY));
                    acceptedTouchesCanvasBoundary = acceptedTouchesCanvasBoundary || worldX == 0
                        || worldY == 0 || worldX + 1 == canvasWidth || worldY + 1 == canvasHeight;

                    if (fillMode == FillSemanticMode::Stroke) {
                        if (originalA == 255) {
                            writeMaskPixel(directMaskTiles, key, localX, localY);
                        } else {
                            writeMaskPixel(preservedMaskTiles, key, localX, localY);
                        }
                    } else if (fillDistance(originalPx, seedPixel, fillMode)
                        <= kInteriorSeedTolerance) {
                        writeMaskPixel(directMaskTiles, key, localX, localY);
                    } else {
                        writeMaskPixel(preservedMaskTiles, key, localX, localY);
                    }
                }
            }
        }
    }

    if (fillMode == FillSemanticMode::Exterior && !useSelectionMask && acceptedTouchesCanvasBoundary
        && !acceptedPixels.empty()) {
        std::unordered_map<uint64_t, uint8_t> distanceCache;
        distanceCache.reserve(acceptedPixels.size() * 2 + 256);
        std::array<std::deque<uint64_t>, 256> rampFrontier;
        std::unordered_map<uint64_t, uint8_t> rampBestLevel;
        rampBestLevel.reserve(acceptedPixels.size() * 2 + 256);

        auto withinCanvas = [&](int32_t x, int32_t y) -> bool {
            return x >= 0 && x < canvasWidth && y >= 0 && y < canvasHeight;
        };

        auto pixelDistanceAt = [&](int32_t x, int32_t y) -> uint8_t {
            const uint64_t key = packCoord(x, y);
            auto it = distanceCache.find(key);
            if (it != distanceCache.end()) {
                return it->second;
            }

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 0;
            samplePixelAt(layerGrid, x, y, r, g, b, a);
            const uint8_t distance = (fillMode == FillSemanticMode::Stroke && a == 0)
                ? std::numeric_limits<uint8_t>::max()
                : static_cast<uint8_t>(
                      fillDistance(PremultPixel { r, g, b, a }, seedPixel, fillMode));
            distanceCache.emplace(key, distance);
            return distance;
        };

        auto tryQueueRamp = [&](int32_t x, int32_t y, uint8_t minLevel) {
            if (!withinCanvas(x, y)) {
                return;
            }

            const uint64_t key = packCoord(x, y);
            if (acceptedPixels.count(key) > 0) {
                return;
            }

            const uint8_t level = pixelDistanceAt(x, y);
            if (level < minLevel || level <= kInteriorSeedTolerance) {
                return;
            }

            auto [it, inserted] = rampBestLevel.emplace(key, level);
            if (!inserted && level >= it->second) {
                return;
            }
            it->second = level;
            rampFrontier[level].push_back(key);
        };

        for (uint64_t key : acceptedPixels) {
            const int32_t x = static_cast<int32_t>(static_cast<uint32_t>(key));
            const int32_t y = static_cast<int32_t>(static_cast<uint32_t>(key >> 32));
            const uint8_t level = pixelDistanceAt(x, y);
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    tryQueueRamp(x + ox, y + oy, level);
                }
            }
        }

        for (int level = 0; level < 256; ++level) {
            auto& frontier = rampFrontier[static_cast<size_t>(level)];
            while (!frontier.empty()) {
                const uint64_t key = frontier.front();
                frontier.pop_front();

                auto bestIt = rampBestLevel.find(key);
                if (bestIt == rampBestLevel.end()
                    || bestIt->second != static_cast<uint8_t>(level)) {
                    continue;
                }
                if (acceptedPixels.count(key) > 0) {
                    continue;
                }

                const int32_t x = static_cast<int32_t>(static_cast<uint32_t>(key));
                const int32_t y = static_cast<int32_t>(static_cast<uint32_t>(key >> 32));
                bool hasHigherAllowedNeighbor = false;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }

                        const int32_t nx = x + ox;
                        const int32_t ny = y + oy;
                        if (!withinCanvas(nx, ny)) {
                            continue;
                        }
                        if (pixelDistanceAt(nx, ny) > static_cast<uint8_t>(level)) {
                            hasHigherAllowedNeighbor = true;
                            break;
                        }
                    }
                    if (hasHigherAllowedNeighbor) {
                        break;
                    }
                }

                uint8_t originalR = 0;
                uint8_t originalG = 0;
                uint8_t originalB = 0;
                uint8_t originalA = 0;
                samplePixelAt(layerGrid, x, y, originalR, originalG, originalB, originalA);
                const bool touchesCanvasBoundary
                    = x == 0 || y == 0 || x + 1 == canvasWidth || y + 1 == canvasHeight;
                if (!hasHigherAllowedNeighbor && !(touchesCanvasBoundary && originalA < 255)) {
                    continue;
                }

                acceptedPixels.insert(key);
                const TileKey tileKey { floorDiv(x, static_cast<int32_t>(TILE_SIZE)),
                    floorDiv(y, static_cast<int32_t>(TILE_SIZE)) };
                const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
                const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
                writeMaskPixel(preservedMaskTiles, tileKey, localX, localY);

                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        tryQueueRamp(x + ox, y + oy, static_cast<uint8_t>(level));
                    }
                }
            }
        }

        std::deque<uint64_t> softBoundaryQueue;
        std::unordered_set<uint64_t> queuedSoftBoundary;
        queuedSoftBoundary.reserve(acceptedPixels.size() * 2 + 256);

        auto tryQueueSoftBoundary = [&](int32_t x, int32_t y) {
            if (!withinCanvas(x, y)) {
                return;
            }

            const uint64_t key = packCoord(x, y);
            if (acceptedPixels.count(key) > 0 || queuedSoftBoundary.count(key) > 0) {
                return;
            }

            uint8_t originalR = 0;
            uint8_t originalG = 0;
            uint8_t originalB = 0;
            uint8_t originalA = 0;
            samplePixelAt(layerGrid, x, y, originalR, originalG, originalB, originalA);
            if (originalA == 0 || originalA == 255) {
                return;
            }

            queuedSoftBoundary.insert(key);
            softBoundaryQueue.push_back(key);
        };
        auto tryQueueCanvasSoftBoundarySeed = [&](int32_t x, int32_t y) {
            if (!withinCanvas(x, y)) {
                return;
            }

            const uint64_t key = packCoord(x, y);
            if (acceptedPixels.count(key) > 0 || queuedSoftBoundary.count(key) > 0) {
                return;
            }

            bool hasBoundaryAcceptedNeighbor = false;
            if (y == 0 || y + 1 == canvasHeight) {
                if (x > 0) {
                    hasBoundaryAcceptedNeighbor = hasBoundaryAcceptedNeighbor
                        || acceptedPixels.count(packCoord(x - 1, y)) > 0;
                }
                if (x + 1 < canvasWidth) {
                    hasBoundaryAcceptedNeighbor = hasBoundaryAcceptedNeighbor
                        || acceptedPixels.count(packCoord(x + 1, y)) > 0;
                }
            }
            if (x == 0 || x + 1 == canvasWidth) {
                if (y > 0) {
                    hasBoundaryAcceptedNeighbor = hasBoundaryAcceptedNeighbor
                        || acceptedPixels.count(packCoord(x, y - 1)) > 0;
                }
                if (y + 1 < canvasHeight) {
                    hasBoundaryAcceptedNeighbor = hasBoundaryAcceptedNeighbor
                        || acceptedPixels.count(packCoord(x, y + 1)) > 0;
                }
            }
            if (!hasBoundaryAcceptedNeighbor) {
                return;
            }

            uint8_t originalR = 0;
            uint8_t originalG = 0;
            uint8_t originalB = 0;
            uint8_t originalA = 0;
            samplePixelAt(layerGrid, x, y, originalR, originalG, originalB, originalA);
            if (originalA == 0 || originalA == 255) {
                return;
            }

            const uint8_t level = pixelDistanceAt(x, y);
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }

                    const int32_t nx = x + ox;
                    const int32_t ny = y + oy;
                    if (!withinCanvas(nx, ny)) {
                        continue;
                    }
                    if (pixelDistanceAt(nx, ny) < level) {
                        return;
                    }
                }
            }

            queuedSoftBoundary.insert(key);
            softBoundaryQueue.push_back(key);
        };

        for (uint64_t key : acceptedPixels) {
            const int32_t x = static_cast<int32_t>(static_cast<uint32_t>(key));
            const int32_t y = static_cast<int32_t>(static_cast<uint32_t>(key >> 32));
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    tryQueueSoftBoundary(x + ox, y + oy);
                }
            }
        }

        for (int32_t x = 0; x < canvasWidth; ++x) {
            tryQueueCanvasSoftBoundarySeed(x, 0);
            if (canvasHeight > 1) {
                tryQueueCanvasSoftBoundarySeed(x, canvasHeight - 1);
            }
        }
        for (int32_t y = 1; y + 1 < canvasHeight; ++y) {
            tryQueueCanvasSoftBoundarySeed(0, y);
            if (canvasWidth > 1) {
                tryQueueCanvasSoftBoundarySeed(canvasWidth - 1, y);
            }
        }

        while (!softBoundaryQueue.empty()) {
            const uint64_t key = softBoundaryQueue.front();
            softBoundaryQueue.pop_front();
            if (acceptedPixels.count(key) > 0) {
                continue;
            }

            const int32_t x = static_cast<int32_t>(static_cast<uint32_t>(key));
            const int32_t y = static_cast<int32_t>(static_cast<uint32_t>(key >> 32));
            acceptedPixels.insert(key);
            const TileKey tileKey { floorDiv(x, static_cast<int32_t>(TILE_SIZE)),
                floorDiv(y, static_cast<int32_t>(TILE_SIZE)) };
            const uint32_t localX = floorMod(x, static_cast<int32_t>(TILE_SIZE));
            const uint32_t localY = floorMod(y, static_cast<int32_t>(TILE_SIZE));
            writeMaskPixel(preservedMaskTiles, tileKey, localX, localY);
            const uint8_t level = pixelDistanceAt(x, y);

            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }

                    const int32_t nx = x + ox;
                    const int32_t ny = y + oy;
                    if (!withinCanvas(nx, ny)) {
                        continue;
                    }

                    const uint64_t neighborKey = packCoord(nx, ny);
                    if (acceptedPixels.count(neighborKey) > 0
                        || queuedSoftBoundary.count(neighborKey) > 0) {
                        continue;
                    }

                    uint8_t neighborR = 0;
                    uint8_t neighborG = 0;
                    uint8_t neighborB = 0;
                    uint8_t neighborA = 0;
                    samplePixelAt(layerGrid, nx, ny, neighborR, neighborG, neighborB, neighborA);
                    if (neighborA == 0 || neighborA == 255) {
                        continue;
                    }
                    if (pixelDistanceAt(nx, ny) < level) {
                        continue;
                    }

                    queuedSoftBoundary.insert(neighborKey);
                    softBoundaryQueue.push_back(neighborKey);
                }
            }
        }
    }

    for (const auto& [key, _] : directMaskTiles) {
        result.affectedTiles.insert(key);
    }
    for (const auto& [key, _] : preservedMaskTiles) {
        result.affectedTiles.insert(key);
    }
    if (result.affectedTiles.empty()) {
        return result;
    }

    for (const TileKey& key : result.affectedTiles) {
        const std::vector<uint8_t>* directTile = nullptr;
        const std::vector<uint8_t>* preservedTile = nullptr;
        const std::vector<uint8_t>* sourceTile = nullptr;

        if (auto it = directMaskTiles.find(key); it != directMaskTiles.end()) {
            directTile = &it->second;
        }
        if (auto it = preservedMaskTiles.find(key); it != preservedMaskTiles.end()) {
            preservedTile = &it->second;
        }
        const TileData* liveTile = layerGrid.getTile(key);
        if (liveTile) {
            if (applyToLayer) {
                snapshotTileBeforeChange(layerGrid, result, key);
                sourceTile = &result.beforeTiles[key];
            } else {
                auto& previewSource = result.beforeTiles[key];
                previewSource.resize(contentTileBytes);
                std::memcpy(previewSource.data(), liveTile->pixels(), contentTileBytes);
                sourceTile = &previewSource;
            }
        } else if (!applyToLayer) {
            result.createdTiles.insert(key);
        }

        if (applyToLayer) {
            const bool existedBefore = liveTile != nullptr;
            TileData& tile = layerGrid.getOrCreateTile(key);
            if (!existedBefore) {
                result.createdTiles.insert(key);
            } else {
                layerGrid.markDirty(key);
            }

            for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
                for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                    if (!maskHasPixel(directTile, localX, localY)) {
                        continue;
                    }

                    writeContentPixelTile(tile, localX, localY, fillR, fillG, fillB, fillA);
                    writeMaskPixel(result, key, localX, localY);
                    ++result.pixelsFilled;
                }
            }

            for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
                for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                    if (!maskHasPixel(preservedTile, localX, localY)) {
                        continue;
                    }

                    const PremultPixel originalPx
                        = samplePixel(sourceTile, localX, localY, contentFormat);
                    const PremultPixel blendedPx = (fillMode == FillSemanticMode::Stroke)
                        ? recolorWithCoverage(originalPx, fillR, fillG, fillB, fillA)
                        : compositeUnder(originalPx, fillR, fillG, fillB, fillA);
                    writeContentPixelTile(
                        tile, localX, localY, blendedPx.r, blendedPx.g, blendedPx.b, blendedPx.a);
                    writeMaskPixel(result, key, localX, localY);
                    ++result.pixelsFilled;
                }
            }

            TileData* currentTile = layerGrid.getTile(key);
            if (!currentTile) {
                continue;
            }

            if (currentTile->isEmpty()) {
                result.afterTiles[key].assign(contentTileBytes, 0);
                if (result.createdTiles.count(key) > 0) {
                    result.createdTiles.erase(key);
                } else {
                    result.removedTiles.insert(key);
                }
                layerGrid.removeTile(key);
                continue;
            }

            auto& after = result.afterTiles[key];
            after.resize(contentTileBytes);
            std::memcpy(after.data(), currentTile->pixels(), contentTileBytes);
            continue;
        }

        std::vector<uint8_t>& previewTile = ensureResultTile(layerGrid, result, key);
        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                if (!maskHasPixel(directTile, localX, localY)) {
                    continue;
                }

                writeContentPixelRaw(
                    previewTile, localX, localY, fillR, fillG, fillB, fillA, contentFormat);
                writeMaskPixel(result, key, localX, localY);
                ++result.pixelsFilled;
            }
        }

        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                if (!maskHasPixel(preservedTile, localX, localY)) {
                    continue;
                }

                const PremultPixel originalPx
                    = samplePixel(sourceTile, localX, localY, contentFormat);
                const PremultPixel blendedPx = (fillMode == FillSemanticMode::Stroke)
                    ? recolorWithCoverage(originalPx, fillR, fillG, fillB, fillA)
                    : compositeUnder(originalPx, fillR, fillG, fillB, fillA);
                writeContentPixelRaw(previewTile, localX, localY, blendedPx.r, blendedPx.g,
                    blendedPx.b, blendedPx.a, contentFormat);
                writeMaskPixel(result, key, localX, localY);
                ++result.pixelsFilled;
            }
        }
    }
    return result;
}

} // namespace aether
