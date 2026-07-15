// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   T R A N S F O R M   R E N D E R E R
// ==========================================================================

#include "GLTransformRenderer.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h" // tileGLPixelType, tileByteSize
#include "features/canvas/rendering/GLTileRenderer.h"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <limits>

namespace aether {

namespace {

// Tessellation density for the commit-time forward-deform pass. Matches
// the preview path's DEFORM_TESS_DENSITY so the on-screen drag preview and
// the committed result sample the same B-spline surface at the same rate.
constexpr int kForwardDeformGrid = TransformState::DEFORM_TESS_DENSITY;
constexpr float kDegenerateTriangleArea = 1e-4f;

bool sameRect(const Rect& a, const Rect& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

// ==========================================================================
//   I N L I N E   S H A D E R S
// ==========================================================================

static const QString kTransformVert
    = QStringLiteral("#version 450 core\n"
                     "out vec2 fragTexCoord;\n"
                     "vec2 positions[6] = vec2[](\n"
                     "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
                     "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
                     ");\n"
                     "void main() {\n"
                     "    vec2 pos = positions[gl_VertexID];\n"
                     "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
                     "    fragTexCoord = pos;\n"
                     "}\n");

static const QString kTransformFrag = QStringLiteral(
    "#version 450 core\n"
    "\n"
    "uniform sampler2D uAtlas;\n"
    "uniform sampler2D uMaskAtlas;\n"
    "uniform int uUseMask;\n"
    "uniform int uPreserveMaskedSource;\n"
    "uniform vec2 uAtlasSize;       // atlas size in pixels\n"
    "uniform vec2 uAtlasMinTile;    // min tile coordinate (tile units)\n"
    "uniform vec2 uAtlasTileCount;  // number of tiles (cols, rows)\n"
    "uniform vec2 uMaskAtlasSize;   // mask atlas size in pixels\n"
    "uniform vec2 uMaskAtlasMinTile;// mask min tile coordinate (tile units)\n"
    "uniform vec2 uDestTileOrigin;  // world-space origin of destination tile (pixels)\n"
    "uniform float uTileSize;       // TILE_SIZE (256)\n"
    "uniform mat3 uInverseTransform;// 3x3 inverse affine matrix\n"
    "uniform vec4 uContentBounds;   // left, top, right, bottom\n"
    "uniform vec4 uSourceBackgroundColor; // src grid default-fill (premult)\n"
    "\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "\n"
    "vec4 sampleAtlas(vec2 worldPos) {\n"
    "    vec2 atlasPixel = worldPos - uAtlasMinTile * uTileSize;\n"
    "    if (atlasPixel.x < -0.5 || atlasPixel.y < -0.5 ||\n"
    "        atlasPixel.x > uAtlasSize.x + 0.5 ||\n"
    "        atlasPixel.y > uAtlasSize.y + 0.5) {\n"
    "        return uSourceBackgroundColor;\n"
    "    }\n"
    "    vec2 atlasUV = atlasPixel / uAtlasSize;\n"
    "    return texture(uAtlas, atlasUV);\n"
    "}\n"
    "\n"
    "vec4 sampleTransformedAtlas(vec2 worldPos) {\n"
    "    vec2 minCenter = uContentBounds.xy + vec2(0.5);\n"
    "    vec2 maxCenter = uContentBounds.zw - vec2(0.5);\n"
    "    vec2 safeMin = min(minCenter, maxCenter);\n"
    "    vec2 safeMax = max(minCenter, maxCenter);\n"
    "    vec2 outside = max(max(safeMin - worldPos,\n"
    "                           worldPos - safeMax), vec2(0.0));\n"
    "    if (outside.x > 0.5 || outside.y > 0.5) {\n"
    "        return uSourceBackgroundColor;\n"
    "    }\n"
    "    return sampleAtlas(clamp(worldPos, safeMin, safeMax));\n"
    "}\n"
    "\n"
    "float sampleMask(vec2 worldPos) {\n"
    "    if (uUseMask == 0) return 1.0;\n"
    "    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;\n"
    "    if (maskPixel.x < 0.0 || maskPixel.y < 0.0 ||\n"
    "        maskPixel.x >= uMaskAtlasSize.x ||\n"
    "        maskPixel.y >= uMaskAtlasSize.y) {\n"
    "        return 0.0;\n"
    "    }\n"
    "    vec2 maskUV = maskPixel / uMaskAtlasSize;\n"
    "    return texture(uMaskAtlas, maskUV).a;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 worldPos = uDestTileOrigin + fragTexCoord * uTileSize;\n"
    "\n"
    "    // Inverse transform to source space\n"
    "    vec3 srcH = uInverseTransform * vec3(worldPos, 1.0);\n"
    "    vec2 srcWorld = srcH.xy;\n"
    "\n"
    "    if (uUseMask == 0) {\n"
    "        outColor = sampleTransformedAtlas(srcWorld);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec4 baseColor = sampleAtlas(worldPos);\n"
    "    float maskDest = sampleMask(worldPos);\n"
    "    if (uPreserveMaskedSource == 0) {\n"
    "        baseColor = mix(baseColor, uSourceBackgroundColor, maskDest);\n"
    "    }\n"
    "\n"
    "    vec4 selColor = sampleTransformedAtlas(srcWorld);\n"
    "    float maskSrc = sampleMask(srcWorld);\n"
    "    selColor *= maskSrc;\n"
    "\n"
    "    // Composite selection over base (premultiplied alpha)\n"
    "    outColor = selColor + baseColor * (1.0 - selColor.a);\n"
    "}\n");

// Quad transform: analytical inverse bilinear interpolation (Inigo Quilez method).
// No iteration, no divergence, handles ALL quad shapes including bowtie.
static const QString kQuadTransformFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform sampler2D uAtlas;\n"
    "uniform sampler2D uMaskAtlas;\n"
    "uniform int uUseMask;\n"
    "uniform int uPreserveMaskedSource;\n"
    "uniform vec2 uAtlasSize;\n"
    "uniform vec2 uAtlasMinTile;\n"
    "uniform vec2 uAtlasTileCount;\n"
    "uniform vec2 uMaskAtlasSize;\n"
    "uniform vec2 uMaskAtlasMinTile;\n"
    "uniform vec2 uDestTileOrigin;\n"
    "uniform float uTileSize;\n"
    "uniform vec2 uQ0, uQ1, uQ2, uQ3;\n"
    "uniform vec4 uContentBounds;\n"
    "uniform vec4 uSourceBackgroundColor; // src grid default-fill (premult)\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec4 sampleAtlas(vec2 worldPos) {\n"
    "    vec2 atlasPixel = worldPos - uAtlasMinTile * uTileSize;\n"
    "    if (atlasPixel.x < -0.5 || atlasPixel.y < -0.5 ||\n"
    "        atlasPixel.x > uAtlasSize.x + 0.5 || atlasPixel.y > uAtlasSize.y + 0.5)\n"
    "        return uSourceBackgroundColor;\n"
    "    return texture(uAtlas, atlasPixel / uAtlasSize);\n"
    "}\n"
    "vec4 sampleTransformedAtlas(vec2 worldPos) {\n"
    "    vec2 minCenter = uContentBounds.xy + vec2(0.5);\n"
    "    vec2 maxCenter = uContentBounds.zw - vec2(0.5);\n"
    "    vec2 safeMin = min(minCenter, maxCenter);\n"
    "    vec2 safeMax = max(minCenter, maxCenter);\n"
    "    vec2 outside = max(max(safeMin - worldPos,\n"
    "                           worldPos - safeMax), vec2(0.0));\n"
    "    if (outside.x > 0.5 || outside.y > 0.5) {\n"
    "        return uSourceBackgroundColor;\n"
    "    }\n"
    "    return sampleAtlas(clamp(worldPos, safeMin, safeMax));\n"
    "}\n"
    "float sampleMask(vec2 worldPos) {\n"
    "    if (uUseMask == 0) return 1.0;\n"
    "    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;\n"
    "    if (maskPixel.x < 0.0 || maskPixel.y < 0.0 ||\n"
    "        maskPixel.x >= uMaskAtlasSize.x || maskPixel.y >= uMaskAtlasSize.y)\n"
    "        return 0.0;\n"
    "    return texture(uMaskAtlas, maskPixel / uMaskAtlasSize).a;\n"
    "}\n"
    "float cross2d(vec2 a, vec2 b) { return a.x*b.y - a.y*b.x; }\n"
    "\n"
    "bool trySolveST(vec2 E, vec2 F, vec2 G, vec2 h, float t, float margin, out vec2 st) {\n"
    "    if (t < -margin || t > 1.0 + margin) return false;\n"
    "    vec2 denom = E + G * t;\n"
    "    float s;\n"
    "    if (abs(denom.x) > abs(denom.y)) {\n"
    "        if (abs(denom.x) < 1e-8) return false;\n"
    "        s = (h.x - F.x * t) / denom.x;\n"
    "    } else {\n"
    "        if (abs(denom.y) < 1e-8) return false;\n"
    "        s = (h.y - F.y * t) / denom.y;\n"
    "    }\n"
    "    if (s < -margin || s > 1.0 + margin) return false;\n"
    "    st = vec2(s, t);\n"
    "    return true;\n"
    "}\n"
    "\n"
    "int inverseBilinearAll(vec2 P, out vec2 st0, out vec2 st1) {\n"
    "    vec2 E0 = uQ1 - uQ0;\n"
    "    vec2 F0 = uQ3 - uQ0;\n"
    "    float quadScale = max(max(length(E0), length(F0)),\n"
    "                          max(length(uQ2 - uQ1), length(uQ2 - uQ3)));\n"
    "    float invScale = 1.0 / max(quadScale, 1e-6);\n"
    "    vec2 E = E0 * invScale;\n"
    "    vec2 F = F0 * invScale;\n"
    "    vec2 G = (uQ0 - uQ1 + uQ2 - uQ3) * invScale;\n"
    "    vec2 h = (P - uQ0) * invScale;\n"
    "\n"
    "    float k2 = cross2d(G, F);\n"
    "    float k1 = cross2d(E, F) + cross2d(h, G);\n"
    "    float k0 = cross2d(h, E);\n"
    "\n"
    "    const float margin = 0.002;\n"
    "\n"
    "    float tCands[2];\n"
    "    int nCands = 0;\n"
    "    if (abs(k2) < 1e-8) {\n"
    "        if (abs(k1) < 1e-8) return 0;\n"
    "        tCands[0] = -k0 / k1;\n"
    "        nCands = 1;\n"
    "    } else {\n"
    "        float disc = k1*k1 - 4.0*k0*k2;\n"
    "        if (disc < 0.0) return 0;\n"
    "        float sq = sqrt(disc);\n"
    "        float signK1 = (k1 >= 0.0) ? 1.0 : -1.0;\n"
    "        float q_stable = -0.5 * (k1 + signK1 * sq);\n"
    "        tCands[0] = q_stable / k2;\n"
    "        tCands[1] = (abs(q_stable) > 1e-8) ? (k0 / q_stable) : tCands[0];\n"
    "        nCands = 2;\n"
    "    }\n"
    "\n"
    "    int count = 0;\n"
    "    vec2 results[2];\n"
    "    for (int i = 0; i < 2; ++i) {\n"
    "        if (i >= nCands) break;\n"
    "        vec2 cand;\n"
    "        if (!trySolveST(E, F, G, h, tCands[i], margin, cand)) continue;\n"
    "        bool dup = false;\n"
    "        for (int j = 0; j < count; ++j) {\n"
    "            if (abs(results[j].x - cand.x) < 1e-4 && abs(results[j].y - cand.y) < 1e-4) {\n"
    "                dup = true;\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "        if (dup) continue;\n"
    "        results[count] = cand;\n"
    "        count++;\n"
    "    }\n"
    "    if (count >= 1) st0 = results[0];\n"
    "    if (count >= 2) st1 = results[1];\n"
    "    return count;\n"
    "}\n"
    "\n"
    "vec2 stToSrc(vec2 st) {\n"
    "    float cL = uContentBounds.x, cT = uContentBounds.y;\n"
    "    float cR = uContentBounds.z, cB = uContentBounds.w;\n"
    "    return vec2(cL + st.s*(cR-cL), cT + st.t*(cB-cT));\n"
    "}\n"
    "void main() {\n"
    "    vec2 worldPos = uDestTileOrigin + fragTexCoord * uTileSize;\n"
    "    vec2 st0, st1;\n"
    "    int n = inverseBilinearAll(worldPos, st0, st1);\n"
    "    if (n == 0) {\n"
    "        if (uUseMask != 0) {\n"
    "            vec4 baseColor = sampleAtlas(worldPos);\n"
    "            float maskDest = sampleMask(worldPos);\n"
    "            outColor = (uPreserveMaskedSource != 0) ? baseColor : mix(baseColor, "
    "uSourceBackgroundColor, maskDest);\n"
    "        } else {\n"
    "            outColor = uSourceBackgroundColor;\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    vec2 srcWorld0 = stToSrc(st0);\n"
    "    vec2 srcWorld1 = (n >= 2) ? stToSrc(st1) : vec2(0.0);\n"
    "    vec4 c0 = sampleTransformedAtlas(srcWorld0);\n"
    "    vec4 c1 = (n >= 2) ? sampleTransformedAtlas(srcWorld1) : vec4(0.0);\n"
    "    if (uUseMask != 0) {\n"
    "        c0 *= sampleMask(srcWorld0);\n"
    "        if (n >= 2) c1 *= sampleMask(srcWorld1);\n"
    "    }\n"
    "    // Composite both bilinear branches: front (c0) over back (c1).\n"
    "    vec4 selColor = c0 + c1 * (1.0 - c0.a);\n"
    "    if (uUseMask == 0) {\n"
    "        outColor = selColor;\n"
    "        return;\n"
    "    }\n"
    "    vec4 baseColor = sampleAtlas(worldPos);\n"
    "    float maskDest = sampleMask(worldPos);\n"
    "    if (uPreserveMaskedSource == 0) {\n"
    "        baseColor = mix(baseColor, uSourceBackgroundColor, maskDest);\n"
    "    }\n"
    "    outColor = selColor + baseColor * (1.0 - selColor.a);\n"
    "}\n");

static const QString kForwardDeformVert
    = QStringLiteral("#version 450 core\n"
                     "layout(location = 0) in vec2 aDestWorld;\n"
                     "layout(location = 1) in vec2 aSrcWorld;\n"
                     "uniform vec2 uDestTileOrigin;\n"
                     "uniform float uTileSize;\n"
                     "out vec2 vSrcWorld;\n"
                     "void main() {\n"
                     "    vec2 local = (aDestWorld - uDestTileOrigin) / uTileSize;\n"
                     "    gl_Position = vec4(local * 2.0 - 1.0, 0.0, 1.0);\n"
                     "    vSrcWorld = aSrcWorld;\n"
                     "}\n");

static const QString kForwardDeformFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform sampler2D uAtlas;\n"
    "uniform sampler2D uMaskAtlas;\n"
    "uniform int uUseMask;\n"
    "uniform int uPreserveMaskedSource;\n"
    "uniform vec2 uAtlasSize;\n"
    "uniform vec2 uAtlasMinTile;\n"
    "uniform vec2 uMaskAtlasSize;\n"
    "uniform vec2 uMaskAtlasMinTile;\n"
    "uniform float uTileSize;\n"
    "uniform vec4 uContentBounds;\n"
    "uniform vec4 uSourceBackgroundColor; // src grid default-fill (premult)\n"
    "in vec2 vSrcWorld;\n"
    "out vec4 outColor;\n"
    "vec4 sampleAtlas(vec2 worldPos) {\n"
    "    vec2 atlasPixel = worldPos - uAtlasMinTile * uTileSize;\n"
    "    if (atlasPixel.x < -0.5 || atlasPixel.y < -0.5 ||\n"
    "        atlasPixel.x > uAtlasSize.x + 0.5 || atlasPixel.y > uAtlasSize.y + 0.5)\n"
    "        return uSourceBackgroundColor;\n"
    "    return texture(uAtlas, atlasPixel / uAtlasSize);\n"
    "}\n"
    "vec4 sampleTransformedAtlas(vec2 worldPos) {\n"
    "    vec2 minCenter = uContentBounds.xy + vec2(0.5);\n"
    "    vec2 maxCenter = uContentBounds.zw - vec2(0.5);\n"
    "    vec2 safeMin = min(minCenter, maxCenter);\n"
    "    vec2 safeMax = max(minCenter, maxCenter);\n"
    "    vec2 outside = max(max(safeMin - worldPos,\n"
    "                           worldPos - safeMax), vec2(0.0));\n"
    "    if (outside.x > 0.5 || outside.y > 0.5) {\n"
    "        return uSourceBackgroundColor;\n"
    "    }\n"
    "    return sampleAtlas(clamp(worldPos, safeMin, safeMax));\n"
    "}\n"
    "float sampleMask(vec2 worldPos) {\n"
    "    if (uUseMask == 0) return 1.0;\n"
    "    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;\n"
    "    if (maskPixel.x < 0.0 || maskPixel.y < 0.0 ||\n"
    "        maskPixel.x >= uMaskAtlasSize.x || maskPixel.y >= uMaskAtlasSize.y)\n"
    "        return 0.0;\n"
    "    return texture(uMaskAtlas, maskPixel / uMaskAtlasSize).a;\n"
    "}\n"
    "void main() {\n"
    "    vec4 color = sampleTransformedAtlas(vSrcWorld);\n"
    "    if (uUseMask != 0) {\n"
    "        color *= sampleMask(vSrcWorld);\n"
    "    }\n"
    "    outColor = color;\n"
    "}\n");

static const QString kMaskBaseFrag = QStringLiteral(
    "#version 450 core\n"
    "uniform sampler2D uAtlas;\n"
    "uniform sampler2D uMaskAtlas;\n"
    "uniform int uUseMask;\n"
    "uniform int uPreserveMaskedSource;\n"
    "uniform vec2 uAtlasSize;\n"
    "uniform vec2 uAtlasMinTile;\n"
    "uniform vec2 uMaskAtlasSize;\n"
    "uniform vec2 uMaskAtlasMinTile;\n"
    "uniform vec2 uDestTileOrigin;\n"
    "uniform float uTileSize;\n"
    "uniform vec4 uSourceBackgroundColor; // src grid default-fill (premult)\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 outColor;\n"
    "vec4 sampleAtlas(vec2 worldPos) {\n"
    "    vec2 atlasPixel = worldPos - uAtlasMinTile * uTileSize;\n"
    "    if (atlasPixel.x < -0.5 || atlasPixel.y < -0.5 ||\n"
    "        atlasPixel.x > uAtlasSize.x + 0.5 || atlasPixel.y > uAtlasSize.y + 0.5)\n"
    "        return uSourceBackgroundColor;\n"
    "    return texture(uAtlas, atlasPixel / uAtlasSize);\n"
    "}\n"
    "float sampleMask(vec2 worldPos) {\n"
    "    if (uUseMask == 0) return 0.0;\n"
    "    vec2 maskPixel = worldPos - uMaskAtlasMinTile * uTileSize;\n"
    "    if (maskPixel.x < 0.0 || maskPixel.y < 0.0 ||\n"
    "        maskPixel.x >= uMaskAtlasSize.x || maskPixel.y >= uMaskAtlasSize.y)\n"
    "        return 0.0;\n"
    "    return texture(uMaskAtlas, maskPixel / uMaskAtlasSize).a;\n"
    "}\n"
    "void main() {\n"
    "    vec2 worldPos = uDestTileOrigin + fragTexCoord * uTileSize;\n"
    "    vec4 baseColor = sampleAtlas(worldPos);\n"
    "    float maskDest = sampleMask(worldPos);\n"
    "    outColor = (uPreserveMaskedSource != 0) ? baseColor : mix(baseColor, "
    "uSourceBackgroundColor, maskDest);\n"
    "}\n");

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLTransformRenderer::GLTransformRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLTransformRenderer::~GLTransformRenderer()
{
    shutdown();
}

// ==========================================================================
//   L I F E C Y C L E
// ==========================================================================

Result<void> GLTransformRenderer::initialize()
{
    if (m_initialized)
        return Result<void>::ok();

    // Compile transform shader
    m_transformProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto result = m_transformProgram->loadFromSource(kTransformVert, kTransformFrag);
    if (!result) {
        return result;
    }

    // Compile quad (free-form) transform shader
    m_quadTransformProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto quadResult = m_quadTransformProgram->loadFromSource(kTransformVert, kQuadTransformFrag);
    if (!quadResult) {
        return quadResult;
    }

    m_forwardDeformProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto forwardDeformResult
        = m_forwardDeformProgram->loadFromSource(kForwardDeformVert, kForwardDeformFrag);
    if (!forwardDeformResult) {
        return forwardDeformResult;
    }

    m_maskBaseProgram = std::make_unique<GLShaderProgram>(m_gl);
    auto maskBaseResult = m_maskBaseProgram->loadFromSource(kTransformVert, kMaskBaseFrag);
    if (!maskBaseResult) {
        return maskBaseResult;
    }

    // Create FBO
    m_gl->glGenFramebuffers(1, &m_fbo);
    if (m_fbo == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create transform FBO" };
    }

    // Create empty VAO
    m_gl->glGenVertexArrays(1, &m_emptyVAO);
    if (m_emptyVAO == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create transform VAO" };
    }

    m_gl->glGenVertexArrays(1, &m_deformMeshVAO);
    m_gl->glGenBuffers(1, &m_deformMeshVBO);
    m_gl->glGenBuffers(1, &m_deformMeshEBO);
    if (m_deformMeshVAO == 0 || m_deformMeshVBO == 0 || m_deformMeshEBO == 0) {
        return { ErrorCode::PipelineCreationFailed, "Failed to create deform mesh buffers" };
    }

    m_gl->glBindVertexArray(m_deformMeshVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_deformMeshVBO);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_deformMeshEBO);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(DeformRasterVertex),
        reinterpret_cast<const void*>(offsetof(DeformRasterVertex, destX)));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(DeformRasterVertex),
        reinterpret_cast<const void*>(offsetof(DeformRasterVertex, srcX)));
    m_gl->glBindVertexArray(0);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Create temp texture for single-tile preview rendering
    m_gl->glGenTextures(1, &m_tempTex);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_tempTex);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Initial placeholder format; re-specified per transform in buildSourceAtlas
    // to match the source grid's per-document format (glCopyImageSubData /
    // sampling need format-compatible textures, not just convertible).
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, tileGLInternalFormat(kDefaultTileFormat), TILE_SIZE,
        TILE_SIZE, 0, GL_RGBA, tileGLPixelType(kDefaultTileFormat), nullptr);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    m_initialized = true;
    return Result<void>::ok();
}

