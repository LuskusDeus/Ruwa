// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   E D I T   C O M M A N D S
// ======================================================================================

#include "commands/definitions/EditCommands.h"
#include "commands/CommandContext.h"
#include "commands/CommandRegistry.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/layers/ui/LayersPanel.h"
namespace ruwa::core::commands {

namespace {

/// True when \a widget is \a container or one of its descendants — i.e. the
/// container holds keyboard focus.
bool widgetIsWithin(const QWidget* widget, const QWidget* container)
{
    if (!widget || !container) {
        return false;
    }
    for (const QWidget* w = widget; w; w = w->parentWidget()) {
        if (w == container) {
            return true;
        }
    }
    return false;
}

/// Delete is registered as an application shortcut, so it fires even while a
/// text field has focus (inline layer rename, search boxes). Deleting a layer
/// out from under a rename is never what the keystroke meant.
bool widgetIsTextInput(const QWidget* widget)
{
    return widget
        && (widget->inherits("QLineEdit") || widget->inherits("QAbstractSpinBox")
            || widget->inherits("QTextEdit") || widget->inherits("QPlainTextEdit"));
}

} // namespace

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

CommandInfo ContextualDeleteCommand::info() const
{
    return CommandInfo { .id = "edit.delete",
        .title = "Delete",
        .category = "Edit",
        .description
        = "Delete the selected layer, its mask, or the pixels inside the active selection, "
          "depending on what is focused",
        .aliases = { "delete", "erase" },
        .defaultShortcut = QKeySequence(Qt::Key_Delete),
        .icon = QIcon() };
}

bool ContextualDeleteCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ContextualDeleteCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    QWidget* focus = ctx.focusWidget();
    if (widgetIsTextInput(focus)) {
        return;
    }

    // Focus decides the domain first: the layers panel owns layer/mask deletion,
    // everything else (in practice the canvas) owns pixel deletion.
    auto* layersPanel = ctx.activeLayersPanel();
    if (layersPanel && widgetIsWithin(focus, layersPanel)) {
        if (layersPanel->selectedLayerMaskIsPaintTarget()) {
            layersPanel->deleteSelectedLayerMask();
        } else {
            layersPanel->deleteSelectedLayers();
        }
        return;
    }

    // No selection → deliberately a no-op rather than falling back to deleting
    // the layer, which is what made the old Delete binding feel unpredictable.
    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->deleteSelectionContent();
    }
}

void registerEditCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<CutCommand>());
    registry.registerCommand(std::make_unique<CopyCommand>());
    registry.registerCommand(std::make_unique<PasteCommand>());
    registry.registerCommand(std::make_unique<DeselectCommand>());
    registry.registerCommand(std::make_unique<FillSelectionCommand>());
    registry.registerCommand(std::make_unique<ContextualDeleteCommand>());
}

} // namespace ruwa::core::commands
