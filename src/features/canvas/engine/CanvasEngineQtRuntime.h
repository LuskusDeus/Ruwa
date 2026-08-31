// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   E N G I N E   Q T   R U N T I M E
// ==========================================================================
// Process-level Qt integration/factory for the canvas rendering engine
// (plan 7.31.1). One selected runtime exists at application/composition-root
// scope. It owns engine-specific process bootstrap (surface-format policy,
// offscreen GL subsystem warm-up, shader-cache warm-up, per-window
// presentation preparation) and creates per-canvas Qt bindings.
//
// This is a Qt integration seam, NOT CanvasEngineSession and not future
// engine-core API: TON618 provides its own runtime without changing
// Application, StartupController, MainWindow, WindowSetupCoordinator or
// CanvasPanel feature code.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINEQTRUNTIME_H
#define RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINEQTRUNTIME_H

#include "features/canvas/CanvasBoundsMode.h"
#include "features/canvas/engine/CanvasEngineTypes.h"

#include <QSize>
#include <QString>

#include <functional>
#include <memory>

class QWidget;

namespace aether {
class OpenGLCanvasWidget;
} // namespace aether

namespace ruwa::ui::workspace {

class CanvasEngineQtBinding;

/// Neutral warm-up progress reported by the runtime. User-facing wording and
/// progress presentation remain application policy (plan 7.6.47); the runtime
/// supplies stage facts.
struct CanvasEngineWarmupStage {
    QString message;
    int percentage = 0;
};
using CanvasEngineWarmupSink = std::function<void(const CanvasEngineWarmupStage&)>;

/// Application-side blocking decision shown when an operation needs to
/// convert isolated layer content (plan 7.15.4). Receives a title and a
/// message; returns true when the user confirmed. The renderer never opens a
/// dialog itself — with no provider injected the operation is declined.
using CanvasDecisionProvider = std::function<bool(const QString& title, const QString& message)>;

/// Construction-time configuration for one per-canvas binding.
struct CanvasEngineCreateInfo {
    QSize initialCanvasSize;
    ruwa::core::canvas::CanvasBoundsMode boundsMode = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    float lassoStabilization = 0.0f;
    float lassoFillStabilization = 0.0f;
    /// Application-owned rasterization decision provider (plan 7.15.4).
    CanvasDecisionProvider rasterizationDecisionProvider;
};

class CanvasEngineQtRuntime {
public:
    virtual ~CanvasEngineQtRuntime() = default;

    /// Engine-specific Qt surface-format policy that must be applied before
    /// windows/widgets are created. The composition/bootstrap layer calls this
    /// around application construction.
    virtual void applySurfaceFormatPolicy() = 0;

    /// Process-level engine subsystem initialization (offscreen context and
    /// surface, hidden presentation warm-up). Idempotent.
    virtual void initialize() = 0;

    /// Engine shader/PSO cache warm-up. Reports neutral stage facts through
    /// @p progress; returns false on failure. May be a no-op for engines
    /// without a shader cache.
    virtual bool warmUpShaders(const CanvasEngineWarmupSink& progress) = 0;

    /// Prepare one top-level window for canvas presentation (legacy engines
    /// may keep a hidden warm-up widget; the implementation detail stays here).
    virtual void prepareTopLevelWindow(QWidget* window) = 0;

    /// Create one per-canvas Qt binding. The binding owns teardown authority
    /// for that canvas (plan 7.17).
    virtual std::unique_ptr<CanvasEngineQtBinding> createBinding(
        const CanvasEngineCreateInfo& createInfo, QWidget* hostParent)
        = 0;

    /// Release process-level engine resources. Idempotent.
    virtual void shutdown() = 0;
};

/// TRANSITIONAL QUARANTINE (Stage 1): the concrete legacy renderer owned by
/// @p binding, for application call sites that have not been migrated onto
/// session capabilities yet. Declared here (engine-neutral seam), defined by
/// each engine's binding; every caller is enumerated in
/// docs/renderer-boundary-quarantine.md. Do not use in new code, and never
/// cast the binding's host widget to the renderer through any other route.
aether::OpenGLCanvasWidget* aetherLegacyRenderer(CanvasEngineQtBinding& binding);

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINEQTRUNTIME_H
