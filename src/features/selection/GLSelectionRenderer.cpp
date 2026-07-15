// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   S E L E C T I O N   R E N D E R E R
// ==========================================================================

#include "GLSelectionRenderer.h"
#include "shared/rendering/GLShaderProgram.h"
#include "features/canvas/rendering/GLTileRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
namespace aether {

GLSelectionRenderer::GLSelectionRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLSelectionRenderer::~GLSelectionRenderer()
{
    shutdown();
}

Result<void> GLSelectionRenderer::initialize()
{
    if (m_initialized)
        return Result<void>::ok();

    static const QString fillVert
        = QStringLiteral("#version 450 core\n"
                         "layout(location = 0) in vec2 aPos;\n"
                         "uniform vec2 uTileOrigin;\n"
                         "uniform float uTileSize;\n"
                         "void main() {\n"
                         "    vec2 local = aPos - uTileOrigin;\n"
                         "    vec2 ndc = (local / uTileSize) * 2.0 - 1.0;\n"
                         "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
                         "}\n");
    static const QString fillFrag = QStringLiteral("#version 450 core\n"
                                                   "uniform vec4 uColor;\n"
                                                   "out vec4 outColor;\n"
                                                   "void main() {\n"
                                                   "    outColor = uColor;\n"
                                                   "}\n");

    static const QString subtractVert
        = QStringLiteral("#version 450 core\n"
                         "out vec2 vUV;\n"
                         "vec2 positions[3] = vec2[](\n"
                         "    vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0)\n"
                         ");\n"
                         "void main() {\n"
                         "    vec2 pos = positions[gl_VertexID];\n"
                         "    vUV = pos * 0.5 + 0.5;\n"
                         "    gl_Position = vec4(pos, 0.0, 1.0);\n"
                         "}\n");
    static const QString subtractFrag = QStringLiteral("#version 450 core\n"
                                                       "uniform sampler2D uDest;\n"
                                                       "uniform sampler2D uSrc;\n"
                                                       "in vec2 vUV;\n"
                                                       "out vec4 outColor;\n"
                                                       "void main() {\n"
                                                       "    vec4 d = texture(uDest, vUV);\n"
                                                       "    vec4 s = texture(uSrc, vUV);\n"
                                                       "    vec4 r = max(d - s, vec4(0.0));\n"
                                                       "    outColor = r;\n"
                                                       "}\n");

    m_fillProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto fillRes = m_fillProgram->loadFromSource(fillVert, fillFrag);
    if (!fillRes) {
        return fillRes;
    }

    m_subtractProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto subRes = m_subtractProgram->loadFromSource(subtractVert, subtractFrag);
    if (!subRes) {
        return subRes;
    }

    m_gl->glGenVertexArrays(1, &m_fillVAO);
    m_gl->glGenBuffers(1, &m_fillVBO);
    m_gl->glBindVertexArray(m_fillVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_fillVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);

    m_gl->glGenFramebuffers(1, &m_fbo);
    if (m_fbo == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create selection FBO" };
    }

    m_gl->glGenTextures(1, &m_tempTexA);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_tempTexA);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, TILE_SIZE, TILE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    m_gl->glGenTextures(1, &m_tempTexB);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_tempTexB);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, TILE_SIZE, TILE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    m_gl->glGenVertexArrays(1, &m_emptyVAO);

    m_pbo = 0;
    m_pboSize = 0;

    m_initialized = true;
    return Result<void>::ok();
}

void GLSelectionRenderer::shutdown()
{
    if (!m_initialized)
        return;

    m_fillProgram.reset();
    m_subtractProgram.reset();

    if (m_fillVBO) {
        m_gl->glDeleteBuffers(1, &m_fillVBO);
        m_fillVBO = 0;
    }
    if (m_fillVAO) {
        m_gl->glDeleteVertexArrays(1, &m_fillVAO);
        m_fillVAO = 0;
    }
    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_tempTexA) {
        m_gl->glDeleteTextures(1, &m_tempTexA);
        m_tempTexA = 0;
    }
    if (m_tempTexB) {
        m_gl->glDeleteTextures(1, &m_tempTexB);
        m_tempTexB = 0;
    }
    if (m_pbo) {
        m_gl->glDeleteBuffers(1, &m_pbo);
        m_pbo = 0;
        m_pboSize = 0;
    }

    m_initialized = false;
}

