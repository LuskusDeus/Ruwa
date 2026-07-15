// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerButton.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNERBUTTON_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNERBUTTON_H

#include "shared/widgets/CapsuleButton.h"

namespace ruwa::ui::widgets {

/**
 * @brief Styled capsule button for WelcomeBanner
 *
 * - Primary: filled primary, brightens on hover
 * - Secondary: outline only at rest (PresetCard-style); soft hover plate, no fill
 */
class WelcomeBannerButton : public CapsuleButton {
    Q_OBJECT

public:
    enum class ButtonStyle {
        Primary, // Filled with primary color
        Secondary // Subtle white overlay
    };

    explicit WelcomeBannerButton(const QString& text, ButtonStyle style, QWidget* parent = nullptr);
    ~WelcomeBannerButton() override = default;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNERBUTTON_H
