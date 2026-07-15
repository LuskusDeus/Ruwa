// SPDX-License-Identifier: MPL-2.0

// DockFloatingContainer.h
#ifndef RUWA_UI_DOCKING_CORE_DOCKFLOATINGCONTAINER_H
#define RUWA_UI_DOCKING_CORE_DOCKFLOATINGCONTAINER_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"

#include <QFrame>
#include <QPoint>
#include <QPointer>

class QVariantAnimation;

namespace ruwa::ui::docking {

class DockPanel;
class DockContainerWidget;

/**
 * @brief Floating container for dock panels inside the main window
 *
 * Unlike QDockWidget's floating which creates separate windows,
 * DockFloatingContainer stays inside the main application window
 * and can be freely dragged and resized within it.
 *
 * Features:
 * - Drag to move
 * - Resize handles on all edges/corners
 * - Shadow and border styling
 * - Stays within parent bounds
 * - Z-order management (raise on click)
 */
class DockFloatingContainer : public QFrame {
    Q_OBJECT

public:
    explicit DockFloatingContainer(DockContainerWidget* container, DockPanel* panel);
    ~DockFloatingContainer() override;

    // === Content ===

    DockPanel* panel() const { return m_panel; }
    DockContainerWidget* container() const { return m_container; }

    // === Position & Size ===

    /// Move to position (constrained to parent)
    void moveTo(const QPoint& pos);

    /// Move by delta
    void moveBy(int dx, int dy);

    /// Resize to size (constrained by min/max)
    void resizeTo(const QSize& size);

    /// Start position for drag
    void startDrag(const QPoint& globalPos);

    /// Update position during drag
    void updateDrag(const QPoint& globalPos);

    /// End drag operation
    void endDrag();

    // === Animation ===

    /**
     * @brief Animate appearance from source geometry to target
     * @param sourceGeom Starting geometry (usually panel's current geometry in parent coords)
     * @param targetPos Target position in parent coordinates (usually cursor position)
     * @param duration Animation duration in ms (0 = use default)
     */
    void animateAppearance(const QRect& sourceGeom, const QPoint& targetPos, int duration = 0);

    /**
     * @brief Update appearance animation with current cursor position
     * Call this during drag to keep container following cursor while animating
     */
    void updateAppearanceAnimation(const QPoint& cursorPos);

    /**
     * @brief Check if appearance animation is running
     */
    bool isAnimatingAppearance() const { return m_animatingAppearance; }

    /**
     * @brief Set default animation duration
     */
    void setAnimationDuration(int ms) { m_animationDuration = ms; }
    int animationDuration() const { return m_animationDuration; }

    // === Resize ===

    /// Resize handle margin (in pixels)
    int resizeMargin() const { return m_resizeMargin; }
    void setResizeMargin(int margin);

    /// Get resize edge at position
    ResizeEdge resizeEdgeAt(const QPoint& localPos) const;

    /// Check if currently being resized
    bool isResizing() const { return m_resizing; }

    // === Theme ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    /// Outer frame size that fits panel min/pref/user float size including layout margins.
    static QSize outerSizeForPanel(const DockPanel* panel);

signals:
    void moved(const QPoint& pos);
    void resized(const QSize& size);
    void dragStarted();
    void dragFinished();
    void dockRequested(const QPoint& globalPos);
    void appearanceAnimationFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onAppearanceAnimationValueChanged(const QVariant& value);
    void onAppearanceAnimationFinished();

private:
    void setupUI();
    void setupAnimation();
    void updateCursor(ResizeEdge edge);
    void constrainToParent();
    void applyAnimationFrame(double progress);
    QRect parentBounds() const;

private:
    DockContainerWidget* m_container;
    DockPanel* m_panel;

    // Resize
    int m_resizeMargin = 6;
    bool m_resizing = false;
    ResizeEdge m_resizeEdge = ResizeEdge::None;
    QPoint m_resizeStartPos;
    QRect m_resizeStartGeom;

    // Drag
    bool m_dragging = false;
    QPoint m_dragStartPos;
    QPoint m_dragStartGeom;

    // Appearance animation
    QPointer<QVariantAnimation> m_appearanceAnimation;
    bool m_animatingAppearance = false;
    int m_animationDuration = 300; // ms
    QSize m_animStartSize;
    QSize m_animTargetSize;
    QPoint m_animStartAnchor; // Starting anchor point (center-top of source)
    QPoint m_lastCursorPos; // Current cursor position in parent coords

    // Style
    int m_shadowRadius = 8;
    QColor m_shadowColor;
    QColor m_borderColor;
    /// Matches DockPanel fill so AA fringes at rounded corners do not show a mismatched plate.
    QColor m_panelSurfaceColor;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_CORE_DOCKFLOATINGCONTAINER_H
