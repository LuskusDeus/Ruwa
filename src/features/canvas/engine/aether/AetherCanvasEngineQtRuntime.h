// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   A E T H E R   C A N V A S   E N G I N E   Q T   R U N T I M E
// ==========================================================================
// The legacy Aether process-level Qt integration (plan 7.31.1). Owns the
// current Aether/OpenGL process bootstrap: default surface-format policy, the
// offscreen GL context/surface, hidden-widget GL warm-up, the Aether shader
// cache warm-up, and per-top-level-window presentation preparation. Creates
// per-canvas Aether bindings.
//
// Only the application composition root may name this concrete type. Generic
// Application/Startup/MainWindow/WindowSetupCoordinator code consumes the
// neutral CanvasEngineQtRuntime interface.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_ENGINE_AETHER_AETHERCANVASENGINEQTRUNTIME_H
#define RUWA_FEATURES_CANVAS_ENGINE_AETHER_AETHERCANVASENGINEQTRUNTIME_H

#include "features/canvas/engine/CanvasEngineQtRuntime.h"

#include <QPointer>

#include <memory>

class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLWidget;

namespace ruwa::ui::workspace {

class AetherCanvasEngineQtRuntime final : public CanvasEngineQtRuntime {
public:
    AetherCanvasEngineQtRuntime();
    ~AetherCanvasEngineQtRuntime() override;

    AetherCanvasEngineQtRuntime(const AetherCanvasEngineQtRuntime&) = delete;
    AetherCanvasEngineQtRuntime& operator=(const AetherCanvasEngineQtRuntime&) = delete;

    // --- CanvasEngineQtRuntime ---
    void applySurfaceFormatPolicy() override;
    void initialize() override;
    bool warmUpShaders(const CanvasEngineWarmupSink& progress) override;
    void prepareTopLevelWindow(QWidget* window) override;
    std::unique_ptr<CanvasEngineQtBinding> createBinding(
        const CanvasEngineCreateInfo& createInfo, QWidget* hostParent) override;
    void shutdown() override;

private:
    QOffscreenSurface* m_glSurface = nullptr;
    QOpenGLContext* m_glContext = nullptr;
    QOpenGLWidget* m_processWarmupWidget = nullptr;
    /// Parented to the top-level window, so Qt may destroy it with that
    /// window; QPointer keeps the runtime's handle honest in that case.
    QPointer<QOpenGLWidget> m_windowWarmupWidget;
    bool m_initialized = false;
    bool m_surfaceFormatApplied = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_ENGINE_AETHER_AETHERCANVASENGINEQTRUNTIME_H
