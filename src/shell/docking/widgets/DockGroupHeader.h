// SPDX-License-Identifier: MPL-2.0

// DockGroupHeader.h
#ifndef RUWA_UI_DOCKING_WIDGETS_DOCKGROUPHEADER_H
#define RUWA_UI_DOCKING_WIDGETS_DOCKGROUPHEADER_H

#include "shell/docking/DockTypes.h"
// Full definition, not a forward declaration: QPointer<DockPanel>::data() casts
// from QObject*, which needs the complete type at every inline use below.
#include "DockPanel.h"
#include "shell/context-menu/IContextMenuProvider.h"
#include "features/theme/manager/ThemeColors.h"

#include <QWidget>
#include <QFont>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRectF>

class QVariantAnimation;
class QWheelEvent;

namespace ruwa::ui::docking {

/**
 * @brief Tab strip identifying the members of a panel group
 *
 * Sits at the top of a DockGroupHost, directly ABOVE the selected member's own
 * title bar — the group header names the group, the panel title bar keeps
 * naming the panel.
 *
 * Visual language is the app tab strip's (hover pill, tinted icon, muted text
 * for inactive tabs, fading close ×) on the panel title bar's surface.
 *
 * This is a passive view: it reports what the user did and repaints from
 * setPanels()/setCurrentPanel(). It never mutates the layout tree itself.
 */
class DockGroupHeader : public QWidget, public ruwa::ui::widgets::IContextMenuProvider {
    Q_OBJECT

public:
    explicit DockGroupHeader(QWidget* parent = nullptr);
    ~DockGroupHeader() override;

    // === Model ===

    /// Replace the tabs, in order. Keeps hover/selection where it can.
    void setPanels(const QList<DockPanel*>& panels);
    QList<DockPanel*> panels() const;

    void setCurrentPanel(DockPanel* panel);
    DockPanel* currentPanel() const { return m_currentPanel.data(); }

    /// Re-read titles/icons of the current members (language or rename change).
    void refreshContents();

    // === Metrics ===

    /// Preferred (fixed) height, theme-scaled.
    int barHeight() const { return m_height; }
    void setBarHeight(int height);

    /// Radius of the strip's TOP corners. The header is the top of the whole
    /// group cell, so it carries the rounding its members give up while
    /// grouped — pass the members' DockPanel::baseCornerRadius().
    void setCornerRadius(int radius);
    int cornerRadius() const { return m_cornerRadius; }

    // === Drop feedback ===

    /**
     * @brief Tab-order index a dragged panel would be inserted at
     *
     * -1 clears the marker. Drawn as a vertical caret between tabs.
     */
    void setDropInsertIndex(int index);
    int dropInsertIndex() const { return m_dropInsertIndex; }

    /**
     * @brief Insertion index for a position in this header, in global coords
     *
     * Also reports which existing tab the position resolves against, which is
     * what decides the slide direction of the incumbent member.
     */
    int insertIndexAt(const QPoint& globalPos, GroupInsertSide* outSide = nullptr) const;

    // === Theme ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    // === IContextMenuProvider ===

