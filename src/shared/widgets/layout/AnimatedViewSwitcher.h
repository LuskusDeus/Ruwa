// SPDX-License-Identifier: MPL-2.0

// AnimatedViewSwitcher.h
#ifndef RUWA_UI_WIDGETS_COMMON_ANIMATEDVIEWSWITCHER_H
#define RUWA_UI_WIDGETS_COMMON_ANIMATEDVIEWSWITCHER_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>

namespace ruwa::ui::widgets {

/**
 * @brief Widget for animated switching between two views (e.g., List and Grid)
 *
 * Inspired by AnimatedTabWidget, provides smooth slide transitions
 * when switching between different view modes.
 *
 * Usage:
 *   AnimatedViewSwitcher* switcher = new AnimatedViewSwitcher(this);
 *   switcher->setFirstView(listWidget);
 *   switcher->setSecondView(gridWidget);
 *   switcher->switchTo(1);  // Animate to grid view
 */
class AnimatedViewSwitcher : public QWidget {
    Q_OBJECT

public:
    explicit AnimatedViewSwitcher(QWidget* parent = nullptr);
    ~AnimatedViewSwitcher() override;

    /// Set the first view (index 0, typically List)
    void setFirstView(QWidget* widget);
    QWidget* firstView() const { return m_firstView; }

    /// Set the second view (index 1, typically Grid)
    void setSecondView(QWidget* widget);
    QWidget* secondView() const { return m_secondView; }

    /// Get current view index (0 or 1)
    int currentIndex() const { return m_currentIndex; }

    /// Get current view widget
    QWidget* currentView() const;

    /// Switch to view by index with animation
    void switchTo(int index);

    /// Switch to view without animation
    void setCurrentIndex(int index);

    /// Animation settings
    void setAnimationDuration(int msec) { m_duration = msec; }
    int animationDuration() const { return m_duration; }
    void setAnimationEasing(QEasingCurve::Type easing) { m_easingCurve = easing; }

    /// Check if animation is running
    bool isAnimating() const
    {
        return m_animation && m_animation->state() == QAbstractAnimation::Running;
    }

signals:
    /// Emitted when switch animation completes
    void switchFinished(int newIndex);

    /// Emitted when current index changes (before animation starts)
    void currentIndexChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void positionView(QWidget* view, int xOffset);
    void animateSwitch(QWidget* from, QWidget* to, int direction);
    void finishAnimation();

private:
    QWidget* m_firstView { nullptr };
    QWidget* m_secondView { nullptr };
    int m_currentIndex { 0 };

    QParallelAnimationGroup* m_animation { nullptr };

    int m_duration { 300 };
    QEasingCurve::Type m_easingCurve { QEasingCurve::OutCubic };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_ANIMATEDVIEWSWITCHER_H
