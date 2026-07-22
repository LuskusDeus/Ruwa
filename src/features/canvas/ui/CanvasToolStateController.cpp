// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   T O O L   S T A T E
// ==========================================================================

#include "features/canvas/ui/CanvasToolStateController.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

namespace ruwa::ui::workspace {
namespace {

constexpr auto kLastToolStateGroup = "Workspace/LastToolState";

void readToolState(QSettings& settings, const QString& prefix, CanvasToolBrushState& dst)
{
    const bool hasStateKeys = settings.contains(prefix + "/brushId")
        || settings.contains(prefix + "/brushSize") || settings.contains(prefix + "/brushOpacity")
        || settings.contains(prefix + "/colorRgba");
    dst.valid = settings.value(prefix + "/valid", hasStateKeys).toBool();
    dst.brushId = settings.value(prefix + "/brushId", QString()).toString();
    dst.brushSize = qBound(0.0, settings.value(prefix + "/brushSize", 0.3).toDouble(), 1.0);
    dst.brushOpacity = qBound(0.0, settings.value(prefix + "/brushOpacity", 1.0).toDouble(), 1.0);

    const QColor storedColor = QColor::fromRgba(
        settings.value(prefix + "/colorRgba", QColor(0, 0, 0, 255).rgba()).toUInt());
    dst.color.r = static_cast<uint8_t>(storedColor.red());
    dst.color.g = static_cast<uint8_t>(storedColor.green());
    dst.color.b = static_cast<uint8_t>(storedColor.blue());
    dst.color.a = static_cast<uint8_t>(qBound(0.0, dst.brushOpacity, 1.0) * 255.0);
}

CanvasToolBrushStateSnapshot snapshotBrushState(const CanvasToolBrushState& src)
{
    CanvasToolBrushStateSnapshot snapshot;
    snapshot.brushId = src.brushId;
    snapshot.brushSize = qBound(0.0, src.brushSize, 1.0);
    snapshot.brushOpacity = qBound(0.0, src.brushOpacity, 1.0);
    snapshot.colorRgba = QColor(src.color.r, src.color.g, src.color.b, src.color.a).rgba();
    snapshot.valid = src.valid;
    return snapshot;
}

void writeToolState(
    QSettings& settings, const QString& prefix, const CanvasToolBrushStateSnapshot& src)
{
    settings.setValue(prefix + "/valid", src.valid);
    settings.setValue(prefix + "/brushId", src.brushId);
    settings.setValue(prefix + "/brushSize", qBound(0.0, src.brushSize, 1.0));
    settings.setValue(prefix + "/brushOpacity", qBound(0.0, src.brushOpacity, 1.0));
    settings.setValue(prefix + "/colorRgba", src.colorRgba);
}

} // namespace

CanvasToolStateController::CanvasToolStateController(QObject* parent)
    : QObject(parent)
{
    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(250);
    connect(m_syncTimer, &QTimer::timeout, this, [this]() { flushQueuedSnapshot(); });

    m_flushWatcher = new QFutureWatcher<void>(this);
    connect(m_flushWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        if (m_syncPending && m_syncTimer) {
            m_syncTimer->start();
        }
    });
}

CanvasToolStateController::~CanvasToolStateController() = default;

void CanvasToolStateController::loadRuntimeState()
{
    const CanvasLoadedToolState loaded = loadPersistedState();
    m_brushState = loaded.brush;
    m_eraserState = loaded.eraser;
    m_blurState = loaded.blur;
    m_smudgeState = loaded.smudge;
    m_currentTool = loaded.currentTool;
    m_lastDrawTool = loaded.lastDrawTool;
    m_currentColor = loaded.currentColor;
    m_lassoStabilization = loaded.lassoStabilization;
    m_lassoFillStabilization = loaded.lassoFillStabilization;
    m_brushEraserActive = loaded.brushEraserActive;
}

void CanvasToolStateController::setLastDrawTool(ToolId tool)
{
    if (isDrawInstrument(tool)) {
        m_lastDrawTool = tool;
    }
}

void CanvasToolStateController::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
}

void CanvasToolStateController::setCurrentColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    m_currentColor = QColor(r, g, b, a);
}

