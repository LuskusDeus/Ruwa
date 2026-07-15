// SPDX-License-Identifier: MPL-2.0

// SidebarButton.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_SIDEBAR_SIDEBARBUTTON_H
#define RUWA_UI_WIDGETS_HOMEPAGE_SIDEBAR_SIDEBARBUTTON_H

#include "shared/widgets/BaseStyledWidget.h"

class QPainter;
class QRectF;

namespace ruwa::ui::widgets {

/**
 * @brief Custom button for HomePage sidebar navigation
 *
 * Uses BaseStyledWidget with "SidebarButton" style.
 * Features:
 * - Icon and text with automatic color matching
 * - Active/inactive states with smooth transitions
 * - Minimal inactive state with light hover fill
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

#endif // RUWA_UI_WIDGETS_HOMEPAGE_SIDEBAR_SIDEBARBUTTON_H
