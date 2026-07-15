// SPDX-License-Identifier: MPL-2.0

// ToolsPanel.h
#ifndef RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include "shared/resources/IconProvider.h"
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
    enum class Tool {
        Hand,
        Brush,
        Eraser,
        Fill,
        ClassicFill,
        Eyedropper,
        Lasso,
        LassoFill,
        SquareSelection,
        CircleSelection,
        Move,
        RotateView,
        CanvasResize,
        Camera,
        Zoom,
        Blur,
        Smudge,
        Liquify,
        Text
    };
    Q_ENUM(Tool)

    explicit ToolsPanel(QWidget* parent = nullptr);
    ~ToolsPanel() override;

    Tool currentTool() const { return m_currentTool; }
    void setCurrentTool(Tool tool);
    void setRelatedPanels(CanvasPanel* canvasPanel, LayersPanel* layersPanel);
    void activateActionTool(Tool tool);

signals:
    void toolChanged(Tool tool);
    void actionToolActivated(Tool tool);

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Orientation { Vertical, Horizontal };

    void addTool(
        Tool tool, ruwa::ui::core::IconProvider::StandardIcon iconType, const QString& tooltip);
    void addGroupTool(
        Tool tool, ruwa::ui::core::IconProvider::StandardIcon iconType, const QString& tooltip);
    void updateIcons();
    void rebuildLayout(bool animate = false);
    void positionLayout(bool animate);
    void setAnimatedGeometry(QWidget* widget, const QRect& target, bool animate);
    void advanceLayoutAnimation();
    void stopLayoutAnimation(QWidget* widget);
    void updateOrientation(bool animate = true);
    void updateGroupButtons();
    void ensureGroupPopup();
    void openToolGroupPopup(Tool representativeTool, QWidget* anchor);
    void hideToolGroupPopup(bool immediate = true);
    Tool resolveSelectedTool(Tool tool) const;
    Tool displayToolFor(Tool tool) const;
    QString tooltipForTool(Tool tool) const;
    ruwa::ui::core::IconProvider::StandardIcon iconForTool(Tool tool) const;

private:
    QWidget* m_contentWidget = nullptr;
    QButtonGroup* m_buttonGroup = nullptr;

    struct ToolButtonInfo {
        ToolButton* button;
        ruwa::ui::core::IconProvider::StandardIcon iconType;
    };
    QMap<Tool, ToolButtonInfo> m_toolsData;
    QList<QWidget*> m_separators;
    QMap<QWidget*, QRect> m_layoutTargets;
    QSet<QWidget*> m_layoutTrackedWidgets;
    QTimer* m_layoutAnimationTimer = nullptr;
    QMap<Tool, Tool> m_groupSelections;
    ToolGroupPopup* m_groupPopup = nullptr;
    CanvasPanel* m_canvasPanel = nullptr;
    LayersPanel* m_layersPanel = nullptr;

    Tool m_currentTool = Tool::Hand;
    Orientation m_orientation = Orientation::Vertical;
    bool m_contentCreated = false;
    bool m_layoutBoundsInitialized = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H
