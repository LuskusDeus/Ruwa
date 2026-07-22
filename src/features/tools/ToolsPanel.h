// SPDX-License-Identifier: MPL-2.0

// ToolsPanel.h
#ifndef RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include "shared/resources/IconProvider.h"
#include "shared/types/ToolId.h"
#include "shared/widgets/ToolButton.h"

#include <QList>
#include <QLayout>
#include <QMap>
#include <QRect>
#include <QSet>

class QButtonGroup;
class QBoxLayout;
class QEvent;
class QObject;
class QTimer;
class QWidget;

namespace ruwa::ui::workspace {

class CanvasPanel;
class LayersPanel;
class ToolGroupPopup;

/**
 * @brief Panel containing drawing tools with adaptive flow layout
 *
 * Automatically switches between vertical and horizontal orientation
 * based on panel aspect ratio. Separators rotate accordingly.
 */
class ToolsPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit ToolsPanel(QWidget* parent = nullptr);
    ~ToolsPanel() override;

    void setActiveTool(ToolId tool);
    void setRelatedPanels(CanvasPanel* canvasPanel, LayersPanel* layersPanel);

signals:
    void toolRequested(ToolId tool);

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Orientation { Vertical, Horizontal };

    void addTool(ToolId tool);
    void addGroupTool(ToolId tool);
    void updateIcons();
    void rebuildLayout(bool animate = false);
    void positionLayout(bool animate);
    void setAnimatedGeometry(QWidget* widget, const QRect& target, bool animate);
    void advanceLayoutAnimation();
    void stopLayoutAnimation(QWidget* widget);
    void updateOrientation(bool animate = true);
    void updateGroupButtons();
    void ensureGroupPopup();
    void openToolGroupPopup(ToolId representativeTool, QWidget* anchor);
    void hideToolGroupPopup(bool immediate = true);
    ToolId resolveSelectedTool(ToolId tool) const;
    ToolId displayToolFor(ToolId tool) const;
    QString tooltipForTool(ToolId tool) const;
    ruwa::ui::core::IconProvider::StandardIcon iconForTool(ToolId tool) const;

private:
    QWidget* m_contentWidget = nullptr;
    QButtonGroup* m_buttonGroup = nullptr;

    struct ToolButtonInfo {
        ToolButton* button;
        ruwa::ui::core::IconProvider::StandardIcon iconType;
    };
    QMap<ToolId, ToolButtonInfo> m_toolsData;
    QList<QWidget*> m_separators;
    QMap<QWidget*, QRect> m_layoutTargets;
    QSet<QWidget*> m_layoutTrackedWidgets;
    QTimer* m_layoutAnimationTimer = nullptr;
    QMap<ToolId, ToolId> m_groupSelections;
    ToolGroupPopup* m_groupPopup = nullptr;
    CanvasPanel* m_canvasPanel = nullptr;
    LayersPanel* m_layersPanel = nullptr;

    ToolId m_currentTool = ToolId::Brush;
    Orientation m_orientation = Orientation::Vertical;
    bool m_contentCreated = false;
    bool m_layoutBoundsInitialized = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H
