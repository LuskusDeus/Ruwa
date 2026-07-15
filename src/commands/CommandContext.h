// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================
//   File        : CommandContext.h
//   Description : Execution context providing access to application state for commands.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_COMMANDCONTEXT_H
#define RUWA_CORE_COMMANDS_COMMANDCONTEXT_H

#include <QWidget>

namespace ruwa::core {

class TabManager;
class BaseTab;

} // namespace ruwa::core

namespace ruwa::ui::windows {
class MainWindow;
}

namespace ruwa::ui::tabs {
class WorkspaceTab;
}

namespace ruwa::ui::workspace {
class CanvasPanel;
class LayersPanel;
} // namespace ruwa::ui::workspace

namespace ruwa::core {

/**
 * @brief Provides commands with access to application state
 *
 * CommandContext is a lightweight object that gives commands
 * access to the current application state without tight coupling.
 */
class CommandContext {
public:
    CommandContext() = default;

    // === Setters (called by CommandExecutor) ===

    void setTabManager(TabManager* manager) { m_tabManager = manager; }
    void setMainWindow(ruwa::ui::windows::MainWindow* window) { m_mainWindow = window; }

    // === Accessors ===

    /// Get the tab manager
    TabManager* tabManager() const { return m_tabManager; }

    /// Get the currently active tab (may be nullptr)
    BaseTab* activeTab() const;

    /// Get the currently active workspace tab (may be nullptr)
    ruwa::ui::tabs::WorkspaceTab* activeWorkspaceTab() const;

    /// Get the active workspace canvas panel (may be nullptr)
    ruwa::ui::workspace::CanvasPanel* activeCanvasPanel() const;

    /// Get the active workspace layers panel (may be nullptr)
    ruwa::ui::workspace::LayersPanel* activeLayersPanel() const;

    /// Get the main window
    ruwa::ui::windows::MainWindow* mainWindow() const { return m_mainWindow; }

    /// Get the currently focused widget
    QWidget* focusWidget() const;

    // === Convenience Templates ===

    /// Cast active tab to specific type
    template <typename T> T* activeTabAs() const { return qobject_cast<T*>(activeTab()); }

    /// Check if active tab is of specific type
    template <typename T> bool isActiveTab() const { return activeTabAs<T>() != nullptr; }

private:
    TabManager* m_tabManager = nullptr;
    ruwa::ui::windows::MainWindow* m_mainWindow = nullptr;
};

} // namespace ruwa::core

#endif // RUWA_CORE_COMMANDS_COMMANDCONTEXT_H
