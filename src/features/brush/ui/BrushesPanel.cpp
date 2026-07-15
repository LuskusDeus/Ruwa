// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L
// ==========================================================================

#include "features/brush/ui/BrushesPanel.h"

#include "features/brush/ui/BrushesPanelContent.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"

namespace ruwa::ui::workspace {

namespace {

int singleBrushMinimumPanelWidth()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();

    // One 108 px brush button, its 6 + 2 px flow insets, the content's
    // scaled 8 px margins, the 12 px scrollbar, and the panel's 1 px frame.
    return theme.scaled(108) + 8 + (theme.scaled(8) * 2) + 12 + 2;
}

} // namespace

BrushesPanel::BrushesPanel(QWidget* parent)
    : DockPanel(tr("Brushes"), parent)
{
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::Brushpack);
    setMinimumPanelSize(singleBrushMinimumPanelWidth(), 180);
    setPreferredPanelSize(280, 340);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
}

BrushesPanel::~BrushesPanel() = default;

void BrushesPanel::setCanvasPanel(CanvasPanel* canvasPanel)
{
    if (m_canvasPanel == canvasPanel) {
        return;
    }

    m_canvasPanel = canvasPanel;
    if (m_contentWidget) {
        m_contentWidget->setCanvasPanel(canvasPanel);
    }
}

QWidget* BrushesPanel::createContent()
{
    m_contentWidget = new BrushesPanelContent(this);
    m_contentWidget->setCanvasPanel(m_canvasPanel);
    connect(m_contentWidget, &BrushesPanelContent::stateChanged, this,
        &BrushesPanel::panelStateChanged);
    if (!m_pendingPanelState.isEmpty()) {
        m_contentWidget->restoreState(m_pendingPanelState);
    }
    return m_contentWidget;
}

void BrushesPanel::onThemeChanged()
{
    DockPanel::onThemeChanged();
    setMinimumPanelSize(singleBrushMinimumPanelWidth(), 180);
    if (m_contentWidget) {
        m_contentWidget->update();
    }
}

QJsonObject BrushesPanel::savePanelState() const
{
    if (m_contentWidget) {
        return m_contentWidget->saveState();
    }

    return m_pendingPanelState;
}

void BrushesPanel::restorePanelState(const QJsonObject& state)
{
    m_pendingPanelState = state;
    if (m_contentWidget) {
        m_contentWidget->restoreState(state);
    }
}

} // namespace ruwa::ui::workspace