void GLTransformRenderer::shutdown()
{
    if (!m_initialized)
        return;

    destroySourceAtlas();
    m_transformProgram.reset();
    m_quadTransformProgram.reset();
    m_forwardDeformProgram.reset();
    m_maskBaseProgram.reset();

    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_emptyVAO) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVAO);
        m_emptyVAO = 0;
    }
    if (m_deformMeshVAO) {
        m_gl->glDeleteVertexArrays(1, &m_deformMeshVAO);
        m_deformMeshVAO = 0;
    }
    if (m_deformMeshVBO) {
        m_gl->glDeleteBuffers(1, &m_deformMeshVBO);
        m_deformMeshVBO = 0;
    }
    if (m_deformMeshEBO) {
        m_gl->glDeleteBuffers(1, &m_deformMeshEBO);
        m_deformMeshEBO = 0;
    }
    if (m_tempTex) {
        m_gl->glDeleteTextures(1, &m_tempTex);
        m_tempTex = 0;
    }
    if (m_pbo) {
        m_gl->glDeleteBuffers(1, &m_pbo);
        m_pbo = 0;
        m_pboSize = 0;
    }

    m_cachedDeformTargets.clear();
    m_cachedDeformTileBatches.clear();
    m_cachedDeformBounds = {};
    m_cachedDeformRows = 0;
    m_cachedDeformCols = 0;
    m_cachedDeformIndexCount = 0;
    m_hasCachedDeformMesh = false;
    m_cachedDeformForwardReady = false;

    m_initialized = false;
}