float GLSelectionRenderer::polygonArea(const std::vector<Vector2>& polygon) const
{
    float area = 0.0f;
    size_t n = polygon.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        area += (polygon[j].x * polygon[i].y) - (polygon[i].x * polygon[j].y);
    }
    return area * 0.5f;
}

std::vector<Vector2> GLSelectionRenderer::triangulate(const std::vector<Vector2>& polygon)
{
    std::vector<Vector2> result;
    if (polygon.size() < 3)
        return result;

    std::vector<Vector2> poly = polygon;
    while (poly.size() >= 2) {
        const Vector2& first = poly.front();
        const Vector2& last = poly.back();
        float dx = first.x - last.x;
        float dy = first.y - last.y;
        if ((dx * dx + dy * dy) < 0.0001f) {
            poly.pop_back();
        } else {
            break;
        }
    }

    // Remove collinear consecutive vertices that confuse ear detection
    auto removeCollinear = [](std::vector<Vector2>& pts) {
        if (pts.size() < 3)
            return;
        std::vector<Vector2> cleaned;
        cleaned.reserve(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            const Vector2& prev = pts[(i + pts.size() - 1) % pts.size()];
            const Vector2& curr = pts[i];
            const Vector2& next = pts[(i + 1) % pts.size()];
            float cross
                = (curr.x - prev.x) * (next.y - prev.y) - (curr.y - prev.y) * (next.x - prev.x);
            if (std::fabs(cross) > 1e-6f) {
                cleaned.push_back(curr);
            }
        }
        if (cleaned.size() >= 3)
            pts = std::move(cleaned);
    };
    removeCollinear(poly);

    const size_t n = poly.size();
    if (n < 3)
        return result;

    std::vector<size_t> indices(n);
    for (size_t i = 0; i < n; ++i)
        indices[i] = i;

    bool ccw = polygonArea(poly) > 0.0f;

    auto crossProduct = [](const Vector2& a, const Vector2& b, const Vector2& c) -> float {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };

    auto isConvex = [&](const Vector2& a, const Vector2& b, const Vector2& c) {
        float cross = crossProduct(a, b, c);
        return ccw ? (cross > 1e-7f) : (cross < -1e-7f);
    };

    auto pointInTriangle
        = [&](const Vector2& p, const Vector2& a, const Vector2& b, const Vector2& c) {
              float c1 = crossProduct(a, b, p);
              float c2 = crossProduct(b, c, p);
              float c3 = crossProduct(c, a, p);
              if (ccw)
                  return (c1 > 0 && c2 > 0 && c3 > 0);
              return (c1 < 0 && c2 < 0 && c3 < 0);
          };

    size_t guard = 0;
    size_t maxIter = n * n;
    while (indices.size() > 2 && guard++ < maxIter) {
        bool earFound = false;
        for (size_t i = 0; i < indices.size(); ++i) {
            size_t i0 = indices[(i + indices.size() - 1) % indices.size()];
            size_t i1 = indices[i];
            size_t i2 = indices[(i + 1) % indices.size()];
            const Vector2& a = poly[i0];
            const Vector2& b = poly[i1];
            const Vector2& c = poly[i2];
            if (!isConvex(a, b, c))
                continue;

            bool anyInside = false;
            for (size_t j = 0; j < indices.size(); ++j) {
                size_t ij = indices[j];
                if (ij == i0 || ij == i1 || ij == i2)
                    continue;
                if (pointInTriangle(poly[ij], a, b, c)) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside)
                continue;

            result.push_back(a);
            result.push_back(b);
            result.push_back(c);
            indices.erase(indices.begin() + static_cast<long long>(i));
            earFound = true;
            break;
        }
        if (!earFound)
            break;
    }

    return result;
}

