// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   T O O L   C O M M A N D S
// ======================================================================================

#include "commands/definitions/ToolCommands.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandContext.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/tools/ToolsPanel.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/color/ColorPanel.h"
namespace ruwa::core::commands {

namespace {

constexpr qreal kBrushSizeStep = 0.02;
constexpr qreal kBrushOpacityStep = 0.05;

bool canExecuteToolCommand(const CommandContext& ctx)
{
    return ctx.activeWorkspaceTab() != nullptr;
}

/// Tool switch commands (B, E, H, etc.) are blocked during drawing.
bool canExecuteToolSwitchCommand(const CommandContext& ctx)
{
    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab || !workspaceTab->canvasPanel()) {
        return false;
    }
    if (workspaceTab->canvasPanel()->isDrawingActive()) {
        return false;
    }
    return true;
}

void executeToolCommand(const CommandContext& ctx, ruwa::ui::workspace::ToolsPanel::Tool tool)
{
    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab) {
        return;
    }

    auto* toolsPanel = workspaceTab->toolsPanel();
    if (!toolsPanel) {
        return;
    }

    toolsPanel->setCurrentTool(tool);
}

ruwa::ui::workspace::CanvasPanel* activeCanvasPanel(const CommandContext& ctx)
{
    auto* workspaceTab = ctx.activeWorkspaceTab();
    return workspaceTab ? workspaceTab->canvasPanel() : nullptr;
}

bool readRequiredDoubleArgument(const QVariantMap& args, const char* name, qreal& outValue)
{
    const QVariant value = args.value(QString::fromLatin1(name));
    if (!value.isValid()) {
        return false;
    }
    if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
        return false;
    }

    bool ok = false;
    const qreal parsed = value.toDouble(&ok);
    if (!ok) {
        return false;
    }

    outValue = parsed;
    return true;
}

} // namespace

// ======================================================================================
//   H A N D
// ======================================================================================

CommandInfo ToolHandCommand::info() const
{
    return CommandInfo { .id = "tools.hand",
        .title = "Hand",
        .category = "Tools",
        .description = "Select the hand (pan) tool",
        .aliases = { "hand", "h", "pan" },
        .defaultShortcut = QKeySequence(Qt::Key_H),
        .icon = QIcon() };
}

bool ToolHandCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolHandCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Hand);
}

// ======================================================================================
//   B R U S H
// ======================================================================================

CommandInfo ToolBrushCommand::info() const
{
    return CommandInfo { .id = "tools.brush",
        .title = "Brush",
        .category = "Tools",
        .description = "Select the brush tool",
        .aliases = { "brush", "b", "paint" },
        .defaultShortcut = QKeySequence(Qt::Key_B),
        .icon = QIcon() };
}

bool ToolBrushCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolBrushCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Brush);
}

// ======================================================================================
//   B L U R
// ======================================================================================

CommandInfo ToolBlurCommand::info() const
{
    return CommandInfo { .id = "tools.blur",
        .title = "Blur",
        .category = "Tools",
        .description = "Select the blur tool",
        .aliases = { "blur" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool ToolBlurCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolBlurCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Blur);
}

// ======================================================================================
//   S M U D G E
// ======================================================================================

CommandInfo ToolSmudgeCommand::info() const
{
    return CommandInfo { .id = "tools.smudge",
        .title = "Smudge",
        .category = "Tools",
        .description = "Select the smudge tool",
        .aliases = { "smudge" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool ToolSmudgeCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolSmudgeCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Smudge);
}

// ======================================================================================
//   T E X T
// ======================================================================================

CommandInfo ToolTextCommand::info() const
{
    return CommandInfo { .id = "tools.text",
        .title = "Text",
        .category = "Tools",
        .description = "Select the text tool",
        .aliases = { "text", "type", "t" },
        .defaultShortcut = QKeySequence(Qt::Key_T),
        .icon = QIcon() };
}

bool ToolTextCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolTextCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Text);
}

// ======================================================================================
//   E R A S E R
// ======================================================================================

CommandInfo ToolEraserCommand::info() const
{
    return CommandInfo { .id = "tools.eraser",
        .title = "Eraser",
        .category = "Tools",
        .description = "Select the eraser tool",
        .aliases = { "eraser", "e" },
        .defaultShortcut = QKeySequence(Qt::Key_E),
        .icon = QIcon() };
}

