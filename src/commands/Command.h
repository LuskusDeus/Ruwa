// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================
//   File        : Command.h
//   Description : Base class for all executable commands in the application.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_COMMAND_H
#define RUWA_CORE_COMMANDS_COMMAND_H

#include <QString>
#include <QStringList>
#include <QKeySequence>
#include <QVariantMap>
#include <QIcon>
#include <QVector>

namespace ruwa::core {

class CommandContext;

/**
 * @brief Preset suggestion: display name (e.g. "Full HD") + args to pass (e.g. width, height)
 */
struct CommandSuggestionPreset {
    QString displayName; // "Full HD", "4K UHD"
    QVariantMap value; // {"width": 1920, "height": 1080}
};

/**
 * @brief Describes a command argument for hints and autocomplete
 */
struct CommandArgument {
    QString name; // Argument key: "width", "height" or "preset"
    QString hint; // Display hint: "Width (px)"
    QString placeholder; // Placeholder: "1920"
    QStringList suggestions; // Optional autocomplete (simple string values)
    QVector<CommandSuggestionPreset> suggestionPresets; // Presets with display names
    bool useRecentProjects = false; // When true, suggestions from RecentProjectsManager
};

/**
 * @brief Metadata about a command for display and search
 */
struct CommandInfo {
    QString id; // Unique identifier: "file.new", "tab.close"
    QString title; // Display name: "New Project"
    QString category; // Group: "File", "Tab", "Navigation", "View"
    QString description; // Tooltip/help text
    QStringList aliases; // Quick search aliases: ["fnp", "newproj"]
    QKeySequence defaultShortcut; // Default hotkey
    QIcon icon; // Optional icon for palette
    QVector<CommandArgument> arguments; // Optional arguments for command palette
    /// When non-empty: selecting without args prefills search with this alias + space
    /// instead of executing. For commands that require an argument (e.g. sbs, sbo).
    QString palettePrefillAlias;
};

/**
 * @brief Abstract base class for all commands
 *
 * Commands encapsulate actions that can be:
 * - Triggered via menu
 * - Triggered via keyboard shortcut
 * - Triggered via command palette
 * - Triggered programmatically
 */
class Command {
public:
    virtual ~Command() = default;

    // === Identification ===

    /// Get command metadata
    virtual CommandInfo info() const = 0;

    /// Convenience: get command ID
    QString id() const { return info().id; }

    // === Execution ===

    /// Check if command can be executed in current context
    virtual bool canExecute(const CommandContext& ctx) const;

    /// Execute the command with optional arguments
    virtual void execute(const CommandContext& ctx, const QVariantMap& args = {}) = 0;

    // === Optional Features ===

    /// Whether this command supports undo (default: false)
    virtual bool isUndoable() const { return false; }

    /// Whether this command should appear in command palette (default: true)
    virtual bool showInPalette() const { return true; }
};

} // namespace ruwa::core

#endif // RUWA_CORE_COMMANDS_COMMAND_H
