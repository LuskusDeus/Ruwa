// SPDX-License-Identifier: MPL-2.0

#include "shared/rendering/OffscreenGlassRenderer.h"

#include "features/canvas/rendering/CanvasBackdropRenderer.h"
#include "shared/rendering/ShaderDirectoryResolver.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>

#include <algorithm>

namespace ruwa::shared::rendering {

namespace {

/// Bucketed capacity, so a widget that resizes by a pixel a frame - the banner
/// does exactly that while the update panel slides in - does not reallocate
/// immutable storage on every one of those frames.
int bucketedCapacity(int value)
{
    constexpr int kBucket = 64;
    return std::max(kBucket, ((value + kBucket - 1) / kBucket) * kBucket);
}

} // namespace

OffscreenGlassRenderer& OffscreenGlassRenderer::instance()
{
    static OffscreenGlassRenderer renderer;
    return renderer;
}

OffscreenGlassRenderer::~OffscreenGlassRenderer()
{
    // Whatever is left here is being torn down after the platform integration
    // has gone; shutdown() is what actually releases the GL objects, and it
    // runs on aboutToQuit while a context can still be made current.
    m_renderer.reset();
}

bool OffscreenGlassRenderer::ensureInitialized()
{
    if (m_initialized) {
        return true;
    }
    if (m_unavailable) {
        return false;
    }
    // Latched up front: every early return below is a permanent failure, and
    // the caller repaints often enough that retrying would mean rebuilding a
    // context on every frame.
    m_unavailable = true;

    auto shaderDir = aether::resolveRuntimeShaderDirectory();
    if (!shaderDir) {
        return false;
    }

    const auto abandon = [this]() {
        m_renderer.reset();
        delete m_context;
        m_context = nullptr;
        delete m_surface;
        m_surface = nullptr;
        m_gl = nullptr;
        return false;
    };

    const QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    m_surface = new QOffscreenSurface;
    m_surface->setFormat(format);
    m_surface->create();
    if (!m_surface->isValid()) {
        return abandon();
    }

    m_context = new QOpenGLContext;
    m_context->setFormat(format);
    if (!m_context->create() || !m_context->makeCurrent(m_surface)) {
        return abandon();
    }

    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(m_context);
    if (!m_gl) {
        m_context->doneCurrent();
        return abandon();
    }

    m_renderer = std::make_unique<aether::CanvasBackdropRenderer>(m_gl);
    const auto initResult = m_renderer->initialize(shaderDir.value());
    m_context->doneCurrent();
    if (!initResult) {
        return abandon();
    }

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [this]() { shutdown(); });

    m_initialized = true;
    m_unavailable = false;
    return true;
}

void OffscreenGlassRenderer::shutdown()
{
    if (!m_context) {
        return;
    }
    if (m_context->makeCurrent(m_surface)) {
        releaseSurfaces();
        if (m_renderer) {
            m_renderer->shutdown();
        }
        m_context->doneCurrent();
    }
    m_renderer.reset();
    delete m_context;
    m_context = nullptr;
    delete m_surface;
    m_surface = nullptr;
    m_gl = nullptr;
    m_initialized = false;
    m_unavailable = true;
}

void OffscreenGlassRenderer::releaseSurfaces()
{
    if (!m_gl) {
        return;
    }
    if (m_sourceFbo) {
        m_gl->glDeleteFramebuffers(1, &m_sourceFbo);
        m_sourceFbo = 0;
    }
    if (m_targetFbo) {
        m_gl->glDeleteFramebuffers(1, &m_targetFbo);
        m_targetFbo = 0;
    }
    if (m_sourceTexture) {
        m_gl->glDeleteTextures(1, &m_sourceTexture);
        m_sourceTexture = 0;
    }
    if (m_targetTexture) {
        m_gl->glDeleteTextures(1, &m_targetTexture);
        m_targetTexture = 0;
    }
    m_capacityWidth = 0;
    m_capacityHeight = 0;
}

