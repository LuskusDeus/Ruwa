// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_GLLASSOMASKRENDERER_H
#define RUWA_FEATURES_CANVAS_RENDERING_GLLASSOMASKRENDERER_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QRect>
#include <QSize>

#include <memory>
#include <vector>

namespace aether {

class GLShaderProgram;

class GLLassoMaskRenderer {
public:
    struct MaskRenderResult {
        GLuint texture = 0;
        QRect bounds;

        bool isValid() const { return texture != 0 && bounds.isValid() && !bounds.isEmpty(); }
    };

    explicit GLLassoMaskRenderer(QOpenGLFunctions_4_5_Core* gl);
    ~GLLassoMaskRenderer();

    GLLassoMaskRenderer(const GLLassoMaskRenderer&) = delete;
    GLLassoMaskRenderer& operator=(const GLLassoMaskRenderer&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    MaskRenderResult renderMask(
        const std::vector<Vector2>& localScreenPolygon, const QRect& bounds);
    bool isInitialized() const { return m_initialized; }

private:
    void ensureMaskTexture(const QSize& size);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_program;
    GLuint m_maskTexture = 0;
    GLuint m_pointSsbo = 0;
    QSize m_maskSize;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_GLLASSOMASKRENDERER_H
