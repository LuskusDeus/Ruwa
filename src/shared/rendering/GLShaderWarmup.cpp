// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   S H A D E R   W A R M U P
// ==========================================================================

#include "shared/rendering/GLShaderWarmup.h"

#include "features/canvas/overlays/CanvasOverlayManager.h"
#include "features/selection/GLSelectionRenderer.h"
#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/RuntimeShaderCatalog.h"
#include "shared/rendering/ShaderDirectoryResolver.h"

#include <QDir>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QStringList>

namespace aether {

namespace {

void emitWarmupProgress(const std::function<void(const QString&, int)>& callback,
    const QString& message, int percentage)
{
    if (callback) {
        callback(message, percentage);
    }
}

QString fromUtf8(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

Result<void> warmUpRuntimeShaderPrograms(QOpenGLFunctions_4_5_Core* gl, const QString& shaderDir,
    const std::function<void(const QString&, int)>& progressCallback)
{
    const QDir directory(shaderDir);
    ErrorCode firstErrorCode = ErrorCode::None;
    QStringList errors;

    for (std::size_t index = 0; index < kRuntimeShaderPrograms.size(); ++index) {
        const auto& definition = kRuntimeShaderPrograms[index];
        const int percentage = 66 + static_cast<int>((index * 14) / kRuntimeShaderPrograms.size());
        emitWarmupProgress(progressCallback,
            QStringLiteral("Warming shader %1/%2: %3...")
                .arg(index + 1)
                .arg(kRuntimeShaderPrograms.size())
                .arg(fromUtf8(definition.name)),
            percentage);

        GLShaderProgram program(gl);
        Result<void> result = definition.type == RuntimeShaderProgramType::Graphics
            ? program.loadFromFiles(directory.filePath(fromUtf8(definition.vertexShader)),
                  directory.filePath(fromUtf8(definition.fragmentShader)))
            : program.loadComputeFromFile(directory.filePath(fromUtf8(definition.computeShader)));
        if (!result) {
            if (firstErrorCode == ErrorCode::None) {
                firstErrorCode = result.error().code == ErrorCode::None
                    ? ErrorCode::ShaderCompilationFailed
                    : result.error().code;
            }
            errors.append(QStringLiteral("%1: %2").arg(
                fromUtf8(definition.name), QString::fromStdString(result.error().message)));
        }
    }

    if (!errors.isEmpty()) {
        return { firstErrorCode,
            QStringLiteral("Runtime shader warmup failed for %1 program(s):\n%2")
                .arg(errors.size())
                .arg(errors.join(QLatin1Char('\n')))
                .toStdString() };
    }

    return Result<void>::ok();
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

    auto runtimeShaderResult = warmUpRuntimeShaderPrograms(gl, shaderDir.value(), progressCallback);
    if (!runtimeShaderResult) {
        context->doneCurrent();
        return runtimeShaderResult;
    }

    emitWarmupProgress(progressCallback, QStringLiteral("Warming overlay shaders..."), 82);
    CanvasOverlayManager overlayManager;
    auto overlayResult = overlayManager.initialize(gl);
    if (!overlayResult) {
        overlayManager.shutdown();
        context->doneCurrent();
        return overlayResult;
    }

    emitWarmupProgress(progressCallback, QStringLiteral("Warming selection shaders..."), 88);
    GLSelectionRenderer selectionRenderer(gl);
    auto selectionResult = selectionRenderer.initialize();
    if (!selectionResult) {
        selectionRenderer.shutdown();
        overlayManager.shutdown();
        context->doneCurrent();
        return selectionResult;
    }

    emitWarmupProgress(progressCallback, QStringLiteral("Finalizing OpenGL warmup..."), 92);

    selectionRenderer.shutdown();
    overlayManager.shutdown();

    context->doneCurrent();

    return Result<void>::ok();
}

} // namespace aether