void CanvasToolStateController::setLassoStabilization(qreal stabilization)
{
    m_lassoStabilization = qBound(0.0, stabilization, 1.0);
}

void CanvasToolStateController::setLassoFillStabilization(qreal stabilization)
{
    m_lassoFillStabilization = qBound(0.0, stabilization, 1.0);
}

bool CanvasToolStateController::setBrushEraserActive(bool active)
{
    if (m_brushEraserActive == active) {
        return false;
    }
    m_brushEraserActive = active;
    return true;
}

bool CanvasToolStateController::shouldEraseForTool(ToolId tool) const
{
    return shouldEraseForTool(tool, m_brushEraserActive);
}

ToolId CanvasToolStateController::overlayInstrumentMode() const
{
    return overlayInstrumentMode(m_currentTool, m_lastDrawTool);
}

bool CanvasToolStateController::overlayMatchesInstrument(ToolId tool) const
{
    return overlayMatchesInstrument(tool, m_currentTool, m_lastDrawTool);
}

CanvasToolBrushState* CanvasToolStateController::stateForInstrument(ToolId tool)
{
    if (tool == ToolId::Liquify) {
        return &m_liquifyState;
    }
    return stateForInstrument(tool, m_brushState, m_eraserState, m_blurState, m_smudgeState);
}

const CanvasToolBrushState* CanvasToolStateController::stateForInstrument(ToolId tool) const
{
    if (tool == ToolId::Liquify) {
        return &m_liquifyState;
    }
    return stateForInstrument(tool, m_brushState, m_eraserState, m_blurState, m_smudgeState);
}

CanvasPersistedToolState CanvasToolStateController::persistedState(ToolId tool) const
{
    const CanvasToolBrushState* state = stateForInstrument(tool);
    return state ? persistedStateFromToolState(*state) : CanvasPersistedToolState {};
}

void CanvasToolStateController::setPersistedState(
    ToolId tool, const CanvasPersistedToolState& state)
{
    if (CanvasToolBrushState* target = stateForInstrument(tool)) {
        applyPersistedToolState(*target, state);
    }
}

CanvasToolStateSnapshot CanvasToolStateController::buildSnapshot() const
{
    return buildSnapshot(m_currentTool, m_lastDrawTool, m_currentColor, m_lassoStabilization,
        m_lassoFillStabilization, m_brushEraserActive, m_brushState, m_eraserState, m_blurState,
        m_smudgeState);
}

void CanvasToolStateController::queueSnapshot(
    const CanvasToolStateSnapshot& snapshot, bool profileFlush)
{
    m_pendingSnapshot = snapshot;
    m_profileFlush = profileFlush;
    m_syncPending = true;
    if (m_syncTimer) {
        m_syncTimer->start();
    }
}

void CanvasToolStateController::flushQueuedSnapshot()
{
    if (!m_syncPending) {
        return;
    }

    if (m_flushWatcher && m_flushWatcher->isRunning()) {
        return;
    }

    if (m_syncTimer) {
        m_syncTimer->stop();
    }

    const CanvasToolStateSnapshot snapshot = m_pendingSnapshot;
    const bool profileFlush = m_profileFlush;
    m_syncPending = false;

    m_flushWatcher->setFuture(QtConcurrent::run([snapshot, profileFlush]() {
        QElapsedTimer timer;
        if (profileFlush) {
            timer.start();
        }

        writeSnapshot(snapshot);

        if (profileFlush) { }
    }));
}

void CanvasToolStateController::flushQueuedSnapshotNoWait()
{
    if (!m_syncPending) {
        return;
    }

    if (m_syncTimer) {
        m_syncTimer->stop();
    }

    const CanvasToolStateSnapshot snapshot = m_pendingSnapshot;
    m_syncPending = false;

    QThreadPool::globalInstance()->start([snapshot]() { writeSnapshot(snapshot); });
}

bool CanvasToolStateController::isDrawInstrument(ToolId tool)
{
    return tool == ToolId::Brush || tool == ToolId::Eraser
        || tool == ToolId::Blur || tool == ToolId::Smudge
        || tool == ToolId::Liquify;
}

