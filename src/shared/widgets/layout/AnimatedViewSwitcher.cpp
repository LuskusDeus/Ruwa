// SPDX-License-Identifier: MPL-2.0

// AnimatedViewSwitcher.cpp
#include "AnimatedViewSwitcher.h"

#include <QResizeEvent>

namespace ruwa::ui::widgets {

AnimatedViewSwitcher::AnimatedViewSwitcher(QWidget* parent)
    : QWidget(parent)
{
}

AnimatedViewSwitcher::~AnimatedViewSwitcher()
{
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
    }
}

void AnimatedViewSwitcher::setFirstView(QWidget* widget)
{
    if (m_firstView == widget) {
        return;
    }

    if (m_firstView) {
        m_firstView->setParent(nullptr);
    }

    m_firstView = widget;

    if (m_firstView) {
        m_firstView->setParent(this);

        if (m_currentIndex == 0) {
            positionView(m_firstView, 0);
            m_firstView->show();
        } else {
            positionView(m_firstView, -width());
            m_firstView->hide();
        }
    }
}

void AnimatedViewSwitcher::setSecondView(QWidget* widget)
{
    if (m_secondView == widget) {
        return;
    }

    if (m_secondView) {
        m_secondView->setParent(nullptr);
    }

    m_secondView = widget;

    if (m_secondView) {
        m_secondView->setParent(this);

        if (m_currentIndex == 1) {
            positionView(m_secondView, 0);
            m_secondView->show();
        } else {
            positionView(m_secondView, width());
            m_secondView->hide();
        }
    }
}

QWidget* AnimatedViewSwitcher::currentView() const
{
    return (m_currentIndex == 0) ? m_firstView : m_secondView;
}

void AnimatedViewSwitcher::switchTo(int index)
{
    if (index < 0 || index > 1) {
        return;
    }

    if (index == m_currentIndex) {
        return;
    }

    // Stop any running animation
    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
    }

    QWidget* fromView = (m_currentIndex == 0) ? m_firstView : m_secondView;
    QWidget* toView = (index == 0) ? m_firstView : m_secondView;

    if (!fromView || !toView) {
        // No animation possible, just switch
        setCurrentIndex(index);
        return;
    }

    // Direction: 1 = slide left (new from right), -1 = slide right (new from left)
    int direction = (index > m_currentIndex) ? 1 : -1;

    m_currentIndex = index;
    emit currentIndexChanged(index);

    animateSwitch(fromView, toView, direction);
}

void AnimatedViewSwitcher::setCurrentIndex(int index)
{
    if (index < 0 || index > 1) {
        return;
    }

    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
    }

    m_currentIndex = index;

    // Position views without animation
    if (m_firstView) {
        if (index == 0) {
            positionView(m_firstView, 0);
            m_firstView->show();
        } else {
            positionView(m_firstView, -width());
            m_firstView->hide();
        }
    }

    if (m_secondView) {
        if (index == 1) {
            positionView(m_secondView, 0);
            m_secondView->show();
        } else {
            positionView(m_secondView, width());
            m_secondView->hide();
        }
    }

    emit currentIndexChanged(index);
    emit switchFinished(index);
}

void AnimatedViewSwitcher::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Update geometry of current view
    QWidget* current = currentView();
    if (current && !isAnimating()) {
        positionView(current, 0);
    }

    // Position hidden view off-screen
    QWidget* other = (m_currentIndex == 0) ? m_secondView : m_firstView;
    if (other && !isAnimating()) {
        int offset = (m_currentIndex == 0) ? width() : -width();
        positionView(other, offset);
    }
}

void AnimatedViewSwitcher::positionView(QWidget* view, int xOffset)
{
    if (view) {
        view->setGeometry(xOffset, 0, width(), height());
    }
}

void AnimatedViewSwitcher::animateSwitch(QWidget* from, QWidget* to, int direction)
{
    int offset = width() * direction;

    // Position "to" view off-screen
    positionView(to, offset);
    to->show();
    to->raise();

    // Create animations
    auto* animFrom = new QPropertyAnimation(from, "pos", this);
    animFrom->setDuration(m_duration);
    animFrom->setStartValue(QPoint(0, 0));
    animFrom->setEndValue(QPoint(-offset, 0)); // Slides opposite direction
    animFrom->setEasingCurve(m_easingCurve);

    auto* animTo = new QPropertyAnimation(to, "pos", this);
    animTo->setDuration(m_duration);
    animTo->setStartValue(QPoint(offset, 0));
    animTo->setEndValue(QPoint(0, 0));
    animTo->setEasingCurve(m_easingCurve);

    // Group animations
    m_animation = new QParallelAnimationGroup(this);
    m_animation->addAnimation(animFrom);
    m_animation->addAnimation(animTo);

    // Store pointers for cleanup
    QWidget* fromWidget = from;
    QWidget* toWidget = to;
    int newIndex = m_currentIndex;

    connect(m_animation, &QParallelAnimationGroup::finished, this,
        [this, fromWidget, toWidget, newIndex]() {
            // Hide old view
            if (fromWidget) {
                fromWidget->hide();
                int offset = (newIndex == 0) ? width() : -width();
                positionView(fromWidget, offset);
            }

            // Ensure new view is properly positioned
            if (toWidget) {
                positionView(toWidget, 0);
            }

            finishAnimation();
        });

    m_animation->start();
}

void AnimatedViewSwitcher::finishAnimation()
{
    if (m_animation) {
        m_animation->deleteLater();
        m_animation = nullptr;
    }

    emit switchFinished(m_currentIndex);
}

} // namespace ruwa::ui::widgets
