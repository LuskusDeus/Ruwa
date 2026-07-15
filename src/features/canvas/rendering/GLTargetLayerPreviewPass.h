// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_GLTARGETLAYERPREVIEWPASS_H
#define RUWA_FEATURES_CANVAS_RENDERING_GLTARGETLAYERPREVIEWPASS_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QRect>

#include <memory>

namespace aether {

class GLShaderProgram;

class GLTargetLayerPreviewPass {
public:
    explicit GLTargetLayerPreviewPass(QOpenGLFunctions_4_5_Core* gl);
    ~GLTargetLayerPreviewPass();

    GLTargetLayerPreviewPass(const GLTargetLayerPreviewPass&) = delete;
    GLTargetLayerPreviewPass& operator=(const GLTargetLayerPreviewPass&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    GLuint render(GLuint targetLayerBaseTexture, GLuint lassoMaskTexture,
        const QRect& lassoMaskBounds, uint32_t viewportWidth, uint32_t viewportHeight,
        const Color& fillColor, bool preserveBaseAlpha, GLuint selectionMaskTexture = 0);

    bool isInitialized() const { return m_initialized; }

private:
    void ensureRenderTarget(uint32_t width, uint32_t height);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_program;
    GLuint m_fbo = 0;
    GLuint m_outputTexture = 0;
    GLuint m_emptyVao = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_GLTARGETLAYERPREVIEWPASS_H