// ==========================================================================
//   A T L A S   M A N A G E M E N T
// ==========================================================================

void GLTransformRenderer::buildSourceAtlas(
    const TileGrid& srcGrid, GLTileRenderer* tileRenderer, bool useNearestFilter)
{
    destroySourceAtlas();

    // Capture the source grid's infinite background so the apply shaders can
    // return it (instead of transparent) for samples outside the atlas / source
    // content — see m_srcBg* in the header. Transparent for every normal grid.
    {
        uint8_t br = 0, bg = 0, bb = 0, ba = 0;
        srcGrid.defaultFill(br, bg, bb, ba);
        m_srcBgR = br / 255.0f;
        m_srcBgG = bg / 255.0f;
        m_srcBgB = bb / 255.0f;
        m_srcBgA = ba / 255.0f;
    }

    // Keep the single-tile preview target in the source grid's per-document
    // format so it stays format-compatible with the tile textures it renders
    // from / is composited against (glCopyImageSubData / sampling).
    if (m_tempTex != 0 && srcGrid.format() != m_tempTexFormat) {
        m_tempTexFormat = srcGrid.format();
        m_gl->glBindTexture(GL_TEXTURE_2D, m_tempTex);
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, tileGLInternalFormat(srcGrid.format()), TILE_SIZE,
            TILE_SIZE, 0, GL_RGBA, tileGLPixelType(srcGrid.format()), nullptr);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (srcGrid.empty() || !tileRenderer)
        return;

    // 1. Compute tile bounds
    int32_t minTX = std::numeric_limits<int32_t>::max();
    int32_t minTY = std::numeric_limits<int32_t>::max();
    int32_t maxTX = std::numeric_limits<int32_t>::min();
    int32_t maxTY = std::numeric_limits<int32_t>::min();

    for (const auto& [key, tile] : srcGrid.tiles()) {
        minTX = std::min(minTX, key.x);
        minTY = std::min(minTY, key.y);
        maxTX = std::max(maxTX, key.x);
        maxTY = std::max(maxTY, key.y);
    }

    m_atlasMinTX = minTX;
    m_atlasMinTY = minTY;
    m_atlasCols = maxTX - minTX + 1;
    m_atlasRows = maxTY - minTY + 1;
    m_atlasWidth = m_atlasCols * TILE_SIZE;
    m_atlasHeight = m_atlasRows * TILE_SIZE;

    // 2. Create atlas texture
    m_gl->glGenTextures(1, &m_atlasTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    const GLint filter = useNearestFilter ? GL_NEAREST : GL_LINEAR;
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Allocate with transparent black. Internal format must match the source
    // tiles' format — glCopyImageSubData below copies raw texels (no conversion)
    // and requires format-compatible src/dst.
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, tileGLInternalFormat(srcGrid.format()), m_atlasWidth,
        m_atlasHeight, 0, GL_RGBA, tileGLPixelType(srcGrid.format()), nullptr);

    // Clear to transparent
    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_atlasTexture, 0);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);
    m_gl->glViewport(0, 0, m_atlasWidth, m_atlasHeight);
    // Clear to the grid's default-fill background so tile positions that don't
    // exist inside the atlas bounding box (e.g. a hide-all mask's black gaps)
    // carry the background instead of transparent. Normal grids have a
    // transparent default fill, so this is identical to clearing to transparent.
    {
        uint8_t fr = 0, fg = 0, fb = 0, fa = 0;
        srcGrid.defaultFill(fr, fg, fb, fa);
        m_gl->glClearColor(fr / 255.0f, fg / 255.0f, fb / 255.0f, fa / 255.0f);
    }
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    // 3. Copy each tile's GPU texture into the atlas at the correct position
    for (const auto& [key, tile] : srcGrid.tiles()) {
        if (!tile.hasTexture()) {
            // Upload CPU data if needed
            TileData& mutableTile = const_cast<TileData&>(tile);
            tileRenderer->ensureTileTexture(mutableTile);
            tileRenderer->uploadTileData(mutableTile);
        } else if (tile.isDirty()) {
            TileData& mutableTile = const_cast<TileData&>(tile);
            tileRenderer->uploadTileData(mutableTile);
        }

        if (!tile.hasTexture())
            continue;

        // Position in atlas
        int32_t col = key.x - m_atlasMinTX;
        int32_t row = key.y - m_atlasMinTY;
        int pixelX = col * TILE_SIZE;
        int pixelY = row * TILE_SIZE;

        // Copy tile texture to atlas using glCopyImageSubData (GL 4.3+)
        m_gl->glCopyImageSubData(tile.textureId(), GL_TEXTURE_2D, 0, 0, 0, 0, // source offset
            m_atlasTexture, GL_TEXTURE_2D, 0, pixelX, pixelY, 0, // dest offset
            TILE_SIZE, TILE_SIZE, 1); // size
    }

    // Restore state
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

