// SPDX-License-Identifier: MPL-2.0

// AnimatedButton.h
#ifndef RUWA_UI_WIDGETS_COMMON_ANIMATEDBUTTON_H
#define RUWA_UI_WIDGETS_COMMON_ANIMATEDBUTTON_H

#include "BaseAnimatedButton.h"

namespace ruwa::ui::widgets {

/**
 * @brief Reusable primary-style animated button
 *
 * Extends BaseAnimatedButton with primary color styling.
 * Used in settings, dialogs, etc. where a compact action button is needed.
 */
class AnimatedButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit AnimatedButton(const QString& text, QWidget* parent = nullptr);
    ~AnimatedButton() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateScaledSizes();

private slots:
    void onThemeChanged();
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_ANIMATEDBUTTON_H
