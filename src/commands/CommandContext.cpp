// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================

#include "commands/CommandContext.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "shell/tab-system/WorkspaceTab.h"

#include <QApplication>

namespace ruwa::core {

BaseTab* CommandContext::activeTab() const
{
    if (m_tabManager) {
        return m_tabManager->activeTab();
    }
    return nullptr;
}

ruwa::ui::tabs::WorkspaceTab* CommandContext::activeWorkspaceTab() const
{
    return activeTabAs<ruwa::ui::tabs::WorkspaceTab>();
}

ruwa::ui::workspace::CanvasPanel* CommandContext::activeCanvasPanel() const
{
    auto* workspaceTab = activeWorkspaceTab();
    return workspaceTab ? workspaceTab->canvasPanel() : nullptr;
}

ruwa::ui::workspace::LayersPanel* CommandContext::activeLayersPanel() const
{
    auto* workspaceTab = activeWorkspaceTab();
    return workspaceTab ? workspaceTab->layersPanel() : nullptr;
}

QWidget* CommandContext::focusWidget() const
{
    return QApplication::focusWidget();
}

} // namespace ruwa::core