bool GLSelectionRenderer::buildJob(const std::vector<Vector2>& polygon,
    std::vector<Vector2>& outTriVerts, std::vector<TileKey>& outTiles)
{
    outTriVerts = triangulate(polygon);
    if (outTriVerts.empty())
        return false;

    float minX = polygon[0].x;
    float minY = polygon[0].y;
    float maxX = polygon[0].x;
    float maxY = polygon[0].y;
    for (const auto& p : polygon) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }

    int32_t tMinX = static_cast<int32_t>(std::floor(minX / TILE_SIZE));
    int32_t tMinY = static_cast<int32_t>(std::floor(minY / TILE_SIZE));
    int32_t tMaxX = static_cast<int32_t>(std::floor(maxX / TILE_SIZE));
    int32_t tMaxY = static_cast<int32_t>(std::floor(maxY / TILE_SIZE));
    if (tMaxX < tMinX || tMaxY < tMinY)
        return false;

    outTiles.clear();
    outTiles.reserve(static_cast<size_t>((tMaxX - tMinX + 1) * (tMaxY - tMinY + 1)));
    for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
        for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
            outTiles.push_back(TileKey { tx, ty });
        }
    }
    return true;
}

size_t GLSelectionRenderer::applyPolygonBatch(TileGrid& maskGrid, GLTileRenderer* tileRenderer,
    const std::vector<Vector2>& triVerts, const std::vector<TileKey>& tiles, size_t startIndex,
    size_t maxTiles, LassoSelectionMode mode, uint8_t strength, std::vector<TileKey>& outProcessed)
{
    if (!m_initialized || !tileRenderer)
        return startIndex;
    if (triVerts.empty() || tiles.empty())
        return startIndex;

    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_fillVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(triVerts.size() * sizeof(Vector2)),
        triVerts.data(), GL_DYNAMIC_DRAW);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevBlend = GL_FALSE;
    m_gl->glGetBooleanv(GL_BLEND, &prevBlend);
    GLint prevBlendEq = 0;
    m_gl->glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEq);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    m_gl->glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    m_gl->glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);

    size_t processed = 0;
    size_t i = startIndex;
    for (; i < tiles.size() && processed < maxTiles; ++i, ++processed) {
        const TileKey& key = tiles[i];
        TileData& tile = maskGrid.getOrCreateTile(key);
        bool wasNew = !tile.hasTexture();
        tileRenderer->ensureTileTexture(tile);
        if (wasNew) {
            tileRenderer->uploadTileData(tile);
        }

        Vector2 tileOrigin(
            static_cast<float>(key.x * TILE_SIZE), static_cast<float>(key.y * TILE_SIZE));

        if (mode == LassoSelectionMode::Subtract) {
            subtractPolygonFromTexture(tile.textureId(), triVerts, tileOrigin, strength);
        } else {
            renderPolygonToTexture(tile.textureId(), triVerts, tileOrigin, strength);
        }

        outProcessed.push_back(key);
    }

    if (!prevBlend)
        m_gl->glDisable(GL_BLEND);
    m_gl->glBlendEquation(prevBlendEq);
    m_gl->glBlendFunc(prevBlendSrc, prevBlendDst);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return i;
}

void GLSelectionRenderer::renderPolygonToTexture(GLuint targetTex,
    const std::vector<Vector2>& triVerts, const Vector2& tileOrigin, uint8_t strength)
{
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTex, 0);

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendEquation(GL_MAX);
    m_gl->glBlendFunc(GL_ONE, GL_ONE);

    m_fillProgram->use();
    float v = static_cast<float>(strength) / 255.0f;
    m_fillProgram->setUniform("uColor", v, v, v, v);
    m_fillProgram->setUniform("uTileOrigin", tileOrigin.x, tileOrigin.y);
    m_fillProgram->setUniform("uTileSize", static_cast<float>(TILE_SIZE));

    m_gl->glBindVertexArray(m_fillVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triVerts.size()));
    m_gl->glBindVertexArray(0);
}

