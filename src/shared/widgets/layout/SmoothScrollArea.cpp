// SPDX-License-Identifier: MPL-2.0

// SmoothScrollArea.cpp
#include "SmoothScrollArea.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/widgets/SmoothScrollbar.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QWheelEvent>
#include <QChildEvent>
#include <QTimer>
#include <QCursor>
#include <QApplication>
#include <QEnterEvent>
#include <QScreen>
#include <QtMath>
#include <cmath>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {
// --- Continuous damping engine ---------------------------------------------------
// Decay rates in 1/s. Each frame the position closes 1-e^(-lambda*dt) of the
// distance that is left, so the motion is frame-rate independent and a new
// impulse arriving mid-flight only raises the velocity instead of restarting
// an easing curve from zero.
constexpr qreal kWheelLambda = 12.0; // Notched wheel: glides, still lands fast.
constexpr qreal kPixelWheelLambda = 26.0; // Trackpad/precision wheel: near 1:1.
constexpr qreal kStepLambda = 13.0; // Scrollbar step buttons.
constexpr qreal kBarFollowLambda = 18.0; // Track click / external bar value change.
constexpr qreal kFlingLambda = 5.0; // Stylus release: long, decaying glide.
// Below this the remaining distance is invisible — snap and stop the timer.
constexpr qreal kDampingSettleEpsilon = 0.06;
constexpr qreal kMaxDampingStepSeconds = 0.05; // Survive a stalled event loop.

// Repeated notches in the same direction accelerate, like a native wheel.
constexpr qint64 kWheelBoostWindowMs = 110;
constexpr qreal kWheelBoostStep = 0.22;
constexpr qreal kWheelBoostMax = 1.9;

constexpr qreal kStylusSwipeVelocityTauSeconds = 0.045;
constexpr qreal kStylusSwipeVelocityDeadzone = 90.0;
// A pen that rested before lifting must not fling — drop a stale velocity.
constexpr qint64 kStylusSwipeStaleSampleMs = 90;
constexpr int kHoverUpdateIntervalMs = 40;
constexpr qreal kScrollBarWidth = 12.0; // Must match SmoothScrollBar::setFixedWidth(12).
constexpr int kReserveAnimationMs = 220;
constexpr int kDefaultScrollDurationMs = 200;
} // namespace

SmoothScrollArea::SmoothScrollArea(QWidget* parent)
    : QWidget(parent)
{
    // Critical for frameless windows — prevent stacking artifacts
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, false);

    m_viewport = new QWidget(this);
    m_viewport->setObjectName("smooth_scroll_viewport");
    m_viewport->setAutoFillBackground(true);

    m_verticalScrollBar = new SmoothScrollBar(Qt::Vertical, this);
    connect(m_verticalScrollBar, &QScrollBar::valueChanged, this,
        &SmoothScrollArea::onScrollBarValueChanged);
    connect(m_verticalScrollBar, &SmoothScrollBar::stepScrollRequested, this,
        &SmoothScrollArea::onStepScrollRequested);

    m_scrollAnimation = new QPropertyAnimation(this, "scrollValue");
    m_scrollAnimation->setDuration(kDefaultScrollDurationMs);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // Animates the reserved scrollbar column width so content is pushed aside
    // smoothly (and the bar slides out) instead of snapping.
    m_reserveAnimation = new QPropertyAnimation(this, "scrollBarReserveExtent");
    m_reserveAnimation->setDuration(kReserveAnimationMs);
    m_reserveAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_reserveAnimation, &QPropertyAnimation::finished, this, [this]() {
        // Content extent may depend on the final viewport width — settle the range.
        updateScrollRange();
    });

    m_wheelLambda = kWheelLambda;
    m_flingLambda = kFlingLambda;

    // Drives the damping engine. Unlike QPropertyAnimation (locked to Qt's 16 ms
    // unified animation clock) this ticks at the display refresh rate, so the
    // motion is genuinely smoother on 120/144 Hz panels. It only runs while
    // scrolling is in flight.
    m_damperTimer = new QTimer(this);
    m_damperTimer->setTimerType(Qt::PreciseTimer);
    connect(m_damperTimer, &QTimer::timeout, this, &SmoothScrollArea::tickDamping);
    m_wheelClock.start();

    m_layoutRefreshTimer = new QTimer(this);
    m_layoutRefreshTimer->setSingleShot(true);
    connect(m_layoutRefreshTimer, &QTimer::timeout, this, &SmoothScrollArea::refreshContentLayout);

    m_hoverUpdateTimer = new QTimer(this);
    m_hoverUpdateTimer->setSingleShot(true);
    m_hoverUpdateTimer->setInterval(kHoverUpdateIntervalMs);
    connect(m_hoverUpdateTimer, &QTimer::timeout, this, &SmoothScrollArea::flushHoverStates);
    connect(m_scrollAnimation, &QPropertyAnimation::finished, this,
        &SmoothScrollArea::flushHoverStates);

    updateGeometry();
}

