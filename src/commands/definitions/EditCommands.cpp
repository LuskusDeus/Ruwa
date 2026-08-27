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
#include "shell/main-window/MainWindow.h"
#include "features/canvas/ui/CanvasPanelHelpers.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextEdit>

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

/// The clipboard payload the Home tab could turn into a new project.
const QMimeData* projectCreatingClipboardMime()
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData || !ruwa::ui::workspace::detail::mayContainImportableImageFromMime(mimeData)) {
        return nullptr;
    }
    return mimeData;
}

enum class ClipboardOp { Copy, Cut, Paste };

/// Cut/copy/paste are application shortcuts too, so a focused text field never
/// gets the keystroke on its own — hand it over instead of editing the document
/// behind the field's back. Returns true when the keystroke was consumed.
bool routeClipboardToFocusedTextInput(QWidget* focus, ClipboardOp op)
{
    const auto dispatch = [op](auto* editor) {
        switch (op) {
        case ClipboardOp::Copy:
            editor->copy();
            break;
        case ClipboardOp::Cut:
            editor->cut();
            break;
        case ClipboardOp::Paste:
            editor->paste();
            break;
        }
    };

    if (auto* lineEdit = qobject_cast<QLineEdit*>(focus)) {
        dispatch(lineEdit);
        return true;
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(focus)) {
        dispatch(textEdit);
        return true;
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(focus)) {
        dispatch(plainTextEdit);
        return true;
    }

    // Any other text field (spin boxes, custom editors): swallow the keystroke
    // rather than let it cut a layer out from under someone who is typing.
    return widgetIsTextInput(focus);
}

/// Ctrl+A is an application shortcut, so a focused text field never sees it on
/// its own — and inside a text field it has to keep meaning "select all text".
bool routeSelectAllToFocusedTextInput(QWidget* focus)
{
    if (auto* lineEdit = qobject_cast<QLineEdit*>(focus)) {
        lineEdit->selectAll();
        return true;
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(focus)) {
        textEdit->selectAll();
        return true;
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(focus)) {
        plainTextEdit->selectAll();
        return true;
    }
    return widgetIsTextInput(focus);
}

/// Keep the application-wide Delete binding from swallowing normal text
/// editing while still preventing it from reaching the document behind a field.
bool routeDeleteToFocusedTextInput(QWidget* focus)
{
    if (auto* lineEdit = qobject_cast<QLineEdit*>(focus)) {
        if (!lineEdit->isReadOnly()) {
            lineEdit->del();
        }
        return true;
    }
    const auto deleteFromEditor = [](auto* editor) {
        if (editor->isReadOnly()) {
            return;
        }
        QTextCursor cursor = editor->textCursor();
        if (cursor.hasSelection()) {
            cursor.removeSelectedText();
        } else {
            cursor.deleteChar();
        }
        editor->setTextCursor(cursor);
    };
    if (auto* textEdit = qobject_cast<QTextEdit*>(focus)) {
        deleteFromEditor(textEdit);
        return true;
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(focus)) {
        deleteFromEditor(plainTextEdit);
        return true;
    }
    return widgetIsTextInput(focus);
}

bool canDeleteFromFocusedTextInput(QWidget* focus)
{
    if (auto* lineEdit = qobject_cast<QLineEdit*>(focus)) {
        return !lineEdit->isReadOnly()
            && (lineEdit->hasSelectedText()
                || lineEdit->cursorPosition() < lineEdit->text().size());
    }
    const auto editorCanDelete = [](const auto* editor) {
        const QTextCursor cursor = editor->textCursor();
        return !editor->isReadOnly() && (cursor.hasSelection() || !cursor.atEnd());
    };
    if (auto* textEdit = qobject_cast<QTextEdit*>(focus)) {
        return editorCanDelete(textEdit);
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(focus)) {
        return editorCanDelete(plainTextEdit);
    }
    return false;
}

bool canExecuteContentTransformAction(const CommandContext& ctx)
{
    auto* panel = ctx.activeCanvasPanel();
    return panel && panel->canApplyContentTransformAction();
}

