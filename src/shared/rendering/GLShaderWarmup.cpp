// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   S H A D E R   W A R M U P
// ==========================================================================

#include "shared/rendering/GLShaderWarmup.h"

#include "features/canvas/overlays/CanvasOverlayManager.h"
#include "features/canvas/rendering/GLRenderer.h"
#include "features/selection/GLSelectionRenderer.h"
#include "shared/rendering/ShaderDirectoryResolver.h"

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
namespace aether {

namespace {

void emitWarmupProgress(const std::function<void(const QString&, int)>& callback,
    const QString& message, int percentage)
{
    if (callback) {
        callback(message, percentage);
    }
}

} // namespace

Result<void> warmUpOpenGLShaderPrograms(QOpenGLContext* context, QOffscreenSurface* surface,
    const std::function<void(const QString&, int)>& progressCallback)
{
    if (!context || !surface) {
        return { ErrorCode::InvalidArgument,
            "OpenGL shader warmup requires a valid context and surface" };
    }

    auto shaderDir = resolveRuntimeShaderDirectory();
    if (!shaderDir) {
        return { shaderDir.error().code, shaderDir.error().message };
    }

    if (!context->makeCurrent(surface)) {
        return { ErrorCode::RenderingFailed, "Failed to make warmup OpenGL surface current" };
    }

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(context);
    if (!gl) {
        context->doneCurrent();
        return { ErrorCode::RenderingFailed, "Failed to acquire OpenGL 4.5 functions for warmup" };
    }

    QElapsedTimer totalTimer;
    totalTimer.start();

    emitWarmupProgress(progressCallback, QStringLiteral("Warming renderer shaders..."), 66);
    GLRenderer renderer(gl);
    auto rendererResult = renderer.initialize(shaderDir.value());
    if (!rendererResult) {
        context->doneCurrent();
        return { rendererResult.error().code, rendererResult.error().message };
    }

    emitWarmupProgress(progressCallback, QStringLiteral("Warming overlay shaders..."), 76);
    CanvasOverlayManager overlayManager;
    auto overlayResult = overlayManager.initialize(gl);
    if (!overlayResult) { }

    emitWarmupProgress(progressCallback, QStringLiteral("Warming selection shaders..."), 84);
    GLSelectionRenderer selectionRenderer(gl);
    auto selectionResult = selectionRenderer.initialize();
    if (!selectionResult) { }

    emitWarmupProgress(progressCallback, QStringLiteral("Finalizing OpenGL warmup..."), 92);

    selectionRenderer.shutdown();
    overlayManager.shutdown();
    renderer.shutdown();

    context->doneCurrent();

    return Result<void>::ok();
}

} // namespace aether
