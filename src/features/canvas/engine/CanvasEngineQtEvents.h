// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   E N G I N E   Q T   E V E N T S
// ==========================================================================
// Qt-only event adapter for semantic engine/session events (plan 7.14/7.32).
//
// This relay belongs to the Qt integration layer (CanvasEngineQtBinding), NOT
// to the rendering engine core: a future engine does not have to be
// QObject-based. The current Aether binding translates its implementation
// callbacks (including inherited QOpenGLWidget signals) into the semantic
// events below, on the GUI thread. All payloads are owned values.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINEQTEVENTS_H
#define RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINEQTEVENTS_H

#include "features/canvas/engine/CanvasEngineTypes.h"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QUuid>

#include <cstdint>

namespace ruwa::ui::workspace {

class CanvasEngineQtEvents : public QObject {
    Q_OBJECT

public:
    explicit CanvasEngineQtEvents(QObject* parent = nullptr);

    /// Publishing helpers for the owning binding. Signals stay an adapter
    /// implementation detail; feature code only connects.
    void publishViewState(const CanvasViewSnapshot& snapshot, CanvasViewChange changes)
    {
        emit viewStateChanged(snapshot, changes);
    }
    void publishPresentationSync(const CanvasViewSnapshot& snapshot)
    {
        emit presentationSyncRequested(snapshot);
    }
    void publishFillActivity(const CanvasFillActivityState& state)
    {
        emit fillActivityChanged(state);
    }
    void publishTransformPresentation(const TransformPresentationState& state)
    {
        emit transformPresentationChanged(state);
    }
    void publishEngineFailure(const CanvasEngineDiagnostic& diagnostic)
    {
        emit engineFailed(diagnostic);
    }

signals:
    /// The engine session finished initializing and is ready to use.
    void engineReady();
    /// One presented frame (composition finished).
    void framePresented();

    /// Renderer initialization/backend failure (plan 7.14.6 / 7.15.5): the
    /// engine reports an owned diagnostic and stops; the UI decides how to
    /// present it. No renderer-owned dialog exists anywhere on this path.
    void engineFailed(const ruwa::ui::workspace::CanvasEngineDiagnostic& diagnostic);

    /// The effective view changed — camera center (including frame-sampled
    /// pan), zoom, rotation, mirror, animation state or mapping metrics
    /// (plan 7.14.2 / 7.32.2). The binding compares complete snapshots at its
    /// presentation synchronization point; consumers must not infer view
    /// changes from zoom/rotation alone.
    void viewStateChanged(const ruwa::ui::workspace::CanvasViewSnapshot& snapshot,
        ruwa::ui::workspace::CanvasViewChange changes);
    /// Qt chrome synchronization hook: "synchronize QWidget overlays with the
    /// view snapshot that is about to be presented" (plan 7.14.3). In the
    /// legacy binding this is sourced from the pre-compose phase; the
    /// implementation detail does not escape.
    void presentationSyncRequested(const ruwa::ui::workspace::CanvasViewSnapshot& snapshot);

    /// The presented surface extent changed (viewport-logical units).
    void viewportMetricsChanged(uint32_t width, uint32_t height);
    /// Camera zoom changed (rendered state, including during animation).
    /// Compatibility event derived from view-state tracking; new consumers
    /// should prefer viewStateChanged.
    void viewZoomChanged(qreal zoom);
    /// Camera rotation changed (rendered state, radians).
    void viewRotationChanged(qreal rotationRadians);

    /// A paint stroke (not erase) completed.
    void strokePainted();
    /// Document content changed inside a world/document region.
    void contentRegionChanged(const QRect& documentRect);
    /// Specific document tiles changed.
    void contentTilesChanged(const QList<QPoint>& tilePositions);
    /// The layer currently being processed by an async fill changed.
    /// TRANSITIONAL compatibility event derived by the binding from
    /// fillActivityChanged (plan 7.14.5); Layers UI consumes it until it
    /// migrates to the activity snapshot.
    void fillProcessingLayerChanged(const QUuid& layerId);
    /// Source-of-truth fill activity snapshot (plan 7.14.5): phase, layer,
    /// origin, algorithm, live-preview/waiting flags. Fill UI policy (the
    /// wait delay, wording, popups) belongs to the application presenter.
    void fillActivityChanged(const ruwa::ui::workspace::CanvasFillActivityState& state);
    /// Interactive transform mode opened.
    void transformModeEntered();
    /// Interactive transform mode closed; @p applied is true when committed.
    void transformModeExited(bool applied);
    /// Transform metric facts for application-owned QWidget presentation
    /// (plan 7.15.1): snap labels, live drag readout segments and the
    /// viewport-local drag anchor.
    void transformPresentationChanged(
        const ruwa::ui::workspace::TransformPresentationState& state);
    /// The GPU backdrop pipeline became ready.
    void backdropAvailabilityChanged();

    // --- history (plan 7.30.1: history events belong to the binding layer,
    // translated from the legacy document undo manager) ---
    void historyCanUndoChanged(bool canUndo);
    void historyCanRedoChanged(bool canRedo);
    void historyIndexChanged(int index);
    void historyCommandApplied(const QList<QPoint>& tilePositions);
};

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_ENGINE_CANVASENGINEQTEVENTS_H
