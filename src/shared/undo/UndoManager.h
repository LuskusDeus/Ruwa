// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   U N D O   M A N A G E R
// ==========================================================================

#ifndef RUWA_CORE_UNDO_UNDOMANAGER_H
#define RUWA_CORE_UNDO_UNDOMANAGER_H

#include <QObject>
#include <QFutureWatcher>
#include <QList>
#include <QPoint>
#include <QString>
#include <QTimer>

#include <atomic>
#include <deque>
#include <memory>
#include <cstdint>

namespace aether {

// ==========================================================================
//   I U N D O   C O M M A N D
// ==========================================================================

class IUndoCommand {
public:
    virtual ~IUndoCommand() = default;

    virtual void undo() = 0;
    virtual void redo() = 0;

    virtual QString text() const = 0;

    /// Actual memory footprint in bytes (used for memory limit enforcement)
    virtual qint64 memorySize() const = 0;
    /// Optionally absorb a newer adjacent command. The current command keeps
    /// its original undo state and adopts the newer command's redo state.
    /// Returning false preserves both commands as separate undo steps.
    virtual bool mergeWith(const IUndoCommand& newer)
    {
        Q_UNUSED(newer);
        return false;
    }
    virtual bool requiresAsyncPreparationForUndo() const { return false; }
    virtual bool requiresAsyncPreparationForRedo() const { return false; }
    virtual void prepareUndo() { }
    virtual void prepareRedo() { }
    virtual QList<QPoint> affectedTilePositions() const { return {}; }

    /// Rebase command snapshots after canvas-space remap:
    /// old(x, y) -> new(x - offsetX, y - offsetY), clipped to new bounds.
    virtual bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
    {
        Q_UNUSED(offsetX);
        Q_UNUSED(offsetY);
        Q_UNUSED(newWidth);
        Q_UNUSED(newHeight);
        return true;
    }
};

// ==========================================================================
//   U N D O   M A N A G E R
// ==========================================================================

class UndoManager : public QObject {
    Q_OBJECT

public:
    explicit UndoManager(QObject* parent = nullptr);
    ~UndoManager() override;

    // ---- Core operations ----

    /// Push a new command. Does NOT call redo() — the caller has already
    /// applied the changes. Clears the redo stack.
    void push(std::unique_ptr<IUndoCommand> cmd);

    void undo();
    void redo();
    void clear();
    void discardRedoStack();
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight);

    // ---- State queries ----

    bool canUndo() const;
    bool canRedo() const;
    bool isUndoRedoInProgress() const { return m_undoRedoInProgress; }
    QString undoText() const;
    QString redoText() const;
    int count() const;
    int index() const;

    // ---- Memory management ----

    void setMemoryLimit(qint64 bytes);
    qint64 memoryLimit() const { return m_memoryLimit; }
    qint64 currentMemoryUsage() const { return m_currentMemory; }

    // ---- Clean state (document saved marker) ----

    void setClean();
    bool isClean() const;

    // ---- Debug: measure latency from undo to first paintGL ----
    // Set to a shared atomic that paintGL reads to compute the gap.
    void setDbgUndoTimestampStore(std::atomic<int64_t>* store) { m_dbgUndoTimestampStore = store; }

signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void undoTextChanged(const QString& text);
    void redoTextChanged(const QString& text);
    void indexChanged(int idx);
    void commandApplied(const QList<QPoint>& tilePositions);
    void cleanChanged(bool clean);
    void memoryUsageChanged(qint64 current, qint64 limit);

private:
    enum class PendingOperation { None, Undo, Redo };

    void enforceMemoryLimit();
    void recomputeCurrentMemory();
    void emitStateChanged();
    void performUndo();
    void performRedo();
    void processNextPending();
    void scheduleNextPending();
    void startAsyncPreparation(PendingOperation operation, IUndoCommand* command);
    void finishPreparedOperation();
    void processPrefetch();
    void startPrefetch(PendingOperation operation, IUndoCommand* command);
    void finishPrefetch();
    void waitForPendingPreparation();

    std::deque<std::unique_ptr<IUndoCommand>> m_commands;
    int m_index = 0; // Number of applied commands (undo pops from here)
    int m_cleanIndex = 0;

    qint64 m_memoryLimit = 3LL * 1024 * 1024 * 1024; // 3 GB
    qint64 m_currentMemory = 0;

    bool m_undoRedoInProgress = false;
    int m_pendingUndoCount = 0;
    int m_pendingRedoCount = 0;
    QFutureWatcher<void>* m_prepareWatcher = nullptr;
    QFutureWatcher<void>* m_prefetchWatcher = nullptr;
    PendingOperation m_preparingOperation = PendingOperation::None;
    IUndoCommand* m_preparingCommand = nullptr;
    PendingOperation m_prefetchOperation = PendingOperation::None;
    IUndoCommand* m_prefetchCommand = nullptr;
    QTimer m_prefetchTimer;

    std::atomic<int64_t>* m_dbgUndoTimestampStore = nullptr;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_UNDOMANAGER_H
