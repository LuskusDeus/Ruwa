// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O M P O S E R   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_COMPOSERPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_COMPOSERPANEL_H

#include "shell/docking/widgets/DockPanel.h"

#include <QTimer>

class QWidget;

namespace ruwa::ui::workspace {
class CanvasPanel;
}

namespace ruwa::ui::workspace {

/**
 * @brief Composer panel — overview of the entire canvas with camera control
 *
 * Shows the full canvas scaled to fit. Displays a viewport rectangle
 * indicating the visible area. Click to center camera, drag to pan.
 */
class ComposerPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit ComposerPanel(QWidget* parent = nullptr);
    ~ComposerPanel() override;

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

#endif // RUWA_UI_WORKSPACE_PANELS_COMPOSERPANEL_H
