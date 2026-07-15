// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   V I E W   C O M M A N D S
// ======================================================================================
//   File        : ViewCommands.h
//   Description : Commands for view and UI management (panels, fullscreen, zoom, etc.)
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_DEFINITIONS_VIEWCOMMANDS_H
#define RUWA_CORE_COMMANDS_DEFINITIONS_VIEWCOMMANDS_H

#include "commands/Command.h"

namespace ruwa::core {
class CommandRegistry;
}

namespace ruwa::core::commands {

// ======================================================================================
//   T O G G L E   C O M M A N D   P A L E T T E
// ======================================================================================

class ToggleCommandPaletteCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;

    /// Command palette itself shouldn't appear in palette (avoid recursion)
    bool showInPalette() const override { return false; }
};

// ======================================================================================
//   Z O O M   I N
// ======================================================================================

class ZoomInCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   Z O O M   O U T
// ======================================================================================

class ZoomOutCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   Z O O M   R E S E T
// ======================================================================================

class ZoomResetCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   Z O O M   T O   F I T
// ======================================================================================

class ZoomToFitCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   R E S E T   L A Y O U T
// ======================================================================================

class ResetLayoutCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   P A N E L   T O G G L E S
// ======================================================================================

class ToggleBrushPanelCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class ToggleColorPanelCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class ToggleLayersPanelCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   C A N V A S   V I E W   F L I P   ( D I S P L A Y   O N L Y )
// ======================================================================================

class ToggleCanvasViewFlipHorizontalCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

class ToggleCanvasViewFlipVerticalCommand : public Command {
public:
    CommandInfo info() const override;
    bool canExecute(const CommandContext& ctx) const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;
};

// ======================================================================================
//   M E O W   ( H I D D E N   E A S T E R   E G G )
// ======================================================================================

class MeowCommand : public Command {
public:
    CommandInfo info() const override;
    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override;

    /// Hidden easter egg — shouldn't appear in palette
    bool showInPalette() const override { return false; }
};

// ======================================================================================
//   R E G I S T R A T I O N
// ======================================================================================

void registerViewCommands(CommandRegistry& registry);

} // namespace ruwa::core::commands

#endif // RUWA_CORE_COMMANDS_DEFINITIONS_VIEWCOMMANDS_H
