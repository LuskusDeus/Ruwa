// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   U N D O   M A N A G E R
// ==========================================================================

#include "shared/undo/UndoManager.h"
#include <QThreadPool>
#include <QtConcurrent>
#include <atomic>
#include <chrono>
#include <vector>

namespace aether {
namespace {

constexpr int kUndoPrefetchDepth = 8;
constexpr int kUndoPrefetchPollMs = 150;

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

UndoManager::UndoManager(QObject* parent)
    : QObject(parent)
    , m_prepareWatcher(new QFutureWatcher<void>(this))
    , m_prefetchWatcher(new QFutureWatcher<void>(this))
{
    QThreadPool::globalInstance()->setExpiryTimeout(-1);

    connect(m_prepareWatcher, &QFutureWatcher<void>::finished, this,
        &UndoManager::finishPreparedOperation);
    connect(m_prefetchWatcher, &QFutureWatcher<void>::finished, this, &UndoManager::finishPrefetch);

    m_prefetchTimer.setSingleShot(false);
    m_prefetchTimer.setInterval(kUndoPrefetchPollMs);
    connect(&m_prefetchTimer, &QTimer::timeout, this, &UndoManager::processPrefetch);

    QtConcurrent::run([]() { });
}

UndoManager::~UndoManager()
{
    clear();
}

// ==========================================================================
//   C O R E   O P E R A T I O N S
// ==========================================================================

void UndoManager::push(std::unique_ptr<IUndoCommand> cmd)
{
    if (!cmd)
        return;
    if (m_undoRedoInProgress)
        return; // Don't push during undo/redo (e.g. selection change from layer removal)

    // Reported size at push time (can be raw/uncompressed for async commands).
    const qint64 reportedCmdSize = cmd->memorySize();
    recomputeCurrentMemory();
    const qint64 usageBeforePush = m_currentMemory;

    if (reportedCmdSize > m_memoryLimit / 2) { }

    // Discard redo stack (everything after current index)
    while (static_cast<int>(m_commands.size()) > m_index) {
        m_commands.pop_back();
    }

    // Invalidate clean state if we discarded commands past it
    if (m_cleanIndex > m_index) {
        m_cleanIndex = -1;
    }

    // Match the standard undo-stack transaction model: an explicitly
    // mergeable command may absorb the next command while preserving the
    // original undo state. Never merge across a clean marker, otherwise a
    // post-save edit could no longer undo back to the exact saved state.
    const bool canMergeWithPrevious = m_index > 0 && m_index == static_cast<int>(m_commands.size())
        && m_cleanIndex != m_index && !m_commands.back()->requiresAsyncPreparationForUndo()
        && !m_commands.back()->requiresAsyncPreparationForRedo()
        && !cmd->requiresAsyncPreparationForUndo() && !cmd->requiresAsyncPreparationForRedo();
    if (canMergeWithPrevious && m_commands.back()->mergeWith(*cmd)) {
        recomputeCurrentMemory();
        enforceMemoryLimit();
        emitStateChanged();
        if (!m_prefetchTimer.isActive()) {
            m_prefetchTimer.start();
        }
        return;
    }

    m_currentMemory += reportedCmdSize;
    m_commands.push_back(std::move(cmd));
    m_index++;

    enforceMemoryLimit();

    const qint64 usageAfterPush = m_currentMemory;
    const qint64 usageDelta = usageAfterPush - usageBeforePush;
    const qint64 freeMemory = qMax<qint64>(0, m_memoryLimit - m_currentMemory);
    emitStateChanged();
    if (!m_prefetchTimer.isActive()) {
        m_prefetchTimer.start();
    }
}

void UndoManager::undo()
{
    if (!canUndo())
        return;

    const bool wasFirst = (m_pendingUndoCount == 0 && m_pendingRedoCount == 0);
    m_pendingUndoCount++;
    if (m_undoRedoInProgress) {
        return;
    }
    if (!wasFirst) {
        return; // Timer already scheduled for pending undo
    }

    const auto dbgUndoCalledAt = std::chrono::high_resolution_clock::now();
    const int delayMs = 0; // First: next event loop
    QTimer::singleShot(delayMs, this, [this, dbgUndoCalledAt]() {
        const auto dbgNow = std::chrono::high_resolution_clock::now();
        const double dbgWaitMs
            = std::chrono::duration<double, std::milli>(dbgNow - dbgUndoCalledAt).count();
        processNextPending();
    });
}

void UndoManager::redo()
{
    if (!canRedo())
        return;

    const bool wasFirst = (m_pendingUndoCount == 0 && m_pendingRedoCount == 0);
    m_pendingRedoCount++;
    if (m_undoRedoInProgress) {
        return;
    }
    if (!wasFirst) {
        return; // Timer already scheduled for pending redo
    }

    const int delayMs = 0;
    QTimer::singleShot(delayMs, this, [this]() { processNextPending(); });
}

void UndoManager::processNextPending()
{
    if (m_undoRedoInProgress) {
        return;
    }

    if (m_pendingUndoCount > 0 && canUndo()) {
        IUndoCommand* command = m_commands[m_index - 1].get();
        if (command && command->requiresAsyncPreparationForUndo()) {
            if (m_prefetchWatcher && m_prefetchWatcher->isRunning()
                && m_prefetchOperation == PendingOperation::Undo && m_prefetchCommand == command) {
                return;
            }
            startAsyncPreparation(PendingOperation::Undo, command);
            return;
        }
        m_pendingUndoCount--;
        performUndo();
    } else if (m_pendingRedoCount > 0 && canRedo()) {
        IUndoCommand* command = m_commands[m_index].get();
        if (command && command->requiresAsyncPreparationForRedo()) {
            if (m_prefetchWatcher && m_prefetchWatcher->isRunning()
                && m_prefetchOperation == PendingOperation::Redo && m_prefetchCommand == command) {
                return;
            }
            startAsyncPreparation(PendingOperation::Redo, command);
            return;
        }
        m_pendingRedoCount--;
        performRedo();
    } else {
        m_pendingUndoCount = 0;
        m_pendingRedoCount = 0;
        return;
    }

    scheduleNextPending();
}

void UndoManager::performUndo()
{
    if (!canUndo())
        return;

    using Clock = std::chrono::high_resolution_clock;
    using Us = std::chrono::microseconds;

    const auto t0 = Clock::now();

    m_undoRedoInProgress = true;
    m_index--;

    const auto tUndoStart = Clock::now();
    m_commands[m_index]->undo();
    const auto tUndoEnd = Clock::now();

    const auto tTilesStart = Clock::now();
    const QList<QPoint> affectedTilePositions = m_commands[m_index]->affectedTilePositions();
    const auto tTilesEnd = Clock::now();

    m_undoRedoInProgress = false;

    const auto tEmitStart = Clock::now();
    if (!affectedTilePositions.isEmpty()) {
        emit commandApplied(affectedTilePositions);
    }
    const auto tEmitEnd = Clock::now();

    const auto tStateStart = Clock::now();
    emitStateChanged();
    const auto tStateEnd = Clock::now();

    const auto tTotal = Clock::now();

    // Store timestamp so paintGL can measure the gap between undo and first frame
    if (m_dbgUndoTimestampStore) {
        m_dbgUndoTimestampStore->store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(tTotal.time_since_epoch()).count(),
            std::memory_order_relaxed);
    }

