// SPDX-License-Identifier: MPL-2.0

// ListCollapseAnimator.cpp
#include "ListCollapseAnimator.h"
#include "shared/style/AnimationPolicy.h"

#include <QBoxLayout>
#include <QEasingCurve>
#include <QLayoutItem>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QWidget>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {

bool isHorizontal(const QBoxLayout* layout)
{
    if (!layout) {
        return false;
    }
    return layout->direction() == QBoxLayout::LeftToRight
        || layout->direction() == QBoxLayout::RightToLeft;
}

/// Placeholder occupying a transitioning item's layout slot. Its extent along the
/// box-layout axis grows or shrinks while the snapshot simultaneously fades.
class TransitionSnapshot final : public QWidget {
public:
    TransitionSnapshot(
        const QPixmap& snapshot, int fullExtent, bool horizontal, bool revealing, QWidget* parent)
        : QWidget(parent)
        , m_snapshot(snapshot)
        , m_fullExtent(qMax(0, fullExtent))
        , m_horizontal(horizontal)
        , m_revealing(revealing)
    {
        // Purely decorative: never steal hover/click from the live list.
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFocusPolicy(Qt::NoFocus);

        const qreal dpr = m_snapshot.devicePixelRatio() > 0.0 ? m_snapshot.devicePixelRatio() : 1.0;
        const QSize logicalSize(
            qMax(1, qRound(m_snapshot.width() / dpr)), qMax(1, qRound(m_snapshot.height() / dpr)));
        if (m_horizontal) {
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            setFixedSize(m_revealing ? 0 : m_fullExtent, logicalSize.height());
        } else {
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            setFixedHeight(m_revealing ? 0 : m_fullExtent);
        }
    }

    /// @param progress 0 (transition start) .. 1 (transition end). Expected pre-eased.
    void setProgress(qreal progress)
    {
        m_progress = qBound(0.0, progress, 1.0);
        const qreal visibleProgress = m_revealing ? m_progress : 1.0 - m_progress;
        const int extent = qMax(0, qRound(m_fullExtent * visibleProgress));
        if (m_horizontal) {
            setFixedWidth(extent);
        } else {
            setFixedHeight(extent);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_snapshot.isNull() || width() <= 0 || height() <= 0) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const qreal visibleProgress = m_revealing ? m_progress : 1.0 - m_progress;
        painter.setOpacity(qBound(0.0, visibleProgress, 1.0));

        const qreal dpr = m_snapshot.devicePixelRatio() > 0.0 ? m_snapshot.devicePixelRatio() : 1.0;
        const qreal pw = m_snapshot.width() / dpr;
        const qreal ph = m_snapshot.height() / dpr;

        // Shrink toward the snapshot's own centre so it reads as the row pulling
        // inward as the gap closes. Horizontally centred in the (full-width) slot;
        // vertically centred on the shrinking widget, which clips to a thinning band.
        const qreal scale = 0.84 + 0.16 * visibleProgress;
        const qreal w = pw * scale;
        const qreal h = ph * scale;
        const qreal x = (width() - w) / 2.0;
        const qreal y = (height() - h) / 2.0;
        painter.drawPixmap(
            QRectF(x, y, w, h), m_snapshot, QRectF(0, 0, m_snapshot.width(), m_snapshot.height()));
    }

private:
    QPixmap m_snapshot;
    int m_fullExtent = 0;
    bool m_horizontal = false;
    bool m_revealing = false;
    qreal m_progress = 0.0;
};

} // namespace

struct ListCollapseAnimator::ActiveCollapse {
    QPointer<QWidget> snapshot; // TransitionSnapshot in the layout slot
    QPointer<QBoxLayout> layout; // owning layout (for clean removal)
    QPointer<QWidget> revealedWidget; // real widget replacing a reveal snapshot
    int revealIndex = -1;
    QVariantAnimation* animation = nullptr;
    std::function<void()> onFinished;
    bool finishing = false; // re-entrancy guard
};

ListCollapseAnimator::ListCollapseAnimator(QObject* parent)
    : QObject(parent)
{
}

ListCollapseAnimator::~ListCollapseAnimator()
{
    finishAll();
}

