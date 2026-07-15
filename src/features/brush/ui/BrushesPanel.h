// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_BRUSHESPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_BRUSHESPANEL_H

#include "shell/docking/widgets/DockPanel.h"

class QWidget;

namespace ruwa::ui::workspace {

class BrushesPanelContent;
class CanvasPanel;

class BrushesPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit BrushesPanel(QWidget* parent = nullptr);
    ~BrushesPanel() override;
    void setCanvasPanel(CanvasPanel* canvasPanel);

signals:
    void panelStateChanged();

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    QJsonObject savePanelState() const override;
    void restorePanelState(const QJsonObject& state) override;

private:
    CanvasPanel* m_canvasPanel = nullptr;
    BrushesPanelContent* m_contentWidget = nullptr;
    QJsonObject m_pendingPanelState;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_BRUSHESPANEL_H
