// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   N A V I G A T O R   P A N E L
// ==========================================================================

#include "NavigatorPanel.h"
#include "NavigatorWidget.h"
#include "CanvasPanel.h"
#include "shared/resources/IconProvider.h"

#include <QVBoxLayout>

namespace ruwa::ui::workspace {

NavigatorPanel::NavigatorPanel(QWidget* parent)
    : DockPanel(tr("Navigator"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Navigator"));
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::NavigatorPanel);
    setMinimumPanelSize(150, 120);
    setPreferredPanelSize(220, 180);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
}

NavigatorPanel::~NavigatorPanel() = default;

void NavigatorPanel::setCanvasPanel(CanvasPanel* panel)
{
    if (m_canvasPanel == panel)
        return;
    m_canvasPanel = panel;
    if (contentWidget()) {
        if (auto* w = qobject_cast<NavigatorWidget*>(contentWidget())) {
            w->setCanvasPanel(panel);
        }
    }
}

QWidget* NavigatorPanel::createContent()
{
    auto* widget = new NavigatorWidget(this);
    widget->setCanvasPanel(m_canvasPanel);
    return widget;
}

void NavigatorPanel::onThemeChanged()
{
    DockPanel::onThemeChanged();
}

} // namespace ruwa::ui::workspace