void ListCollapseAnimator::collapseRange(QBoxLayout* layout, QWidget* content, int startIndex,
    int endIndex, int durationMs, std::function<void()> onFinished)
{
    const auto runFinishedNow = [&onFinished]() {
        if (onFinished) {
            onFinished();
        }
    };

    if (!layout || !content || startIndex < 0 || endIndex < startIndex
        || endIndex >= layout->count()) {
        runFinishedNow();
        return;
    }

    const bool horizontal = isHorizontal(layout);

    // Measure the run: bounding box of its widget items (snapshot source) and its
    // extent along the layout axis.
    QRect itemBounds;
    int measuredExtent = 0;
    int measuredItemCount = 0;
    int widgetCount = 0;
    for (int i = startIndex; i <= endIndex; ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item) {
            continue;
        }
        ++measuredItemCount;
        if (QWidget* w = item->widget()) {
            const QRect g = w->geometry();
            itemBounds = itemBounds.isNull() ? g : itemBounds.united(g);
            measuredExtent += horizontal ? g.width() : g.height();
            ++widgetCount;
        } else {
            const QSize hint = item->sizeHint();
            measuredExtent += horizontal ? hint.width() : hint.height();
        }
    }

    if (widgetCount == 0 || !itemBounds.isValid()) {
        // Nothing visible to snapshot — just drop the items and finish.
        for (int i = endIndex; i >= startIndex; --i) {
            QLayoutItem* item = layout->takeAt(i);
            if (!item) {
                continue;
            }
            if (QWidget* w = item->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete item;
        }
        runFinishedNow();
        return;
    }

    const QRect snapRect = horizontal
        ? itemBounds
        : QRect(0, itemBounds.top(), qMax(1, content->width()), itemBounds.height());
    const QPixmap snapshot = content->grab(snapRect);
    if (measuredItemCount > 1 && layout->spacing() > 0) {
        measuredExtent += (measuredItemCount - 1) * layout->spacing();
    }
    const int boundsExtent = horizontal ? itemBounds.width() : itemBounds.height();
    const int fullExtent = qMax(measuredExtent, boundsExtent);

    // Tear out the run (bottom-up so indices stay valid) and replace it with one
    // placeholder. Widgets are hidden + deleteLater()'d; the caller's model can
    // already treat them as gone.
    for (int i = endIndex; i >= startIndex; --i) {
        QLayoutItem* item = layout->takeAt(i);
        if (!item) {
            continue;
        }
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    auto* snap
        = new TransitionSnapshot(snapshot, fullExtent, horizontal, /*revealing=*/false, content);
    layout->insertWidget(startIndex, snap);
    snap->show();

    auto* collapse = new ActiveCollapse;
    collapse->snapshot = snap;
    collapse->layout = layout;
    collapse->onFinished = std::move(onFinished);

    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(anim::duration(durationMs > 0 ? durationMs : kDefaultDurationMs));
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    collapse->animation = animation;

    QPointer<TransitionSnapshot> snapGuard(snap);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [this, snapGuard](const QVariant& value) {
            if (snapGuard) {
                snapGuard->setProgress(value.toReal());
            }
            emit stepped();
        });
    connect(animation, &QVariantAnimation::finished, this,
        [this, collapse]() { finishCollapse(collapse, /*jumpToEnd=*/false); });

    m_active.append(collapse);
    anim::start(animation);
}

void ListCollapseAnimator::revealWidget(QBoxLayout* layout, QWidget* content, QWidget* widget,
    int index, int durationMs, std::function<void()> onFinished)
{
    const auto runFinishedNow = [&onFinished]() {
        if (onFinished) {
            onFinished();
        }
    };

    if (!layout || !content || !widget || index < 0 || index > layout->count()) {
        runFinishedNow();
        return;
    }

    widget->setParent(content);
    widget->ensurePolished();
    const QSize targetSize
        = widget->sizeHint().expandedTo(widget->minimumSizeHint()).boundedTo(widget->maximumSize());
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        layout->insertWidget(index, widget);
        widget->show();
        runFinishedNow();
        return;
    }

    widget->resize(targetSize);
    const QPixmap snapshot = widget->grab(QRect(QPoint(0, 0), targetSize));
    if (snapshot.isNull()) {
        layout->insertWidget(index, widget);
        widget->show();
        runFinishedNow();
        return;
    }
    widget->hide();

    const bool horizontal = isHorizontal(layout);
    const int fullExtent = horizontal ? targetSize.width() : targetSize.height();
    auto* snap
        = new TransitionSnapshot(snapshot, fullExtent, horizontal, /*revealing=*/true, content);
    layout->insertWidget(index, snap);
    snap->show();

    auto* transition = new ActiveCollapse;
    transition->snapshot = snap;
    transition->layout = layout;
    transition->revealedWidget = widget;
    transition->revealIndex = index;
    transition->onFinished = std::move(onFinished);

    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(anim::duration(durationMs > 0 ? durationMs : kDefaultDurationMs));
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    transition->animation = animation;

    QPointer<TransitionSnapshot> snapGuard(snap);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [this, snapGuard](const QVariant& value) {
            if (snapGuard) {
                snapGuard->setProgress(value.toReal());
            }
            emit stepped();
        });
    connect(animation, &QVariantAnimation::finished, this,
        [this, transition]() { finishCollapse(transition, /*jumpToEnd=*/false); });

    m_active.append(transition);
    anim::start(animation);
}

void ListCollapseAnimator::finishCollapse(ActiveCollapse* collapse, bool jumpToEnd)
{
    if (!collapse || collapse->finishing) {
        return;
    }
    collapse->finishing = true;

    if (collapse->animation) {
        collapse->animation->stop();
        collapse->animation->deleteLater();
        collapse->animation = nullptr;
    }

    if (collapse->snapshot && collapse->layout) {
        collapse->layout->removeWidget(collapse->snapshot);
    }
    if (collapse->snapshot) {
        collapse->snapshot->deleteLater();
        collapse->snapshot = nullptr;
    }

    if (collapse->revealedWidget) {
        if (collapse->layout) {
            const int index = qBound(0, collapse->revealIndex, collapse->layout->count());
            collapse->layout->insertWidget(index, collapse->revealedWidget);
        }
        collapse->revealedWidget->show();
        collapse->revealedWidget = nullptr;
    }

    m_active.removeAll(collapse);

    std::function<void()> cb = std::move(collapse->onFinished);
    delete collapse;

    Q_UNUSED(jumpToEnd);
    emit stepped();
    if (cb) {
        cb();
    }
}

void ListCollapseAnimator::finishAll()
{
    // Snapshot the list: each finish mutates m_active.
    const QVector<ActiveCollapse*> pending = m_active;
    for (ActiveCollapse* collapse : pending) {
        finishCollapse(collapse, /*jumpToEnd=*/true);
    }
}

} // namespace ruwa::ui::widgets
