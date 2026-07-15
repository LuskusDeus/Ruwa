// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   V I E W   C O M M A N D S
// ======================================================================================

#include "commands/definitions/ViewCommands.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandContext.h"
#include "shell/main-window/MainWindow.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/settings/SettingsManager.h"
#include <QMetaObject>

namespace ruwa::core::commands {
namespace {

bool canExecuteCameraNavigationCommand(const CommandContext& ctx)
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->isInteractionEnabled() && !canvasPanel->isExportMode();
}

} // namespace

// ======================================================================================
//   T O G G L E   C O M M A N D   P A L E T T E
// ======================================================================================

CommandInfo ToggleCommandPaletteCommand::info() const
{
    return CommandInfo { .id = "view.commandPalette",
        .title = "Command Palette",
        .category = "View",
        .description = "Open the command palette",
        .aliases = { "palette", "commands", "cp" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
        .icon = QIcon() };
}

void ToggleCommandPaletteCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* mainWindow = ctx.mainWindow()) {
        // Call the Q_INVOKABLE method on MainWindow
        QMetaObject::invokeMethod(mainWindow, "showCommandPalette", Qt::DirectConnection);
    }
}

// ======================================================================================
//   Z O O M   I N
// ======================================================================================

CommandInfo ZoomInCommand::info() const
{
    return CommandInfo { .id = "view.zoomIn",
        .title = "Zoom In",
        .category = "View",
        .description = "Zoom in on the canvas",
        .aliases = { "zoomin", "zi", "+" },
        .defaultShortcut = QKeySequence::ZoomIn,
        .icon = QIcon() };
}

bool ZoomInCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteCameraNavigationCommand(ctx);
}

void ZoomInCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = ctx.activeCanvasPanel();
    if (!canvasPanel || !canExecuteCameraNavigationCommand(ctx))
        return;
    canvasPanel->zoomBy(1.15f);
}

// ======================================================================================
//   Z O O M   O U T
// ======================================================================================

CommandInfo ZoomOutCommand::info() const
{
    return CommandInfo { .id = "view.zoomOut",
        .title = "Zoom Out",
        .category = "View",
        .description = "Zoom out on the canvas",
        .aliases = { "zoomout", "zo", "-" },
        .defaultShortcut = QKeySequence::ZoomOut,
        .icon = QIcon() };
}

bool ZoomOutCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteCameraNavigationCommand(ctx);
}

void ZoomOutCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = ctx.activeCanvasPanel();
    if (!canvasPanel || !canExecuteCameraNavigationCommand(ctx))
        return;
    canvasPanel->zoomBy(1.0f / 1.15f);
}

// ======================================================================================
//   Z O O M   R E S E T
// ======================================================================================

CommandInfo ZoomResetCommand::info() const
{
    return CommandInfo { .id = "view.zoomReset",
        .title = "Reset Zoom",
        .category = "View",
        .description = "Reset zoom to 100%",
        .aliases = { "zoom100", "zr", "actualsize" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_1), // Photoshop: 100%
        .icon = QIcon() };
}

bool ZoomResetCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteCameraNavigationCommand(ctx);
}

void ZoomResetCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = ctx.activeCanvasPanel();
    if (!canvasPanel || !canExecuteCameraNavigationCommand(ctx))
        return;
    canvasPanel->setZoomSmooth(1.0f);
}

// ======================================================================================
//   Z O O M   T O   F I T
// ======================================================================================

CommandInfo ZoomToFitCommand::info() const
{
    return CommandInfo { .id = "view.zoomToFit",
        .title = "Zoom to Fit",
        .category = "View",
        .description = "Fit the entire canvas in the view",
        .aliases = { "fit", "zoomfit", "zf" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_0), // Photoshop: Fit on Screen
        .icon = QIcon() };
}

bool ZoomToFitCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteCameraNavigationCommand(ctx);
}

void ZoomToFitCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = ctx.activeCanvasPanel();
    if (!canvasPanel || !canExecuteCameraNavigationCommand(ctx))
        return;
    canvasPanel->zoomToFit();
}

// ======================================================================================
//   R E S E T   L A Y O U T
// ======================================================================================

CommandInfo ResetLayoutCommand::info() const
{
    return CommandInfo { .id = "view.resetLayout",
        .title = "Reset Layout",
        .category = "View",
        .description = "Reset panels to default layout",
        .aliases = { "resetlayout", "defaultlayout" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool ResetLayoutCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ResetLayoutCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab)
        return;

    wsTab->resetDockLayout();
}

// ======================================================================================
//   P A N E L   T O G G L E S
// ======================================================================================

CommandInfo ToggleBrushPanelCommand::info() const
{
    return CommandInfo { .id = "view.toggleBrushPanel",
        .title = "Toggle Brush Panel",
        .category = "View",
        .description = "Show or hide the brush panel",
        .aliases = { "brush-panel", "toggle-brush-panel" },
        .defaultShortcut = QKeySequence(Qt::Key_F5),
        .icon = QIcon() };
}

bool ToggleBrushPanelCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ToggleBrushPanelCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab)
        return;

    wsTab->setBrushesPanelVisible(!wsTab->isBrushesPanelVisible());
}