bool ToolEraserCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolEraserCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Eraser);
}

// ======================================================================================
//   T O G G L E   B R U S H   E R A S E R
// ======================================================================================

CommandInfo ToggleBrushEraserCommand::info() const
{
    return CommandInfo { .id = "tools.toggleBrushEraser",
        .title = "Toggle Eraser Brush",
        .category = "Tools",
        .description = "Toggle erasing with the brush's own tip",
        .aliases = { "eraser-brush", "brush-eraser", "toggle-eraser-brush" },
        .defaultShortcut = QKeySequence(Qt::SHIFT | Qt::Key_E),
        .icon = QIcon() };
}

bool ToggleBrushEraserCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToggleBrushEraserCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab || !workspaceTab->canvasPanel()) {
        return;
    }

    workspaceTab->canvasPanel()->toggleBrushEraserMode();
}

// ======================================================================================
//   F I L L
// ======================================================================================

CommandInfo ToolFillCommand::info() const
{
    return CommandInfo { .id = "tools.fill",
        .title = "Fill",
        .category = "Tools",
        .description = "Select the fill tool",
        .aliases = { "fill", "g", "paint-bucket" },
        .defaultShortcut = QKeySequence(Qt::Key_G),
        .icon = QIcon() };
}

bool ToolFillCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolFillCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Fill);
}

CommandInfo ToolClassicFillCommand::info() const
{
    return CommandInfo { .id = "tools.classic-fill",
        .title = "Classic Fill",
        .category = "Tools",
        .description = "Select the classic CPU fill tool",
        .aliases = { "classic-fill", "cpu-fill", "bucket-fill" },
        .defaultShortcut = QKeySequence(Qt::SHIFT | Qt::Key_G),
        .icon = QIcon() };
}

bool ToolClassicFillCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolClassicFillCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::ClassicFill);
}

// ======================================================================================
//   E Y E D R O P P E R
// ======================================================================================

CommandInfo ToolEyedropperCommand::info() const
{
    return CommandInfo { .id = "tools.eyedropper",
        .title = "Eyedropper",
        .category = "Tools",
        .description = "Select the eyedropper tool",
        .aliases = { "eyedropper", "i", "pipette" },
        .defaultShortcut = QKeySequence(Qt::Key_I),
        .icon = QIcon() };
}

bool ToolEyedropperCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolEyedropperCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Eyedropper);
}

// ======================================================================================
//   L A S S O
// ======================================================================================

CommandInfo ToolLassoCommand::info() const
{
    return CommandInfo { .id = "tools.lasso",
        .title = "Lasso",
        .category = "Tools",
        .description = "Select the lasso selection tool",
        .aliases = { "lasso", "l", "selection" },
        .defaultShortcut = QKeySequence(Qt::Key_L),
        .icon = QIcon() };
}

bool ToolLassoCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolLassoCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Lasso);
}

// ======================================================================================
//   L A S S O   F I L L
// ======================================================================================

CommandInfo ToolLassoFillCommand::info() const
{
    return CommandInfo { .id = "tools.lasso-fill",
        .title = "Lasso Fill",
        .category = "Tools",
        .description = "Select the lasso fill tool",
        .aliases = { "lasso-fill", "lasso-fill-tool" },
        .defaultShortcut = QKeySequence(Qt::SHIFT | Qt::Key_L),
        .icon = QIcon() };
}

bool ToolLassoFillCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolLassoFillCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::LassoFill);
}

// ======================================================================================
//   S Q U A R E   S E L E C T I O N
// ======================================================================================

CommandInfo ToolSquareSelectionCommand::info() const
{
    return CommandInfo { .id = "tools.square-selection",
        .title = "Square Selection",
        .category = "Tools",
        .description = "Select a rectangular region",
        .aliases = { "square-selection", "marquee", "m", "rect-selection" },
        .defaultShortcut = QKeySequence(Qt::Key_M),
        .icon = QIcon() };
}

bool ToolSquareSelectionCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolSquareSelectionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::SquareSelection);
}

// ======================================================================================
//   C I R C L E   S E L E C T I O N
// ======================================================================================

