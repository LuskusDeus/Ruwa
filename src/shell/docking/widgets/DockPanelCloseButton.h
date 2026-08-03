// SPDX-License-Identifier: MPL-2.0

// DockPanelCloseButton.h
#ifndef RUWA_UI_DOCKING_WIDGETS_DOCKPANELCLOSEBUTTON_H
#define RUWA_UI_DOCKING_WIDGETS_DOCKPANELCLOSEBUTTON_H

#include "shared/widgets/BaseAnimatedButton.h"

namespace ruwa::ui::docking {

/**
 * @brief Compact close ("cross") control shown on the right of a panel title bar.
 *
 * Visuals follow the app chrome: the shared Close asset from IconProvider,
 * tinted textMuted → text on hover, over a subtle rounded overlay highlight.
 * Sizing is theme-scaled so it matches the other title-bar controls.
 */
class DockPanelCloseButton : public ruwa::ui::widgets::BaseAnimatedButton {
    Q_OBJECT

public:
    explicit DockPanelCloseButton(QWidget* parent = nullptr);
    ~DockPanelCloseButton() override = default;

    /// Re-read theme-scaled metrics (call from the owner's applyTheme()).
    /// @param maxExtent hard cap on the square side, so the control always fits
    ///                  a title bar whose height does not follow the UI scale.
    ///                  Pass 0 for the unclamped theme-scaled size.
    void applyTheme(int maxExtent = 0);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_WIDGETS_DOCKPANELCLOSEBUTTON_H
