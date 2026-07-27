// SPDX-License-Identifier: MPL-2.0

// DockFloatingContainer.h
#ifndef RUWA_UI_DOCKING_CORE_DOCKFLOATINGCONTAINER_H
#define RUWA_UI_DOCKING_CORE_DOCKFLOATINGCONTAINER_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"

#include <QFrame>
#include <QPixmap>
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

    // === Freeze ===

    /**
     * @brief Paint from a cached snapshot instead of the live widget tree.
     *
     * Qt marks the whole rect of a QOpenGLWidget dirty on every one of its frames
     * (QWidgetRepaintManager::paintAndFlush: `dirty += rect; toClean += rect;`), so a
     * floating panel overlapping the canvas is fully repainted at canvas frame rate.
     * While frozen the frame costs one pixmap blit instead of a whole widget tree.
     * Refused while the panel is being dragged, resized, animated, or holds focus.
     */
    void setFrozen(bool frozen);
    bool isFrozen() const { return m_frozen; }

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
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
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
    void setupShadow();
    void updateCursor(ResizeEdge edge);
    void constrainToParent();
    void applyAnimationFrame(double progress);
    QRect parentBounds() const;

    /// Keep the cached shadow directly behind this frame (geometry + z-order).
    void syncShadowGeometry();
    void syncShadowStacking();
    void refreshShadowAppearance();
    qreal shadowCornerRadius() const;

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

    // Freeze (see setFrozen)
    bool m_frozen = false;
    QPixmap m_frozenSnapshot;
    QPoint m_parkedPanelPos;

    // Style
    int m_shadowRadius = 8;
    /// Sibling layer that paints the pre-blurred drop shadow behind this frame.
    /// A live QGraphicsDropShadowEffect cannot be used here: Qt invalidates a
    /// graphics effect whenever ANY descendant repaints, so one dirty pixel inside
    /// a floating panel re-rendered the whole panel and re-blurred it on the GUI
    /// thread — which starved brush strokes while panels were floating.
    QPointer<QWidget> m_shadowLayer;
    QColor m_shadowColor;
    QColor m_borderColor;
    /// Matches DockPanel fill so AA fringes at rounded corners do not show a mismatched plate.
    QColor m_panelSurfaceColor;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_CORE_DOCKFLOATINGCONTAINER_H
