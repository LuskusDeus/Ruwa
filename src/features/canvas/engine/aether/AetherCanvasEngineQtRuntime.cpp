// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/engine/aether/AetherCanvasEngineQtRuntime.h"

#include "features/canvas/engine/aether/AetherCanvasEngineQtBinding.h"
#include "shared/rendering/GLShaderWarmup.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QWidget>

namespace ruwa::ui::workspace {

AetherCanvasEngineQtRuntime::AetherCanvasEngineQtRuntime() = default;

AetherCanvasEngineQtRuntime::~AetherCanvasEngineQtRuntime()
{
    shutdown();
}

void AetherCanvasEngineQtRuntime::applySurfaceFormatPolicy()
{
    if (m_surfaceFormatApplied) {
        return;
    }
    m_surfaceFormatApplied = true;

    // Legacy Aether surface policy (moved from Application::setupDefaultSettings
    // in Stage 1; behavior preserved verbatim).
    QSurfaceFormat format;
    format.setVersion(4, 5); // OpenGL 4.5
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    // No MSAA. This default format is what the top-level window's compositing
    // surface gets, and everything visible is either raster UI or the canvas
    // QOpenGLWidget which renders into its own non-multisampled FBO — so a 4x
    // multisampled window surface would only add a full-screen resolve + 4x
    // color bandwidth to every present, which weak GPUs pay for in frame time.
    format.setSamples(0);

    QSurfaceFormat::setDefaultFormat(format);
}

void AetherCanvasEngineQtRuntime::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    // Pre-warm the GL subsystem at startup to avoid delay when creating the
    // first canvas; also ensures context sharing works properly.
    m_glSurface = new QOffscreenSurface;
    m_glSurface->setFormat(QSurfaceFormat::defaultFormat());
    m_glSurface->create();

    m_glContext = new QOpenGLContext;
    m_glContext->setFormat(QSurfaceFormat::defaultFormat());

    if (m_glContext->create()) {
        m_glContext->makeCurrent(m_glSurface);

        // Log OpenGL info (vendor/renderer/version strings are read while the
        // context is current; the values intentionally stay Aether-internal).
        const char* vendor
            = reinterpret_cast<const char*>(m_glContext->functions()->glGetString(GL_VENDOR));
        const char* renderer
            = reinterpret_cast<const char*>(m_glContext->functions()->glGetString(GL_RENDERER));
        const char* version
            = reinterpret_cast<const char*>(m_glContext->functions()->glGetString(GL_VERSION));
        Q_UNUSED(vendor);
        Q_UNUSED(renderer);
        Q_UNUSED(version);

        m_glContext->doneCurrent();
    }

    // Hidden OpenGL widget to force Qt to initialize the OpenGL subsystem.
    // This prevents window recreation when the first visible canvas widget is
    // created.
    m_processWarmupWidget = new QOpenGLWidget;
    m_processWarmupWidget->setFixedSize(1, 1);
    m_processWarmupWidget->setAttribute(Qt::WA_DontShowOnScreen);
    m_processWarmupWidget->setAttribute(Qt::WA_QuitOnClose, false);
    m_processWarmupWidget->show(); // Triggers initializeGL()
}

bool AetherCanvasEngineQtRuntime::warmUpShaders(const CanvasEngineWarmupSink& progress)
{
    if (!m_glContext || !m_glSurface) {
        return false;
    }

    auto warmupResult = aether::warmUpOpenGLShaderPrograms(
        m_glContext, m_glSurface, [progress](const QString& message, int percentage) {
            if (progress) {
                progress({ message, percentage });
            }
        });

    if (!warmupResult) {
        return false;
    }
    return true;
}

void AetherCanvasEngineQtRuntime::prepareTopLevelWindow(QWidget* window)
{
    if (!window || m_windowWarmupWidget) {
        return;
    }

    // Pre-warm OpenGL with a hidden widget so Qt initializes the GL subsystem
    // BEFORE any visible canvas widgets are created, preventing window
    // recreation when the canvas is first shown (behavior moved from
    // WindowSetupCoordinator::setupOpenGLWarmup).
    m_windowWarmupWidget = new QOpenGLWidget(window);
    m_windowWarmupWidget->setFixedSize(1, 1);
    m_windowWarmupWidget->setAttribute(Qt::WA_DontShowOnScreen);
    m_windowWarmupWidget->show(); // Triggers initializeGL()
}

std::unique_ptr<CanvasEngineQtBinding> AetherCanvasEngineQtRuntime::createBinding(
    const CanvasEngineCreateInfo& createInfo, QWidget* hostParent)
{
    return std::make_unique<AetherCanvasEngineQtBinding>(createInfo, hostParent);
}

void AetherCanvasEngineQtRuntime::shutdown()
{
    if (m_windowWarmupWidget) {
        m_windowWarmupWidget->deleteLater();
    }
    m_windowWarmupWidget = nullptr;
    delete m_processWarmupWidget;
    m_processWarmupWidget = nullptr;
    delete m_glContext;
    m_glContext = nullptr;
    delete m_glSurface;
    m_glSurface = nullptr;
    m_initialized = false;
}

} // namespace ruwa::ui::workspace