    if (!m_prefetchTimer.isActive()) {
        m_prefetchTimer.start();
    }
}

void UndoManager::performRedo()
{
    if (!canRedo())
        return;

    m_undoRedoInProgress = true;
    m_commands[m_index]->redo();
    const QList<QPoint> affectedTilePositions = m_commands[m_index]->affectedTilePositions();
    m_index++;
    m_undoRedoInProgress = false;

    if (!affectedTilePositions.isEmpty()) {
        emit commandApplied(affectedTilePositions);
    }
    emitStateChanged();
    if (!m_prefetchTimer.isActive()) {
        m_prefetchTimer.start();
    }
}

void UndoManager::clear()
{
    waitForPendingPreparation();
    m_commands.clear();
    m_currentMemory = 0;
    m_index = 0;
    m_cleanIndex = 0;
    m_pendingUndoCount = 0;
    m_pendingRedoCount = 0;
    m_prefetchTimer.stop();

    emitStateChanged();
}

void UndoManager::discardRedoStack()
{
    bool changed = false;
    while (static_cast<int>(m_commands.size()) > m_index) {
        m_commands.pop_back();
        changed = true;
    }

    if (m_cleanIndex > m_index) {
        m_cleanIndex = -1;
        changed = true;
    }

    if (changed) {
        recomputeCurrentMemory();
        emitStateChanged();
        if (!m_prefetchTimer.isActive()) {
            m_prefetchTimer.start();
        }
    }
}

bool UndoManager::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    // Per-command remap is O(1): heavy tile commands queue the remap and
    // apply it lazily when they are next prepared or executed.
    bool ok = true;
    for (const auto& command : m_commands) {
        if (command && !command->remapForCanvasResize(offsetX, offsetY, newWidth, newHeight)) {
            ok = false;
        }
    }