    /// The panel context menu, targeting the TAB under the cursor — the strip
    /// itself owns no panel, so an empty stretch of it offers no menu.
    ruwa::ui::widgets::ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;

signals:
    /// A tab was clicked: the host should make that panel current.
    void panelActivationRequested(DockPanel* panel);
    /**
     * @brief A tab's × was clicked and its farewell has just started
     *
     * @p successor is the member that should take the stage while the tab
     * fades — null when the closing tab was not the visible one (nothing moves)
     * or when it has no living neighbour left.
     *
     * The panel is NOT closed yet: it stays alive and visible for the whole
     * farewell, so the content transition has something to slide out.
     */
    void panelCloseStarted(DockPanel* closing, DockPanel* successor);
    /// The tab's farewell has landed. The host reports the close for real once
    /// the content transition it started has landed too.
    void panelFarewellFinished(DockPanel* panel);
    /// A tab was dragged past the threshold: the panel should leave the group.
    void panelDragStarted(DockPanel* panel, const QPoint& globalPos);
    void panelDragMoved(DockPanel* panel, const QPoint& globalPos);
    void panelDragFinished(DockPanel* panel, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct TabItem {
        QPointer<DockPanel> panel;
        QString title;
        QRectF rect;
        QRectF closeRect;
        bool closeHovered = false;
        /// The tab's × was clicked and the farewell is playing; it no longer
        /// answers to the mouse and the close is only reported when it ends.
        bool isClosing = false;
        qreal hoverProgress = 0.0;
        qreal closeRevealProgress = 0.0;
        /// Farewell: fades out while dropping by kTabPopDistance px.
        qreal opacity = 1.0;
        qreal verticalOffset = 0.0;
        /// Horizontal catch-up after a neighbour left the strip (visual x is
        /// rect.x() + slideOffsetX), interpolated from slideStartOffsetX to 0.
        qreal slideOffsetX = 0.0;
        qreal slideStartOffsetX = 0.0;
        QVariantAnimation* hoverAnim = nullptr;
        QVariantAnimation* closeRevealAnim = nullptr;
        QVariantAnimation* fadeAnim = nullptr;
    };

    void updateLayout();
    void drawTab(QPainter& painter, const TabItem& item, bool isActive) const;
    int tabIndexAt(const QPointF& pos) const;
    /// Index of the tab under the mouse cursor right now, or -1.
    int tabIndexAtCursor() const;
    bool isCloseButtonAt(int index, const QPointF& pos) const;
    void startHoverAnimation(int index, bool hovering);
    void startCloseRevealAnimation(int index, bool reveal);
    /// Play the tab's farewell; panelFarewellFinished follows when it lands.
    /// @p matchHandover stretches it to the length of the content transition
    /// the close started, so both land together.
    void startCloseAnimation(int index, bool matchHandover = false);
    /// Slide the surviving tabs from where they were drawn into their new slots.
    void runPostRemoveSlide(const QHash<DockPanel*, qreal>& visualLeftBeforeRemove);
    void destroyItemAnimations(TabItem& item);
    void updateHoverState(const QPointF& pos);
    void updateScaledSizes();
    int indexOfPanel(DockPanel* panel) const;
    /// The member that should take the stage when the tab at @p index closes:
    /// its right-hand neighbour, or its left-hand one if it was the last tab.
    DockPanel* neighbourOfTab(int index) const;

private:
    QList<TabItem> m_items;
    QPointer<DockPanel> m_currentPanel;
    int m_hoveredIndex = -1;
    int m_dropInsertIndex = -1;

    // Drag-out detection: a press on a tab arms it, crossing the threshold
    // promotes it to a panel drag (same threshold as DockPanelTitleBar).
    int m_pressedIndex = -1;
    QPoint m_pressStartGlobal;
    bool m_dragging = false;
    static constexpr int kDragThreshold = 5;

    // Wheel cycling accumulates sub-step deltas (high-resolution trackpads
    // deliver a few degrees per event; a notch is 120/8 = 15).
    int m_wheelAccumulator = 0;

    // Post-removal catch-up of the surviving tabs.
    QVariantAnimation* m_layoutSlideAnim = nullptr;

    // Metrics (theme-scaled in updateScaledSizes)
    int m_cornerRadius = 6;
    int m_height = kDefaultGroupHeaderHeight;
    int m_iconSize = 12;
    int m_iconMargin = 6;
    int m_closeSize = 12;
    int m_closeMargin = 6;
    int m_tabPadding = 6;
    int m_tabSpacing = 2;

    ruwa::ui::core::ThemeColors m_colors;
    QFont m_titleFont;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_WIDGETS_DOCKGROUPHEADER_H
