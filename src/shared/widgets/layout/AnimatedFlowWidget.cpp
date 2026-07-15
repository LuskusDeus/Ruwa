// SPDX-License-Identifier: MPL-2.0

#include "shared/widgets/layout/AnimatedFlowWidget.h"

#include <QMargins>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>

#include <utility>

namespace ruwa::ui::widgets {

namespace {

template <typename Container, typename Predicate>
void eraseMatching(Container& container, Predicate predicate)
{
    for (auto it = container.begin(); it != container.end();) {
        if (predicate(*it)) {
            it = container.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

AnimatedFlowWidget::AnimatedFlowWidget(LayoutStyle style, QWidget* parent)
    : QWidget(parent)
    , m_style(style)
{
    m_animationTimer.setInterval(16);
    connect(&m_animationTimer, &QTimer::timeout, this, [this]() { advanceAnimation(); });

    if (m_style == LayoutStyle::UniformWrap) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
}

AnimatedFlowWidget::~AnimatedFlowWidget()
{
    shutdown();
}

void AnimatedFlowWidget::setFlowSpacing(int horizontal, int vertical)
{
    m_horizontalSpacing = horizontal;
    m_verticalSpacing = vertical;
    relayout(false);
}

void AnimatedFlowWidget::setItems(
    const QList<QWidget*>& flowItems, const QList<QWidget*>& pinnedItems)
{
    if (m_shuttingDown) {
        return;
    }

    m_flowItems.clear();
    m_pinnedItems.clear();
    m_flowItems.reserve(flowItems.size());
    m_pinnedItems.reserve(pinnedItems.size());

    const auto adoptItems = [this](const QList<QWidget*>& items, QList<QPointer<QWidget>>& target) {
        for (QWidget* widget : items) {
            if (!widget) {
                continue;
            }
            widget->setParent(this);
            widget->show();
            target.append(widget);
            trackItem(widget);
        }
    };

    adoptItems(flowItems, m_flowItems);
    adoptItems(pinnedItems, m_pinnedItems);

    m_initialized = false;
    relayout(false);
    updateGeometry();
}

void AnimatedFlowWidget::clearItems(ItemDisposal disposal)
{
    QList<QPointer<QWidget>> items = m_flowItems;
    items.append(m_pinnedItems);

    for (const QPointer<QWidget>& item : std::as_const(items)) {
        QWidget* widget = item.data();
        if (!widget) {
            continue;
        }
        removeTarget(widget);
        removeHiddenItem(widget);
        untrackItem(widget);
        if (disposal == ItemDisposal::DeleteLater) {
            widget->deleteLater();
        }
    }

    m_flowItems.clear();
    m_pinnedItems.clear();
    pruneDeadState();
    if (m_targets.isEmpty() && m_currentHeight == m_targetHeight) {
        m_animationTimer.stop();
    }
}

void AnimatedFlowWidget::setHeightCallback(std::function<void(int)> callback)
{
    m_heightCallback = std::move(callback);
}

void AnimatedFlowWidget::setSeparatorPropertyName(QByteArray propertyName)
{
    m_separatorPropertyName = std::move(propertyName);
    relayout(false);
}

int AnimatedFlowWidget::targetHeightForWidth(int width) const
{
    if (width <= 0) {
        width = this->width();
    }
    if (width <= 0) {
        width = naturalWidth();
    }
    return buildPlacements(width, nullptr);
}

void AnimatedFlowWidget::shutdown()
{
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;
    m_animationTimer.stop();
    disconnect(&m_animationTimer, nullptr, this, nullptr);
    m_heightCallback = {};
    disconnectTrackedItems();
    m_targets.clear();
    m_hiddenItems.clear();
    m_flowItems.clear();
    m_pinnedItems.clear();
}

int AnimatedFlowWidget::heightForWidth(int width) const
{
    return m_currentHeight >= 0 ? m_currentHeight : targetHeightForWidth(width);
}

QSize AnimatedFlowWidget::sizeHint() const
{
    const int hintWidth = width() > 0 ? width() : naturalWidth();
    const int hintHeight = m_currentHeight >= 0 ? m_currentHeight : targetHeightForWidth(hintWidth);
    if (m_style == LayoutStyle::PinnedToolbar) {
        return QSize(naturalWidth(), hintHeight);
    }
    return QSize(hintWidth, hintHeight);
}

QSize AnimatedFlowWidget::minimumSizeHint() const
{
    if (m_style == LayoutStyle::PinnedToolbar) {
        return QSize(maxItemWidth(), rowHeight());
    }
    return QSize(0, 0);
}

void AnimatedFlowWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_shuttingDown) {
        return;
    }
    if (m_style == LayoutStyle::UniformWrap && event->size().width() == event->oldSize().width()) {
        return;
    }

    relayout(m_initialized && isVisible() && m_reflowAnimated);
    m_initialized = true;
}

void AnimatedFlowWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_shuttingDown) {
        relayout(false);
    }
}

QSize AnimatedFlowWidget::itemSize(const QWidget* widget)
{
    return widget
        ? widget->sizeHint().expandedTo(widget->minimumSize()).boundedTo(widget->maximumSize())
        : QSize();
}

int AnimatedFlowWidget::advanceValue(int current, int target)
{
    const int delta = target - current;
    if (qAbs(delta) <= 1) {
        return target;
    }
    int step = qRound(delta * 0.35);
    if (step == 0) {
        step = delta > 0 ? 1 : -1;
    }
    return current + step;
}

int AnimatedFlowWidget::rowHeight() const
{
    int height = 0;
    for (const QPointer<QWidget>& item : m_flowItems) {
        if (item) {
            height = qMax(height, itemSize(item.data()).height());
        }
    }
    if (m_style == LayoutStyle::PinnedToolbar) {
        for (const QPointer<QWidget>& item : m_pinnedItems) {
            if (item) {
                height = qMax(height, itemSize(item.data()).height());
            }
        }
    }
    return qMax(height, 1);
}

int AnimatedFlowWidget::maxItemWidth() const
{
    int width = 1;
    for (const QPointer<QWidget>& item : m_flowItems) {
        if (item) {
            width = qMax(width, itemSize(item.data()).width());
        }
    }
    if (m_style == LayoutStyle::PinnedToolbar) {
        for (const QPointer<QWidget>& item : m_pinnedItems) {
            if (item) {
                width = qMax(width, itemSize(item.data()).width());
            }
        }
    }
    return width;
}

int AnimatedFlowWidget::pinnedBlockWidth() const
{
    int count = 0;
    int width = 0;
    for (const QPointer<QWidget>& item : m_pinnedItems) {
        if (!item) {
            continue;
        }
        width += itemSize(item.data()).width();
        ++count;
    }
    if (count > 1) {
        width += (count - 1) * m_horizontalSpacing;
    }
    return width;
}

int AnimatedFlowWidget::naturalWidth() const
{
    int width = 0;
    for (const QPointer<QWidget>& item : m_flowItems) {
        if (item) {
            width += itemSize(item.data()).width() + m_horizontalSpacing;
        }
    }

    if (m_style == LayoutStyle::PinnedToolbar) {
        width += pinnedBlockWidth();
        return qMax(width, maxItemWidth());
    }

    const QMargins margins = contentsMargins();
    return qMax(width, maxItemWidth()) + margins.left() + margins.right();
}

int AnimatedFlowWidget::buildPlacements(int width, QList<Placement>* placements) const
{
    return m_style == LayoutStyle::PinnedToolbar ? buildPinnedToolbarPlacements(width, placements)
                                                 : buildUniformPlacements(width, placements);
}

int AnimatedFlowWidget::buildUniformPlacements(int width, QList<Placement>* placements) const
{
    const QMargins margins = contentsMargins();
    if (width <= 0) {
        width = naturalWidth();
    }

    const int left = margins.left();
    const int right = width - margins.right();
    const int itemRowHeight = rowHeight();
    int x = left;
    int y = margins.top();
    int bottom = margins.top();
    bool rowEmpty = true;

    for (const QPointer<QWidget>& item : m_flowItems) {
        QWidget* widget = item.data();
        if (!widget) {
            continue;
        }
        const QSize size = itemSize(widget);
        if (!rowEmpty && x + size.width() > right) {
            x = left;
            y += itemRowHeight + m_verticalSpacing;
            rowEmpty = true;
        }
        if (placements) {
            const int centeredY = y + (itemRowHeight - size.height()) / 2;
            placements->append({ widget, QRect(QPoint(x, centeredY), size), false });
        }
        x += size.width() + m_horizontalSpacing;
        rowEmpty = false;
        bottom = qMax(bottom, y + itemRowHeight);
    }

    return bottom + margins.bottom();
}

int AnimatedFlowWidget::buildPinnedToolbarPlacements(int width, QList<Placement>* placements) const
{
    if (width <= 0) {
        width = naturalWidth();
    }

    const int itemRowHeight = rowHeight();
    const int pinnedWidth = pinnedBlockWidth();
    int pinnedX = width - pinnedWidth;
    for (const QPointer<QWidget>& item : m_pinnedItems) {
        QWidget* widget = item.data();
        if (!widget) {
            continue;
        }
        const QSize size = itemSize(widget);
        if (placements) {
            const int centeredY = (itemRowHeight - size.height()) / 2;
            placements->append({ widget, QRect(QPoint(pinnedX, centeredY), size), true });
        }
        pinnedX += size.width() + m_horizontalSpacing;
    }

    const int extraGap = pinnedWidth > 0 ? maxItemWidth() : 0;
    const int firstRowRight
        = pinnedWidth > 0 ? width - pinnedWidth - m_horizontalSpacing - extraGap : width;
    int x = 0;
    int y = 0;
    int row = 0;

    for (int i = 0; i < m_flowItems.size(); ++i) {
        QWidget* widget = m_flowItems[i].data();
        if (!widget) {
            continue;
        }
        const QSize size = itemSize(widget);
        const int edge = row == 0 ? firstRowRight : width;
        const bool separator = !m_separatorPropertyName.isEmpty()
            && widget->property(m_separatorPropertyName.constData()).toBool();
        if (separator) {
            QWidget* next = i + 1 < m_flowItems.size() ? m_flowItems[i + 1].data() : nullptr;
            const int nextWidth = next ? itemSize(next).width() : 0;
            const bool pairFits = x + size.width() + m_horizontalSpacing + nextWidth <= edge;
            if (x == 0 || !pairFits) {
                if (placements) {
                    placements->append({ widget, QRect(), false });
                }
                continue;
            }
            if (placements) {
                const int centeredY = y + (itemRowHeight - size.height()) / 2;
                placements->append({ widget, QRect(QPoint(x, centeredY), size), false });
            }
            x += size.width() + m_horizontalSpacing;
            continue;
        }

        if (x > 0 && x + size.width() > edge) {
            x = 0;
            y += itemRowHeight + m_verticalSpacing;
            ++row;
        }
        if (placements) {
            const int centeredY = y + (itemRowHeight - size.height()) / 2;
            placements->append({ widget, QRect(QPoint(x, centeredY), size), false });
        }
        x += size.width() + m_horizontalSpacing;
    }

    return y + itemRowHeight;
}

void AnimatedFlowWidget::relayout(bool animate)
{
    if (m_shuttingDown) {
        return;
    }

    pruneDeadState();
    QList<Placement> placements;
    const int totalHeight = buildPlacements(width(), &placements);

    for (int i = 0; i < placements.size(); ++i) {
        const Placement& placement = placements[i];
        QWidget* widget = placement.widget.data();
        if (!widget) {
            continue;
        }

        if (m_style == LayoutStyle::PinnedToolbar && !placement.rect.isValid()) {
            removeTarget(widget);
            widget->hide();
            addHiddenItem(widget);
            continue;
        }

        const bool wasHidden = removeHiddenItem(widget);
        if (m_style == LayoutStyle::PinnedToolbar && wasHidden && animate && !placement.snap
            && i + 1 < placements.size()) {
            const Placement& followingPlacement = placements[i + 1];
            QWidget* followingWidget = followingPlacement.widget.data();
            if (followingWidget && followingPlacement.rect.isValid()
                && followingWidget->isVisible()) {
                const QPoint delta
                    = followingWidget->geometry().topLeft() - followingPlacement.rect.topLeft();
                QRect seed = placement.rect;
                seed.translate(delta);
                widget->setGeometry(seed);
                widget->show();
                setAnimatedGeometry(widget, placement.rect, true);
                continue;
            }
        }

        const bool shouldAnimate = m_style == LayoutStyle::PinnedToolbar
            ? animate && !placement.snap && !wasHidden
            : animate;
        setAnimatedGeometry(widget, placement.rect, shouldAnimate);
    }

    if (totalHeight != m_targetHeight || m_currentHeight < 0) {
        m_targetHeight = totalHeight;
        if (m_currentHeight < 0 || !animate || !isVisible()) {
            m_currentHeight = totalHeight;
            if (m_style == LayoutStyle::UniformWrap) {
                if (m_heightCallback) {
                    m_heightCallback(m_currentHeight);
                }
            } else {
                updateGeometry();
            }
        } else if (!m_animationTimer.isActive()) {
            m_animationTimer.start();
        }
    }
}

void AnimatedFlowWidget::setAnimatedGeometry(QWidget* widget, const QRect& target, bool animate)
{
    if (!widget || m_shuttingDown) {
        return;
    }

    trackItem(widget);
    widget->show();
    if (!animate || !isVisible()) {
        removeTarget(widget);
        widget->setGeometry(target);
        return;
    }
    if (widget->geometry() == target) {
        removeTarget(widget);
        return;
    }

    setTarget(widget, target);
    if (!m_animationTimer.isActive()) {
        m_animationTimer.start();
    }
}

void AnimatedFlowWidget::advanceAnimation()
{
    if (m_shuttingDown) {
        m_animationTimer.stop();
        return;
    }

    bool heightBusy = false;
    if (m_currentHeight >= 0 && m_currentHeight != m_targetHeight) {
        m_currentHeight = advanceValue(m_currentHeight, m_targetHeight);
        if (m_style == LayoutStyle::UniformWrap) {
            if (m_heightCallback) {
                m_heightCallback(m_currentHeight);
            }
        } else {
            updateGeometry();
        }
        heightBusy = m_currentHeight != m_targetHeight;
    }

    for (auto it = m_targets.begin(); it != m_targets.end();) {
        QWidget* widget = it->widget.data();
        if (!widget) {
            it = m_targets.erase(it);
            continue;
        }

        const QRect current = widget->geometry();
        const QRect& target = it->rect;
        const QRect next(advanceValue(current.x(), target.x()),
            advanceValue(current.y(), target.y()), advanceValue(current.width(), target.width()),
            advanceValue(current.height(), target.height()));
        widget->setGeometry(next);
        if (next == target) {
            it = m_targets.erase(it);
        } else {
            ++it;
        }
    }

    if (!heightBusy && m_targets.isEmpty()) {
        m_animationTimer.stop();
    }
}

void AnimatedFlowWidget::trackItem(QWidget* widget)
{
    if (!widget || m_shuttingDown) {
        return;
    }
    for (const TrackedItem& tracked : std::as_const(m_trackedItems)) {
        if (tracked.widget == widget) {
            return;
        }
    }

    TrackedItem tracked;
    tracked.widget = widget;
    tracked.destroyedConnection = connect(widget, &QObject::destroyed, this, [this]() {
        if (!m_shuttingDown) {
            pruneDeadState();
        }
    });
    m_trackedItems.append(std::move(tracked));
}

void AnimatedFlowWidget::untrackItem(QWidget* widget)
{
    for (auto it = m_trackedItems.begin(); it != m_trackedItems.end();) {
        if (!it->widget || it->widget == widget) {
            disconnect(it->destroyedConnection);
            it = m_trackedItems.erase(it);
        } else {
            ++it;
        }
    }
}

void AnimatedFlowWidget::disconnectTrackedItems()
{
    for (const TrackedItem& tracked : std::as_const(m_trackedItems)) {
        disconnect(tracked.destroyedConnection);
    }
    m_trackedItems.clear();
}

void AnimatedFlowWidget::pruneDeadState()
{
    eraseMatching(m_flowItems, [](const QPointer<QWidget>& item) { return item.isNull(); });
    eraseMatching(m_pinnedItems, [](const QPointer<QWidget>& item) { return item.isNull(); });
    eraseMatching(m_targets, [](const AnimatedTarget& target) { return target.widget.isNull(); });
    eraseMatching(m_hiddenItems, [](const QPointer<QWidget>& item) { return item.isNull(); });
    eraseMatching(
        m_trackedItems, [](const TrackedItem& tracked) { return tracked.widget.isNull(); });
}

int AnimatedFlowWidget::targetIndex(QWidget* widget) const
{
    for (int i = 0; i < m_targets.size(); ++i) {
        if (m_targets[i].widget == widget) {
            return i;
        }
    }
    return -1;
}

void AnimatedFlowWidget::setTarget(QWidget* widget, const QRect& rect)
{
    const int index = targetIndex(widget);
    if (index >= 0) {
        m_targets[index].rect = rect;
    } else {
        m_targets.append({ widget, rect });
    }
}

void AnimatedFlowWidget::removeTarget(QWidget* widget)
{
    eraseMatching(m_targets, [widget](const AnimatedTarget& target) {
        return !target.widget || target.widget == widget;
    });
}

bool AnimatedFlowWidget::isHiddenItem(QWidget* widget) const
{
    for (const QPointer<QWidget>& item : m_hiddenItems) {
        if (item == widget) {
            return true;
        }
    }
    return false;
}

bool AnimatedFlowWidget::removeHiddenItem(QWidget* widget)
{
    const bool wasHidden = isHiddenItem(widget);
    eraseMatching(
        m_hiddenItems, [widget](const QPointer<QWidget>& item) { return !item || item == widget; });
    return wasHidden;
}

void AnimatedFlowWidget::addHiddenItem(QWidget* widget)
{
    if (widget && !isHiddenItem(widget)) {
        m_hiddenItems.append(widget);
    }
}

} // namespace ruwa::ui::widgets