CommandInfo ToolCircleSelectionCommand::info() const
{
    return CommandInfo { .id = "tools.circle-selection",
        .title = "Circle Selection",
        .category = "Tools",
        .description = "Select a circular region",
        .aliases = { "circle-selection", "oval", "o", "ellipse-selection" },
        .defaultShortcut = QKeySequence(Qt::Key_O),
        .icon = QIcon() };
}

bool ToolCircleSelectionCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolCircleSelectionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::CircleSelection);
}

// ======================================================================================
//   M O V E
// ======================================================================================

CommandInfo ToolMoveCommand::info() const
{
    return CommandInfo { .id = "tools.move",
        .title = "Move",
        .category = "Tools",
        .description = "Select the move tool",
        .aliases = { "move", "v" },
        .defaultShortcut = QKeySequence(Qt::Key_V),
        .icon = QIcon() };
}

bool ToolMoveCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolMoveCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Move);
}

// ======================================================================================
//   R O T A T E   V I E W
// ======================================================================================

CommandInfo ToolRotateViewCommand::info() const
{
    return CommandInfo { .id = "tools.rotate-view",
        .title = "Rotate View",
        .category = "Tools",
        .description = "Rotate the canvas view",
        .aliases = { "rotate-view", "rotate", "r" },
        .defaultShortcut = QKeySequence(Qt::Key_R),
        .icon = QIcon() };
}

bool ToolRotateViewCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolRotateViewCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::RotateView);
}

// ======================================================================================
//   C A N V A S   R E S I Z E
// ======================================================================================

CommandInfo ToolCanvasResizeCommand::info() const
{
    return CommandInfo { .id = "tools.canvas-resize",
        .title = "Canvas Resize",
        .category = "Tools",
        .description = "Select canvas resize area",
        .aliases = { "canvas-resize", "resize-canvas" },
        .defaultShortcut = QKeySequence(Qt::Key_C),
        .icon = QIcon() };
}

bool ToolCanvasResizeCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolCanvasResizeCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::CanvasResize);
}

// ======================================================================================
//   Z O O M
// ======================================================================================

CommandInfo ToolZoomCommand::info() const
{
    return CommandInfo { .id = "tools.zoom",
        .title = "Zoom",
        .category = "Tools",
        .description = "Select the zoom tool",
        .aliases = { "zoom", "z" },
        .defaultShortcut = QKeySequence(Qt::Key_Z),
        .icon = QIcon() };
}

bool ToolZoomCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolSwitchCommand(ctx);
}

void ToolZoomCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    executeToolCommand(ctx, ruwa::ui::workspace::ToolsPanel::Tool::Zoom);
}

// ======================================================================================
//   C A M E R A
// ======================================================================================

CommandInfo ToolCameraCommand::info() const
{
    return CommandInfo { .id = "tools.camera",
        .title = "Camera",
        .category = "Tools",
        .description = "Copy canvas to clipboard",
        .aliases = { "camera", "copy-canvas", "screenshot" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
        .icon = QIcon() };
}

bool ToolCameraCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void ToolCameraCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab)
        return;
    workspaceTab->copyCanvasToClipboard();
}

// ======================================================================================
//   D E F A U L T   C O L O R S
// ======================================================================================

CommandInfo ResetForegroundBackgroundColorsCommand::info() const
{
    return CommandInfo { .id = "tools.colors.default",
        .title = "Default Foreground/Background Colors",
        .category = "Tools",
        .description = "Reset foreground/background colors to black and white",
        .aliases = { "default-colors", "reset-colors", "black-white" },
        .defaultShortcut = QKeySequence(Qt::Key_D),
        .icon = QIcon() };
}

bool ResetForegroundBackgroundColorsCommand::canExecute(const CommandContext& ctx) const
{
    auto* workspaceTab = ctx.activeWorkspaceTab();
    return workspaceTab && workspaceTab->colorPanel();
}

void ResetForegroundBackgroundColorsCommand::execute(
    const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab || !workspaceTab->colorPanel()) {
        return;
    }
    workspaceTab->colorPanel()->resetForegroundBackgroundColors();
}

// ======================================================================================
//   S W A P   C O L O R S
// ======================================================================================

