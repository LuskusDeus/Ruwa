// SPDX-License-Identifier: MPL-2.0

#include "shared/widgets/layout/CollapsibleSection.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/SectionHeaderButton.h"
#include "shared/style/AnimationPolicy.h"

#include <QEvent>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemeManager;

namespace {
constexpr int kExpandAnimationMinMs = 170;
constexpr int kExpandAnimationMaxMs = 320;
constexpr qreal kExpandAnimationMsPerPixel = 0.85;
} // namespace

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    // Fixed vertically: the section is exactly as tall as its header plus the
    // current clip height. Left to Preferred, the parent layout would hand it
    // slack and redistribute it every animation frame — which is what makes a
    // column of groups shiver while one of them opens.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_header = new SectionHeaderButton(this);
    m_header->setTitle(title);
    rootLayout->addWidget(m_header);

    // No layout on the clip: the content keeps its natural height and the clip
    // simply shows less of it, so nothing inside gets squeezed while closing.
    m_clip = new QWidget(this);
    m_clip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_clip->setMinimumHeight(0);
    m_clip->setMaximumHeight(0);
    m_clip->setAttribute(Qt::WA_TranslucentBackground);
    rootLayout->addWidget(m_clip);

    m_expandAnimation = new QPropertyAnimation(this, "contentHeight", this);
    m_expandAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    m_header->setExpanded(m_expanded, false);
    connect(
        m_header, &QAbstractButton::clicked, this, [this]() { setExpanded(!m_expanded, true); });
}

void CollapsibleSection::setTitle(const QString& title)
{
    m_header->setTitle(title);
}

QString CollapsibleSection::title() const
{
    return m_header->title();
}

void CollapsibleSection::setContentWidget(QWidget* content)
{
    if (m_content == content) {
        return;
    }

    if (m_content) {
        m_content->removeEventFilter(this);
        m_content->deleteLater();
    }

    m_content = content;
    if (m_content) {
        m_content->setParent(m_clip);
        m_content->installEventFilter(this);
        m_content->show();
    }

    layoutContent();
    setContentHeight(m_expanded ? expandedContentHeight() : 0);
}

void CollapsibleSection::setContentMargins(const QMargins& margins)
{
    if (m_margins == margins) {
        return;
    }
    m_margins = margins;
    layoutContent();
    refreshContentHeight(false);
}

void CollapsibleSection::setExpanded(bool expanded, bool animated)
{
    const bool changed = (m_expanded != expanded);
    m_expanded = expanded;
    m_header->setExpanded(expanded, animated);

    const int target = expanded ? expandedContentHeight() : 0;
    if (!animated) {
        m_expandAnimation->stop();
        setContentHeight(target);
    } else {
        animateContentHeightTo(target);
    }

    if (changed) {
        emit toggled(m_expanded);
    }
}

void CollapsibleSection::setContentHeight(int height)
{
    const int clamped = qMax(0, height);
    if (m_contentHeight == clamped) {
        return;
    }

    m_contentHeight = clamped;
    // One call, so the clip never passes through a frame where the new minimum
    // exceeds the old maximum.
    m_clip->setFixedHeight(clamped);
    updateGeometry();
}

void CollapsibleSection::refreshContentHeight(bool animated)
{
    layoutContent();
    if (!m_expanded) {
        return;
    }

    const int target = expandedContentHeight();
    // Retarget rather than snap while opening or closing: a snap here is a
    // visible jump, and the content does emit layout requests mid-animation.
    if (animated || m_expandAnimation->state() == QAbstractAnimation::Running) {
        animateContentHeightTo(target);
    } else {
        m_expandAnimation->stop();
        setContentHeight(target);
    }
}

void CollapsibleSection::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (event->size().width() == event->oldSize().width()) {
        return;
    }

    // A width change can reflow the content to a different height. Snap to it
    // rather than animating — the user is dragging the panel edge, and a lagging
    // group would trail the drag.
    layoutContent();
    if (m_expanded && m_expandAnimation->state() != QAbstractAnimation::Running) {
        setContentHeight(expandedContentHeight());
    }
}

bool CollapsibleSection::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_content && event->type() == QEvent::LayoutRequest) {
        refreshContentHeight(false);
    }
    return QWidget::eventFilter(watched, event);
}

int CollapsibleSection::contentAreaWidth() const
{
    auto& tm = ThemeManager::instance();
    const int outer = m_clip->width() > 0 ? m_clip->width() : width();
    return qMax(0, outer - tm.scaled(m_margins.left()) - tm.scaled(m_margins.right()));
}

void CollapsibleSection::layoutContent()
{
    if (!m_content) {
        return;
    }

    auto& tm = ThemeManager::instance();
    const int innerWidth = contentAreaWidth();
    const int innerHeight = m_content->hasHeightForWidth() ? m_content->heightForWidth(innerWidth)
                                                           : m_content->sizeHint().height();

    m_content->setGeometry(
        tm.scaled(m_margins.left()), tm.scaled(m_margins.top()), innerWidth, qMax(0, innerHeight));
}

int CollapsibleSection::expandedContentHeight() const
{
    if (!m_content) {
        return 0;
    }

    auto& tm = ThemeManager::instance();
    const int innerWidth = contentAreaWidth();
    const int innerHeight = m_content->hasHeightForWidth() ? m_content->heightForWidth(innerWidth)
                                                           : m_content->sizeHint().height();

    return qMax(0, innerHeight) + tm.scaled(m_margins.top()) + tm.scaled(m_margins.bottom());
}

void CollapsibleSection::animateContentHeightTo(int targetHeight)
{
    const int clamped = qMax(0, targetHeight);
    const bool running = m_expandAnimation->state() == QAbstractAnimation::Running;
    if (running && m_expandAnimation->endValue().toInt() == clamped) {
        return; // already on its way there; restarting would reset the easing
    }
    if (!running && m_contentHeight == clamped) {
        return;
    }

    const int delta = qAbs(clamped - m_contentHeight);
    m_expandAnimation->stop();
    m_expandAnimation->setDuration(anim::duration(qBound(
        kExpandAnimationMinMs, qRound(delta * kExpandAnimationMsPerPixel), kExpandAnimationMaxMs)));
    m_expandAnimation->setStartValue(m_contentHeight);
    m_expandAnimation->setEndValue(clamped);
    m_expandAnimation->start();
}

} // namespace ruwa::ui::widgets