bool CanvasToolStateController::shouldEraseForTool(ToolId tool, bool brushEraserActive)
{
    return tool == ToolId::Eraser || (tool == ToolId::Brush && brushEraserActive);
}

ToolId CanvasToolStateController::overlayInstrumentMode(
    ToolId currentTool, ToolId lastDrawTool)
{
    if (isDrawInstrument(currentTool)) {
        return currentTool;
    }
    if (isDrawInstrument(lastDrawTool)) {
        return lastDrawTool;
    }
    return ToolId::Brush;
}

bool CanvasToolStateController::overlayMatchesInstrument(
    ToolId tool, ToolId currentTool, ToolId lastDrawTool)
{
    return isDrawInstrument(tool) && overlayInstrumentMode(currentTool, lastDrawTool) == tool;
}

CanvasToolBrushState* CanvasToolStateController::stateForInstrument(ToolId tool,
    CanvasToolBrushState& brush, CanvasToolBrushState& eraser, CanvasToolBrushState& blur,
    CanvasToolBrushState& smudge)
{
    if (tool == ToolId::Eraser) {
        return &eraser;
    }
    if (tool == ToolId::Blur) {
        return &blur;
    }
    if (tool == ToolId::Smudge) {
        return &smudge;
    }
    if (tool == ToolId::Brush) {
        return &brush;
    }
    return nullptr;
}

const CanvasToolBrushState* CanvasToolStateController::stateForInstrument(ToolId tool,
    const CanvasToolBrushState& brush, const CanvasToolBrushState& eraser,
    const CanvasToolBrushState& blur, const CanvasToolBrushState& smudge)
{
    if (tool == ToolId::Eraser) {
        return &eraser;
    }
    if (tool == ToolId::Blur) {
        return &blur;
    }
    if (tool == ToolId::Smudge) {
        return &smudge;
    }
    if (tool == ToolId::Brush) {
        return &brush;
    }
    return nullptr;
}

CanvasPersistedToolState CanvasToolStateController::persistedStateFromToolState(
    const CanvasToolBrushState& state)
{
    CanvasPersistedToolState snapshot;
    snapshot.brushId = state.brushId;
    snapshot.brushSize = state.brushSize;
    snapshot.brushOpacity = state.brushOpacity;
    snapshot.color = QColor(state.color.r, state.color.g, state.color.b, state.color.a);
    snapshot.valid = state.valid;
    return snapshot;
}

void CanvasToolStateController::applyPersistedToolState(
    CanvasToolBrushState& target, const CanvasPersistedToolState& state)
{
    target.brushId = state.brushId;
    target.brushSize = qBound(0.0, state.brushSize, 1.0);
    target.brushOpacity = qBound(0.0, state.brushOpacity, 1.0);
    target.color.r = static_cast<uint8_t>(state.color.red());
    target.color.g = static_cast<uint8_t>(state.color.green());
    target.color.b = static_cast<uint8_t>(state.color.blue());
    target.color.a = static_cast<uint8_t>(state.color.alpha());
    target.valid = state.valid;
}