CommandInfo SwapForegroundBackgroundColorsCommand::info() const
{
    return CommandInfo { .id = "tools.colors.swap",
        .title = "Swap Foreground/Background Colors",
        .category = "Tools",
        .description = "Swap foreground and background colors",
        .aliases = { "swap-colors", "foreground-background" },
        .defaultShortcut = QKeySequence(Qt::Key_X),
        .icon = QIcon() };
}

bool SwapForegroundBackgroundColorsCommand::canExecute(const CommandContext& ctx) const
{
    auto* workspaceTab = ctx.activeWorkspaceTab();
    return workspaceTab && workspaceTab->colorPanel();
}

void SwapForegroundBackgroundColorsCommand::execute(
    const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab || !workspaceTab->colorPanel()) {
        return;
    }
    workspaceTab->colorPanel()->swapForegroundBackgroundColors();
}

// ======================================================================================
//   B R U S H   S I Z E   +
// ======================================================================================

CommandInfo BrushSizeIncreaseCommand::info() const
{
    return CommandInfo { .id = "tools.brushSizeIncrease",
        .title = "Increase Brush Size",
        .category = "Tools",
        .description = "Increase brush size",
        .aliases = { "brush-size-plus", "brush-size-increase", "bracket-right" },
        .defaultShortcut = QKeySequence(Qt::Key_BracketRight),
        .icon = QIcon() };
}

bool BrushSizeIncreaseCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void BrushSizeIncreaseCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = activeCanvasPanel(ctx);
    if (!canvasPanel) {
        return;
    }

    canvasPanel->adjustBrushSizeNormalized(kBrushSizeStep);
}

// ======================================================================================
//   B R U S H   S I Z E   -
// ======================================================================================

CommandInfo BrushSizeDecreaseCommand::info() const
{
    return CommandInfo { .id = "tools.brushSizeDecrease",
        .title = "Decrease Brush Size",
        .category = "Tools",
        .description = "Decrease brush size",
        .aliases = { "brush-size-minus", "brush-size-decrease", "bracket-left" },
        .defaultShortcut = QKeySequence(Qt::Key_BracketLeft),
        .icon = QIcon() };
}

bool BrushSizeDecreaseCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void BrushSizeDecreaseCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = activeCanvasPanel(ctx);
    if (!canvasPanel) {
        return;
    }

    canvasPanel->adjustBrushSizeNormalized(-kBrushSizeStep);
}

// ======================================================================================
//   B R U S H   O P A C I T Y   +
// ======================================================================================

CommandInfo BrushOpacityIncreaseCommand::info() const
{
    return CommandInfo { .id = "tools.brushOpacityIncrease",
        .title = "Increase Brush Opacity",
        .category = "Tools",
        .description = "Increase brush opacity",
        .aliases = { "brush-opacity-plus", "brush-opacity-increase", "shift-bracket-right" },
        .defaultShortcut = QKeySequence(Qt::SHIFT | Qt::Key_BracketRight),
        .icon = QIcon() };
}

bool BrushOpacityIncreaseCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void BrushOpacityIncreaseCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = activeCanvasPanel(ctx);
    if (!canvasPanel) {
        return;
    }

    canvasPanel->adjustBrushOpacityNormalized(kBrushOpacityStep);
}

// ======================================================================================
//   B R U S H   O P A C I T Y   -
// ======================================================================================

CommandInfo BrushOpacityDecreaseCommand::info() const
{
    return CommandInfo { .id = "tools.brushOpacityDecrease",
        .title = "Decrease Brush Opacity",
        .category = "Tools",
        .description = "Decrease brush opacity",
        .aliases = { "brush-opacity-minus", "brush-opacity-decrease", "shift-bracket-left" },
        .defaultShortcut = QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft),
        .icon = QIcon() };
}

bool BrushOpacityDecreaseCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void BrushOpacityDecreaseCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    auto* canvasPanel = activeCanvasPanel(ctx);
    if (!canvasPanel) {
        return;
    }

    canvasPanel->adjustBrushOpacityNormalized(-kBrushOpacityStep);
}

// ======================================================================================
//   S E T   B R U S H   S I Z E   ( s b s )
// ======================================================================================

