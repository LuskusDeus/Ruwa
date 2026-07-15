// SPDX-License-Identifier: MPL-2.0

#include "WelcomeBannerButton.h"

namespace ruwa::ui::widgets {

namespace {
CapsuleButton::Variant toCapsuleVariant(WelcomeBannerButton::ButtonStyle style)
{
    return style == WelcomeBannerButton::ButtonStyle::Primary ? CapsuleButton::Variant::Primary
                                                              : CapsuleButton::Variant::Secondary;
}
} // namespace

WelcomeBannerButton::WelcomeBannerButton(const QString& text, ButtonStyle style, QWidget* parent)
    : CapsuleButton(text, toCapsuleVariant(style), parent)
{
}

} // namespace ruwa::ui::widgets
