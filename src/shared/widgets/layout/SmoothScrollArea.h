// SPDX-License-Identifier: MPL-2.0

// SmoothScrollArea.h
#ifndef RUWA_UI_WIDGETS_COMMON_SMOOTHSCROLLAREA_H
#define RUWA_UI_WIDGETS_COMMON_SMOOTHSCROLLAREA_H

#include <QWidget>
#include <QEasingCurve>
#include <QPoint>
#include <QPointer>
#include <QPropertyAnimation>
#include <QElapsedTimer>

class QVBoxLayout;
class QTimer;

namespace ruwa::ui::widgets {

class SmoothScrollBar;

/**
 * @brief Smooth scrolling area with kinetic/inertial scrolling
 *
 * Features:
 * - Smooth animated scrolling
 * - Kinetic/inertial wheel scrolling
 * - Custom animated scrollbar
 * - Auto-hide scrollbar
 * - Drop-in replacement for QScrollArea
 */
class SmoothScrollArea : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int scrollValue READ scrollValue WRITE setScrollValue)
    Q_PROPERTY(
        qreal scrollBarReserveExtent READ scrollBarReserveExtent WRITE setScrollBarReserveExtent)

public:
    explicit SmoothScrollArea(QWidget* parent = nullptr);
    ~SmoothScrollArea() override;

    /// Set the content widget
    void setWidget(QWidget* widget);
    QWidget* widget() const { return m_contentWidget; }

    /// Select the scrolling axis. Vertical remains the default for existing users.
    /// Horizontal mode uses the same smooth wheel and stylus-swipe machinery but
    /// intentionally has no visible scrollbar.
    void setOrientation(Qt::Orientation orientation);
    Qt::Orientation orientation() const { return m_orientation; }

    /// Viewport widget (the visible area where content is clipped)
    QWidget* viewport() const { return m_viewport; }

    /// Scroll to position with animation
    void scrollTo(int value, bool animated = true);

    /// Scroll to position with a caller-defined animation.
    void scrollTo(int value, int durationMs, QEasingCurve::Type easingCurve);

    /// Enable or disable user-initiated scrolling without disabling child controls.
    /// Programmatic scrollTo() calls remain available.
    void setUserScrollingEnabled(bool enabled);
    bool isUserScrollingEnabled() const { return m_userScrollingEnabled; }

    /// Recompute content size and scroll range (call after bulk changes inside the content widget).
    void refreshScrollGeometry();

    /// Finish the animated scrollbar reservation at the geometry implied by
    /// the current content. Intended for rendering a stable UI snapshot.
    void finishLayoutTransitions();

    /// Get current scroll position
    int scrollValue() const { return m_currentScrollValue; }
    void setScrollValue(int value);

    /// Animated width (px) currently reserved for the scrollbar column. Drives the
    /// smooth "slide out / push content" transition; not meant for external callers.
    qreal scrollBarReserveExtent() const { return m_scrollBarReserveExtent; }
    void setScrollBarReserveExtent(qreal extent);

    /// Scroll bar visibility
    void setVerticalScrollBarPolicy(Qt::ScrollBarPolicy policy);
    Qt::ScrollBarPolicy verticalScrollBarPolicy() const { return m_scrollBarPolicy; }

    /// Margin between viewport and scrollbar (default 0)
    void setScrollBarMargin(int pixels);
    int scrollBarMargin() const { return m_scrollBarMargin; }

    /// When true, always reserve space for scrollbar column (never overlay content).
    /// Fixes frameless window transparency on Windows 10. Default: false.
    void setScrollBarAlwaysReserved(bool reserved);
    bool scrollBarAlwaysReserved() const { return m_scrollBarAlwaysReserved; }

    /// When false, content keeps its own width (for clipping effect when viewport is narrower).
    /// Default: true.
    void setContentWidthFixedToViewport(bool fixed);
    bool contentWidthFixedToViewport() const { return m_contentWidthFixedToViewport; }

    /// When false, no palette().window() fill (use inside card-style parents e.g.
    /// PresetMenuListWidget popup). Default: true (opaque chrome avoids frameless stacking
    /// glitches).
    void setFillBackground(bool fill);
    bool fillBackground() const { return m_fillBackground; }

    /// Scrollbar track/handle chrome only — independent of \ref setFillBackground.
    /// Use e.g. on first-run setup where the area is transparent but the bar should not paint
    /// window color.
    void setScrollBarTransparentTrack(bool transparent);
    bool scrollBarTransparentTrack() const { return m_scrollBarTransparentTrack; }

    /// Tuning of the continuous damping engine. \a lambda is the exponential
    /// decay rate in 1/s: the position covers 1-e^(-lambda*dt) of the remaining
    /// distance each frame, so higher is snappier and lower is more floaty.
    /// Defaults suit panel-sized content; override per area if needed.
    void setWheelDamping(qreal lambda);
    qreal wheelDamping() const { return m_wheelLambda; }
    void setFlingDamping(qreal lambda);
    qreal flingDamping() const { return m_flingLambda; }

    /// Stylus swipe scrolling API used by the global tablet filter.
    void beginStylusSwipe(const QPoint& globalPos);
    void updateStylusSwipe(const QPoint& globalPos);
    void endStylusSwipe(const QPoint& globalPos);
    void cancelStylusSwipe();
    bool isStylusSwipeActive() const { return m_stylusSwipeActive; }

