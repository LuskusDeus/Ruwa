// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   A E T H E R   C A N V A S   E N G I N E   Q T   B I N D I N G
// ==========================================================================
// The legacy Aether per-canvas Qt binding (Stage 1 decoupling, plan 7.31.2).
//
// This is the sanctioned bridge between the renderer-neutral application
// contract (CanvasEngineQtBinding / CanvasEngineSession / CanvasEngineQtEvents)
// and the concrete Aether implementation. It is the only place — besides the
// legacy renderer itself and its internal helpers — allowed to include
// OpenGLCanvasWidget.h. The adapter is intentionally disposable: when Aether
// is replaced, these files are replaced with the next engine's binding and
// nothing else in Ruwa changes.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_ENGINE_AETHER_AETHERCANVASENGINEQTBINDING_H
#define RUWA_FEATURES_CANVAS_ENGINE_AETHER_AETHERCANVASENGINEQTBINDING_H

#include "features/canvas/engine/CanvasEngineQtRuntime.h"
#include "features/canvas/engine/CanvasEngineSession.h"

#include <memory>

class QWidget;

namespace aether {
class OpenGLCanvasWidget;
} // namespace aether

namespace ruwa::ui::workspace {

class AetherCanvasSession;
class AetherHistoryFacade;
class AetherDocumentFacade;

class AetherCanvasEngineQtBinding final : public CanvasEngineQtBinding {
public:
    AetherCanvasEngineQtBinding(const CanvasEngineCreateInfo& createInfo, QWidget* hostParent);
    ~AetherCanvasEngineQtBinding() override;

    AetherCanvasEngineQtBinding(const AetherCanvasEngineQtBinding&) = delete;
    AetherCanvasEngineQtBinding& operator=(const AetherCanvasEngineQtBinding&) = delete;

    friend aether::OpenGLCanvasWidget* aetherLegacyRenderer(CanvasEngineQtBinding& binding);

    // --- CanvasEngineQtBinding ---
    QWidget* viewportHostWidget() const override;
    CanvasEngineSession& session() override;
    CanvasEngineQtEvents& events() override;
    CanvasHistoryFacade& history() override;
    CanvasDocumentFacade& document() override;
    ruwa::shared::rendering::ICanvasBackdropSource* backdropSource() override;
    bool isShuttingDown() const override;
    void shutdown() override;

private:
    std::unique_ptr<CanvasEngineQtEvents> m_events;
    std::unique_ptr<AetherCanvasSession> m_session;
    std::unique_ptr<AetherHistoryFacade> m_history;
    std::unique_ptr<AetherDocumentFacade> m_document;
    aether::OpenGLCanvasWidget* m_widget = nullptr;
    bool m_shuttingDown = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_ENGINE_AETHER_AETHERCANVASENGINEQTBINDING_H
