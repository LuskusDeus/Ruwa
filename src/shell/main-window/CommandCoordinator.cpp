// SPDX-License-Identifier: MPL-2.0

// CommandCoordinator.cpp
#include "CommandCoordinator.h"
#include "shell/main-window/MainWindow.h"
#include "shell/tab-system/TabManager.h"
#include "commands/CommandExecutor.h"
#include "commands/CommandRegistry.h"
#include "commands/ShortcutManager.h"
#include "shared/undo/UndoRedoCommands.h"
#include "features/transform/TransformActionCommands.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/canvas/ui/CanvasPanel.h"

#include <QShortcut>
namespace ruwa::ui::windows {

CommandCoordinator::CommandCoordinator(QObject* parent)
    : QObject(parent)
{
}

void CommandCoordinator::initialize(ruwa::core::TabManager* tabManager, MainWindow* mainWindow)
{
    m_tabManager = tabManager;
    m_mainWindow = mainWindow;

    setupCommandSystem();
    setupShortcuts(mainWindow);
}

void CommandCoordinator::setupCommandSystem()
{
    auto& executor = ruwa::core::CommandExecutor::instance();
    executor.initialize(m_tabManager, m_mainWindow);

    // Resolver: active tab → WorkspaceTab → canvasPanel → canvas → undoManager
    auto undoResolver = [this]() -> aether::UndoManager* {
        if (!m_tabManager) {
            return nullptr;
        }

        auto* activeTab = m_tabManager->activeTab();
        if (!activeTab) {
            return nullptr;
        }

        auto* wsTab = dynamic_cast<ruwa::ui::tabs::WorkspaceTab*>(activeTab);
        if (!wsTab) {
            return nullptr;
        }

        auto* panel = wsTab->canvasPanel();
        if (!panel) {
            return nullptr;
        }

        auto* mgr = panel->activeUndoManagerOrNull();
        if (!mgr) {
            return nullptr;
        }
        return mgr;
    };

    auto& registry = ruwa::core::CommandRegistry::instance();
    registry.registerCommand(std::make_unique<ruwa::core::UndoActionCommand>(undoResolver));
    registry.registerCommand(std::make_unique<ruwa::core::RedoActionCommand>(undoResolver));

    // Transform command: Ctrl+T
    auto transformActivator = [this]() {
        if (!m_tabManager)
            return;
        auto* activeTab = m_tabManager->activeTab();
        if (!activeTab)
            return;
        auto* wsTab = dynamic_cast<ruwa::ui::tabs::WorkspaceTab*>(activeTab);
        if (!wsTab)
            return;
        auto* panel = wsTab->canvasPanel();
        if (!panel)
            return;

        if (panel->isTransformActive()) {
            panel->confirmTransform();
        } else {
            panel->enterTransformMode();
        }
    };
    registry.registerCommand(
        std::make_unique<ruwa::core::TransformActionCommand>(transformActivator));

    auto& shortcuts = ruwa::core::ShortcutManager::instance();
    shortcuts.setShortcutContext(m_mainWindow);
    shortcuts.registerAllShortcuts();
}

void CommandCoordinator::setupShortcuts(MainWindow* mainWindow)
{
    if (!mainWindow)
        return;
}

} // namespace ruwa::ui::windows