CommandInfo ToggleColorPanelCommand::info() const
{
    return CommandInfo { .id = "view.toggleColorPanel",
        .title = "Toggle Color Panel",
        .category = "View",
        .description = "Show or hide the color panel",
        .aliases = { "color-panel", "toggle-color-panel" },
        .defaultShortcut = QKeySequence(Qt::Key_F6),
        .icon = QIcon() };
}

bool ToggleColorPanelCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ToggleColorPanelCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab)
        return;

    wsTab->setColorPanelVisible(!wsTab->isColorPanelVisible());
}

CommandInfo ToggleLayersPanelCommand::info() const
{
    return CommandInfo { .id = "view.toggleLayersPanel",
        .title = "Toggle Layers Panel",
        .category = "View",
        .description = "Show or hide the layers panel",
        .aliases = { "layers-panel", "toggle-layers-panel" },
        .defaultShortcut = QKeySequence(Qt::Key_F7),
        .icon = QIcon() };
}

bool ToggleLayersPanelCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ToggleLayersPanelCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab)
        return;

    wsTab->setLayersPanelVisible(!wsTab->isLayersPanelVisible());
}

// ======================================================================================
//   C A N V A S   V I E W   F L I P
// ======================================================================================

CommandInfo ToggleCanvasViewFlipHorizontalCommand::info() const
{
    return CommandInfo { .id = "view.flipCanvasHorizontal",
        .title = "Flip Canvas View Horizontally",
        .category = "View",
        .description = "Mirror the canvas left-right in the editor (document pixels unchanged)",
        .aliases = { "flip-canvas-h", "mirror-canvas-horizontal", "view-flip-h" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool ToggleCanvasViewFlipHorizontalCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ToggleCanvasViewFlipHorizontalCommand::execute(
    const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab || !wsTab->canvasPanel())
        return;

    wsTab->canvasPanel()->toggleCanvasViewFlipHorizontal();
}

CommandInfo ToggleCanvasViewFlipVerticalCommand::info() const
{
    return CommandInfo { .id = "view.flipCanvasVertical",
        .title = "Flip Canvas View Vertically",
        .category = "View",
        .description = "Mirror the canvas top-bottom in the editor (document pixels unchanged)",
        .aliases = { "flip-canvas-v", "mirror-canvas-vertical", "view-flip-v" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool ToggleCanvasViewFlipVerticalCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeWorkspaceTab() != nullptr;
}

void ToggleCanvasViewFlipVerticalCommand::execute(
    const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* wsTab = ctx.activeWorkspaceTab();
    if (!wsTab || !wsTab->canvasPanel())
        return;

    wsTab->canvasPanel()->toggleCanvasViewFlipVertical();
}

// ======================================================================================
//   M E O W   ( H I D D E N   E A S T E R   E G G )
// ======================================================================================

CommandInfo MeowCommand::info() const
{
    return CommandInfo { .id = "easter.meow",
        .title = "Meow",
        .category = "Easter Eggs",
        .description = "meow~",
        .aliases = { "meow" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

void MeowCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(ctx);
    Q_UNUSED(args);

    ruwa::core::SettingsManager::instance().setWelcomeBannerFixedKeyDisablingRandomize(
        ":/images/Banner1April");
}

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerViewCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<ToggleCommandPaletteCommand>());
    registry.registerCommand(std::make_unique<ZoomInCommand>());
    registry.registerCommand(std::make_unique<ZoomOutCommand>());
    registry.registerCommand(std::make_unique<ZoomResetCommand>());
    registry.registerCommand(std::make_unique<ZoomToFitCommand>());
    registry.registerCommand(std::make_unique<ResetLayoutCommand>());
    registry.registerCommand(std::make_unique<ToggleBrushPanelCommand>());
    registry.registerCommand(std::make_unique<ToggleColorPanelCommand>());
    registry.registerCommand(std::make_unique<ToggleLayersPanelCommand>());
    registry.registerCommand(std::make_unique<ToggleCanvasViewFlipHorizontalCommand>());
    registry.registerCommand(std::make_unique<ToggleCanvasViewFlipVerticalCommand>());
    registry.registerCommand(std::make_unique<MeowCommand>());
}

} // namespace ruwa::core::commands
