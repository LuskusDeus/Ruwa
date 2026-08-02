// SPDX-License-Identifier: MPL-2.0

// ToolsPanel.h
#ifndef RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include "shared/resources/IconProvider.h"
#include "shared/types/ToolId.h"
#include "shared/widgets/ToolButton.h"

#include <QList>
#include <QMap>
#include <QPoint>
#include <QPointer>
#include <QStringList>

class QButtonGroup;
class QEvent;
class QObject;
class QVariantAnimation;
class QWidget;

namespace ruwa::ui::workspace {

class CanvasPanel;
class LayersPanel;
class ToolGroupPopup;

} // namespace ruwa::ui::workspace

namespace ruwa::ui::widgets {
class AnimatedFlowWidget;
class DragGhostWidget;
}

namespace ruwa::ui::workspace {

/**
 * @brief Panel containing drawing tools in one reorderable animated flow
 */
class ToolsPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit ToolsPanel(QWidget* parent = nullptr);
    ~ToolsPanel() override;

    void setActiveTool(ToolId tool);
    void setRelatedPanels(CanvasPanel* canvasPanel, LayersPanel* layersPanel);
    void preparePresentationSnapshot();

    static QList<ToolId> configurableTools();
    static QString toolDisplayName(ToolId tool);
    static ruwa::ui::core::IconProvider::StandardIcon toolIconType(ToolId tool);
    bool isToolVisible(ToolId tool) const;
    void setToolVisible(ToolId tool, bool visible);

signals:
    void toolRequested(ToolId tool);
    void panelStateChanged();

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    QJsonObject savePanelState() const override;
    void restorePanelState(const QJsonObject& state) override;

private:
    void addTool(ToolId tool);
    void addGroupTool(ToolId tool);
    void updateIcons();
    void applyToolOrder(bool animate);
    void animateToolButtonVisibility(ToolId displayTool, bool visible);
    void cancelToolButtonVisibilityAnimation(ToolId displayTool);
    bool isDisplayToolVisible(ToolId displayTool) const;
    QList<ToolId> visibleGroupMembers(ToolId representative) const;
    void normalizeGroupSelections();
    void startToolDrag(ToolId tool, ToolButton* button, const QPoint& globalPos);
    void updateToolDrag(const QPoint& globalPos);
    void finishToolDrag(bool accepted, const QPoint& globalPos);
    QPoint ghostTargetPosition(const QPoint& globalPos) const;
    int toolInsertIndexAt(const QPoint& contentPos) const;
    void moveDraggedToolTo(int insertIndex);
    void cancelToolDragCandidate();
    void updateGroupButtons();
    void ensureGroupPopup();
    void openToolGroupPopup(ToolId representativeTool, QWidget* anchor);
    void hideToolGroupPopup(bool immediate = true);
    ToolId resolveSelectedTool(ToolId tool) const;
    ToolId displayToolFor(ToolId tool) const;

private:
    ruwa::ui::widgets::AnimatedFlowWidget* m_contentWidget = nullptr;
    QButtonGroup* m_buttonGroup = nullptr;

    struct ToolButtonInfo {
        ToolButton* button;
        ruwa::ui::core::IconProvider::StandardIcon iconType;
    };
    QMap<ToolId, ToolButtonInfo> m_toolsData;
    QList<ToolId> m_toolOrder;
    QList<ToolId> m_appliedToolOrder;
    QList<ToolId> m_dragStartOrder;
    QList<ToolId> m_hiddenTools;
    QMap<ToolId, QPointer<QVariantAnimation>> m_visibilityAnimations;
    QMap<ToolId, ToolId> m_groupSelections;
    ToolGroupPopup* m_groupPopup = nullptr;
    CanvasPanel* m_canvasPanel = nullptr;
    LayersPanel* m_layersPanel = nullptr;

    ToolId m_currentTool = ToolId::Brush;
    ToolButton* m_dragCandidateButton = nullptr;
    ToolButton* m_draggedButton = nullptr;
    QPointer<ruwa::ui::widgets::DragGhostWidget> m_dragGhost;
    QPoint m_dragPressPosition;
    QPoint m_dragOffset;
    ToolId m_draggedTool = ToolId::Hand;
    bool m_dragActive = false;
    bool m_dragSettling = false;
    bool m_dragInsideContent = false;
    bool m_dragCursorOverride = false;
    bool m_contentCreated = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_TOOLSPANEL_H
