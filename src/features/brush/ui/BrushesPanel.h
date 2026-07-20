// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_BRUSHESPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_BRUSHESPANEL_H

#include "shell/docking/widgets/DockPanel.h"

#include <QHash>
#include <QStringList>

class QWidget;
class QHBoxLayout;

namespace ruwa::ui::widgets {
class BaseAnimatedButton;
class SmoothScrollArea;
}

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
    void setupFilterBar();
    void rebuildFilterButtons(const QStringList& packIds, const QStringList& packNames);
    void activateFilter(const QString& filterId);
    void updateFilterSelection();
    void revealActiveFilter();

    CanvasPanel* m_canvasPanel = nullptr;
    BrushesPanelContent* m_contentWidget = nullptr;
    widgets::SmoothScrollArea* m_filterScrollArea = nullptr;
    QWidget* m_filterContent = nullptr;
    QHBoxLayout* m_filterLayout = nullptr;
    QHash<QString, widgets::BaseAnimatedButton*> m_filterButtons;
    QStringList m_packFilterIds;
    QStringList m_packFilterNames;
    QString m_activeFilterId;
    bool m_filterBarInitializing = false;
    QJsonObject m_pendingPanelState;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_BRUSHESPANEL_H
