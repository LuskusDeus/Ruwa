// SPDX-License-Identifier: MPL-2.0

// AnimatedStackedWidget.h
#ifndef RUWA_UI_WIDGETS_COMMON_ANIMATEDSTACKEDWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_ANIMATEDSTACKEDWIDGET_H

#include "shared/types/ScopedProfiler.h"

#include <QStackedWidget>
#include <QSize>
#include <QResizeEvent>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

/**
 * @brief QStackedWidget with animated transitions
 *
 * Features:
 * - Smooth slide transitions based on widget index
 * - Auto-detects direction (up/down) based on index change
 * - Supports interrupting animations for fast switching
 * - Customizable animation duration and easing
 *
 * Usage:
 *   AnimatedStackedWidget* stack = new AnimatedStackedWidget();
 *   stack->addWidget(widget1);  // index 0
 *   stack->addWidget(widget2);  // index 1
 *   stack->addWidget(widget3);  // index 2
 *
 *   stack->setCurrentIndex(1);  // Slides down (0 -> 1)
 *   stack->setCurrentIndex(0);  // Slides up (1 -> 0)
 */
class AnimatedStackedWidget : public QStackedWidget {
    Q_OBJECT

public:
    enum class AnimationDirection {
        Automatic, ///< Auto-detect based on index
        Up, ///< Slide upward (or left for horizontal)
        Down ///< Slide downward (or right for horizontal)
    };

    enum class SlideOrientation {
        Vertical, ///< Slide vertically (default)
        Horizontal ///< Slide horizontally
    };

    explicit AnimatedStackedWidget(QWidget* parent = nullptr);
    ~AnimatedStackedWidget() override;

    /// Set animation duration in milliseconds
    void setAnimationDuration(int msec) { m_duration = msec; }

    /// Get animation duration
    int animationDuration() const { return m_duration; }

    /// Set animation easing curve
    void setAnimationEasing(QEasingCurve::Type easing) { m_easingCurve = easing; }

    /// Easing used when a running animation is interrupted by another switch.
    /// Should be an "out" curve (fast start, smooth landing) so the new slide
    /// continues seamlessly from the current in-flight position without the
    /// velocity discontinuity that an "inOut" curve would introduce at t=0.
    void setInterruptEasing(QEasingCurve::Type easing) { m_interruptEasingCurve = easing; }

    /// Set slide orientation (horizontal or vertical)
    void setSlideOrientation(SlideOrientation orientation) { m_orientation = orientation; }
    SlideOrientation slideOrientation() const { return m_orientation; }
    void setPreservePageSize(bool preserve) { m_preservePageSize = preserve; }
    bool preservePageSize() const { return m_preservePageSize; }
    void setSuspendLayoutDuringAnimation(bool suspend) { m_suspendLayoutDuringAnimation = suspend; }
    bool suspendLayoutDuringAnimation() const { return m_suspendLayoutDuringAnimation; }

    /// Index of the page being shown or transitioned to (updates when a slide starts).
    int activeIndex() const;

signals:
    /// Emitted when the current widget actually changes (after animation completes)
    void currentChanged(int index);

public slots:
    /// Switch to widget at index with animation
    void setCurrentIndex(int index);

    /// Switch to widget with animation
    void setCurrentWidget(QWidget* widget);

    /// Jump to index immediately (no slide, clears any running animation)
    void setCurrentIndexWithoutAnimation(int index);

private:
    void slideToWidget(int newIndex);
    void finishAnimation();
    AnimationDirection determineDirection(int fromIndex, int toIndex) const;
    QRect pageRectForOffset(QWidget* page, int offset) const;
    void updateCurrentWidgetGeometry();
    void updateTransitionGeometry(qreal progress);
    void updateVisibleWidgetGeometriesDuringAnimation();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    int m_duration { 300 };
    QEasingCurve::Type m_easingCurve { QEasingCurve::InOutCubic };
    QEasingCurve::Type m_interruptEasingCurve { QEasingCurve::OutCubic };
    SlideOrientation m_orientation { SlideOrientation::Vertical };
    bool m_preservePageSize { false };
    bool m_suspendLayoutDuringAnimation { false };

    QVariantAnimation* m_animation { nullptr };
    int m_currentIndex { -1 };
    int m_animatingFromIndex { -1 }; // Index we're animating FROM (for interruption)
    QWidget* m_animatingWidget { nullptr }; // Widget currently being animated out
    int m_transitionToIndex { -1 };
    qreal m_outgoingStartRatio { 0.0 };
    qreal m_outgoingEndRatio { 0.0 };
    qreal m_incomingStartRatio { 0.0 };
    qreal m_incomingEndRatio { 0.0 };

#if RUWA_PROFILING
    ::ruwa::diag::FrameTimer m_frameProfiler; // measures inter-tick cadence
#endif
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_ANIMATEDSTACKEDWIDGET_H