bool canEnterWarpTransform(const CommandContext& ctx)
{
    auto* panel = ctx.activeCanvasPanel();
    return panel && panel->canEnterWarpTransformMode();
}

} // namespace

CommandInfo CopyCommand::info() const
{
    return CommandInfo { .id = "edit.copy",
        .title = "Copy",
        .category = "Edit",
        .description = "Copy the selected layer, its mask, or the pixels inside the active "
                       "selection, depending on what is focused",
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

    if (routeClipboardToFocusedTextInput(ctx.focusWidget(), ClipboardOp::Copy)) {
        return;
    }

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab) {
        return;
    }
    workspaceTab->handleCopyRequest();
}

CommandInfo CopyMergedCommand::info() const
{
    return CommandInfo { .id = "edit.copyMerged",
        .title = "Copy Merged",
        .category = "Edit",
        .description = "Copy the merged visible result inside the active selection",
        .aliases = { "copy-merged", "merged-copy" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
        .icon = QIcon(),
        .legacyIds = { "tools.camera" } };
}

bool CopyMergedCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->hasActiveSelection();
}

void CopyMergedCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* workspaceTab = ctx.activeWorkspaceTab()) {
        workspaceTab->handleCopyMergedRequest();
    }
}

CommandInfo CutCommand::info() const
{
    return CommandInfo { .id = "edit.cut",
        .title = "Cut",
        .category = "Edit",
        .description = "Cut the selected layer, its mask, or the pixels inside the active "
                       "selection, depending on what is focused",
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

    if (routeClipboardToFocusedTextInput(ctx.focusWidget(), ClipboardOp::Cut)) {
        return;
    }

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
        .description = "Paste the last copy: layers, a layer mask, or copied pixels dropped onto a "
                       "new layer in transform mode",
        .aliases = { "paste" },
        .defaultShortcut = QKeySequence::Paste,
        .icon = QIcon() };
}

bool PasteCommand::canExecute(const CommandContext& ctx) const
{
    if (ctx.activeWorkspaceTab()) {
        return true;
    }
    // The keystroke belongs to whatever is being typed into.
    if (widgetIsTextInput(ctx.focusWidget())) {
        return true;
    }
    // No document open: a clipboard image is still pasteable — it becomes a new
    // project, the same way dropping an image onto the Home tab does.
    return ctx.mainWindow() != nullptr && projectCreatingClipboardMime() != nullptr;
}

void PasteCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (routeClipboardToFocusedTextInput(ctx.focusWidget(), ClipboardOp::Paste)) {
        return;
    }

    auto* workspaceTab = ctx.activeWorkspaceTab();
    if (!workspaceTab) {
        if (auto* mainWindow = ctx.mainWindow()) {
            mainWindow->createProjectFromMimeData(projectCreatingClipboardMime());
        }
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
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->hasActiveSelection();
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
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->hasActiveSelection();
}

void FillSelectionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->fillSelectionWithCurrentColor();
    }
}

CommandInfo SelectAllCommand::info() const
{
    return CommandInfo { .id = "selection.selectAll",
        .title = "Select All",
        .category = "Selection",
        .description = "Select the whole document",
        .aliases = { "select-all" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_A),
        .icon = QIcon() };
}

bool SelectAllCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeCanvasPanel() != nullptr;
}

void SelectAllCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (routeSelectAllToFocusedTextInput(ctx.focusWidget())) {
        return;
    }

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->selectAllCanvas();
    }
}

CommandInfo InvertSelectionCommand::info() const
{
    return CommandInfo { .id = "selection.invert",
        .title = "Invert Selection",
        .category = "Selection",
        .description = "Swap the selected and unselected areas, keeping partial coverage",
        .aliases = { "invert-selection", "inverse" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
        .icon = QIcon() };
}

bool InvertSelectionCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->hasActiveSelection();
}

void InvertSelectionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->invertSelection();
    }
}

CommandInfo ReselectCommand::info() const
{
    return CommandInfo { .id = "selection.reselect",
        .title = "Reselect",
        .category = "Selection",
        .description = "Bring back the selection that was last deselected",
        .aliases = { "reselect" },
        .defaultShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
        .icon = QIcon() };
}

