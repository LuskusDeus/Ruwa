// SPDX-License-Identifier: MPL-2.0

// ChoiceButton.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_CHOICEBUTTON_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_CHOICEBUTTON_H

#include "shared/widgets/BaseStyledWidget.h"

namespace ruwa::ui::widgets {

/**
 * @brief Animated button for SettingsChoice
 *
 * Uses BaseStyledWidget with "ChoiceButton" style.
 * Features:
 * - Active/inactive states with smooth transitions
 * - Gradient borders and hover effects
 * - Primary color when selected
 */
class ChoiceButton : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit ChoiceButton(const QString& text, QWidget* parent = nullptr);
    ~ChoiceButton() override = default;

    /// Override setChecked to trigger active state
    void setChecked(bool checked);
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_CHOICEBUTTON_H