bool OffscreenGlassRenderer::ensureSurfaces(int width, int height)
{
    if (m_sourceFbo && width <= m_capacityWidth && height <= m_capacityHeight) {
        return true;
    }

    const int capacityWidth = std::max(bucketedCapacity(width), m_capacityWidth);
    const int capacityHeight = std::max(bucketedCapacity(height), m_capacityHeight);
    releaseSurfaces();

    const auto createPair = [this, capacityWidth, capacityHeight](GLuint& texture, GLuint& fbo) {
        m_gl->glCreateTextures(GL_TEXTURE_2D, 1, &texture);
        // Immutable storage: reallocated wholesale above, never respecified.
        m_gl->glTextureStorage2D(texture, 1, GL_RGBA8, capacityWidth, capacityHeight);
        m_gl->glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_gl->glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl->glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gl->glCreateFramebuffers(1, &fbo);
        m_gl->glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, texture, 0);
        return m_gl->glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };

    const bool ok
        = createPair(m_sourceTexture, m_sourceFbo) && createPair(m_targetTexture, m_targetFbo);
    if (!ok) {
        releaseSurfaces();
        return false;
    }
    m_capacityWidth = capacityWidth;
    m_capacityHeight = capacityHeight;
    return true;
}

bool OffscreenGlassRenderer::composeInPlace(
    QImage& backdrop, qreal deviceScale, const std::vector<GlassPlate>& plates)
{
    if (backdrop.isNull() || plates.empty() || deviceScale <= 0.0) {
        return false;
    }
    if (!ensureInitialized()) {
        return false;
    }

    QOpenGLContext* previousContext = QOpenGLContext::currentContext();
    QSurface* previousSurface = previousContext ? previousContext->surface() : nullptr;
    const auto restorePrevious = [previousContext, previousSurface]() {
        if (previousContext && previousSurface) {
            previousContext->makeCurrent(previousSurface);
        }
    };

    if (!m_context->makeCurrent(m_surface)) {
        restorePrevious();
        return false;
    }

    const int width = backdrop.width();
    const int height = backdrop.height();
    if (!ensureSurfaces(width, height)) {
        m_context->doneCurrent();
        restorePrevious();
        return false;
    }

    // GL reads bottom-up and QImage top-down. Flipping on upload - and back on
    // read - anchors the image at the GL origin, so every rect the renderer
    // derives from surfaceHeight lands on the row the caller meant rather than
    // in the unused slack above a bucketed allocation.
    const QImage uploaded = backdrop.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    m_gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(uploaded.bytesPerLine() / 4));
    m_gl->glTextureSubImage2D(
        m_sourceTexture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, uploaded.constBits());
    m_gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // The composite blends over whatever the destination already holds, and it
    // only covers the plates: the destination starts as a copy of the backdrop
    // so the rest of the image survives the round trip untouched.
    m_gl->glBlitNamedFramebuffer(m_sourceFbo, m_targetFbo, 0, 0, width, height, 0, 0, width, height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);

    std::vector<aether::CanvasBackdropRegion> regions;
    regions.reserve(plates.size());
    for (const GlassPlate& plate : plates) {
        if (plate.rect.isEmpty()) {
            continue;
        }
        aether::CanvasBackdropRegion region;
        // The renderer takes logical pixels and applies the scale itself.
        region.rect = QRectF(plate.rect.x() / deviceScale, plate.rect.y() / deviceScale,
            plate.rect.width() / deviceScale, plate.rect.height() / deviceScale);
        region.cornerRadius = plate.cornerRadius / deviceScale;
        region.opacity = plate.opacity;
        region.surfaceTint = plate.surfaceTint;
        region.frostLevels = plate.frostLevels;
        region.shadowStrength = plate.shadowStrength;
        region.refractionStrength = plate.refractionStrength;
        regions.push_back(region);
    }

    const bool rendered = m_renderer->render(
        m_sourceFbo, m_targetFbo, width, height, deviceScale, deviceScale, regions);

    if (rendered) {
        QImage result(width, height, QImage::Format_RGBA8888);
        m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        m_gl->glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(result.bytesPerLine() / 4));
        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_targetFbo);
        m_gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.bits());
        m_gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        const qreal devicePixelRatio = backdrop.devicePixelRatio();
        backdrop
            = result.flipped(Qt::Vertical).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        backdrop.setDevicePixelRatio(devicePixelRatio);
    }

    m_context->doneCurrent();
    restorePrevious();
    return rendered;
}

} // namespace ruwa::shared::rendering