bool ReselectCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->canReselectSelection();
}

void ReselectCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->reselectSelection();
    }
}

CommandInfo SelectLayerContentCommand::info() const
{
    return CommandInfo { .id = "selection.selectLayerContent",
        .title = "Select Layer Content",
        .category = "Selection",
        .description = "Select the opaque pixels of the current layer, following the shape its "
                       "effect chain renders",
        .aliases = { "select-layer-content", "select-opaque" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool SelectLayerContentCommand::canExecute(const CommandContext& ctx) const
{
    return ctx.activeCanvasPanel() != nullptr;
}

void SelectLayerContentCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->selectActiveLayerContent();
    }
}

CommandInfo SelectLayerMaskCommand::info() const
{
    return CommandInfo { .id = "selection.selectLayerMask",
        .title = "Select Layer Mask",
        .category = "Selection",
        .description = "Load the current layer's mask into the selection; partially revealed areas "
                       "become partially selected",
        .aliases = { "select-layer-mask", "mask-to-selection" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool SelectLayerMaskCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->selectedLayerHasMask();
}

void SelectLayerMaskCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->selectActiveLayerMaskContent();
    }
}

CommandInfo DeleteSelectionContentCommand::info() const
{
    return CommandInfo { .id = "selection.deleteContent",
        .title = "Delete Content",
        .category = "Selection",
        .description = "Erase the pixels inside the active selection",
        .aliases = { "delete-selection", "erase-selection" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool DeleteSelectionContentCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->hasActiveSelection();
}

void DeleteSelectionContentCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->deleteSelectionContent();
    }
}

CommandInfo TransformSelectionCommand::info() const
{
    return CommandInfo { .id = "selection.transform",
        .title = "Transform Selection",
        .category = "Selection",
        .description = "Enter transform mode on the content under the active selection",
        .aliases = { "transform-selection" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool TransformSelectionCommand::canExecute(const CommandContext& ctx) const
{
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->hasActiveSelection() && !canvasPanel->isTransformActive();
}

void TransformSelectionCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    if (auto* canvasPanel = ctx.activeCanvasPanel()) {
        canvasPanel->enterTransformMode();
    }
}

CommandInfo WarpTransformCommand::info() const
{
    return CommandInfo { .id = "edit.warp",
        .title = "Warp",
        .category = "Edit",
        .description = "Enter Warp mode for the active selection or selected layer",
        .aliases = { "warp", "deform" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool WarpTransformCommand::canExecute(const CommandContext& ctx) const
{
    return canEnterWarpTransform(ctx);
}

void WarpTransformCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeCanvasPanel()) {
        panel->enterWarpTransformMode();
    }
}

CommandInfo Rotate90ClockwiseCommand::info() const
{
    return CommandInfo { .id = "edit.rotate90Clockwise",
        .title = "Rotate 90 Clockwise",
        .category = "Edit",
        .description = "Rotate the active selection or selected layer 90 degrees clockwise",
        .aliases = { "rotate-90-clockwise", "rotate-right" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool Rotate90ClockwiseCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteContentTransformAction(ctx);
}

void Rotate90ClockwiseCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeCanvasPanel()) {
        panel->rotateContent90Clockwise();
    }
}

CommandInfo Rotate90CounterclockwiseCommand::info() const
{
    return CommandInfo { .id = "edit.rotate90Counterclockwise",
        .title = "Rotate 90 Counterclockwise",
        .category = "Edit",
        .description = "Rotate the active selection or selected layer 90 degrees counterclockwise",
        .aliases = { "rotate-90-counterclockwise", "rotate-left" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool Rotate90CounterclockwiseCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteContentTransformAction(ctx);
}

void Rotate90CounterclockwiseCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeCanvasPanel()) {
        panel->rotateContent90Counterclockwise();
    }
}

CommandInfo Rotate180Command::info() const
{
    return CommandInfo { .id = "edit.rotate180",
        .title = "Rotate 180",
        .category = "Edit",
        .description = "Rotate the active selection or selected layer 180 degrees",
        .aliases = { "rotate-180", "turn-upside-down" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool Rotate180Command::canExecute(const CommandContext& ctx) const
{
    return canExecuteContentTransformAction(ctx);
}

void Rotate180Command::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeCanvasPanel()) {
        panel->rotateContent180();
    }
}

CommandInfo FlipHorizontalCommand::info() const
{
    return CommandInfo { .id = "edit.flipHorizontal",
        .title = "Flip Horizontal",
        .category = "Edit",
        .description = "Flip the active selection or selected layer horizontally",
        .aliases = { "flip-horizontal", "mirror-horizontal" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool FlipHorizontalCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteContentTransformAction(ctx);
}

void FlipHorizontalCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeCanvasPanel()) {
        panel->flipContentHorizontally();
    }
}

CommandInfo FlipVerticalCommand::info() const
{
    return CommandInfo { .id = "edit.flipVertical",
        .title = "Flip Vertical",
        .category = "Edit",
        .description = "Flip the active selection or selected layer vertically",
        .aliases = { "flip-vertical", "mirror-vertical" },
        .defaultShortcut = QKeySequence(),
        .icon = QIcon() };
}

bool FlipVerticalCommand::canExecute(const CommandContext& ctx) const
{
    return canExecuteContentTransformAction(ctx);
}

void FlipVerticalCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);
    if (auto* panel = ctx.activeCanvasPanel()) {
        panel->flipContentVertically();
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
    QWidget* focus = ctx.focusWidget();
    if (widgetIsTextInput(focus)) {
        return canDeleteFromFocusedTextInput(focus);
    }
    if (!ctx.activeWorkspaceTab()) {
        return false;
    }
    auto* layersPanel = ctx.activeLayersPanel();
    if (layersPanel && widgetIsWithin(focus, layersPanel)) {
        return layersPanel->canDeleteSelectedLayersOrMask();
    }
    auto* canvasPanel = ctx.activeCanvasPanel();
    return canvasPanel && canvasPanel->canDeleteSelectionContent();
}

void ContextualDeleteCommand::execute(const CommandContext& ctx, const QVariantMap& args)
{
    Q_UNUSED(args);

    QWidget* focus = ctx.focusWidget();
    if (routeDeleteToFocusedTextInput(focus)) {
        return;
    }

    // Focus decides the domain first: the layers panel owns layer/mask deletion,
    // everything else (in practice the canvas) owns pixel deletion.
    auto* layersPanel = ctx.activeLayersPanel();
    if (layersPanel && widgetIsWithin(focus, layersPanel)) {
        layersPanel->deleteSelectedLayersOrMask();
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
    registry.registerCommand(std::make_unique<CopyMergedCommand>());
    registry.registerCommand(std::make_unique<PasteCommand>());
    registry.registerCommand(std::make_unique<DeselectCommand>());
    registry.registerCommand(std::make_unique<FillSelectionCommand>());
    registry.registerCommand(std::make_unique<SelectAllCommand>());
    registry.registerCommand(std::make_unique<InvertSelectionCommand>());
    registry.registerCommand(std::make_unique<ReselectCommand>());
    registry.registerCommand(std::make_unique<SelectLayerContentCommand>());
    registry.registerCommand(std::make_unique<SelectLayerMaskCommand>());
    registry.registerCommand(std::make_unique<DeleteSelectionContentCommand>());
    registry.registerCommand(std::make_unique<TransformSelectionCommand>());
    registry.registerCommand(std::make_unique<WarpTransformCommand>());
    registry.registerCommand(std::make_unique<Rotate90ClockwiseCommand>());
    registry.registerCommand(std::make_unique<Rotate90CounterclockwiseCommand>());
    registry.registerCommand(std::make_unique<Rotate180Command>());
    registry.registerCommand(std::make_unique<FlipHorizontalCommand>());
    registry.registerCommand(std::make_unique<FlipVerticalCommand>());
    registry.registerCommand(std::make_unique<ContextualDeleteCommand>());
}

} // namespace ruwa::core::commands