signals:
    void scrolled(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void refreshContentLayout();
    void updateScrollRange();
    void onScrollBarValueChanged(int value);
    void onStepScrollRequested(int delta);
    void flushHoverStates();

private:
    void installContentEventFilters(QObject* object);
    void removeContentEventFilters(QObject* object);
    void scheduleContentLayoutRefresh();
    void updateGeometry();
    void updateScrollBarVisibility();
    void updateHoverStates();
    void syncContentPosition(int previousScrollValue, bool updateHoverImmediately);
    void scheduleHoverStateUpdate();

    // --- Continuous damping engine -------------------------------------------------
    // One frame-driven loop that eases the sub-pixel position toward an
    // accumulating target instead of restarting a fixed-duration tween per
    // input event. Because the step is exp(-lambda*dt), the motion is
    // frame-rate independent and velocity stays continuous when new input
    // lands mid-flight.
    void startDamping(qreal lambda);
    void stopDamping();
    void tickDamping();
    void applyScrollPosition(qreal position);
    void syncDampingStateToCurrentValue();
    qreal dampingBase() const;
    qreal takeWheelBoost(int direction, bool highResolutionDelta);
    int damperIntervalMs() const;

private:
    QWidget* m_viewport { nullptr };
    QWidget* m_contentWidget { nullptr };
    SmoothScrollBar* m_verticalScrollBar { nullptr };

    int m_currentScrollValue { 0 };
    int m_targetScrollValue { 0 };
    int m_maxScroll { 0 };

    Qt::ScrollBarPolicy m_scrollBarPolicy { Qt::ScrollBarAsNeeded };
    int m_scrollBarMargin { 0 };
    bool m_scrollBarReserved { false };
    bool m_scrollBarAlwaysReserved { false };
    bool m_contentWidthFixedToViewport { true };
    bool m_fillBackground { true };
    bool m_scrollBarTransparentTrack { false };
    bool m_refreshingLayout { false };
    bool m_finishingLayoutTransitions { false };
    bool m_userScrollingEnabled { true };
    Qt::Orientation m_orientation { Qt::Vertical };

    QPropertyAnimation* m_scrollAnimation { nullptr };
    QPropertyAnimation* m_reserveAnimation { nullptr };
    qreal m_scrollBarReserveExtent { 0.0 };
    bool m_stylusSwipeActive { false };
    bool m_stylusSwipeAnchorPending { false };
    QPoint m_stylusSwipeStartGlobalPos;
    QPoint m_stylusSwipeLastGlobalPos;
    qreal m_stylusSwipeStartPosition { 0.0 };
    qreal m_stylusSwipeVelocity { 0.0 };
    QElapsedTimer m_stylusSwipeTimer;
    qint64 m_stylusSwipeLastSampleMs { 0 };

    // Damping engine state.
    QTimer* m_damperTimer { nullptr };
    QElapsedTimer m_damperClock;
    qint64 m_damperLastTickNs { 0 };
    qreal m_scrollPosition { 0.0 };
    qreal m_scrollTarget { 0.0 };
    qreal m_damperLambda { 12.0 };
    qreal m_wheelLambda { 12.0 };
    qreal m_flingLambda { 5.0 };
    bool m_applyingDamperTick { false };

    // Wheel acceleration (fast repeated notches in one direction travel further).
    QElapsedTimer m_wheelClock;
    qint64 m_lastWheelMs { -100000 };
    int m_lastWheelDirection { 0 };
    qreal m_wheelBoost { 1.0 };
    QPointer<QWidget> m_hoveredWidget;
    QTimer* m_layoutRefreshTimer { nullptr };
    QTimer* m_hoverUpdateTimer { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_SMOOTHSCROLLAREA_H