SmoothScrollArea::~SmoothScrollArea()
{
    cancelStylusSwipe();
    stopDamping();

    if (m_layoutRefreshTimer && m_layoutRefreshTimer->isActive()) {
        m_layoutRefreshTimer->stop();
    }
    if (m_hoverUpdateTimer && m_hoverUpdateTimer->isActive()) {
        m_hoverUpdateTimer->stop();
    }

    if (m_hoveredWidget) {
        m_hoveredWidget->setAttribute(Qt::WA_UnderMouse, false);
        m_hoveredWidget.clear();
    }

    if (m_contentWidget) {
        removeContentEventFilters(m_contentWidget);
    }

    delete m_scrollAnimation;
    delete m_reserveAnimation;
}

void SmoothScrollArea::setFillBackground(bool fill)
{
    if (m_fillBackground == fill) {
        return;
    }
    m_fillBackground = fill;
    setAttribute(Qt::WA_OpaquePaintEvent, fill);
    setAttribute(Qt::WA_NoSystemBackground, !fill);
    setAttribute(Qt::WA_TranslucentBackground, !fill);
    setAutoFillBackground(fill);
    if (m_viewport) {
        m_viewport->setAutoFillBackground(fill);
        m_viewport->setAttribute(Qt::WA_NoSystemBackground, !fill);
        m_viewport->setAttribute(Qt::WA_TranslucentBackground, !fill);
    }
    update();
}

void SmoothScrollArea::setScrollBarTransparentTrack(bool transparent)
{
    if (m_scrollBarTransparentTrack == transparent) {
        return;
    }
    m_scrollBarTransparentTrack = transparent;
    if (m_verticalScrollBar) {
        m_verticalScrollBar->setTransparentTrack(transparent);
    }
}

void SmoothScrollArea::setWidget(QWidget* widget)
{
    if (m_layoutRefreshTimer && m_layoutRefreshTimer->isActive()) {
        m_layoutRefreshTimer->stop();
    }

    if (m_hoveredWidget) {
        m_hoveredWidget->setAttribute(Qt::WA_UnderMouse, false);
        m_hoveredWidget.clear();
    }

    if (m_contentWidget) {
        removeContentEventFilters(m_contentWidget);
        m_contentWidget->setParent(nullptr);
    }

    m_contentWidget = widget;

    if (m_contentWidget) {
        m_contentWidget->setParent(m_viewport);
        m_contentWidget->move(0, 0);
        installContentEventFilters(m_contentWidget);
        connect(
            m_contentWidget, &QWidget::destroyed, this, [this]() { m_contentWidget = nullptr; });

        refreshContentLayout();
        scheduleContentLayoutRefresh();
        return;
    }

    refreshContentLayout();
}

void SmoothScrollArea::setOrientation(Qt::Orientation orientation)
{
    if (m_orientation == orientation) {
        return;
    }

    m_scrollAnimation->stop();
    stopDamping();
    cancelStylusSwipe();
    m_orientation = orientation;
    m_currentScrollValue = 0;
    m_targetScrollValue = 0;
    m_maxScroll = 0;
    syncDampingStateToCurrentValue();

    if (m_orientation == Qt::Horizontal) {
        m_contentWidthFixedToViewport = false;
        if (m_contentWidget) {
            m_contentWidget->setMinimumWidth(0);
            m_contentWidget->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    }
    if (m_contentWidget) {
        m_contentWidget->move(0, 0);
    }

    refreshContentLayout();
}

void SmoothScrollArea::refreshScrollGeometry()
{
    refreshContentLayout();
}

void SmoothScrollArea::finishLayoutTransitions()
{
    if (m_layoutRefreshTimer && m_layoutRefreshTimer->isActive()) {
        m_layoutRefreshTimer->stop();
    }

    m_reserveAnimation->stop();
    m_finishingLayoutTransitions = true;

    // The first pass resolves the range from the final content height. The
    // second resolves it once more after the scrollbar reservation has snapped
    // to that range and changed the viewport width.
    refreshContentLayout();
    refreshContentLayout();

    m_finishingLayoutTransitions = false;
}

void SmoothScrollArea::scrollTo(int value, bool animated)
{
    if (animated) {
        scrollTo(value, kDefaultScrollDurationMs, QEasingCurve::OutCubic);
        return;
    }

    m_scrollAnimation->stop();
    stopDamping();
    m_targetScrollValue = qBound(0, value, m_maxScroll);
    setScrollValue(m_targetScrollValue);
}

void SmoothScrollArea::scrollTo(int value, int durationMs, QEasingCurve::Type easingCurve)
{
    m_targetScrollValue = qBound(0, value, m_maxScroll);
    m_scrollAnimation->stop();
    stopDamping();

    if (durationMs > 0 && m_targetScrollValue != m_currentScrollValue) {
        m_scrollAnimation->setDuration(anim::duration(durationMs));
        m_scrollAnimation->setEasingCurve(easingCurve);
        m_scrollAnimation->setStartValue(m_currentScrollValue);
        m_scrollAnimation->setEndValue(m_targetScrollValue);
        anim::start(m_scrollAnimation);
    } else {
        setScrollValue(m_targetScrollValue);
    }
}

void SmoothScrollArea::setUserScrollingEnabled(bool enabled)
{
    if (m_userScrollingEnabled == enabled) {
        return;
    }

    m_userScrollingEnabled = enabled;
    m_verticalScrollBar->setEnabled(enabled);
    m_verticalScrollBar->setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);

    if (!enabled) {
        m_scrollAnimation->stop();
        stopDamping();
        m_targetScrollValue = m_currentScrollValue;
        syncDampingStateToCurrentValue();
        cancelStylusSwipe();
    }
}

void SmoothScrollArea::setScrollValue(int value)
{
    const int previousScrollValue = m_currentScrollValue;
    m_currentScrollValue = qBound(0, value, m_maxScroll);

    // Any write that is not the damper's own tick (a direct call, or the
    // duration-based scrollTo animation driving this property) takes ownership
    // of the position: cancel the damping flight and re-seed its state.
    if (!m_applyingDamperTick) {
        stopDamping();
        syncDampingStateToCurrentValue();
    }

    m_verticalScrollBar->blockSignals(true);
    m_verticalScrollBar->setValue(m_currentScrollValue);
    m_verticalScrollBar->blockSignals(false);

    syncContentPosition(previousScrollValue, false);

    emit scrolled(m_currentScrollValue);
}

void SmoothScrollArea::setVerticalScrollBarPolicy(Qt::ScrollBarPolicy policy)
{
    m_scrollBarPolicy = policy;
    updateScrollBarVisibility();
}

void SmoothScrollArea::setScrollBarMargin(int pixels)
{
    if (m_scrollBarMargin != pixels) {
        m_scrollBarMargin = qMax(0, pixels);
        refreshContentLayout();
    }
}

void SmoothScrollArea::setScrollBarAlwaysReserved(bool reserved)
{
    if (m_scrollBarAlwaysReserved != reserved) {
        m_scrollBarAlwaysReserved = reserved;
        updateScrollBarVisibility();
    }
}

void SmoothScrollArea::setContentWidthFixedToViewport(bool fixed)
{
    if (m_contentWidthFixedToViewport != fixed) {
        m_contentWidthFixedToViewport = fixed;
        if (!fixed && m_contentWidget) {
            m_contentWidget->setMinimumWidth(0);
            m_contentWidget->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        refreshContentLayout();
    }
}

int SmoothScrollArea::damperIntervalMs() const
{
    qreal refreshRate = 60.0;
    if (const QScreen* widgetScreen = screen()) {
        const qreal rate = widgetScreen->refreshRate();
        if (rate > 1.0) {
            refreshRate = rate;
        }
    }
    // Floor, not round: at 60 Hz this gives 16 ms rather than 17 ms.
    return qBound(4, static_cast<int>(1000.0 / refreshRate), 32);
}

void SmoothScrollArea::startDamping(qreal lambda)
{
    // The decay rate is the animation's speed: a faster policy scales lambda up,
    // and with animations off there is no flight at all — the content is already
    // where the impulse was sending it.
    if (!anim::enabled()) {
        if (m_scrollAnimation->state() == QAbstractAnimation::Running) {
            m_scrollAnimation->stop();
        }
        applyScrollPosition(m_scrollTarget);
        stopDamping();
        return;
    }

    m_damperLambda = qMax(0.5, lambda * anim::speed());

    // The damper and the explicit-duration scrollTo() animation are mutually
    // exclusive owners of the position.
    if (m_scrollAnimation->state() == QAbstractAnimation::Running) {
        m_scrollAnimation->stop();
    }

    if (qAbs(m_scrollTarget - m_scrollPosition) < kDampingSettleEpsilon) {
        applyScrollPosition(m_scrollTarget);
        stopDamping();
        return;
    }

    if (!m_damperTimer->isActive()) {
        m_damperClock.start();
        m_damperLastTickNs = 0;
        m_damperTimer->start(damperIntervalMs());
    }
}

void SmoothScrollArea::stopDamping()
{
    if (m_damperTimer && m_damperTimer->isActive()) {
        m_damperTimer->stop();
        // Ranges that changed mid-flight may have moved widgets under the pointer.
        scheduleHoverStateUpdate();
    }
}

void SmoothScrollArea::tickDamping()
{
    const qint64 nowNs = m_damperClock.nsecsElapsed();
    const qreal dt = qBound(
        0.001, static_cast<qreal>(nowNs - m_damperLastTickNs) / 1.0e9, kMaxDampingStepSeconds);
    m_damperLastTickNs = nowNs;

    m_scrollTarget = qBound(0.0, m_scrollTarget, static_cast<qreal>(m_maxScroll));

    const qreal distance = m_scrollTarget - m_scrollPosition;
    if (qAbs(distance) < kDampingSettleEpsilon) {
        applyScrollPosition(m_scrollTarget);
        stopDamping();
        return;
    }

    // Lenis' core step: close a fixed *fraction* of the remaining distance per
    // unit of time. Expressed as an exponential it is exact for any dt.
    const qreal factor = 1.0 - std::exp(-m_damperLambda * dt);
    applyScrollPosition(m_scrollPosition + (distance * factor));

    // Clamped against a range edge — no distance left to travel, so stop
    // rather than spin the timer against the boundary.
    if (qAbs(m_scrollTarget - m_scrollPosition) < kDampingSettleEpsilon) {
        stopDamping();
    }
}

void SmoothScrollArea::applyScrollPosition(qreal position)
{
    m_scrollPosition = qBound(0.0, position, static_cast<qreal>(m_maxScroll));
    m_targetScrollValue = qRound(qBound(0.0, m_scrollTarget, static_cast<qreal>(m_maxScroll)));

    const int rounded = qRound(m_scrollPosition);
    if (rounded == m_currentScrollValue) {
        // Sub-pixel progress: the widget cannot move yet, but the fractional
        // position is kept so slow scrolling paces evenly instead of stuttering.
        return;
    }

    m_applyingDamperTick = true;
    setScrollValue(rounded);
    m_applyingDamperTick = false;
}

void SmoothScrollArea::syncDampingStateToCurrentValue()
{
    m_scrollPosition = m_currentScrollValue;
    m_scrollTarget = m_currentScrollValue;
}

qreal SmoothScrollArea::dampingBase() const
{
    // Chain onto the in-flight target so successive impulses add up instead of
    // each one restarting from where the content happens to be right now.
    return m_damperTimer && m_damperTimer->isActive() ? m_scrollTarget : m_scrollPosition;
}

qreal SmoothScrollArea::takeWheelBoost(int direction, bool highResolutionDelta)
{
    // Precision devices already send continuous, physically-scaled deltas.
    if (highResolutionDelta) {
        m_wheelBoost = 1.0;
        m_lastWheelDirection = 0;
        return 1.0;
    }

    const qint64 nowMs = m_wheelClock.elapsed();
    const bool continuesFlick
        = direction == m_lastWheelDirection && (nowMs - m_lastWheelMs) <= kWheelBoostWindowMs;

    m_wheelBoost = continuesFlick ? qMin(kWheelBoostMax, m_wheelBoost + kWheelBoostStep) : 1.0;
    m_lastWheelMs = nowMs;
    m_lastWheelDirection = direction;

    return m_wheelBoost;
}

void SmoothScrollArea::setWheelDamping(qreal lambda)
{
    m_wheelLambda = qMax(0.5, lambda);
}

void SmoothScrollArea::setFlingDamping(qreal lambda)
{
    m_flingLambda = qMax(0.5, lambda);
}

void SmoothScrollArea::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!m_fillBackground) {
        return;
    }
    // Fill background to prevent frameless window artifacts (stacking, transparency)
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
}

void SmoothScrollArea::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshContentLayout();
}

void SmoothScrollArea::beginStylusSwipe(const QPoint& globalPos)
{
    if (!m_userScrollingEnabled) {
        return;
    }

    m_scrollAnimation->stop();
    stopDamping();
    syncDampingStateToCurrentValue();
    m_stylusSwipeActive = true;
    // The filter begins the swipe at the press point once the drag threshold is
    // crossed, so the pen is already ~10 px away. Re-anchor on the first move
    // sample instead of snapping the content by that slop.
    m_stylusSwipeAnchorPending = true;
    m_stylusSwipeStartGlobalPos = globalPos;
    m_stylusSwipeLastGlobalPos = globalPos;
    m_stylusSwipeStartPosition = m_scrollPosition;
    m_stylusSwipeVelocity = 0.0;
    m_stylusSwipeTimer.start();
    m_stylusSwipeLastSampleMs = 0;
    if (m_orientation == Qt::Vertical) {
        m_verticalScrollBar->showAnimated();
    }
}

void SmoothScrollArea::updateStylusSwipe(const QPoint& globalPos)
{
    if (!m_userScrollingEnabled || !m_stylusSwipeActive) {
        return;
    }

    if (m_stylusSwipeAnchorPending) {
        m_stylusSwipeAnchorPending = false;
        m_stylusSwipeStartGlobalPos = globalPos;
        m_stylusSwipeLastGlobalPos = globalPos;
        m_stylusSwipeLastSampleMs = m_stylusSwipeTimer.elapsed();
        if (m_orientation == Qt::Vertical) {
            m_verticalScrollBar->showAnimated();
        }
        return;
    }

    const int dragDelta = m_orientation == Qt::Horizontal
        ? globalPos.x() - m_stylusSwipeStartGlobalPos.x()
        : globalPos.y() - m_stylusSwipeStartGlobalPos.y();
    // Direct manipulation: the content stays pinned to the pen, no damping.
    m_scrollTarget
        = qBound(0.0, m_stylusSwipeStartPosition - dragDelta, static_cast<qreal>(m_maxScroll));
    m_applyingDamperTick = true;
    applyScrollPosition(m_scrollTarget);
    m_applyingDamperTick = false;

    const qint64 nowMs = m_stylusSwipeTimer.elapsed();
    const qreal dtSeconds = qMax<qint64>(1, nowMs - m_stylusSwipeLastSampleMs) / 1000.0;
    const int stepDelta = m_orientation == Qt::Horizontal
        ? globalPos.x() - m_stylusSwipeLastGlobalPos.x()
        : globalPos.y() - m_stylusSwipeLastGlobalPos.y();
    const qreal instantVelocity = -stepDelta / dtSeconds;
    // Time-constant EWMA — a fixed per-sample blend would make the tracked
    // velocity depend on the tablet's report rate.
    const qreal blend = 1.0 - std::exp(-dtSeconds / kStylusSwipeVelocityTauSeconds);
    m_stylusSwipeVelocity += (instantVelocity - m_stylusSwipeVelocity) * blend;

    m_stylusSwipeLastGlobalPos = globalPos;
    m_stylusSwipeLastSampleMs = nowMs;
    if (m_orientation == Qt::Vertical) {
        m_verticalScrollBar->showAnimated();
    }
}

void SmoothScrollArea::endStylusSwipe(const QPoint& globalPos)
{
    if (!m_userScrollingEnabled || !m_stylusSwipeActive) {
        return;
    }

    const qint64 lastSampleMs = m_stylusSwipeLastSampleMs;
    updateStylusSwipe(globalPos);
    m_stylusSwipeActive = false;

    // A pen that hovered in place before lifting reports no fresh movement, so
    // the last tracked velocity is stale — releasing must simply stop there.
    const bool velocityIsStale
        = (m_stylusSwipeTimer.elapsed() - lastSampleMs) > kStylusSwipeStaleSampleMs;
    if (velocityIsStale || qAbs(m_stylusSwipeVelocity) < kStylusSwipeVelocityDeadzone) {
        return;
    }

    // Hand the release velocity to the damper. Damping toward a target starts
    // out at lambda*(target-position), so projecting the target that far ahead
    // makes the glide continue at exactly the speed the pen was moving and then
    // decay away — no seam between the drag and the inertia.
    m_scrollAnimation->stop();
    m_scrollTarget = qBound(0.0, m_scrollPosition + (m_stylusSwipeVelocity / m_flingLambda),
        static_cast<qreal>(m_maxScroll));
    m_targetScrollValue = qRound(m_scrollTarget);

    startDamping(m_flingLambda);
    if (m_orientation == Qt::Vertical) {
        m_verticalScrollBar->showAnimated();
    }
}

void SmoothScrollArea::cancelStylusSwipe()
{
    m_stylusSwipeActive = false;
    m_stylusSwipeAnchorPending = false;
    m_stylusSwipeVelocity = 0.0;
}

bool SmoothScrollArea::eventFilter(QObject* watched, QEvent* event)
{
    const bool isTrackedContentWidget = watched == m_contentWidget
        || (m_contentWidget && watched->isWidgetType()
            && m_contentWidget->isAncestorOf(static_cast<QWidget*>(watched)));

    if (isTrackedContentWidget) {
        switch (event->type()) {
        case QEvent::Hide:
        case QEvent::HideToParent:
            scheduleContentLayoutRefresh();
            break;
        case QEvent::Resize:
        case QEvent::LayoutRequest:
        case QEvent::Show:
        case QEvent::ShowToParent:
        case QEvent::ContentsRectChange:
        case QEvent::StyleChange:
        case QEvent::FontChange:
        case QEvent::PolishRequest:
            scheduleContentLayoutRefresh();
            break;
        case QEvent::ChildAdded: {
            auto* childEvent = static_cast<QChildEvent*>(event);
            if (QObject* child = childEvent->child(); child && child->isWidgetType()) {
                installContentEventFilters(child);
            }
            scheduleContentLayoutRefresh();
            break;
        }
        case QEvent::ChildRemoved: {
            auto* childEvent = static_cast<QChildEvent*>(event);
            if (QObject* child = childEvent->child(); child && child->isWidgetType()) {
                removeContentEventFilters(child);
            }
            scheduleContentLayoutRefresh();
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SmoothScrollArea::installContentEventFilters(QObject* object)
{
    if (!object) {
        return;
    }

    object->installEventFilter(this);

    const auto children = object->children();
    for (QObject* child : children) {
        if (child && child->isWidgetType()) {
            installContentEventFilters(child);
        }
    }
}

void SmoothScrollArea::removeContentEventFilters(QObject* object)
{
    if (!object) {
        return;
    }

    object->removeEventFilter(this);

    const auto children = object->children();
    for (QObject* child : children) {
        if (child && child->isWidgetType()) {
            removeContentEventFilters(child);
        }
    }
}

void SmoothScrollArea::refreshContentLayout()
{
    if (m_refreshingLayout) {
        return;
    }

    m_refreshingLayout = true;
    updateGeometry();
    updateScrollRange();
    m_refreshingLayout = false;
}

void SmoothScrollArea::scheduleContentLayoutRefresh()
{
    if (!m_contentWidget || !m_layoutRefreshTimer || m_refreshingLayout) {
        return;
    }

    if (!m_layoutRefreshTimer->isActive()) {
        m_layoutRefreshTimer->start(0);
    }
}

void SmoothScrollArea::wheelEvent(QWheelEvent* event)
{
    if (!m_userScrollingEnabled) {
        event->accept();
        return;
    }

    if (!m_contentWidget || m_maxScroll <= 0) {
        return;
    }

    int delta = 0;
    bool highResolutionDelta = false;
    if (!event->pixelDelta().isNull()) {
        delta
            = m_orientation == Qt::Horizontal ? -event->pixelDelta().x() : -event->pixelDelta().y();
        if (delta == 0 && m_orientation == Qt::Horizontal) {
            delta = -event->pixelDelta().y();
        }
        highResolutionDelta = delta != 0;
    }
    if (delta == 0) {
        delta
            = m_orientation == Qt::Horizontal ? -event->angleDelta().x() : -event->angleDelta().y();
        if (delta == 0 && m_orientation == Qt::Horizontal) {
            delta = -event->angleDelta().y();
        }
    }
    if (delta == 0) {
        event->accept();
        return;
    }

    // A scroll that is still in flight keeps its target: the new notch is added
    // on top, so the velocity rises smoothly instead of the motion restarting.
    const qreal baseValue = (m_scrollAnimation->state() == QAbstractAnimation::Running)
        ? static_cast<qreal>(m_targetScrollValue)
        : dampingBase();

    m_scrollAnimation->stop();

    const qreal boost = takeWheelBoost(delta > 0 ? 1 : -1, highResolutionDelta);
    m_scrollTarget = qBound(0.0, baseValue + (delta * boost), static_cast<qreal>(m_maxScroll));
    m_targetScrollValue = qRound(m_scrollTarget);

    startDamping(highResolutionDelta ? kPixelWheelLambda : m_wheelLambda);

    if (m_orientation == Qt::Vertical) {
        m_verticalScrollBar->showAnimated();
    }
    event->accept();
}

void SmoothScrollArea::updateScrollRange()
{
    if (!m_contentWidget) {
        m_maxScroll = 0;
        m_currentScrollValue = 0;
        m_targetScrollValue = 0;
        stopDamping();
        syncDampingStateToCurrentValue();
        m_verticalScrollBar->setRange(0, 0);
        updateScrollBarVisibility();
        return;
    }

    const bool wasRefreshingLayout = m_refreshingLayout;
    m_refreshingLayout = true;

    const QSize widgetSizeHint = m_contentWidget->sizeHint();

    QSize layoutSizeHint;
    if (m_contentWidget->layout()) {
        auto* contentLayout = m_contentWidget->layout();
        // LayoutRequest is one of the events that schedules this refresh. Marking
        // the same layout dirty here would post another LayoutRequest after
        // m_refreshingLayout is cleared and keep the zero-delay timer alive forever.
        // activate() still resolves a genuinely pending layout synchronously.
        contentLayout->activate();
        layoutSizeHint = contentLayout->sizeHint();
    }

    if (m_orientation == Qt::Horizontal) {
        int contentWidth = qMax(widgetSizeHint.width(), layoutSizeHint.width());
        if (contentWidth <= 0) {
            contentWidth = m_contentWidget->width();
        }
        contentWidth = qMax(contentWidth, m_viewport->width());

        int contentHeight = qMax(widgetSizeHint.height(), layoutSizeHint.height());
        if (contentHeight <= 0) {
            contentHeight = m_contentWidget->height();
        }
        contentHeight = qMax(contentHeight, m_viewport->height());

        const QSize desiredContentSize(qMax(0, contentWidth), qMax(0, contentHeight));
        if (m_contentWidget->size() != desiredContentSize) {
            m_contentWidget->resize(desiredContentSize);
        }

        m_maxScroll = qMax(0, contentWidth - m_viewport->width());
        m_verticalScrollBar->setRange(0, m_maxScroll);
        m_verticalScrollBar->setPageStep(m_viewport->width());
        m_verticalScrollBar->setSingleStep(qMax(20, m_viewport->width() / 10));
    } else {
        int contentWidth
            = m_contentWidthFixedToViewport ? m_viewport->width() : widgetSizeHint.width();
        if (contentWidth <= 0) {
            contentWidth = m_contentWidget->width();
        }
        if (contentWidth <= 0) {
            contentWidth = m_viewport->width();
        }

        int contentHeight = 0;
        if (m_contentWidget->layout()) {
            auto* contentLayout = m_contentWidget->layout();

            if (contentWidth > 0 && contentLayout->hasHeightForWidth()) {
                contentHeight = contentLayout->totalHeightForWidth(contentWidth);
            }

            if (contentHeight <= 0) {
                contentHeight = layoutSizeHint.height();
            }
        }

        if (contentWidth > 0 && contentHeight <= 0 && m_contentWidget->hasHeightForWidth()) {
            contentHeight = m_contentWidget->heightForWidth(contentWidth);
        }
        if (contentHeight <= 0) {
            contentHeight = widgetSizeHint.height();
        }
        if (contentHeight <= 0) {
            contentHeight = m_contentWidget->height();
        }

        if (!m_contentWidthFixedToViewport && layoutSizeHint.width() > 0) {
            contentWidth = layoutSizeHint.width();
        }
        if (contentWidth <= 0) {
            contentWidth = m_viewport->width();
        }

        const int viewportHeight = m_viewport->height();
        const int widgetHeight = qMax(contentHeight, viewportHeight);
        const QSize desiredContentSize(qMax(0, contentWidth), qMax(0, widgetHeight));
        if (m_contentWidget->size() != desiredContentSize) {
            m_contentWidget->resize(desiredContentSize);
        }

        m_maxScroll = qMax(0, contentHeight - viewportHeight);
        m_verticalScrollBar->setRange(0, m_maxScroll);
        m_verticalScrollBar->setPageStep(viewportHeight);
        m_verticalScrollBar->setSingleStep(qMax(20, viewportHeight / 10));
    }

    const int clampedTarget = qBound(0, m_targetScrollValue, m_maxScroll);
    if (clampedTarget != m_targetScrollValue) {
        m_targetScrollValue = clampedTarget;
        if (m_scrollAnimation->state() == QAbstractAnimation::Running) {
            m_scrollAnimation->stop();
        }
    }
    // The damper reads the float target every tick; keep it inside the new range.
    m_scrollTarget = qBound(0.0, m_scrollTarget, static_cast<qreal>(m_maxScroll));
    m_scrollPosition = qBound(0.0, m_scrollPosition, static_cast<qreal>(m_maxScroll));

    if (m_currentScrollValue > m_maxScroll) {
        setScrollValue(m_maxScroll);
    }

    updateScrollBarVisibility();

    m_refreshingLayout = wasRefreshingLayout;
}

void SmoothScrollArea::onScrollBarValueChanged(int value)
{
    if (m_verticalScrollBar->isDragging()) {
        // Dragging the handle is direct manipulation — track it 1:1.
        m_scrollAnimation->stop();
        stopDamping();
        const int previousScrollValue = m_currentScrollValue;
        m_currentScrollValue = qBound(0, value, m_maxScroll);
        syncDampingStateToCurrentValue();
        syncContentPosition(previousScrollValue, true);
        emit scrolled(m_currentScrollValue);
        return;
    }

    if (qAbs(value - m_currentScrollValue) > 2) {
        m_scrollAnimation->stop();
        m_scrollTarget = qBound(0.0, static_cast<qreal>(value), static_cast<qreal>(m_maxScroll));
        m_targetScrollValue = qRound(m_scrollTarget);
        startDamping(kBarFollowLambda);
    } else {
        setScrollValue(value);
    }
}

void SmoothScrollArea::onStepScrollRequested(int delta)
{
    if (!m_userScrollingEnabled || !m_contentWidget || m_maxScroll <= 0) {
        return;
    }

    // Accumulate target so held button smoothly chains steps
    const qreal baseValue = (m_scrollAnimation->state() == QAbstractAnimation::Running)
        ? static_cast<qreal>(m_targetScrollValue)
        : dampingBase();

    m_scrollAnimation->stop();

    m_scrollTarget = qBound(0.0, baseValue + delta, static_cast<qreal>(m_maxScroll));
    m_targetScrollValue = qRound(m_scrollTarget);

    startDamping(kStepLambda);

    if (m_orientation == Qt::Vertical) {
        m_verticalScrollBar->showAnimated();
    }
}

void SmoothScrollArea::flushHoverStates()
{
    if (m_hoverUpdateTimer && m_hoverUpdateTimer->isActive()) {
        m_hoverUpdateTimer->stop();
    }
    updateHoverStates();
}

void SmoothScrollArea::syncContentPosition(int previousScrollValue, bool updateHoverImmediately)
{
    if (!m_contentWidget) {
        return;
    }

    if (m_orientation == Qt::Horizontal) {
        m_contentWidget->move(-m_currentScrollValue, 0);
    } else {
        m_contentWidget->move(0, -m_currentScrollValue);
    }

    if (m_viewport) {
        const int delta = m_currentScrollValue - previousScrollValue;
        const int viewportExtent
            = m_orientation == Qt::Horizontal ? m_viewport->width() : m_viewport->height();
        const int exposedExtent = qMin(qAbs(delta), viewportExtent);

        if (exposedExtent <= 0 || exposedExtent >= viewportExtent) {
            m_viewport->update();
        } else if (m_orientation == Qt::Horizontal && delta > 0) {
            m_viewport->update(
                m_viewport->width() - exposedExtent, 0, exposedExtent, m_viewport->height());
        } else if (m_orientation == Qt::Horizontal) {
            m_viewport->update(0, 0, exposedExtent, m_viewport->height());
        } else if (delta > 0) {
            m_viewport->update(
                0, m_viewport->height() - exposedExtent, m_viewport->width(), exposedExtent);
        } else {
            m_viewport->update(0, 0, m_viewport->width(), exposedExtent);
        }
    }

    if (updateHoverImmediately) {
        flushHoverStates();
    } else {
        scheduleHoverStateUpdate();
    }
}

void SmoothScrollArea::scheduleHoverStateUpdate()
{
    if (!m_hoverUpdateTimer || m_stylusSwipeActive) {
        updateHoverStates();
        return;
    }

    m_hoverUpdateTimer->start();
}

void SmoothScrollArea::updateGeometry()
{
    if (m_orientation == Qt::Horizontal) {
        m_viewport->setGeometry(rect());
        m_verticalScrollBar->setGeometry(width(), 0, static_cast<int>(kScrollBarWidth), height());
        return;
    }

    // Reserved column width is animated (0..kScrollBarWidth) so the viewport shifts
    // smoothly. The bar itself keeps its fixed width and slides in from the right edge.
    const int reserved = qRound(m_scrollBarReserveExtent);
    const int scrollBarX = width() - reserved;
    const int viewportWidth = qMax(0, scrollBarX - m_scrollBarMargin);

    m_viewport->setGeometry(0, 0, viewportWidth, height());
    m_verticalScrollBar->setGeometry(scrollBarX, 0, static_cast<int>(kScrollBarWidth), height());

    if (m_contentWidget) {
        if (m_contentWidthFixedToViewport) {
            m_contentWidget->setFixedWidth(m_viewport->width());
        }
    }
}

void SmoothScrollArea::setScrollBarReserveExtent(qreal extent)
{
    extent = qBound(0.0, extent, kScrollBarWidth);
    if (qFuzzyCompare(m_scrollBarReserveExtent, extent)) {
        return;
    }
    m_scrollBarReserveExtent = extent;
    // Re-lay the viewport/content to the new reserved width every animation frame.
    updateGeometry();
}

void SmoothScrollArea::updateScrollBarVisibility()
{
    if (m_orientation == Qt::Horizontal) {
        m_scrollBarReserved = false;
        m_reserveAnimation->stop();
        setScrollBarReserveExtent(0.0);
        m_verticalScrollBar->hideAnimated();
        return;
    }

    const bool shouldReserve = m_scrollBarAlwaysReserved
        || (m_scrollBarPolicy == Qt::ScrollBarAlwaysOn)
        || (m_scrollBarPolicy == Qt::ScrollBarAsNeeded && m_maxScroll > 0);
    const bool reserveChanged = (m_scrollBarReserved != shouldReserve);
    m_scrollBarReserved = shouldReserve;

    const qreal target = shouldReserve ? kScrollBarWidth : 0.0;
    if (m_finishingLayoutTransitions) {
        m_reserveAnimation->stop();
        setScrollBarReserveExtent(target);
    } else if (reserveChanged) {
        m_reserveAnimation->stop();
        m_reserveAnimation->setDuration(anim::duration(kReserveAnimationMs));
        m_reserveAnimation->setStartValue(m_scrollBarReserveExtent);
        m_reserveAnimation->setEndValue(target);
        anim::start(m_reserveAnimation);
    }

    switch (m_scrollBarPolicy) {
    case Qt::ScrollBarAlwaysOff:
        m_verticalScrollBar->hideAnimated();
        break;
    case Qt::ScrollBarAlwaysOn:
        m_verticalScrollBar->showAnimated();
        break;
    case Qt::ScrollBarAsNeeded:
    default:
        if (m_maxScroll > 0) {
            m_verticalScrollBar->showAnimated();
        } else {
            m_verticalScrollBar->hideAnimated();
        }
        break;
    }
    // Note: the scroll range is re-settled by the reserve animation's finished
    // handler once the viewport reaches its final width.
}

void SmoothScrollArea::updateHoverStates()
{
    if (!m_contentWidget || !m_viewport || !isVisible() || !m_viewport->isVisible()) {
        if (m_hoveredWidget) {
            m_hoveredWidget->setAttribute(Qt::WA_UnderMouse, false);
            m_hoveredWidget.clear();
        }
        return;
    }

    const QPoint globalPos = QCursor::pos();
    const QPoint viewportPos = m_viewport->mapFromGlobal(globalPos);

    QWidget* widgetUnderCursor = nullptr;
    if (m_viewport->rect().contains(viewportPos)) {
        const QPoint contentPos = m_contentWidget->mapFromGlobal(globalPos);
        widgetUnderCursor = m_contentWidget->childAt(contentPos);
        while (widgetUnderCursor && widgetUnderCursor->parentWidget() != m_contentWidget) {
            widgetUnderCursor = widgetUnderCursor->parentWidget();
        }
    }

    if (m_hoveredWidget == widgetUnderCursor) {
        return;
    }

    if (m_hoveredWidget) {
        QEvent leaveEvent(QEvent::Leave);
        QApplication::sendEvent(m_hoveredWidget, &leaveEvent);
        m_hoveredWidget->setAttribute(Qt::WA_UnderMouse, false);
    }

    m_hoveredWidget = widgetUnderCursor;

    if (m_hoveredWidget) {
        const QPoint localPos = m_hoveredWidget->mapFromGlobal(globalPos);
        QEnterEvent enterEvent(localPos, localPos, globalPos);
        QApplication::sendEvent(m_hoveredWidget, &enterEvent);
        m_hoveredWidget->setAttribute(Qt::WA_UnderMouse, true);
    }
}

} // namespace ruwa::ui::widgets
