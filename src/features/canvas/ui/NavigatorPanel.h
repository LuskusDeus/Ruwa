// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   N A V I G A T O R   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_NAVIGATORPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_NAVIGATORPANEL_H

#include "shell/docking/widgets/DockPanel.h"

#include <QTimer>

class QWidget;

namespace ruwa::ui::widgets {
class ProgressHandleSlider;
}

namespace ruwa::ui::workspace {
class CanvasPanel;
class NavigatorWidget;
class ToolButton;
}

namespace ruwa::ui::workspace {

/**
 * @brief Navigator panel — overview of the entire canvas with camera control
 *
 * Shows the full canvas scaled to fit. Displays a viewport rectangle
 * indicating the visible area. Click to center camera, drag to pan.
 */
class NavigatorPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit NavigatorPanel(QWidget* parent = nullptr);
    ~NavigatorPanel() override;

    void setCanvasPanel(CanvasPanel* panel);
    CanvasPanel* canvasPanel() const { return m_canvasPanel; }
    NavigatorWidget* navigatorWidget() const { return m_navigatorWidget; }

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    void retranslateUi() override;

private:
    void syncZoomControls();
    void syncZoomSlider(qreal zoom);
    void setZoomLimits(qreal minZoom, qreal maxZoom);
    void onZoomSliderValueChanged(int value);
    void resetZoom();

    CanvasPanel* m_canvasPanel = nullptr;
    NavigatorWidget* m_navigatorWidget = nullptr;
    ruwa::ui::widgets::ProgressHandleSlider* m_zoomSlider = nullptr;
    ToolButton* m_resetZoomButton = nullptr;
    qreal m_minZoom = 0.8;
    qreal m_maxZoom = 58.0;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_NAVIGATORPANEL_H
