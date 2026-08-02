// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_SYNTHETICMOUSEFOCUS_H
#define RUWA_UI_WIDGETS_SYNTHETICMOUSEFOCUS_H

#include <QWidget>

namespace ruwa::ui::widgets {

/**
 * Apply the focus transition that Qt normally performs before delivering a
 * real mouse press. Directly sent synthetic mouse events bypass that step.
 */
inline void applySyntheticMousePressFocus(QWidget* target)
{
    for (QWidget* candidate = target; candidate; candidate = candidate->parentWidget()) {
        if (candidate->isEnabled() && (candidate->focusPolicy() & Qt::ClickFocus)) {
            candidate->setFocus(Qt::MouseFocusReason);
            return;
        }

        if (candidate->isWindow()) {
            return;
        }
    }
}

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SYNTHETICMOUSEFOCUS_H
