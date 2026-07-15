// SPDX-License-Identifier: MPL-2.0

// AspectRatioLockButton.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_ASPECTRATIOLOCKBUTTON_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_ASPECTRATIOLOCKBUTTON_H

#include "shared/widgets/AssetToggleButton.h"

namespace ruwa::ui::widgets {

/**
 * @brief Icon toggle for aspect ratio lock between width and height.
 *
 * Same Link icon in both states (tint follows locked/active). Unlocked: transparent at rest,
 * rounded hover plate on hover. Locked: primary pill + border. Hover leave is debounced.
 */
class AspectRatioLockButton : public AssetToggleButton {
    Q_OBJECT

public:
    explicit AspectRatioLockButton(QWidget* parent = nullptr);
    ~AspectRatioLockButton() override = default;

    /// Whether the aspect ratio is currently locked
    bool isLocked() const { return isChecked(); }
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_ASPECTRATIOLOCKBUTTON_H
