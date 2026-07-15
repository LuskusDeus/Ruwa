// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   T O O L   S T A T E
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASTOOLSTATECONTROLLER_H
#define RUWA_UI_WORKSPACE_CANVASTOOLSTATECONTROLLER_H

#include "features/canvas/ui/CanvasPanelTypes.h"

#include <QColor>
#include <QObject>

#include <cstdint>

template <typename T> class QFutureWatcher;
class QTimer;

namespace ruwa::ui::workspace {

struct CanvasLoadedToolState {
    CanvasToolMode currentTool = CanvasToolMode::Hand;
    CanvasToolMode lastDrawTool = CanvasToolMode::Brush;
    QColor currentColor = QColor(0, 0, 0, 255);
    qreal lassoStabilization = 0.0;
    qreal lassoFillStabilization = 0.0;
    bool brushEraserActive = false;
    CanvasToolBrushState brush;
    CanvasToolBrushState eraser;
    CanvasToolBrushState blur;
    CanvasToolBrushState smudge;
};

class CanvasToolStateController : public QObject {
public:
    explicit CanvasToolStateController(QObject* parent = nullptr);
    ~CanvasToolStateController() override;

    void loadRuntimeState();

    CanvasToolMode currentTool() const { return m_currentTool; }
    void setCurrentTool(CanvasToolMode tool) { m_currentTool = tool; }
    CanvasToolMode lastDrawTool() const { return m_lastDrawTool; }
    void setLastDrawTool(CanvasToolMode tool);

    QColor currentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);
    void setCurrentColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    uint8_t currentAlpha() const { return static_cast<uint8_t>(m_currentColor.alpha()); }

    qreal lassoStabilization() const { return m_lassoStabilization; }
    void setLassoStabilization(qreal stabilization);
    qreal lassoFillStabilization() const { return m_lassoFillStabilization; }
    void setLassoFillStabilization(qreal stabilization);

    bool brushEraserActive() const { return m_brushEraserActive; }
    bool setBrushEraserActive(bool active);

    // Liquify sub-mode (0=Push,1=TwirlCW,2=TwirlCCW,3=Bloat,4=Pucker).
    // Session runtime state only (not persisted across restart yet).
    int liquifyToolMode() const { return m_liquifyToolMode; }
    void setLiquifyToolMode(int mode) { m_liquifyToolMode = mode; }

    bool shouldEraseForTool(CanvasToolMode tool) const;
    CanvasToolMode overlayInstrumentMode() const;
    bool overlayMatchesInstrument(CanvasToolMode tool) const;

    CanvasToolBrushState* stateForInstrument(CanvasToolMode tool);
    const CanvasToolBrushState* stateForInstrument(CanvasToolMode tool) const;
    const CanvasToolBrushState& brushState() const { return m_brushState; }
    const CanvasToolBrushState& eraserState() const { return m_eraserState; }
    const CanvasToolBrushState& blurState() const { return m_blurState; }
    const CanvasToolBrushState& smudgeState() const { return m_smudgeState; }

    CanvasPersistedToolState persistedState(CanvasToolMode tool) const;
    void setPersistedState(CanvasToolMode tool, const CanvasPersistedToolState& state);
    CanvasToolStateSnapshot buildSnapshot() const;

    bool suppressPersistDuringRestore() const { return m_suppressPersistDuringRestore; }
    void setSuppressPersistDuringRestore(bool suppress)
    {
        m_suppressPersistDuringRestore = suppress;
    }

    void queueSnapshot(const CanvasToolStateSnapshot& snapshot, bool profileFlush);
    void flushQueuedSnapshot();
    void flushQueuedSnapshotNoWait();

    static bool isDrawInstrument(CanvasToolMode tool);
    static bool shouldEraseForTool(CanvasToolMode tool, bool brushEraserActive);
    static CanvasToolMode overlayInstrumentMode(
        CanvasToolMode currentTool, CanvasToolMode lastDrawTool);
    static bool overlayMatchesInstrument(
        CanvasToolMode tool, CanvasToolMode currentTool, CanvasToolMode lastDrawTool);
    static CanvasToolBrushState* stateForInstrument(CanvasToolMode tool,
        CanvasToolBrushState& brush, CanvasToolBrushState& eraser, CanvasToolBrushState& blur,
        CanvasToolBrushState& smudge);
    static const CanvasToolBrushState* stateForInstrument(CanvasToolMode tool,
        const CanvasToolBrushState& brush, const CanvasToolBrushState& eraser,
        const CanvasToolBrushState& blur, const CanvasToolBrushState& smudge);
    static CanvasPersistedToolState persistedStateFromToolState(const CanvasToolBrushState& state);
    static void applyPersistedToolState(
        CanvasToolBrushState& target, const CanvasPersistedToolState& state);
    static CanvasLoadedToolState loadPersistedState();
    static CanvasToolStateSnapshot buildSnapshot(CanvasToolMode currentTool,
        CanvasToolMode lastDrawTool, const QColor& currentColor, qreal lassoStabilization,
        qreal lassoFillStabilization, bool brushEraserActive, const CanvasToolBrushState& brush,
        const CanvasToolBrushState& eraser, const CanvasToolBrushState& blur,
        const CanvasToolBrushState& smudge);
    static void writeSnapshot(const CanvasToolStateSnapshot& snapshot);

private:
    QTimer* m_syncTimer = nullptr;
    QFutureWatcher<void>* m_flushWatcher = nullptr;
    bool m_syncPending = false;
    bool m_profileFlush = false;
    CanvasToolStateSnapshot m_pendingSnapshot;

    CanvasToolMode m_currentTool = CanvasToolMode::Hand;
    CanvasToolMode m_lastDrawTool = CanvasToolMode::Brush;
    QColor m_currentColor = QColor(0, 0, 0, 255);
    qreal m_lassoStabilization = 0.0;
    qreal m_lassoFillStabilization = 0.0;
    bool m_brushEraserActive = false;
    int m_liquifyToolMode = 0;
    CanvasToolBrushState m_brushState;
    CanvasToolBrushState m_eraserState;
    CanvasToolBrushState m_blurState;
    CanvasToolBrushState m_smudgeState;
    // Liquify keeps its OWN state (a fixed soft round brush; no brush selection),
    // separate from blur so its forced settings don't clobber the blur brush.
    CanvasToolBrushState m_liquifyState;
    bool m_suppressPersistDuringRestore = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASTOOLSTATECONTROLLER_H
