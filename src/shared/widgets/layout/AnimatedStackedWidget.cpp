// SPDX-License-Identifier: MPL-2.0

// AnimatedStackedWidget.cpp
#include "AnimatedStackedWidget.h"

#include "shared/types/ScopedProfiler.h"

#include <QLayout>

namespace ruwa::ui::widgets {

namespace {

QSize naturalPageSize(QWidget* page)
{
    RUWA_PROFILE_ZONE("naturalPageSize (layout activate + sizeHint)");
    if (!page) {
        return QSize();
    }

    if (page->layout()) {
        page->layout()->activate();
    }

    QSize hint = page->sizeHint().expandedTo(page->minimumSizeHint());
    if (!hint.isValid()) {
        hint = page->size();
    }
    return hint;
}

} // namespace

AnimatedStackedWidget::AnimatedStackedWidget(QWidget* parent)
    : QStackedWidget(parent)
{
}

AnimatedStackedWidget::~AnimatedStackedWidget()
{
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
    }
}

int AnimatedStackedWidget::activeIndex() const
{
    return m_currentIndex >= 0 ? m_currentIndex : QStackedWidget::currentIndex();
}

void AnimatedStackedWidget::setCurrentIndex(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }

    // First time initialization - just set without animation
    if (m_currentIndex == -1) {
        m_currentIndex = index;
        QStackedWidget::setCurrentIndex(index);
        updateCurrentWidgetGeometry();
        emit currentChanged(index);
        return;
    }

    // Same index - nothing to do
    if (index == m_currentIndex) {
        return;
    }

    slideToWidget(index);
}

void AnimatedStackedWidget::setCurrentWidget(QWidget* widget)
{
    int index = indexOf(widget);
    if (index != -1) {
        setCurrentIndex(index);
    }
}

void AnimatedStackedWidget::setCurrentIndexWithoutAnimation(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }

    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
    }

    m_animatingFromIndex = -1;
    m_animatingWidget = nullptr;
    m_transitionToIndex = -1;
    m_outgoingStartRatio = 0.0;
    m_outgoingEndRatio = 0.0;
    m_incomingStartRatio = 0.0;
    m_incomingEndRatio = 0.0;
    if (m_suspendLayoutDuringAnimation) {
        if (QLayout* l = layout()) {
            l->setEnabled(true);
        }
    }

    m_currentIndex = index;
    QStackedWidget::setCurrentIndex(index);

    for (int i = 0; i < count(); ++i) {
        QWidget* page = widget(i);
        if (!page) {
            continue;
        }
        if (i == index) {
            page->show();
            page->setGeometry(pageRectForOffset(page, 0));
        } else {
            page->hide();
            page->setGeometry(pageRectForOffset(page, 0));
        }
    }
}

void AnimatedStackedWidget::slideToWidget(int newIndex)
{
    if (newIndex == m_currentIndex) {
        return;
    }

    // Get widgets
    QWidget* nextWidget = widget(newIndex);
    if (!nextWidget) {
        return;
    }

    QWidget* currentWidget = nullptr;
    bool wasAnimating = false;

    // Stop any existing animation BUT keep widgets at their current positions
    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
        wasAnimating = true;
        // Layout stays disabled — the new slide will continue managing geometry.

        // The widget at m_currentIndex is what we were animating TO
        currentWidget = widget(m_currentIndex);
    } else {
        // No animation running - use current index widget
        currentWidget = widget(m_currentIndex);
    }

    if (!currentWidget || currentWidget == nextWidget) {
        return;
    }

    if (!wasAnimating) {
        currentWidget->setGeometry(pageRectForOffset(currentWidget, 0));
    }

    // If we're interrupting and nextWidget is already on screen (e.g. it was
    // the outgoing page of the previous slide), we MUST keep its current
    // position so the new slide picks up seamlessly. Otherwise it's a fresh
    // page that needs its layout activated and a sane size.
    const bool nextAlreadyInFlight = wasAnimating && nextWidget->isVisible();

    for (int i = 0; i < count(); ++i) {
        QWidget* page = widget(i);
        if (!page || page == currentWidget || page == nextWidget) {
            continue;
        }
        page->hide();
        page->setGeometry(pageRectForOffset(page, 0));
    }

    // QStackedLayout only updates the geometry of the *current* page.
    // A page that has never been current may have stale/zero geometry and
    // un-activated child layouts, causing it to appear blank during animation.
    // For fresh pages we'll position them offscreen below; only activate the
    // layout here. Do NOT snap geometry yet — that would clobber the in-flight
    // position of a page being re-targeted mid-slide.
    if (nextWidget->layout()) {
        nextWidget->layout()->activate();
    }

    // Store for potential interruption
    const int fromIndex = indexOf(currentWidget);
    m_animatingFromIndex = fromIndex;
    m_animatingWidget = currentWidget;
    m_transitionToIndex = newIndex;

    // Disable QStackedLayout for the duration of the slide. Otherwise every
    // layout pass (triggered by ancestors resizing us — e.g. a parent panel
    // animating its height in parallel) would call setGeometry(contentsRect)
    // on the "current" page, snapping the outgoing widget back to (0,0) and
    // clobbering the slide. Previously this was worked around via
    // setFixedSize locks on the pages, but that couldn't follow a resizing
    // container without triggering further layout invalidations. Disabling
    // the layout gives us sole control over page geometry during the slide.
    if (m_suspendLayoutDuringAnimation) {
        if (QLayout* l = layout()) {
            l->setEnabled(false);
        }
    }

    // Determine animation direction
    AnimationDirection direction = determineDirection(fromIndex, newIndex);

    // Calculate offsets
    const bool horizontal = (m_orientation == SlideOrientation::Horizontal);
    const qreal distance = qMax(1, horizontal ? width() : height());
    const qreal outgoingEndRatio = (direction == AnimationDirection::Down) ? -1.0 : 1.0;
    const qreal incomingDefaultStartRatio = -outgoingEndRatio;

    const QRect currentCenteredRect = pageRectForOffset(currentWidget, 0);
    const qreal outgoingStartOffset = horizontal
        ? (currentWidget->pos().x() - currentCenteredRect.x())
        : (currentWidget->pos().y() - currentCenteredRect.y());

    // Position next widget.
    // If it's already on screen (was being animated), keep its current position
    // as start so the new slide continues seamlessly. Otherwise position it
    // off-screen at the default start offset.
    qreal incomingStartRatio = incomingDefaultStartRatio;
    if (nextAlreadyInFlight) {
        const QRect centeredRect = pageRectForOffset(nextWidget, 0);
        const qreal incomingStartOffset = horizontal ? (nextWidget->pos().x() - centeredRect.x())
                                                     : (nextWidget->pos().y() - centeredRect.y());
        incomingStartRatio = incomingStartOffset / distance;
    } else {
        nextWidget->setGeometry(
            pageRectForOffset(nextWidget, qRound(incomingDefaultStartRatio * distance)));
    }

    nextWidget->show();
    nextWidget->raise();
    currentWidget->show();

    m_outgoingStartRatio = outgoingStartOffset / distance;
    m_outgoingEndRatio = outgoingEndRatio;
    m_incomingStartRatio = incomingStartRatio;
    m_incomingEndRatio = 0.0;

    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(m_duration);
    // On interruption use an "out" curve: the in-flight slide is already
    // moving, so an inOut curve (zero velocity at t=0) would visibly stall.
    // OutCubic starts at max velocity → seamless pickup → smooth landing,
    // which matches "previous animation eases out, new one continues".
    m_animation->setEasingCurve(wasAnimating ? m_interruptEasingCurve : m_easingCurve);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    connect(m_animation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { updateTransitionGeometry(value.toReal()); });

    // Capture widget pointers directly — safer than index-based lookup
    // since widget order could theoretically change during animation.
    // No guard needed: stop() on the old animation prevents its finished signal.
    QWidget* outgoingCapture = currentWidget;
    QWidget* incomingCapture = nextWidget;
    int targetIdx = newIndex;

    connect(m_animation, &QVariantAnimation::finished, this,
        [this, outgoingCapture, incomingCapture, targetIdx]() {
            // Hide and reset the outgoing widget
            if (outgoingCapture) {
                outgoingCapture->hide();
                outgoingCapture->setGeometry(pageRectForOffset(outgoingCapture, 0));
            }

            // Ensure incoming widget is at its final position
            if (incomingCapture) {
                incomingCapture->setGeometry(pageRectForOffset(incomingCapture, 0));
            }

            // Update state
            m_currentIndex = targetIdx;
            QStackedWidget::setCurrentIndex(targetIdx);

            finishAnimation();

            emit currentChanged(m_currentIndex);
        });

    // Update current index immediately for direction calculation on next switch
    m_currentIndex = newIndex;

    // Start a fresh measurement window for this slide. An interrupted slide
    // (wasAnimating) keeps accumulating into the same window so the report
    // covers the whole user-visible transition.
    if (!wasAnimating) {
        RUWA_PROFILE_RESET();
        RUWA_PROFILE_FRAME_RESET(m_frameProfiler);
    }

    updateTransitionGeometry(0.0);
    m_animation->start();
}

void AnimatedStackedWidget::finishAnimation()
{
    // One aggregated report per completed transition, then clear for next time.
    RUWA_PROFILE_REPORT(QStringLiteral("AnimatedStackedWidget slide (%1 -> %2)")
            .arg(m_animatingFromIndex)
            .arg(m_transitionToIndex));
    RUWA_PROFILE_RESET();

    // Re-enable QStackedLayout now that we no longer need exclusive control
    // over the page geometry. Any pending invalidations accumulated during
    // the slide will be resolved on the next layout pass.
    if (m_suspendLayoutDuringAnimation) {
        if (QLayout* l = layout()) {
            l->setEnabled(true);
        }
    }
    m_animatingWidget = nullptr;
    m_animatingFromIndex = -1;
    m_transitionToIndex = -1;
    m_outgoingStartRatio = 0.0;
    m_outgoingEndRatio = 0.0;
    m_incomingStartRatio = 0.0;
    m_incomingEndRatio = 0.0;

    if (m_animation) {
        m_animation->deleteLater();
        m_animation = nullptr;
    }
}

QRect AnimatedStackedWidget::pageRectForOffset(QWidget* page, int offset) const
{
    RUWA_PROFILE_ZONE("pageRectForOffset");
    if (!page) {
        return QRect(0, 0, width(), height());
    }

    if (!m_preservePageSize) {
        if (m_orientation == SlideOrientation::Horizontal) {
            return QRect(offset, 0, width(), height());
        }
        return QRect(0, offset, width(), height());
    }

    const QSize pageSize = naturalPageSize(page);
    const int centeredX = (width() - pageSize.width()) / 2;
    const int centeredY = (height() - pageSize.height()) / 2;

    if (m_orientation == SlideOrientation::Horizontal) {
        return QRect(centeredX + offset, centeredY, pageSize.width(), pageSize.height());
    }

    return QRect(centeredX, centeredY + offset, pageSize.width(), pageSize.height());
}

void AnimatedStackedWidget::updateCurrentWidgetGeometry()
{
    QWidget* current = currentWidget();
    if (!current) {
        return;
    }

    current->setGeometry(pageRectForOffset(current, 0));
}

void AnimatedStackedWidget::updateTransitionGeometry(qreal progress)
{
    // Wall-clock cadence between successive animation ticks. If this is far
    // above ~16700 us while the zones below stay cheap, the bottleneck is page
    // repaint (event loop blocked between ticks), not this code.
    RUWA_PROFILE_FRAME(m_frameProfiler, "frame interval (anim tick)");
    RUWA_PROFILE_ZONE("updateTransitionGeometry (total)");

    if (m_animatingFromIndex < 0 || m_transitionToIndex < 0) {
        return;
    }

    QWidget* outgoingWidget = widget(m_animatingFromIndex);
    QWidget* incomingWidget = widget(m_transitionToIndex);
    if (!outgoingWidget || !incomingWidget) {
        return;
    }

    // Defensive: QStackedLayout may hide one of the animating widgets during
    // layout passes (e.g. resize). Re-show them to prevent visual glitches.
    if (!outgoingWidget->isVisible())
        outgoingWidget->show();
    if (!incomingWidget->isVisible())
        incomingWidget->show();

    const qreal t = qBound(0.0, progress, 1.0);
    const qreal distance
        = qMax(1, (m_orientation == SlideOrientation::Horizontal) ? width() : height());
    const qreal outgoingRatio
        = m_outgoingStartRatio + (m_outgoingEndRatio - m_outgoingStartRatio) * t;
    const qreal incomingRatio
        = m_incomingStartRatio + (m_incomingEndRatio - m_incomingStartRatio) * t;
    const QRect outgoingBaseRect = pageRectForOffset(outgoingWidget, 0);
    const QRect incomingBaseRect = pageRectForOffset(incomingWidget, 0);
    const int outgoingOffset = qRound(outgoingRatio * distance);
    const int incomingOffset = qRound(incomingRatio * distance);

    if (m_orientation == SlideOrientation::Horizontal) {
        outgoingWidget->move(outgoingBaseRect.x() + outgoingOffset, outgoingBaseRect.y());
        incomingWidget->move(incomingBaseRect.x() + incomingOffset, incomingBaseRect.y());
    } else {
        outgoingWidget->move(outgoingBaseRect.x(), outgoingBaseRect.y() + outgoingOffset);
        incomingWidget->move(incomingBaseRect.x(), incomingBaseRect.y() + incomingOffset);
    }
}

void AnimatedStackedWidget::updateVisibleWidgetGeometriesDuringAnimation()
{
    if (!m_animation || m_animation->state() != QAbstractAnimation::Running) {
        return;
    }

    updateTransitionGeometry(m_animation->currentValue().toReal());
}

void AnimatedStackedWidget::resizeEvent(QResizeEvent* event)
{
    QStackedWidget::resizeEvent(event);

    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        // QStackedLayout is disabled during the slide (see slideToWidget),
        // so we manage the animating pages' geometry ourselves. Re-show in
        // case anything hid them, then reposition/resize to fit the new
        // stack dimensions.
        if (auto* out = widget(m_animatingFromIndex))
            out->show();
        if (auto* in = widget(m_transitionToIndex))
            in->show();
        updateVisibleWidgetGeometriesDuringAnimation();
        return;
    }

    updateCurrentWidgetGeometry();
}

AnimatedStackedWidget::AnimationDirection AnimatedStackedWidget::determineDirection(
    int fromIndex, int toIndex) const
{
    if (toIndex > fromIndex) {
        return AnimationDirection::Down;
    } else {
        return AnimationDirection::Up;
    }
}

} // namespace ruwa::ui::widgets
