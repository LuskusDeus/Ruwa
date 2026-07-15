// SPDX-License-Identifier: MPL-2.0

// ChoiceButton.cpp
#include "features/settings/ChoiceButton.h"

namespace ruwa::ui::widgets {

ChoiceButton::ChoiceButton(const QString& text, QWidget* parent)
    : BaseStyledWidget("ChoiceButton", parent)
{
    setText(text);
    setCheckable(true);
}

void ChoiceButton::setChecked(bool checked)
{
    QPushButton::setChecked(checked);
    setActive(checked);
}

} // namespace ruwa::ui::widgets
