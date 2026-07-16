// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   N A V I G A T O R   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_NAVIGATORPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_NAVIGATORPANEL_H

#include "shell/docking/widgets/DockPanel.h"

#include <QTimer>

class QWidget;

namespace ruwa::ui::workspace {
class CanvasPanel;
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

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;

private:
    CanvasPanel* m_canvasPanel = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_NAVIGATORPANEL_H