void GLSelectionRenderer::subtractPolygonFromTexture(GLuint destTex,
    const std::vector<Vector2>& triVerts, const Vector2& tileOrigin, uint8_t strength)
{
    // 1) Render polygon to temp A
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tempTexA, 0);
    m_gl->glDisable(GL_BLEND);
    m_gl->glClearColor(0, 0, 0, 0);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_fillProgram->use();
    float v = static_cast<float>(strength) / 255.0f;
    m_fillProgram->setUniform("uColor", v, v, v, v);
    m_fillProgram->setUniform("uTileOrigin", tileOrigin.x, tileOrigin.y);
    m_fillProgram->setUniform("uTileSize", static_cast<float>(TILE_SIZE));

    m_gl->glBindVertexArray(m_fillVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triVerts.size()));
    m_gl->glBindVertexArray(0);

    // 2) Subtract dest - src into temp B
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tempTexB, 0);
    m_subtractProgram->use();
    m_subtractProgram->setUniform("uDest", 0);
    m_subtractProgram->setUniform("uSrc", 1);

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, destTex);
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_tempTexA);

    m_gl->glBindVertexArray(m_emptyVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    m_gl->glBindVertexArray(0);

    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);

    // 3) Copy temp B to dest
    m_gl->glCopyImageSubData(m_tempTexB, GL_TEXTURE_2D, 0, 0, 0, 0, destTex, GL_TEXTURE_2D, 0, 0, 0,
        0, TILE_SIZE, TILE_SIZE, 1);
}

GLsync GLSelectionRenderer::startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys)
{
    if (keys.empty())
        return nullptr;

    size_t required = static_cast<size_t>(keys.size()) * TILE_BYTE_SIZE;
    if (m_pbo == 0) {
        m_gl->glGenBuffers(1, &m_pbo);
    }
    if (required > m_pboSize) {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
        m_gl->glBufferData(GL_PIXEL_PACK_BUFFER, required, nullptr, GL_STREAM_READ);
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_pboSize = required;
    }

    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);

    size_t offset = 0;
    for (const auto& key : keys) {
        TileData* tile = grid.getTile(key);
        if (!tile || !tile->hasTexture()) {
            offset += TILE_BYTE_SIZE;
            continue;
        }
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile->textureId(), 0);
        m_gl->glReadPixels(
            0, 0, TILE_SIZE, TILE_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, reinterpret_cast<void*>(offset));
        offset += TILE_BYTE_SIZE;
    }

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

    return m_gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

bool GLSelectionRenderer::isReadbackComplete(GLsync fence)
{
    if (!fence)
        return true;
    GLenum result = m_gl->glClientWaitSync(fence, 0, 0);
    return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

void GLSelectionRenderer::finishReadback(
    GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys)
{
    if (!fence)
        return;
    m_gl->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
    m_gl->glDeleteSync(fence);

    if (m_pbo == 0 || keys.empty())
        return;

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    void* ptr = m_gl->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (!ptr) {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        return;
    }

    uint8_t* bytes = static_cast<uint8_t*>(ptr);
    size_t offset = 0;
    for (const auto& key : keys) {
        TileData* tile = grid.getTile(key);
        if (tile) {
            std::memcpy(tile->pixels(), bytes + offset, TILE_BYTE_SIZE);
            tile->clearDirty();
        }
        offset += TILE_BYTE_SIZE;
    }

    m_gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void GLSelectionRenderer::deleteFence(GLsync fence)
{
    if (fence) {
        m_gl->glDeleteSync(fence);
    }
}

} // namespace aether
