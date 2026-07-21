// SPDX-License-Identifier: MPL-2.0

// SmoothScrollArea.cpp
#include "SmoothScrollArea.h"
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
#include <QtMath>

namespace ruwa::ui::widgets {

namespace {
constexpr int kStylusSwipeInertiaDurationMs = 180;
constexpr qreal kStylusSwipeVelocityBlend = 0.35;
constexpr qreal kStylusSwipeVelocityDeadzone = 120.0;
constexpr qreal kStylusSwipeInertiaFactor = 0.18;
constexpr int kHoverUpdateIntervalMs = 40;
constexpr qreal kScrollBarWidth = 12.0; // Must match SmoothScrollBar::setFixedWidth(12).
constexpr int kReserveAnimationMs = 220;
constexpr int kDefaultScrollDurationMs = 200;
constexpr int kStepScrollDurationMs = 120;
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
    cancelStylusSwipe();
    m_orientation = orientation;
    m_currentScrollValue = 0;
    m_targetScrollValue = 0;
    m_maxScroll = 0;

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

void SmoothScrollArea::scrollTo(int value, bool animated)
{
    if (animated) {
        scrollTo(value, kDefaultScrollDurationMs, QEasingCurve::OutCubic);
        return;
    }

    m_scrollAnimation->stop();
    m_targetScrollValue = qBound(0, value, m_maxScroll);
    setScrollValue(m_targetScrollValue);
}

void SmoothScrollArea::scrollTo(int value, int durationMs, QEasingCurve::Type easingCurve)
{
    m_targetScrollValue = qBound(0, value, m_maxScroll);
    m_scrollAnimation->stop();

    if (durationMs > 0 && m_targetScrollValue != m_currentScrollValue) {
        m_scrollAnimation->setDuration(durationMs);
        m_scrollAnimation->setEasingCurve(easingCurve);
        m_scrollAnimation->setStartValue(m_currentScrollValue);
        m_scrollAnimation->setEndValue(m_targetScrollValue);
        m_scrollAnimation->start();
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
        m_targetScrollValue = m_currentScrollValue;
        cancelStylusSwipe();
    }
}

void SmoothScrollArea::setScrollValue(int value)
{
    const int previousScrollValue = m_currentScrollValue;
    m_currentScrollValue = qBound(0, value, m_maxScroll);

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
    m_stylusSwipeActive = true;
    m_stylusSwipeStartGlobalPos = globalPos;
    m_stylusSwipeLastGlobalPos = globalPos;
    m_stylusSwipeStartScrollValue = m_currentScrollValue;
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

    const int dragDelta = m_orientation == Qt::Horizontal
        ? globalPos.x() - m_stylusSwipeStartGlobalPos.x()
        : globalPos.y() - m_stylusSwipeStartGlobalPos.y();
    setScrollValue(m_stylusSwipeStartScrollValue - dragDelta);

    const qint64 nowMs = m_stylusSwipeTimer.elapsed();
    const qint64 dtMs = qMax<qint64>(1, nowMs - m_stylusSwipeLastSampleMs);
    const int stepDelta = m_orientation == Qt::Horizontal
        ? globalPos.x() - m_stylusSwipeLastGlobalPos.x()
        : globalPos.y() - m_stylusSwipeLastGlobalPos.y();
    const qreal instantVelocity = (-stepDelta * 1000.0) / dtMs;
    m_stylusSwipeVelocity = (m_stylusSwipeVelocity * (1.0 - kStylusSwipeVelocityBlend))
        + (instantVelocity * kStylusSwipeVelocityBlend);

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

    updateStylusSwipe(globalPos);
    m_stylusSwipeActive = false;

    if (qAbs(m_stylusSwipeVelocity) < kStylusSwipeVelocityDeadzone) {
        return;
    }

    const int inertiaTarget = qBound(0,
        qRound(m_currentScrollValue + (m_stylusSwipeVelocity * kStylusSwipeInertiaFactor)),
        m_maxScroll);

    if (inertiaTarget == m_currentScrollValue) {
        return;
    }

    m_targetScrollValue = inertiaTarget;
    m_scrollAnimation->stop();
    m_scrollAnimation->setDuration(kStylusSwipeInertiaDurationMs);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_scrollAnimation->setStartValue(m_currentScrollValue);
    m_scrollAnimation->setEndValue(m_targetScrollValue);
    m_scrollAnimation->start();
    if (m_orientation == Qt::Vertical) {
        m_verticalScrollBar->showAnimated();
    }
}

void SmoothScrollArea::cancelStylusSwipe()
{
    m_stylusSwipeActive = false;
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
    if (!event->pixelDelta().isNull()) {
        delta
            = m_orientation == Qt::Horizontal ? -event->pixelDelta().x() : -event->pixelDelta().y();
        if (delta == 0 && m_orientation == Qt::Horizontal) {
            delta = -event->pixelDelta().y();
        }
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

    const int baseValue = (m_scrollAnimation->state() == QAbstractAnimation::Running)
        ? m_targetScrollValue
        : m_currentScrollValue;

    m_scrollAnimation->stop();

    m_targetScrollValue = qBound(0, baseValue + delta, m_maxScroll);

    m_scrollAnimation->setDuration(kStepScrollDurationMs);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_scrollAnimation->setStartValue(m_currentScrollValue);
    m_scrollAnimation->setEndValue(m_targetScrollValue);
    m_scrollAnimation->start();

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
        m_verticalScrollBar->setRange(0, 0);
        updateScrollBarVisibility();
        return;
    }

    const bool wasRefreshingLayout = m_refreshingLayout;
    m_refreshingLayout = true;

    m_contentWidget->updateGeometry();

    const QSize widgetSizeHint = m_contentWidget->sizeHint();

    QSize layoutSizeHint;
    if (m_contentWidget->layout()) {
        auto* contentLayout = m_contentWidget->layout();
        contentLayout->invalidate();
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

    if (m_currentScrollValue > m_maxScroll) {
        setScrollValue(m_maxScroll);
    }

    updateScrollBarVisibility();

    m_refreshingLayout = wasRefreshingLayout;
}

void SmoothScrollArea::onScrollBarValueChanged(int value)
{
    if (m_verticalScrollBar->isDragging()) {
        m_scrollAnimation->stop();
        const int previousScrollValue = m_currentScrollValue;
        m_currentScrollValue = qBound(0, value, m_maxScroll);
        syncContentPosition(previousScrollValue, true);
        emit scrolled(m_currentScrollValue);
        return;
    }

    if (qAbs(value - m_currentScrollValue) > 2) {
        m_scrollAnimation->stop();
        m_scrollAnimation->setDuration(kDefaultScrollDurationMs);
        m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_scrollAnimation->setStartValue(m_currentScrollValue);
        m_scrollAnimation->setEndValue(value);
        m_scrollAnimation->start();
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
    int baseValue = (m_scrollAnimation->state() == QAbstractAnimation::Running)
        ? m_targetScrollValue
        : m_currentScrollValue;

    m_targetScrollValue = qBound(0, baseValue + delta, m_maxScroll);

    m_scrollAnimation->stop();
    m_scrollAnimation->setDuration(kStepScrollDurationMs);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_scrollAnimation->setStartValue(m_currentScrollValue);
    m_scrollAnimation->setEndValue(m_targetScrollValue);
    m_scrollAnimation->start();

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

        if (m_contentWidget && m_contentWidget->layout()) {
            m_contentWidget->layout()->invalidate();
            m_contentWidget->layout()->activate();
        }
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

        if (m_contentWidget->layout()) {
            m_contentWidget->layout()->invalidate();
            m_contentWidget->layout()->activate();
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

    if (reserveChanged) {
        const qreal target = shouldReserve ? kScrollBarWidth : 0.0;
        m_reserveAnimation->stop();
        m_reserveAnimation->setStartValue(m_scrollBarReserveExtent);
        m_reserveAnimation->setEndValue(target);
        m_reserveAnimation->start();
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