CanvasLoadedToolState CanvasToolStateController::loadPersistedState()
{
    CanvasLoadedToolState loaded;

    QSettings settings;
    settings.beginGroup(kLastToolStateGroup);

    readToolState(settings, QStringLiteral("brush"), loaded.brush);
    readToolState(settings, QStringLiteral("eraser"), loaded.eraser);
    readToolState(settings, QStringLiteral("blur"), loaded.blur);
    readToolState(settings, QStringLiteral("smudge"), loaded.smudge);
    if (!loaded.blur.valid && loaded.brush.valid) {
        loaded.blur = loaded.brush;
    }
    if (!loaded.smudge.valid && loaded.brush.valid) {
        loaded.smudge = loaded.brush;
    }

    const int storedTool
        = settings.value("currentTool", persistentValueForToolId(ToolId::Brush)).toInt();
    loaded.currentTool = toolIdFromPersistentValue(storedTool).value_or(ToolId::Brush);

    const int storedLastDraw
        = settings.value("lastDrawTool", persistentValueForToolId(ToolId::Brush)).toInt();
    const ToolId storedLastDrawTool
        = toolIdFromPersistentValue(storedLastDraw).value_or(ToolId::Brush);
    loaded.lastDrawTool
        = isDrawInstrument(storedLastDrawTool) ? storedLastDrawTool : ToolId::Brush;
    if (isDrawInstrument(loaded.currentTool)) {
        loaded.lastDrawTool = loaded.currentTool;
    }

    loaded.currentColor = QColor::fromRgba(
        settings.value("currentColorRgba", QColor(0, 0, 0, 255).rgba()).toUInt());
    loaded.lassoStabilization
        = qBound(0.0, settings.value("lassoStabilization", 0.0).toDouble(), 1.0);
    loaded.lassoFillStabilization
        = qBound(0.0, settings.value("lassoFillStabilization", 0.0).toDouble(), 1.0);
    loaded.brushEraserActive = settings.value("brushEraserActive", false).toBool();

    const CanvasToolBrushState* activeState = nullptr;
    if (loaded.currentTool == ToolId::Brush && loaded.brush.valid) {
        activeState = &loaded.brush;
    } else if (loaded.currentTool == ToolId::Eraser && loaded.eraser.valid) {
        activeState = &loaded.eraser;
    } else if (loaded.currentTool == ToolId::Blur && loaded.blur.valid) {
        activeState = &loaded.blur;
    } else if (loaded.currentTool == ToolId::Smudge && loaded.smudge.valid) {
        activeState = &loaded.smudge;
    }
    if (activeState) {
        loaded.currentColor
            = QColor(activeState->color.r, activeState->color.g, activeState->color.b,
                static_cast<int>(qBound(0.0, activeState->brushOpacity, 1.0) * 255.0));
    }

    settings.endGroup();
    return loaded;
}

CanvasToolStateSnapshot CanvasToolStateController::buildSnapshot(ToolId currentTool,
    ToolId lastDrawTool, const QColor& currentColor, qreal lassoStabilization,
    qreal lassoFillStabilization, bool brushEraserActive, const CanvasToolBrushState& brush,
    const CanvasToolBrushState& eraser, const CanvasToolBrushState& blur,
    const CanvasToolBrushState& smudge)
{
    CanvasToolStateSnapshot snapshot;
    snapshot.currentTool = persistentValueForToolId(currentTool);
    snapshot.lastDrawTool = persistentValueForToolId(lastDrawTool);
    snapshot.currentColorRgba = currentColor.rgba();
    snapshot.lassoStabilization = qBound(0.0, lassoStabilization, 1.0);
    snapshot.lassoFillStabilization = qBound(0.0, lassoFillStabilization, 1.0);
    snapshot.brushEraserActive = brushEraserActive;
    snapshot.brush = snapshotBrushState(brush);
    snapshot.eraser = snapshotBrushState(eraser);
    snapshot.blur = snapshotBrushState(blur);
    snapshot.smudge = snapshotBrushState(smudge);
    return snapshot;
}

void CanvasToolStateController::writeSnapshot(const CanvasToolStateSnapshot& snapshot)
{
    static QMutex settingsWriteMutex;
    QMutexLocker lock(&settingsWriteMutex);

    QSettings settings;
    settings.beginGroup(kLastToolStateGroup);
    settings.setValue("currentTool", snapshot.currentTool);
    settings.setValue("lastDrawTool", snapshot.lastDrawTool);
    settings.setValue("currentColorRgba", snapshot.currentColorRgba);
    settings.setValue("lassoStabilization", qBound(0.0, snapshot.lassoStabilization, 1.0));
    settings.setValue("lassoFillStabilization", qBound(0.0, snapshot.lassoFillStabilization, 1.0));
    settings.setValue("brushEraserActive", snapshot.brushEraserActive);

    writeToolState(settings, QStringLiteral("brush"), snapshot.brush);
    writeToolState(settings, QStringLiteral("eraser"), snapshot.eraser);
    writeToolState(settings, QStringLiteral("blur"), snapshot.blur);
    writeToolState(settings, QStringLiteral("smudge"), snapshot.smudge);
    settings.endGroup();
    settings.sync();
}

} // namespace ruwa::ui::workspace
