// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_SIDEBARBUTTON_H
#define RUWA_UI_WIDGETS_SIDEBARBUTTON_H

#include "shared/widgets/BaseStyledWidget.h"

class QPainter;
class QRectF;

namespace ruwa::ui::widgets {

/**
 * @brief Shared navigation button used by full-page sidebars.
 *
 * Uses the SidebarButton style and provides the animated active indicator used
 * by both the home page and editor tabs.
 */
class SidebarButton : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit SidebarButton(
        const QString& text, const QIcon& icon = QIcon(), QWidget* parent = nullptr);
    ~SidebarButton() override = default;

protected:
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;
    void drawCustomLayers(QPainter& painter, const QRectF& rect) override;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SIDEBARBUTTON_H