CommandInfo SetBrushSizeCommand::info() const
{
    return CommandInfo { .id = "tools.setBrushSize",
        .title = "Set Brush Size",
        .category = "Tools",
        .description = "Set brush size in pixels",
        .aliases = { "set-brush-size", "sbs", "brush-size" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon(),
        .arguments
        = { CommandArgument { .name = "value", .hint = "Size (px)", .placeholder = "50" } },
        .palettePrefillAlias = QStringLiteral("sbs") };
}

bool SetBrushSizeCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void SetBrushSizeCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    auto* canvasPanel = activeCanvasPanel(ctx);
    if (!canvasPanel) {
        return;
    }

    qreal value = 0.0;
    if (!readRequiredDoubleArgument(args, "value", value)) {
        return;
    }

    const float radiusPx = static_cast<float>(qMax(1.0, value));
    const QSize canvasSize = canvasPanel->canvasSize();
    const qreal normalized = ruwa::ui::widgets::normalizedSizeFromRadiusPxForCanvasMode(
        radiusPx, canvasSize.width(), canvasSize.height(), canvasPanel->hasFiniteDocumentBounds());
    canvasPanel->setBrushSizeNormalized(normalized);
}

// ======================================================================================
//   S E T   B R U S H   O P A C I T Y   ( s b o )
// ======================================================================================

CommandInfo SetBrushOpacityCommand::info() const
{
    return CommandInfo { .id = "tools.setBrushOpacity",
        .title = "Set Brush Opacity",
        .category = "Tools",
        .description = "Set brush opacity (0-100%)",
        .aliases = { "set-brush-opacity", "sbo", "brush-opacity" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon(),
        .arguments
        = { CommandArgument { .name = "value", .hint = "Opacity (%)", .placeholder = "100" } },
        .palettePrefillAlias = QStringLiteral("sbo") };
}

bool SetBrushOpacityCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteToolCommand(ctx);
}

void SetBrushOpacityCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    auto* canvasPanel = activeCanvasPanel(ctx);
    if (!canvasPanel) {
        return;
    }

    qreal value = 0.0;
    if (!readRequiredDoubleArgument(args, "value", value)) {
        return;
    }

    const qreal opacity = qBound(0.0, value / 100.0, 1.0);
    canvasPanel->setBrushOpacityNormalized(opacity);
}

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerToolCommands(CommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<ToolHandCommand>());
    registry.registerCommand(std::make_unique<ToolBrushCommand>());
    registry.registerCommand(std::make_unique<ToolBlurCommand>());
    registry.registerCommand(std::make_unique<ToolSmudgeCommand>());
    registry.registerCommand(std::make_unique<ToolTextCommand>());
    registry.registerCommand(std::make_unique<ToolEraserCommand>());
    registry.registerCommand(std::make_unique<ToggleBrushEraserCommand>());
    registry.registerCommand(std::make_unique<ToolFillCommand>());
    registry.registerCommand(std::make_unique<ToolClassicFillCommand>());
    registry.registerCommand(std::make_unique<ToolEyedropperCommand>());
    registry.registerCommand(std::make_unique<ToolLassoCommand>());
    registry.registerCommand(std::make_unique<ToolLassoFillCommand>());
    registry.registerCommand(std::make_unique<ToolSquareSelectionCommand>());
    registry.registerCommand(std::make_unique<ToolCircleSelectionCommand>());
    registry.registerCommand(std::make_unique<ToolMoveCommand>());
    registry.registerCommand(std::make_unique<ToolRotateViewCommand>());
    registry.registerCommand(std::make_unique<ToolCanvasResizeCommand>());
    registry.registerCommand(std::make_unique<ToolZoomCommand>());
    registry.registerCommand(std::make_unique<ToolCameraCommand>());
    registry.registerCommand(std::make_unique<ResetForegroundBackgroundColorsCommand>());
    registry.registerCommand(std::make_unique<SwapForegroundBackgroundColorsCommand>());
    registry.registerCommand(std::make_unique<BrushSizeIncreaseCommand>());
    registry.registerCommand(std::make_unique<BrushSizeDecreaseCommand>());
    registry.registerCommand(std::make_unique<BrushOpacityIncreaseCommand>());
    registry.registerCommand(std::make_unique<BrushOpacityDecreaseCommand>());
    registry.registerCommand(std::make_unique<SetBrushSizeCommand>());
    registry.registerCommand(std::make_unique<SetBrushOpacityCommand>());
}

} // namespace ruwa::core::commands
