// SPDX-License-Identifier: MPL-2.0

#include "ZoomFitIconButton.h"
#include "shared/widgets/overlays/ToolTipController.h"

namespace ruwa::ui::widgets {

ZoomFitIconButton::ZoomFitIconButton(QWidget* parent)
    : ruwa::ui::workspace::ToolButton(ruwa::ui::workspace::ToolButton::Mode::Action, parent)
{
    setBaseSquareSize(28, 16);
    setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::Surface);
    setBorderVisible(true);
    setMutedNormalIcon(true);
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::Zoom);
    setToolTip(tr("Zoom to Fit"));
    ToolTipController::setShortcutCommand(this, QStringLiteral("view.zoomToFit"));
}

} // namespace ruwa::ui::widgets
