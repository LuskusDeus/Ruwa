// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_GLRETAINEDRENDERER_H
#define RUWA_FEATURES_CANVAS_RENDERING_GLRETAINEDRENDERER_H

#include "shared/types/Result.h"
#include "shared/tiles/TileTypes.h"
#include "features/canvas/rendering/RetainedRenderPayload.h"

#include <QImage>
#include <QOpenGLFunctions_4_5_Core>

#include <memory>
#include <vector>

namespace aether {

class GLShaderProgram;

class GLRetainedRenderer {
public:
    explicit GLRetainedRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLRetainedRenderer();

    GLRetainedRenderer(const GLRetainedRenderer&) = delete;
    GLRetainedRenderer& operator=(const GLRetainedRenderer&) = delete;

    Result<void> initialize();
    void shutdown();

    static QImage renderPayloadTileImage(const RetainedRenderPayload& payload, const TileKey& key);
    GLuint renderPayloadTile(const RetainedRenderPayload& payload, const TileKey& key);
    bool isInitialized() const { return m_initialized; }

private:
    struct CachedPrimitive {
        uint32_t pointOffset = 0;
        uint32_t pointCount = 0;
    };

    void ensureRenderTarget();
    void clearRenderTarget();
    void uploadPayloadIfNeeded(const RetainedRenderPayload& payload);
    bool renderGlyphPayloadTile(const RetainedRenderPayload& payload, const TileKey& key);
    void uploadGlyphImage(const QImage& image);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_program;
    GLuint m_fbo = 0;
    GLuint m_texture = 0;
    GLuint m_vao = 0;
    GLuint m_pointBuffer = 0;
    const RetainedRenderPayload* m_cachedPayload = nullptr;
    uint64_t m_cachedRevision = 0;
    std::vector<CachedPrimitive> m_cachedPrimitives;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_GLRETAINEDRENDERER_H
