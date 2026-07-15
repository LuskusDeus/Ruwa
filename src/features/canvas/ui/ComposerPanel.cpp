// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O M P O S E R   P A N E L
// ==========================================================================

#include "ComposerPanel.h"
#include "ComposerWidget.h"
#include "CanvasPanel.h"
#include "shared/resources/IconProvider.h"

#include <QVBoxLayout>

namespace ruwa::ui::workspace {

ComposerPanel::ComposerPanel(QWidget* parent)
    : DockPanel(tr("Composer"), parent)
{
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::ComposerPanel);
    setMinimumPanelSize(150, 120);
    setPreferredPanelSize(220, 180);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
}

ComposerPanel::~ComposerPanel() = default;

void ComposerPanel::setCanvasPanel(CanvasPanel* panel)
{
    if (m_canvasPanel == panel)
        return;
    m_canvasPanel = panel;
    if (contentWidget()) {
        if (auto* w = qobject_cast<ComposerWidget*>(contentWidget())) {
            w->setCanvasPanel(panel);
        }
    }
}

QWidget* ComposerPanel::createContent()
{
    auto* widget = new ComposerWidget(this);
    widget->setCanvasPanel(m_canvasPanel);
    return widget;
}

void ComposerPanel::onThemeChanged()
{
    DockPanel::onThemeChanged();
}

} // namespace ruwa::ui::workspace
