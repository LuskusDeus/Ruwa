// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   L A Y E R   C O M M A N D S
// ======================================================================================

#include "commands/definitions/LayerCommands.h"
#include "commands/CommandContext.h"
#include "commands/CommandRegistry.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/layers/ui/LayersPanel.h"
namespace ruwa::core::commands {

namespace {

bool canExecuteLayerCommand(const CommandContext& ctx)
{
    auto* panel = ctx.activeLayersPanel();
    return panel && panel->layerModel();
}

} // namespace

// ======================================================================================
//   A D D   L A Y E R
// ======================================================================================

CommandInfo AddLayerCommand::info() const
{
    return CommandInfo { .id = "layers.add",
        .title = "Add Layer",
        .category = "Layers",
        .description = "Create a new layer",
        .aliases = { "new-layer", "create-layer", "add-layer" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), // Photoshop: New Layer
        .icon = QIcon() };
}

bool AddLayerCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void AddLayerCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->addLayer();
    }
}

// ======================================================================================
//   A D D   G R O U P
// ======================================================================================

CommandInfo AddGroupCommand::info() const
{
    return CommandInfo { .id = "layers.add-group",
        .title = "Add Group",
        .category = "Layers",
        .description = "Create a new layer group",
        .aliases = { "new-group", "create-group", "add-group" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_G), // Photoshop: Group Layers
        .icon = QIcon() };
}

bool AddGroupCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void AddGroupCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->addGroup();
    }
}

// ======================================================================================
//   A D D   A D J U S T M E N T   L A Y E R
// ======================================================================================

CommandInfo AddAdjustmentLayerCommand::info() const
{
    return CommandInfo { .id = "layers.add-adjustment",
        .title = "Add Adjustment Layer",
        .category = "Layers",
        .description = "Create a new adjustment layer that applies its effects to the layers below",
        .aliases = { "new-adjustment", "create-adjustment", "add-adjustment", "adjustment-layer" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool AddAdjustmentLayerCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void AddAdjustmentLayerCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->addAdjustmentLayer();
    }
}

// ======================================================================================
//   D E L E T E   S E L E C T E D   L A Y E R S
// ======================================================================================

CommandInfo DeleteSelectedLayersCommand::info() const
{
    return CommandInfo { .id = "layers.delete",
        .title = "Delete Selected Layers",
        .category = "Layers",
        .description = "Delete currently selected layers",
        .aliases = { "layer-delete", "delete-layer", "remove-layer" },
        .defaultShortcut = QKeySequence(Qt::Key_Delete),
        .icon = QIcon() };
}

bool DeleteSelectedLayersCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void DeleteSelectedLayersCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->deleteSelectedLayers();
    }
}

CommandInfo DuplicateSelectedLayersCommand::info() const
{
    return CommandInfo { .id = "layers.duplicate",
        .title = "Duplicate Selected Layers",
        .category = "Layers",
        .description = "Duplicate currently selected layers",
        .aliases = { "layer-duplicate", "duplicate-layer", "copy-layer" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_J),
        .icon = QIcon() };
}

bool DuplicateSelectedLayersCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void DuplicateSelectedLayersCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->duplicateSelectedLayers();
    }
}

CommandInfo MergeDownCommand::info() const
{
    return CommandInfo { .id = "layers.merge-down",
        .title = "Merge Layers",
        .category = "Layers",
        .description = "Merge the selected layers, or merge the layer down when one is selected",
        .aliases = { "merge-down", "merge-layer-down", "layer-merge-down", "merge-selected",
            "merge-layers" },
        .defaultShortcut
        = QKeySequence(Qt::CTRL | Qt::Key_E), // Photoshop: Merge Layers / Merge Down
        .icon = QIcon() };
}

bool MergeDownCommand::canExecute(const CommandContext& ctx) const
{
    auto* panel = ctx.activeLayersPanel();
    return panel && panel->hasMergeIntent();
}

void MergeDownCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        // Contextual, like Photoshop's Ctrl+E: merge the selected layers when several
        // are selected, otherwise merge down. Shows a custom warning when blocked by a
        // Background or Smart/Board layer.
        panel->performMerge();
    }
}

CommandInfo MergeVisibleCommand::info() const
{
    return CommandInfo { .id = "layers.merge-visible",
        .title = "Merge Visible",
        .category = "Layers",
        .description = "Merge all visible layers into one",
        .aliases = { "merge-visible", "flatten-visible", "layer-merge-visible" },
        .defaultShortcut
        = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), // Photoshop: Merge Visible
        .icon = QIcon() };
}

bool MergeVisibleCommand::canExecute(const CommandContext& ctx) const
{
    auto* panel = ctx.activeLayersPanel();
    return panel && panel->canMergeVisibleLayers();
}

void MergeVisibleCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->mergeVisibleLayers();
    }
}

CommandInfo QuickClippingMaskCommand::info() const
{
    return CommandInfo { .id = "layers.quick-clipping-mask",
        .title = "Quick Clipping Mask",
        .category = "Layers",
        .description = "Apply clipping mask to selected layers",
        .aliases = { "quick-clip", "clip-mask", "clipping-mask" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G),
        .icon = QIcon() };
}

bool QuickClippingMaskCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void QuickClippingMaskCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->applyQuickClippingMask();
    }
}

CommandInfo ToggleSelectedLayerVisibilityCommand::info() const
{
    return CommandInfo { .id = "layers.toggle-visibility",
        .title = "Toggle Selected Layer Visibility",
        .category = "Layers",
        .description = "Hide or show selected layers",
        .aliases = { "hide-layer", "show-layer", "toggle-layer-visibility" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_Comma),
        .icon = QIcon() };
}

bool ToggleSelectedLayerVisibilityCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteLayerCommand(ctx);
}

void ToggleSelectedLayerVisibilityCommand::execute(
    const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeLayersPanel()) {
        panel->toggleSelectedLayerVisibility();
    }
}

void registerLayerCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<AddLayerCommand>());
    registry.registerCommand(std::make_unique<AddGroupCommand>());
    registry.registerCommand(std::make_unique<AddAdjustmentLayerCommand>());
    registry.registerCommand(std::make_unique<DeleteSelectedLayersCommand>());
    registry.registerCommand(std::make_unique<DuplicateSelectedLayersCommand>());
    registry.registerCommand(std::make_unique<MergeDownCommand>());
    registry.registerCommand(std::make_unique<MergeVisibleCommand>());
    registry.registerCommand(std::make_unique<QuickClippingMaskCommand>());
    registry.registerCommand(std::make_unique<ToggleSelectedLayerVisibilityCommand>());
}

} // namespace ruwa::core::commands
