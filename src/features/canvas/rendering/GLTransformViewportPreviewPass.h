// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_GLTRANSFORMVIEWPORTPREVIEWPASS_H
#define RUWA_FEATURES_CANVAS_RENDERING_GLTRANSFORMVIEWPORTPREVIEWPASS_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"

#include <QOpenGLFunctions_4_5_Core>

#include <memory>

namespace aether {

class GLShaderProgram;
struct TransformState;

class GLTransformViewportPreviewPass {
public:
    explicit GLTransformViewportPreviewPass(QOpenGLFunctions_4_5_Core* gl);
    ~GLTransformViewportPreviewPass();

    GLTransformViewportPreviewPass(const GLTransformViewportPreviewPass&) = delete;
    GLTransformViewportPreviewPass& operator=(const GLTransformViewportPreviewPass&) = delete;

    Result<void> initialize(const QString& shaderDir);
    void shutdown();

    GLuint render(GLuint sourceAtlasTexture, int32_t sourceAtlasMinTX, int32_t sourceAtlasMinTY,
        uint32_t sourceAtlasWidth, uint32_t sourceAtlasHeight, GLuint targetBaseTexture,
        GLuint selectionMaskAtlasTexture, int32_t selectionMaskAtlasMinTX,
        int32_t selectionMaskAtlasMinTY, uint32_t selectionMaskAtlasWidth,
        uint32_t selectionMaskAtlasHeight, const TransformState& state, uint32_t viewportWidth,
        uint32_t viewportHeight, const Vector2& cameraPosition, float cameraZoom,
        float cameraRotation, uint32_t canvasWidth, uint32_t canvasHeight, float canvasCornerRadius,
        bool flipH, bool flipV, bool preserveMaskedSource = false,
        const Color& sourceBackgroundColor = Color::transparent(), bool clipToCanvas = true);

    GLuint renderFromScreenSource(GLuint sourceScreenTexture, GLuint targetBaseTexture,
        GLuint selectionMaskAtlasTexture, int32_t selectionMaskAtlasMinTX,
        int32_t selectionMaskAtlasMinTY, uint32_t selectionMaskAtlasWidth,
        uint32_t selectionMaskAtlasHeight, const TransformState& state, uint32_t viewportWidth,
        uint32_t viewportHeight, const Vector2& cameraPosition, float cameraZoom,
        float cameraRotation, uint32_t canvasWidth, uint32_t canvasHeight, float canvasCornerRadius,
        bool flipH, bool flipV, bool preserveMaskedSource = false, uint32_t sourceViewportWidth = 0,
        uint32_t sourceViewportHeight = 0, Vector2 sourceScreenOffset = {},
        uint32_t targetBaseViewportWidth = 0, uint32_t targetBaseViewportHeight = 0,
        Vector2 targetBaseScreenOffset = {}, bool clipToCanvas = true);

    bool isInitialized() const { return m_initialized; }

private:
    void ensureRenderTarget(uint32_t width, uint32_t height);
    Result<void> initDeformMeshGrid();

    // Forward-rasterized deform pass. Renders the tessellated B-spline
    // mesh into m_outputTexture with premultiplied src-over blending.
    // Called from render() / renderFromScreenSource() when state has a
    // deform mesh and no selection mask is active.
    GLuint renderDeformMeshPass(GLuint sourceTexture, bool sourceIsScreen, int32_t sourceAtlasMinTX,
        int32_t sourceAtlasMinTY, uint32_t sourceAtlasWidth, uint32_t sourceAtlasHeight,
        uint32_t sourceTextureWidth, uint32_t sourceTextureHeight, Vector2 sourceScreenOffset,
        GLuint targetBaseTexture, uint32_t targetBaseTextureWidth, uint32_t targetBaseTextureHeight,
        Vector2 targetBaseScreenOffset, GLuint selectionMaskAtlasTexture,
        int32_t selectionMaskAtlasMinTX, int32_t selectionMaskAtlasMinTY,
        uint32_t selectionMaskAtlasWidth, uint32_t selectionMaskAtlasHeight,
        bool preserveMaskedSource, const TransformState& state, uint32_t viewportWidth,
        uint32_t viewportHeight, const Vector2& cameraPosition, float cameraZoom,
        float cameraRotation, uint32_t canvasWidth, uint32_t canvasHeight, float canvasCornerRadius,
        bool flipH, bool flipV, bool clipToCanvas);

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLShaderProgram> m_program;
    // Forward-rasterized deform path (Path B). Used only when the transform
    // state carries a deform mesh; affine and free-quad still go through the
    // single fullscreen-quad fragment path via m_program.
    std::unique_ptr<GLShaderProgram> m_deformMeshProgram;
    // Base-only pass for the deform+selection-mask case. Renders the target
    // base content (with mask carved out) into the FBO before the mesh pass
    // composites the transformed source on top via src-over blending.
    std::unique_ptr<GLShaderProgram> m_deformBaseProgram;
    GLuint m_fbo = 0;
    GLuint m_outputTexture = 0;
    GLuint m_emptyVao = 0;
    GLuint m_deformMeshVao = 0;
    GLuint m_deformMeshVbo = 0;
    GLuint m_deformMeshIbo = 0;
    GLsizei m_deformMeshIndexCount = 0;
    // Sampler object for source texture sampling during deform mesh draws.
    // Overrides the source texture's per-object sampler params to enable
    // LINEAR_MIPMAP_LINEAR filtering without mutating shared sampler state.
    // Combined with a per-frame glGenerateMipmap on the source, this gives
    // proper LOD selection for stretched regions and cuts texture-cache
    // misses dramatically on large source atlases.
    GLuint m_deformMeshSampler = 0;
    // Tracks the source texture for which mip levels were last generated.
    // During an interactive deform drag the source content does not change
    // (only the lattice control points), so regenerating the mip pyramid
    // every frame is wasted work and on some drivers introduces sync stalls.
    GLuint m_lastMippedSourceTexture = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_initialized = false;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_GLTRANSFORMVIEWPORTPREVIEWPASS_H