    recomputeCurrentMemory();
    emitStateChanged();
    if (!m_prefetchTimer.isActive()) {
        m_prefetchTimer.start();
    }
    return ok;
}

// ==========================================================================
//   S T A T E   Q U E R I E S
// ==========================================================================

bool UndoManager::canUndo() const
{
    return m_index > 0;
}
bool UndoManager::canRedo() const
{
    return m_index < static_cast<int>(m_commands.size());
}

QString UndoManager::undoText() const
{
    if (canUndo())
        return QStringLiteral("Undo ") + m_commands[m_index - 1]->text();
    return QStringLiteral("Undo");
}

QString UndoManager::redoText() const
{
    if (canRedo())
        return QStringLiteral("Redo ") + m_commands[m_index]->text();
    return QStringLiteral("Redo");
}

int UndoManager::count() const
{
    return static_cast<int>(m_commands.size());
}
int UndoManager::index() const
{
    return m_index;
}

// ==========================================================================
//   M E M O R Y   M A N A G E M E N T
// ==========================================================================

void UndoManager::setMemoryLimit(qint64 bytes)
{
    if (bytes > 0) {
        m_memoryLimit = bytes;
        enforceMemoryLimit();
        emitStateChanged();
    }
}

void UndoManager::enforceMemoryLimit()
{
    recomputeCurrentMemory();
    int removed = 0;

    while (m_currentMemory > m_memoryLimit && !m_commands.empty()) {
        m_currentMemory -= m_commands.front()->memorySize();
        m_commands.pop_front();
        removed++;

        if (m_index > 0)
            m_index--;
        if (m_cleanIndex > 0)
            m_cleanIndex--;
        else
            m_cleanIndex = -1;
    }

    if (removed > 0) {
        recomputeCurrentMemory();
    }
}

void UndoManager::recomputeCurrentMemory()
{
    qint64 total = 0;
    for (const auto& command : m_commands) {
        if (command) {
            total += command->memorySize();
        }
    }
    m_currentMemory = total;
}

// ==========================================================================
//   C L E A N   S T A T E
// ==========================================================================

void UndoManager::setClean()
{
    m_cleanIndex = m_index;
    emit cleanChanged(true);
}

bool UndoManager::isClean() const
{
    return m_index == m_cleanIndex;
}

// ==========================================================================
//   S I G N A L S
// ==========================================================================

void UndoManager::emitStateChanged()
{
    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    emit undoTextChanged(undoText());
    emit redoTextChanged(redoText());
    emit indexChanged(m_index);
    emit cleanChanged(isClean());
    emit memoryUsageChanged(m_currentMemory, m_memoryLimit);
}

void UndoManager::scheduleNextPending()
{
    if (m_undoRedoInProgress) {
        return;
    }

    const bool hasMore = (m_pendingUndoCount > 0 || m_pendingRedoCount > 0);
    if (!hasMore) {
        return;
    }

    QTimer::singleShot(0, this, [this]() { processNextPending(); });
}

void UndoManager::startAsyncPreparation(PendingOperation operation, IUndoCommand* command)
{
    if (!command || !m_prepareWatcher || m_prepareWatcher->isRunning()) {
        return;
    }

    m_undoRedoInProgress = true;
    m_preparingOperation = operation;
    m_preparingCommand = command;

    switch (operation) {
    case PendingOperation::Undo:
        m_prepareWatcher->setFuture(QtConcurrent::run([command]() { command->prepareUndo(); }));
        break;
    case PendingOperation::Redo:
        m_prepareWatcher->setFuture(QtConcurrent::run([command]() { command->prepareRedo(); }));
        break;
    case PendingOperation::None:
        m_undoRedoInProgress = false;
        m_preparingCommand = nullptr;
        break;
    }
}

