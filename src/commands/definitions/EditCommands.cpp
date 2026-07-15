// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   E D I T   C O M M A N D S
// ======================================================================================

#include "commands/definitions/EditCommands.h"
#include "commands/CommandContext.h"
#include "commands/CommandRegistry.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/canvas/ui/CanvasPanel.h"
namespace ruwa::core::commands {

CommandInfo CopyCommand::info() const
{
    return CommandInfo { .id = "edit.copy",
        .title = "Copy",
        .category = "Edit",
        .description = "Copy the current workspace selection or layer",
        .aliases = { "copy" },
        .defaultShortcut = QKeySequence::Copy,
        .icon = QIcon() };
}

bool CopyCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void CopyCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab) {
        return;
    }
    workspaceTab->handleCopyRequest();
}

CommandInfo CutCommand::info() const
{
    return CommandInfo { .id = "edit.cut",
        .title = "Cut",
        .category = "Edit",
        .description = "Cut selected layers",
        .aliases = { "cut", "cut-layer" },
        .defaultShortcut = QKeySequence::Cut,
        .icon = QIcon() };
}

bool CutCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void CutCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab) {
        return;
    }
    workspaceTab->handleCutRequest();
}

CommandInfo PasteCommand::info() const
{
    return CommandInfo { .id = "edit.paste",
        .title = "Paste",
        .category = "Edit",
        .description = "Paste into the current workspace",
        .aliases = { "paste" },
        .defaultShortcut = QKeySequence::Paste,
        .icon = QIcon() };
}

bool PasteCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void PasteCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab) {
        return;
    }
    workspaceTab->handlePasteRequest();
}

CommandInfo DeselectCommand::info() const
{
    return CommandInfo { .id = "selection.deselect",
        .title = "Deselect",
        .category = "Selection",
        .description = "Clear the current selection",
        .aliases = { "deselect", "clear-selection" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_D),
        .icon = QIcon() };
}

bool DeselectCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeCanvasPanel() != nullptr;
}

void DeselectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->clearSelectionMask();
    }
}

CommandInfo FillSelectionCommand::info() const
{
    return CommandInfo { .id = "selection.fill",
        .title = "Fill",
        .category = "Selection",
        .description = "Fill the current selection with the active color",
        .aliases = { "fill", "fill-selection" },
        .defaultShortcut = QKeySequence(Qt::SHIFT | Qt::Key_F5),
        .icon = QIcon() };
}

bool FillSelectionCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeCanvasPanel() != nullptr;
}

void FillSelectionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->fillSelectionWithCurrentColor();
    }
}

void registerEditCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<CutCommand>());
    registry.registerCommand(std::make_unique<CopyCommand>());
    registry.registerCommand(std::make_unique<PasteCommand>());
    registry.registerCommand(std::make_unique<DeselectCommand>());
    registry.registerCommand(std::make_unique<FillSelectionCommand>());
}

} // namespace ruwa::core::commands