void GLTransformRenderer::buildMaskAtlas(const TileGrid& maskGrid, GLTileRenderer* tileRenderer)
{
    if (!tileRenderer)
        return;
    if (maskGrid.empty()) {
        if (m_maskAtlasTexture) {
            m_gl->glDeleteTextures(1, &m_maskAtlasTexture);
            m_maskAtlasTexture = 0;
        }
        m_maskAtlasMinTX = m_maskAtlasMinTY = 0;
        m_maskAtlasCols = m_maskAtlasRows = 0;
        m_maskAtlasWidth = m_maskAtlasHeight = 0;
        return;
    }

    if (m_maskAtlasTexture) {
        m_gl->glDeleteTextures(1, &m_maskAtlasTexture);
        m_maskAtlasTexture = 0;
    }

    int32_t minTX = std::numeric_limits<int32_t>::max();
    int32_t minTY = std::numeric_limits<int32_t>::max();
    int32_t maxTX = std::numeric_limits<int32_t>::min();
    int32_t maxTY = std::numeric_limits<int32_t>::min();

    for (const auto& [key, tile] : maskGrid.tiles()) {
        minTX = std::min(minTX, key.x);
        minTY = std::min(minTY, key.y);
        maxTX = std::max(maxTX, key.x);
        maxTY = std::max(maxTY, key.y);
    }

    m_maskAtlasMinTX = minTX;
    m_maskAtlasMinTY = minTY;
    m_maskAtlasCols = maxTX - minTX + 1;
    m_maskAtlasRows = maxTY - minTY + 1;
    m_maskAtlasWidth = m_maskAtlasCols * TILE_SIZE;
    m_maskAtlasHeight = m_maskAtlasRows * TILE_SIZE;

    m_gl->glGenTextures(1, &m_maskAtlasTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_maskAtlasTexture);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Match mask grid format (glCopyImageSubData requires format-compatible).
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, tileGLInternalFormat(maskGrid.format()), m_maskAtlasWidth,
        m_maskAtlasHeight, 0, GL_RGBA, tileGLPixelType(maskGrid.format()), nullptr);

    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_maskAtlasTexture, 0);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);
    m_gl->glViewport(0, 0, m_maskAtlasWidth, m_maskAtlasHeight);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    for (const auto& [key, tile] : maskGrid.tiles()) {
        if (!tile.hasTexture()) {
            TileData& mutableTile = const_cast<TileData&>(tile);
            tileRenderer->ensureTileTexture(mutableTile);
            tileRenderer->uploadTileData(mutableTile);
        } else if (tile.isDirty()) {
            TileData& mutableTile = const_cast<TileData&>(tile);
            tileRenderer->uploadTileData(mutableTile);
        }

        if (!tile.hasTexture())
            continue;

        int32_t col = key.x - m_maskAtlasMinTX;
        int32_t row = key.y - m_maskAtlasMinTY;
        int pixelX = col * TILE_SIZE;
        int pixelY = row * TILE_SIZE;

        m_gl->glCopyImageSubData(tile.textureId(), GL_TEXTURE_2D, 0, 0, 0, 0, m_maskAtlasTexture,
            GL_TEXTURE_2D, 0, pixelX, pixelY, 0, TILE_SIZE, TILE_SIZE, 1);
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

void GLTransformRenderer::destroySourceAtlas()
{
    if (m_atlasTexture) {
        m_gl->glDeleteTextures(1, &m_atlasTexture);
        m_atlasTexture = 0;
    }
    m_atlasMinTX = m_atlasMinTY = 0;
    m_atlasCols = m_atlasRows = 0;
    m_atlasWidth = m_atlasHeight = 0;

    if (m_maskAtlasTexture) {
        m_gl->glDeleteTextures(1, &m_maskAtlasTexture);
        m_maskAtlasTexture = 0;
    }
    m_maskAtlasMinTX = m_maskAtlasMinTY = 0;
    m_maskAtlasCols = m_maskAtlasRows = 0;
    m_maskAtlasWidth = m_maskAtlasHeight = 0;
}

Rect GLTransformRenderer::computeRenderBounds(const TransformState& state, bool useMask) const
{
    Rect bounds = state.transformedAABB();
    if (useMask) {
        Rect srcAABB(static_cast<float>(m_atlasMinTX) * TILE_SIZE,
            static_cast<float>(m_atlasMinTY) * TILE_SIZE,
            static_cast<float>(m_atlasCols) * TILE_SIZE,
            static_cast<float>(m_atlasRows) * TILE_SIZE);
        float minX = std::min(bounds.left(), srcAABB.left());
        float minY = std::min(bounds.top(), srcAABB.top());
        float maxX = std::max(bounds.right(), srcAABB.right());
        float maxY = std::max(bounds.bottom(), srcAABB.bottom());
        bounds = { minX, minY, maxX - minX, maxY - minY };
    }

    constexpr float margin = 2.0f;
    bounds.x -= margin;
    bounds.y -= margin;
    bounds.width += margin * 2.0f;
    bounds.height += margin * 2.0f;
    return bounds;
}

bool GLTransformRenderer::tileIntersectsRenderBounds(
    const TileKey& destKey, const TransformState& state, bool useMask) const
{
    const Rect renderBounds = computeRenderBounds(state, useMask);
    const Rect tileBounds(static_cast<float>(destKey.x) * TILE_SIZE,
        static_cast<float>(destKey.y) * TILE_SIZE, static_cast<float>(TILE_SIZE),
        static_cast<float>(TILE_SIZE));
    return renderBounds.intersects(tileBounds);
}

bool GLTransformRenderer::ensureForwardDeformMesh(const TransformState& state)
{
    if (!state.deformMesh.has_value() || !m_deformMeshVAO || !m_deformMeshVBO || !m_deformMeshEBO) {
        return false;
    }

    const auto& mesh = *state.deformMesh;
    std::vector<Vector2> targets;
    targets.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        targets.push_back(vertex.target);
    }

    bool cacheMatches = m_hasCachedDeformMesh && m_cachedDeformRows == mesh.rows
        && m_cachedDeformCols == mesh.cols && sameRect(m_cachedDeformBounds, state.contentBounds)
        && m_cachedDeformTargets.size() == targets.size();
    if (cacheMatches) {
        for (size_t i = 0; i < targets.size(); ++i) {
            if (m_cachedDeformTargets[i].x != targets[i].x
                || m_cachedDeformTargets[i].y != targets[i].y) {
                cacheMatches = false;
                break;
            }
        }
    }
    if (cacheMatches) {
        return m_cachedDeformForwardReady;
    }

    const int segX = kForwardDeformGrid;
    const int segY = kForwardDeformGrid;
    const int vertsX = segX + 1;
    const int vertsY = segY + 1;

    std::vector<DeformRasterVertex> vertices;
    vertices.reserve(static_cast<size_t>(vertsX * vertsY));
    std::vector<Vector2> destSamples;
    destSamples.reserve(static_cast<size_t>(vertsX * vertsY));
    std::unordered_map<TileKey, std::vector<uint32_t>, TileKeyHash> tileIndices;
    tileIndices.reserve(static_cast<size_t>(segX * segY));

    for (int y = 0; y < vertsY; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(segY);
        const float srcY = state.contentBounds.top() + v * state.contentBounds.height;
        for (int x = 0; x < vertsX; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segX);
            const float srcX = state.contentBounds.left() + u * state.contentBounds.width;
            const Vector2 dest = state.evaluateBSplineSurface(u, v);
            vertices.push_back({ dest.x, dest.y, srcX, srcY });
            destSamples.push_back(dest);
        }
    }

    int validTriangleCount = 0;
    auto pushTriangleIfValid = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        const Vector2& p0 = destSamples[static_cast<size_t>(i0)];
        const Vector2& p1 = destSamples[static_cast<size_t>(i1)];
        const Vector2& p2 = destSamples[static_cast<size_t>(i2)];
        const float area2 = TransformState::triangleArea2(p0, p1, p2);
        // Use absolute area: triangleArea2 is signed (positive CCW, negative
        // CW). When the B-spline surface folds, back-folded cells flip
        // winding and produce negative area; those triangles must still
        // rasterize so the folded region composites onto the front side via
        // premultiplied src-over. Only truly degenerate (near-zero-area)
        // triangles are skipped.
        if (std::abs(area2) <= kDegenerateTriangleArea) {
            return;
        }
        ++validTriangleCount;

        const float minX = std::min({ p0.x, p1.x, p2.x });
        const float minY = std::min({ p0.y, p1.y, p2.y });
        const float maxX = std::max({ p0.x, p1.x, p2.x });
        const float maxY = std::max({ p0.y, p1.y, p2.y });
        const int32_t minTX = static_cast<int32_t>(std::floor(minX / TILE_SIZE));
        const int32_t minTY = static_cast<int32_t>(std::floor(minY / TILE_SIZE));
        const int32_t maxTX = static_cast<int32_t>(std::floor(maxX / TILE_SIZE));
        const int32_t maxTY = static_cast<int32_t>(std::floor(maxY / TILE_SIZE));

        for (int32_t ty = minTY; ty <= maxTY; ++ty) {
            for (int32_t tx = minTX; tx <= maxTX; ++tx) {
                auto& batch = tileIndices[TileKey { tx, ty }];
                batch.push_back(i0);
                batch.push_back(i1);
                batch.push_back(i2);
            }
        }
    };

    for (int y = 0; y < segY; ++y) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * vertsX + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = static_cast<uint32_t>((y + 1) * vertsX + x);
            const uint32_t i3 = i2 + 1;
            pushTriangleIfValid(i0, i1, i3);
            pushTriangleIfValid(i0, i3, i2);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(validTriangleCount * 3));
    std::unordered_map<TileKey, DeformTileBatch, TileKeyHash> tileBatches;
    tileBatches.reserve(tileIndices.size());
    for (auto& entry : tileIndices) {
        DeformTileBatch batch;
        batch.indexOffset = static_cast<GLintptr>(indices.size() * sizeof(uint32_t));
        batch.indexCount = static_cast<GLsizei>(entry.second.size());
        indices.insert(indices.end(), entry.second.begin(), entry.second.end());
        tileBatches.emplace(entry.first, batch);
    }

    m_gl->glBindVertexArray(m_deformMeshVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_deformMeshVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(DeformRasterVertex)), vertices.data(),
        GL_DYNAMIC_DRAW);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_deformMeshEBO);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
        indices.empty() ? nullptr : indices.data(), GL_DYNAMIC_DRAW);
    m_gl->glBindVertexArray(0);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_cachedDeformTargets = std::move(targets);
    m_cachedDeformTileBatches = std::move(tileBatches);
    m_cachedDeformBounds = state.contentBounds;
    m_cachedDeformRows = mesh.rows;
    m_cachedDeformCols = mesh.cols;
    m_cachedDeformIndexCount = static_cast<GLsizei>(indices.size());
    m_hasCachedDeformMesh = true;
    // Keep the forward path even when a few cells produced degenerate/folded
    // triangles: dropping those individual triangles is far better than falling
    // back to the inverse-Newton shader, which has multi-solution failures in
    // exactly the regions where folds appear (extreme interior-point moves).
    m_cachedDeformForwardReady = (validTriangleCount > 0);
    return m_cachedDeformForwardReady;
}

