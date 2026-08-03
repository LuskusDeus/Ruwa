// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   |   U N D O / R E D O   C O M M A N D S
// ======================================================================================

#include "shared/undo/UndoRedoCommands.h"
#include "commands/CommandContext.h"
#include "shared/undo/UndoManager.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/canvas/ui/CanvasPanel.h"

namespace {

ruwa::ui::workspace::CanvasPanel* activeCanvasPanel(const ruwa::core::CommandContext& ctx)
{
    auto* workspaceTab = ctx.activeTabAs<ruwa::ui::tabs::WorkspaceTab>();
    return workspaceTab ? workspaceTab->canvasPanel() : nullptr;
}

} // namespace

namespace ruwa::core {

// ======================================================================================
//   U N D O
// ======================================================================================

bool UndoActionCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = activeCanvasPanel(ctx);
    if (canvasPanel && canvasPanel->isDrawingActive()) {
        return false;
    }
    auto* mgr = m_resolve ? m_resolve() : nullptr;
    return mgr && (mgr->canUndo() || (canvasPanel && canvasPanel->hasPendingStrokeFinalization()));
}

void UndoActionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    auto* canvasPanel = activeCanvasPanel(ctx);
    if (canvasPanel && canvasPanel->isDrawingActive()) {
        return;
    }

    // A released GPU stroke is visible before its DrawCommand reaches the undo
    // stack. Finalize it first so undo always targets the latest visible edit,
    // rather than racing its deferred tile readback and undoing the command below.
    if (canvasPanel && canvasPanel->hasPendingStrokeFinalization()) {
        canvasPanel->flushPendingStrokeFinalization();
    }

    // Finalization can push a command and discard the redo tail, so resolve and
    // validate the manager only after the stack has reached its committed state.
    if (auto* mgr = m_resolve ? m_resolve() : nullptr) {
        if (mgr->canUndo()) {
            mgr->undo();
        }
    }
}

// ======================================================================================
//   R E D O
// ======================================================================================

bool RedoActionCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = activeCanvasPanel(ctx);
    if (canvasPanel
        && (canvasPanel->isDrawingActive() || canvasPanel->hasPendingStrokeFinalization())) {
        return false;
    }
    auto* mgr = m_resolve ? m_resolve() : nullptr;
    return mgr && mgr->canRedo();
}

void RedoActionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    auto* canvasPanel = activeCanvasPanel(ctx);
    if (canvasPanel && canvasPanel->isDrawingActive()) {
        return;
    }

    // A pending stroke is a new edit and therefore invalidates the redo tail.
    // Commit it before re-checking the manager in case execute() is invoked
    // directly rather than through canExecute().
    if (canvasPanel && canvasPanel->hasPendingStrokeFinalization()) {
        canvasPanel->flushPendingStrokeFinalization();
    }

    if (auto* mgr = m_resolve ? m_resolve() : nullptr) {
        if (mgr->canRedo()) {
            mgr->redo();
        }
    }
}

} // namespace ruwa::core
