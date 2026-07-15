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

bool isUndoBlockedByCanvasInteraction(const ruwa::core::CommandContext& ctx)
{
    auto* workspaceTab = ctx.activeTabAs<ruwa::ui::tabs::WorkspaceTab>();
    if (!workspaceTab || !workspaceTab->canvasPanel()) {
        return false;
    }

    auto* canvasPanel = workspaceTab->canvasPanel();
    return canvasPanel->isDrawingActive();
}

} // namespace

namespace ruwa::core {

// ======================================================================================
//   U N D O
// ======================================================================================

bool UndoActionCommand::canExecute(const CommandContext& ctx) const
{
    if (isUndoBlockedByCanvasInteraction(ctx)) {
        return false;
    }
    auto* mgr = m_resolve ? m_resolve() : nullptr;
    return mgr && mgr->canUndo();
}

void UndoActionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (isUndoBlockedByCanvasInteraction(ctx)) {
        return;
    }
    if (auto* mgr = m_resolve ? m_resolve() : nullptr) {
        mgr->undo();
    }
}

// ======================================================================================
//   R E D O
// ======================================================================================

bool RedoActionCommand::canExecute(const CommandContext& ctx) const
{
    if (isUndoBlockedByCanvasInteraction(ctx)) {
        return false;
    }
    auto* mgr = m_resolve ? m_resolve() : nullptr;
    return mgr && mgr->canRedo();
}

void RedoActionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (isUndoBlockedByCanvasInteraction(ctx)) {
        return;
    }
    if (auto* mgr = m_resolve ? m_resolve() : nullptr) {
        mgr->redo();
    }
}

} // namespace ruwa::core