bool GLTransformRenderer::drawForwardDeformTile(
    const TileKey& destKey, bool useMask, bool preserveMaskedSource)
{
    if (!m_forwardDeformProgram || !m_deformMeshVAO) {
        return false;
    }

    const auto batchIt = m_cachedDeformTileBatches.find(destKey);
    const bool hasMeshForTile
        = batchIt != m_cachedDeformTileBatches.end() && batchIt->second.indexCount > 0;
    if (!hasMeshForTile && !useMask) {
        return false;
    }

    const float tileOriginX = static_cast<float>(destKey.x) * TILE_SIZE;
    const float tileOriginY = static_cast<float>(destKey.y) * TILE_SIZE;

    if (useMask) {
        if (!m_maskBaseProgram) {
            return false;
        }
        m_maskBaseProgram->use();
        bindSharedUniforms(
            m_maskBaseProgram.get(), true, preserveMaskedSource, tileOriginX, tileOriginY);
        m_gl->glDisable(GL_BLEND);
        m_gl->glBindVertexArray(m_emptyVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    m_forwardDeformProgram->use();
    bindSharedUniforms(
        m_forwardDeformProgram.get(), useMask, preserveMaskedSource, tileOriginX, tileOriginY);
    m_forwardDeformProgram->setUniform("uContentBounds", m_cachedDeformBounds.left(),
        m_cachedDeformBounds.top(), m_cachedDeformBounds.right(), m_cachedDeformBounds.bottom());
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (hasMeshForTile) {
        m_gl->glBindVertexArray(m_deformMeshVAO);
        m_gl->glDrawElements(GL_TRIANGLES, batchIt->second.indexCount, GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(batchIt->second.indexOffset));
        m_gl->glBindVertexArray(0);
    }
    m_gl->glDisable(GL_BLEND);
    return hasMeshForTile || useMask;
}

void GLTransformRenderer::bindSharedUniforms(GLShaderProgram* program, bool useMask,
    bool preserveMaskedSource, float tileOriginX, float tileOriginY)
{
    if (!program)
        return;
    program->setUniform("uAtlas", 0);
    program->setUniform(
        "uAtlasSize", static_cast<float>(m_atlasWidth), static_cast<float>(m_atlasHeight));
    program->setUniform(
        "uAtlasMinTile", static_cast<float>(m_atlasMinTX), static_cast<float>(m_atlasMinTY));
    program->setUniform("uDestTileOrigin", tileOriginX, tileOriginY);
    program->setUniform("uTileSize", static_cast<float>(TILE_SIZE));
    program->setUniform("uUseMask", useMask ? 1 : 0);
    program->setUniform("uPreserveMaskedSource", preserveMaskedSource ? 1 : 0);
    program->setUniform("uSourceBackgroundColor", m_srcBgR, m_srcBgG, m_srcBgB, m_srcBgA);
    if (useMask) {
        program->setUniform("uMaskAtlas", 1);
        program->setUniform("uMaskAtlasSize", static_cast<float>(m_maskAtlasWidth),
            static_cast<float>(m_maskAtlasHeight));
        program->setUniform("uMaskAtlasMinTile", static_cast<float>(m_maskAtlasMinTX),
            static_cast<float>(m_maskAtlasMinTY));
    }
}

// ==========================================================================
//   P R E V I E W   R E N D E R I N G
// ==========================================================================

GLuint GLTransformRenderer::renderTransformedTile(
    const TileKey& destKey, const TransformState& state, bool preserveMaskedSource)
{
    if (!m_initialized || !m_atlasTexture)
        return 0;
    const bool useMask = (m_maskAtlasTexture != 0);
    const bool useMesh = state.hasDeformMesh();
    const bool useQuad = state.hasFreeQuad();
    if (useMesh && !tileIntersectsRenderBounds(destKey, state, useMask))
        return 0;
    const bool useForwardMesh = useMesh && m_forwardDeformProgram && m_deformMeshVAO
        && (!useMask || m_maskBaseProgram) && ensureForwardDeformMesh(state);
    // Deform mode requires the forward-rasterized path; the legacy Newton
    // fallback shader has been removed (forward path handles folds correctly,
    // and the few pathological cases where ensureForwardDeformMesh returns
    // false would also fail Newton).
    if (useMesh && !useForwardMesh)
        return 0;
    if (useQuad && !m_quadTransformProgram)
        return 0;
    if (!useMesh && !useQuad && !m_transformProgram)
        return 0;

    // Save GL state
    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Render into temp texture
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tempTex, 0);
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glDisable(GL_BLEND);

    // Clear to the source grid's background (transparent for normal grids; the
    // hide-all background for an inverted mask, so deform gaps / uncovered areas
    // stay hidden instead of reading as reveal-all).
    m_gl->glClearColor(m_srcBgR, m_srcBgG, m_srcBgB, m_srcBgA);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    float tileOriginX = static_cast<float>(destKey.x) * TILE_SIZE;
    float tileOriginY = static_cast<float>(destKey.y) * TILE_SIZE;

    // Deform mode: drawForwardDeformTile sets up its own program and uniforms.
    // Skip the global setup that follows.
    if (!useMesh && useQuad) {
        const auto& q = *state.freeCorners;
        float l = state.contentBounds.left();
        float t = state.contentBounds.top();
        float r = state.contentBounds.right();
        float b = state.contentBounds.bottom();

        m_quadTransformProgram->use();
        bindSharedUniforms(
            m_quadTransformProgram.get(), useMask, preserveMaskedSource, tileOriginX, tileOriginY);
        m_quadTransformProgram->setUniform(
            "uAtlasTileCount", static_cast<float>(m_atlasCols), static_cast<float>(m_atlasRows));
        m_quadTransformProgram->setUniform("uQ0", q[0].x, q[0].y);
        m_quadTransformProgram->setUniform("uQ1", q[1].x, q[1].y);
        m_quadTransformProgram->setUniform("uQ2", q[2].x, q[2].y);
        m_quadTransformProgram->setUniform("uQ3", q[3].x, q[3].y);
        m_quadTransformProgram->setUniform("uContentBounds", l, t, r, b);
    } else if (!useMesh) {
        auto invMatrix = computeInverseMatrix(state);
        const float l = state.contentBounds.left();
        const float t = state.contentBounds.top();
        const float r = state.contentBounds.right();
        const float b = state.contentBounds.bottom();
        m_transformProgram->use();
        bindSharedUniforms(
            m_transformProgram.get(), useMask, preserveMaskedSource, tileOriginX, tileOriginY);
        m_transformProgram->setUniform(
            "uAtlasTileCount", static_cast<float>(m_atlasCols), static_cast<float>(m_atlasRows));
        m_transformProgram->setUniform("uContentBounds", l, t, r, b);

        GLint matLoc
            = m_gl->glGetUniformLocation(m_transformProgram->handle(), "uInverseTransform");
        m_gl->glUniformMatrix3fv(matLoc, 1, GL_FALSE, invMatrix.data());
    }

    // Bind atlas
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    if (useMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_maskAtlasTexture);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }

    if (useForwardMesh) {
        if (!drawForwardDeformTile(destKey, useMask, preserveMaskedSource)) {
            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_2D, 0);
            if (useMask) {
                m_gl->glActiveTexture(GL_TEXTURE1);
                m_gl->glBindTexture(GL_TEXTURE_2D, 0);
                m_gl->glActiveTexture(GL_TEXTURE0);
            }
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            return 0;
        }
    } else {
        m_gl->glBindVertexArray(m_emptyVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        m_gl->glBindVertexArray(0);
    }

    // Restore state (useMask was set above)
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return m_tempTex;
}

