// SPDX-License-Identifier: MPL-2.0

// DockPanelTitleBar.h
#ifndef RUWA_UI_DOCKING_WIDGETS_DOCKPANELTITLEBAR_H
#define RUWA_UI_DOCKING_WIDGETS_DOCKPANELTITLEBAR_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"
#include "shell/context-menu/IContextMenuProvider.h"

#include <QWidget>
#include <QPoint>
#include <QFont>

class QShowEvent;
class QVariantAnimation;

namespace ruwa::ui::docking {

class DockPanel;

/**
 * @brief Minimal title bar widget for DockPanel
 *
 * Features:
 * - Panel title with icon placeholder
 * - Drag support for moving/floating panels
 * - Theme-aware styling
 * - Compact height (19px)
 */
class DockPanelTitleBar : public QWidget, public ruwa::ui::widgets::IContextMenuProvider {
    Q_OBJECT

public:
    explicit DockPanelTitleBar(DockPanel* panel);
    ~DockPanelTitleBar() override;
    DockPanel* panel() const { return m_panel; }

    // === Configuration ===

    /// Set fixed height (default: 19)
    void setBarHeight(int height);
    int barHeight() const { return m_height; }

    /// Attach a small interactive widget to the left side of the title bar.
    /// Ownership is transferred to the title bar. Pass nullptr to remove it.
    void setLeadingWidget(QWidget* widget);
    QWidget* leadingWidget() const { return m_leadingWidget; }

    /// Attach a small interactive widget to the right side of the title bar.
    /// Ownership is transferred to the title bar. Pass nullptr to remove it.
    void setTrailingWidget(QWidget* widget);
    QWidget* trailingWidget() const { return m_trailingWidget; }

    /// Keep leading/trailing controls visible and stationary while the title
    /// and icon crossfade into the floating drag handle.
    void setInteractiveWidgetsVisibleWhenFloating(bool visible);
    bool interactiveWidgetsVisibleWhenFloating() const
    {
        return m_interactiveWidgetsVisibleWhenFloating;
    }

    /// When true (default), a 1px border is drawn at the bottom of the title bar.
    /// Set to false when a subtitle widget is attached so that the border moves
    /// below the subtitle instead.
    void setDrawBottomBorder(bool draw);
    bool drawBottomBorder() const { return m_drawBottomBorder; }

    // === Updates (called by DockPanel) ===

    void updateTitle();
    void updateIcon();
    void updateButtons(); // Repaints + syncs floating chrome
    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    /// Animate between docked title row and floating layout (drag strip + title row).
    void syncFloatingLayout(bool floating);

    qreal floatingLayoutProgress() const { return m_floatingLayoutProgress; }
    void setFloatingLayoutProgress(qreal progress);

    // IContextMenuProvider
    ruwa::ui::widgets::ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;

signals:
    void dragStarted(const QPoint& globalPos);
    void dragging(const QPoint& globalPos);
    void dragFinished(const QPoint& globalPos);
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    void setupFloatingLayoutAnimation();
    void updateInteractiveWidgetGeometries();
    QWidget* replaceInteractiveWidget(QWidget* currentWidget, QWidget* newWidget, QWidget* host);
    QRect titleContentRect() const;
    void notifyPanelLayoutGeometry();

private:
    DockPanel* m_panel;

    // Configuration
    int m_height = 19; // 1.5x smaller than original 28

    // Drag state
    bool m_dragging = false;
    QPoint m_dragStartPos;
    static constexpr int DragThreshold = 5;

    // Bottom border
    bool m_drawBottomBorder = true;

    // Theme colors
    QColor m_iconPlaceholderColor;
    QColor m_textColor;
    QColor m_backgroundColor;
    QColor m_borderColor;
    QFont m_titleFont;

    QWidget* m_leadingHost = nullptr;
    QWidget* m_trailingHost = nullptr;
    QWidget* m_leadingWidget = nullptr;
    QWidget* m_trailingWidget = nullptr;
    bool m_interactiveWidgetsVisibleWhenFloating = false;

    // Floating: max slide distance (theme-scaled); title shifts inside fixed bar height
    QVariantAnimation* m_floatingLayoutAnim = nullptr;
    qreal m_floatingLayoutProgress = 0.0;
    int m_scaledSlideExtra = 10;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_WIDGETS_DOCKPANELTITLEBAR_H
