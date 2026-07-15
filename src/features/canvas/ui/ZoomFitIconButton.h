// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_ZOOMFITICONBUTTON_H
#define RUWA_UI_WIDGETS_WORKSPACE_ZOOMFITICONBUTTON_H

#include "shared/widgets/ToolButton.h"

namespace ruwa::ui::widgets {

class ZoomFitIconButton : public ruwa::ui::workspace::ToolButton {
    Q_OBJECT

public:
    explicit ZoomFitIconButton(QWidget* parent = nullptr);
    ~ZoomFitIconButton() override = default;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_ZOOMFITICONBUTTON_H