// ==========================================================================
//   G P U   A P P L Y
// ==========================================================================

std::unordered_set<TileKey, TileKeyHash> GLTransformRenderer::applyGPU(const TransformState& state,
    TileGrid& destGrid, GLTileRenderer* tileRenderer, bool preserveMaskedSource)
{
    std::unordered_set<TileKey, TileKeyHash> resultKeys;
    const bool useMesh = state.hasDeformMesh();
    const bool useQuad = state.hasFreeQuad();
    const bool useMask = (m_maskAtlasTexture != 0);
    const bool useForwardMesh = useMesh && m_forwardDeformProgram && m_deformMeshVAO
        && (!useMask || m_maskBaseProgram) && ensureForwardDeformMesh(state);
    if (!m_initialized || !m_atlasTexture || !tileRenderer) {
        return resultKeys;
    }
    // Deform mode requires the forward-rasterized path (Newton fallback removed).
    if (useMesh && !useForwardMesh)
        return resultKeys;
    if (useQuad && !m_quadTransformProgram)
        return resultKeys;
    if (!useMesh && !useQuad && !m_transformProgram)
        return resultKeys;

    // Compute destination AABB
    Rect destAABB = computeRenderBounds(state, useMask);

    int32_t destMinTX = static_cast<int32_t>(std::floor(destAABB.left() / TILE_SIZE));
    int32_t destMinTY = static_cast<int32_t>(std::floor(destAABB.top() / TILE_SIZE));
    int32_t destMaxTX = static_cast<int32_t>(std::floor(destAABB.right() / TILE_SIZE));
    int32_t destMaxTY = static_cast<int32_t>(std::floor(destAABB.bottom() / TILE_SIZE));

    // Save GL state
    GLint prevFBO = 0;
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    m_gl->glGetIntegerv(GL_VIEWPORT, prevViewport);

    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);
    m_gl->glDisable(GL_BLEND);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Deform mode: drawForwardDeformTile sets up its own program per tile.
    if (!useMesh) {
        if (useQuad) {
            const auto& q = *state.freeCorners;
            float l = state.contentBounds.left();
            float t = state.contentBounds.top();
            float r = state.contentBounds.right();
            float b = state.contentBounds.bottom();
            m_quadTransformProgram->use();
            bindSharedUniforms(
                m_quadTransformProgram.get(), useMask, preserveMaskedSource, 0.0f, 0.0f);
            m_quadTransformProgram->setUniform("uAtlasTileCount", static_cast<float>(m_atlasCols),
                static_cast<float>(m_atlasRows));
            m_quadTransformProgram->setUniform("uQ0", q[0].x, q[0].y);
            m_quadTransformProgram->setUniform("uQ1", q[1].x, q[1].y);
            m_quadTransformProgram->setUniform("uQ2", q[2].x, q[2].y);
            m_quadTransformProgram->setUniform("uQ3", q[3].x, q[3].y);
            m_quadTransformProgram->setUniform("uContentBounds", l, t, r, b);
        } else {
            auto invMatrix = computeInverseMatrix(state);
            const float l = state.contentBounds.left();
            const float t = state.contentBounds.top();
            const float r = state.contentBounds.right();
            const float b = state.contentBounds.bottom();
            m_transformProgram->use();
            bindSharedUniforms(m_transformProgram.get(), useMask, preserveMaskedSource, 0.0f, 0.0f);
            m_transformProgram->setUniform("uAtlasTileCount", static_cast<float>(m_atlasCols),
                static_cast<float>(m_atlasRows));
            m_transformProgram->setUniform("uContentBounds", l, t, r, b);
            GLint matLoc
                = m_gl->glGetUniformLocation(m_transformProgram->handle(), "uInverseTransform");
            m_gl->glUniformMatrix3fv(matLoc, 1, GL_FALSE, invMatrix.data());
        }
    }

    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    if (useMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_maskAtlasTexture);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }
    m_gl->glBindVertexArray(m_emptyVAO);

    // activeProgram is only used for the affine/quad fullscreen-quad path
    // below (useMesh routes through drawForwardDeformTile).
    GLShaderProgram* activeProgram
        = useQuad ? m_quadTransformProgram.get() : m_transformProgram.get();

    // Render each destination tile
    for (int32_t ty = destMinTY; ty <= destMaxTY; ++ty) {
        for (int32_t tx = destMinTX; tx <= destMaxTX; ++tx) {
            TileKey key { tx, ty };

            float tileOriginX = static_cast<float>(tx) * TILE_SIZE;
            float tileOriginY = static_cast<float>(ty) * TILE_SIZE;

            // Ensure tile has a GPU texture
            TileData& tile = destGrid.getOrCreateTile(key);
            if (!tile.hasTexture()) {
                tileRenderer->ensureTileTexture(tile);
                // ensureTileTexture unbinds GL_TEXTURE0 — re-bind atlas
                m_gl->glActiveTexture(GL_TEXTURE0);
                m_gl->glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
                if (useMask) {
                    m_gl->glActiveTexture(GL_TEXTURE1);
                    m_gl->glBindTexture(GL_TEXTURE_2D, m_maskAtlasTexture);
                    m_gl->glActiveTexture(GL_TEXTURE0);
                }
            }

            // Render into tile texture
            m_gl->glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.textureId(), 0);
            // Clear to the source grid's background, not transparent: for a
            // hide-all mask (defaultFill = opaque black) the parts of a deform
            // destination tile not covered by the warped mesh must stay hidden
            // (reveal 0). Affine/quad overwrite the whole tile so this is a
            // no-op there; normal grids have a transparent fill = unchanged.
            m_gl->glClearColor(m_srcBgR, m_srcBgG, m_srcBgB, m_srcBgA);
            m_gl->glClear(GL_COLOR_BUFFER_BIT);
            if (useForwardMesh) {
                if (!drawForwardDeformTile(key, useMask, preserveMaskedSource)) {
                    if (tile.hasTexture()) {
                        tileRenderer->destroyTileTexture(tile);
                    }
                    destGrid.removeTile(key);
                    continue;
                }
            } else {
                activeProgram->setUniform("uDestTileOrigin", tileOriginX, tileOriginY);
                m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
            }

            tile.clearDirty();
            destGrid.removeDirty(key);
            // GPU texture changed but the CPU buffer/dirty flag were left as-is
            // (same out-of-band write as the brush flatten), so no markDirty(key)
            // fires. Bump the whole-grid content counter so a whole-layer
            // distortion cache over this grid invalidates and rebuilds; without
            // it, transforming a layer that carries a distortion leaves the
            // cached distorted region stale on tiles that already existed.
            destGrid.notePixelsChangedOutOfBand();
            resultKeys.insert(key);
        }
    }

    m_gl->glBindVertexArray(0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (useMask) {
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    m_gl->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // Prune empty tiles (fully transparent after transform)
    // We need to readback first for accurate pruning; for now mark all as result
    // Empty tile pruning happens after PBO readback in finalization

    return resultKeys;
}

// ==========================================================================
//   A S Y N C   P B O   R E A D B A C K
// ==========================================================================

GLsync GLTransformRenderer::startAsyncReadback(TileGrid& grid, const std::vector<TileKey>& keys)
{
    if (keys.empty())
        return nullptr;

    // Format-sized tile transport (see GLBrushRenderer::startAsyncReadback):
    // RGBA8 == TILE_BYTE_SIZE; wider formats move the full payload + matching type.
    const TilePixelFormat fmt = grid.format();
    const size_t bytesPerTile = tileByteSize(fmt);
    const GLenum pixelType = tileGLPixelType(fmt);

    size_t totalBytes = keys.size() * bytesPerTile;

    if (!m_pbo)
        m_gl->glGenBuffers(1, &m_pbo);

    if (m_pboSize < totalBytes) {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
        m_gl->glBufferData(
            GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(totalBytes), nullptr, GL_STREAM_READ);
        m_pboSize = totalBytes;
    } else {
        m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    size_t offset = 0;
    for (const auto& key : keys) {
        TileData* tile = grid.getTile(key);
        if (!tile || !tile->hasTexture()) {
            offset += bytesPerTile;
            continue;
        }
        m_gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile->textureId(), 0);
        m_gl->glReadPixels(
            0, 0, TILE_SIZE, TILE_SIZE, GL_RGBA, pixelType, reinterpret_cast<void*>(offset));
        offset += bytesPerTile;
    }

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GLsync fence = m_gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_gl->glFlush();

    return fence;
}

bool GLTransformRenderer::isReadbackComplete(GLsync fence)
{
    if (!fence) {
        return true;
    }

    const GLenum result = m_gl->glClientWaitSync(fence, 0, 0);
    return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

void GLTransformRenderer::finishReadback(
    GLsync fence, TileGrid& grid, const std::vector<TileKey>& keys)
{
    if (!fence || keys.empty())
        return;

    m_gl->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 500000000ULL);
    m_gl->glDeleteSync(fence);

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    const uint8_t* mapped
        = static_cast<const uint8_t*>(m_gl->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));

    if (mapped) {
        const size_t bytesPerTile = tileByteSize(grid.format());
        size_t offset = 0;
        for (const auto& key : keys) {
            TileData* tile = grid.getTile(key);
            if (tile) {
                std::memcpy(tile->pixels(), mapped + offset, bytesPerTile);
            }
            offset += bytesPerTile;
        }
        m_gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    m_gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void GLTransformRenderer::deleteFence(GLsync fence)
{
    if (fence)
        m_gl->glDeleteSync(fence);
}

// ==========================================================================
//   I N V E R S E   M A T R I X   C O M P U T A T I O N
// ==========================================================================

std::array<float, 9> GLTransformRenderer::computeInverseMatrix(const TransformState& state)
{
    // Forward transform: result = R * S * (p - pivot) + pivot + translation
    //
    // Forward matrix M (3x3):
    //   | sx*cos  -sy*sin  px + tx - px*sx*cos + py*sy*sin |
    //   | sx*sin   sy*cos  py + ty - px*sx*sin - py*sy*cos |
    //   |   0        0      1                               |
    //
    // Inverse: M^-1 = [A^-1 | -A^-1 * t]
    //                  [  0  |     1     ]
    //
    // A = | sx*cos  -sy*sin |   det(A) = sx*sy
    //     | sx*sin   sy*cos |
    //
    // A^-1 = (1/det) * |  sy*cos   sy*sin |   = | cos/sx   sin/sx |
    //                   | -sx*sin   sx*cos |     | -sin/sy  cos/sy |

    float sx = state.scale.x;
    float sy = state.scale.y;
    float cosR = std::cos(state.rotation);
    float sinR = std::sin(state.rotation);
    float px = state.pivot.x;
    float py = state.pivot.y;
    float tx = state.translation.x;
    float ty = state.translation.y;

    // Guard against zero scale
    float invSx = (std::abs(sx) > 0.0001f) ? 1.0f / sx : 0.0f;
    float invSy = (std::abs(sy) > 0.0001f) ? 1.0f / sy : 0.0f;

    // A^-1 components
    float a00 = cosR * invSx;
    float a01 = sinR * invSx;
    float a10 = -sinR * invSy;
    float a11 = cosR * invSy;

    // Forward translation part
    float fwdTx = px + tx - px * sx * cosR + py * sy * sinR;
    float fwdTy = py + ty - px * sx * sinR - py * sy * cosR;

    // Inverse translation: -A^-1 * t
    float invTx = -(a00 * fwdTx + a01 * fwdTy);
    float invTy = -(a10 * fwdTx + a11 * fwdTy);

    // Column-major 3x3 for GLSL
    return {
        a00, a10, 0.0f, // column 0
        a01, a11, 0.0f, // column 1
        invTx, invTy, 1.0f // column 2
    };
}

} // namespace aether
