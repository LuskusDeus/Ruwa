// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   E D I T   C O M M A N D S
// ======================================================================================
//   File        : EditCommands.h
//   Description : Commands for edit and selection operations.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_EDITCOMMANDS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_EDITCOMMANDS_H

#include "commands/Command.h"

namespace ruwa::core {
class CommandRegistry;
}

namespace ruwa::core::commands {

class CopyCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class CutCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class PasteCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class DeselectCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class FillSelectionCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

/// Select the whole document. Routes to the focused text field when there is
/// one, so Ctrl+A keeps meaning "select all text" while typing.
class SelectAllCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class InvertSelectionCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

/// Bring back the selection that was last deselected.
class ReselectCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

/// Replace the selection with the selected layer's content silhouette — the
/// same thing Ctrl+click on a layer thumbnail does.
class SelectLayerContentCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

/// Replace the selection with the selected layer's mask coverage (gray mask
/// areas become partially selected).
class SelectLayerMaskCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

/// Erase the pixels under the active selection. Unlike edit.delete this is not
/// focus-sensitive: it always means the selection, never the layer.
class DeleteSelectionContentCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class TransformSelectionCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class FlipSelectionHorizontalCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class FlipSelectionVerticalCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

/**
 * @brief Context-sensitive Delete: what it removes depends on where focus is.
 *
 * Layers panel focused → the selected layer, or just its mask when the mask
 * thumbnail is the active paint target. Anywhere else (normally the canvas) →
 * the pixels under an active selection, and nothing at all without one.
 */
class ContextualDeleteCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

void registerEditCommands(CommandRegistry& registry);

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_EDITCOMMANDS_H