void UndoManager::finishPreparedOperation()
{
    if (m_preparingOperation == PendingOperation::None) {
        return;
    }

    const PendingOperation operation = m_preparingOperation;
    IUndoCommand* preparedCommand = m_preparingCommand;

    m_preparingOperation = PendingOperation::None;
    m_preparingCommand = nullptr;
    m_undoRedoInProgress = false;

    if (operation == PendingOperation::Undo) {
        if (m_pendingUndoCount > 0 && canUndo()
            && m_commands[m_index - 1].get() == preparedCommand) {
            m_pendingUndoCount--;
            performUndo();
            scheduleNextPending();
            return;
        }
    } else if (operation == PendingOperation::Redo) {
        if (m_pendingRedoCount > 0 && canRedo() && m_commands[m_index].get() == preparedCommand) {
            m_pendingRedoCount--;
            performRedo();
            scheduleNextPending();
            return;
        }
    }

    processNextPending();
}

void UndoManager::processPrefetch()
{
    if (m_undoRedoInProgress || m_pendingUndoCount > 0 || m_pendingRedoCount > 0
        || !m_prefetchWatcher || m_prefetchWatcher->isRunning()
        || (m_prepareWatcher && m_prepareWatcher->isRunning())) {
        return;
    }

    for (int offset = 1; offset <= kUndoPrefetchDepth; ++offset) {
        const int commandIndex = m_index - offset;
        if (commandIndex < 0) {
            break;
        }

        IUndoCommand* command = m_commands[commandIndex].get();
        if (command && command->requiresAsyncPreparationForUndo()) {
            startPrefetch(PendingOperation::Undo, command);
            return;
        }
    }

    for (int offset = 0; offset < kUndoPrefetchDepth; ++offset) {
        const int commandIndex = m_index + offset;
        if (commandIndex >= static_cast<int>(m_commands.size())) {
            break;
        }

        IUndoCommand* command = m_commands[commandIndex].get();
        if (command && command->requiresAsyncPreparationForRedo()) {
            startPrefetch(PendingOperation::Redo, command);
            return;
        }
    }

    if (m_commands.empty()) {
        m_prefetchTimer.stop();
    }
}

void UndoManager::startPrefetch(PendingOperation operation, IUndoCommand* command)
{
    if (!command || !m_prefetchWatcher || m_prefetchWatcher->isRunning()) {
        return;
    }

    m_prefetchOperation = operation;
    m_prefetchCommand = command;

    switch (operation) {
    case PendingOperation::Undo:
        m_prefetchWatcher->setFuture(QtConcurrent::run([command]() { command->prepareUndo(); }));
        break;
    case PendingOperation::Redo:
        m_prefetchWatcher->setFuture(QtConcurrent::run([command]() { command->prepareRedo(); }));
        break;
    case PendingOperation::None:
        m_prefetchCommand = nullptr;
        break;
    }
}

void UndoManager::finishPrefetch()
{
    if (m_prefetchOperation == PendingOperation::None) {
        return;
    }

    const PendingOperation operation = m_prefetchOperation;
    IUndoCommand* prefetchedCommand = m_prefetchCommand;

    m_prefetchOperation = PendingOperation::None;
    m_prefetchCommand = nullptr;

    if (operation == PendingOperation::Undo) {
        if (m_pendingUndoCount > 0 && canUndo()
            && m_commands[m_index - 1].get() == prefetchedCommand) {
            m_pendingUndoCount--;
            performUndo();
            scheduleNextPending();
            return;
        }
    } else if (operation == PendingOperation::Redo) {
        if (m_pendingRedoCount > 0 && canRedo() && m_commands[m_index].get() == prefetchedCommand) {
            m_pendingRedoCount--;
            performRedo();
            scheduleNextPending();
            return;
        }
    }

    processNextPending();
    if (!m_prefetchTimer.isActive()) {
        m_prefetchTimer.start();
    }
}

void UndoManager::waitForPendingPreparation()
{
    if (!m_prepareWatcher || !m_prepareWatcher->isRunning()) {
        m_preparingOperation = PendingOperation::None;
        m_preparingCommand = nullptr;
    } else {
        m_prepareWatcher->waitForFinished();
    }

    if (m_prefetchWatcher && m_prefetchWatcher->isRunning()) {
        m_prefetchWatcher->waitForFinished();
    }

    m_preparingOperation = PendingOperation::None;
    m_preparingCommand = nullptr;
    m_prefetchOperation = PendingOperation::None;
    m_prefetchCommand = nullptr;
    m_undoRedoInProgress = false;
}

} // namespace aether
